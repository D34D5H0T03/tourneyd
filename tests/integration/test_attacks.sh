#!/bin/bash
cd "$(dirname "$0")/../.." || exit 1
source tests/integration/lib.sh
setup_docroot; make_conf; start_server || exit 1

H='Host: x\r\nConnection: close\r\n'          # "close" so each probe ends immediately
A600=$(head -c 600 /dev/zero | tr '\0' a);  A2000=$(head -c 2000 /dev/zero | tr '\0' a)
A5000=$(head -c 5000 /dev/zero | tr '\0' a)
MANY=$(for i in $(seq 60); do printf 'X-H%d: v\\r\\n' "$i"; done)
FAT=$(for i in $(seq 35); do printf 'X-F%d: %s\\r\\n' "$i" "$(head -c 480 /dev/zero | tr '\0' b)"; done)

attack() {   # name, payload, expected status (regex)
    local got; got=$(raw_status "$2")
    if [[ "$got" =~ ^($3)$ ]]; then pass "$1"; else fail "$1 (got $got, want $3)"; fi
}

echo "-- malformed requests"
attack "garbage line"             "hello\r\n\r\n"                               400
attack "unknown method"           "BREW / HTTP/1.1\r\n$H\r\n"                   501
attack "HTTP/2.0"                 "GET / HTTP/2.0\r\n$H\r\n"                    505
attack "HTTP/1.1 without Host"    "GET / HTTP/1.1\r\nConnection: close\r\n\r\n" 400
attack "HTTP/1.0 without Host"    "GET / HTTP/1.0\r\n\r\n"                      200
attack "absolute-form target"     "GET http://x/ HTTP/1.1\r\n$H\r\n"            400
attack "OPTIONS *"                "OPTIONS * HTTP/1.1\r\n$H\r\n"                400
attack "tab inside path"          "GET /a\tb HTTP/1.1\r\n$H\r\n"                400
attack "2000-char path"           "GET /$A2000 HTTP/1.1\r\n$H\r\n"              414
attack "5000 bytes, no CRLF"      "GET /$A5000"                                 414

echo "-- headers"
attack "NUL in header name"       "GET / HTTP/1.1\r\n${H}X-Bad\0name: 1\r\n\r\n" 400
attack "space in header name"     "GET / HTTP/1.1\r\nHost : x\r\n\r\n"           400
attack "DEL in header value"      "GET / HTTP/1.1\r\n${H}X-A: a\x7fb\r\n\r\n"    400
attack "600-byte header value"    "GET / HTTP/1.1\r\n${H}X-A: $A600\r\n\r\n"     431
attack "5000-byte header line"    "GET / HTTP/1.1\r\n${H}X-A: $A5000\r\n\r\n"    431
attack "60 headers"               "GET / HTTP/1.1\r\n${H}${MANY}\r\n"            431
attack "header bytes over 16 KB"  "GET / HTTP/1.1\r\n${H}${FAT}\r\n"             431

echo "-- bodies and framing"
attack "differing Content-Length" "POST / HTTP/1.1\r\n${H}Content-Length: 3\r\nContent-Length: 4\r\n\r\nabc" 400
attack "Content-Length: abc"      "POST / HTTP/1.1\r\n${H}Content-Length: abc\r\n\r\n"  400
attack "Content-Length: -1"       "POST / HTTP/1.1\r\n${H}Content-Length: -1\r\n\r\n"   400
attack "Content-Length 2 MB"      "POST / HTTP/1.1\r\n${H}Content-Length: 2000000\r\n\r\n" 413
attack "Content-Length overflow"  "POST / HTTP/1.1\r\n${H}Content-Length: 99999999999999999999999\r\n\r\n" 413
attack "chunked unsupported"      "POST / HTTP/1.1\r\n${H}Transfer-Encoding: chunked\r\n\r\n0\r\n\r\n" 501
attack "Content-Length + chunked" "POST / HTTP/1.1\r\n${H}Content-Length: 4\r\nTransfer-Encoding: chunked\r\n\r\n0\r\n\r\n" 501
attack "small body is consumed"   "POST / HTTP/1.1\r\n${H}Content-Length: 5\r\n\r\nhello" "200|404|405"

out=$(raw "GET / HTTP/1.1\r\nHost: x\r\n\r\nGET /nope HTTP/1.1\r\nHost: x\r\nConnection: close\r\n\r\n")
assert_eq "$(grep -c '^HTTP/1.1 ' <<<"$out")" 2 "pipelined requests get two responses"
out=$(raw "BREW / HTTP/1.1\r\n$H\r\nGET / HTTP/1.1\r\n$H\r\n")
assert_eq "$(grep -c '^HTTP/1.1 ' <<<"$out")" 1 "connection closes after a parse error"

echo "-- path traversal (nothing from outside the doc root may come back)"
for p in "/../../../etc/passwd" "/%2e%2e/%2e%2e/etc/passwd" "/..%2f..%2fetc/passwd" "//etc/passwd" \
         "/%2e%2e%5c%2e%2e%5cetc/passwd" "/./../etc/passwd" "/.hidden" "/leak.txt" "/index.html%00.png"; do
    out=$(raw "GET $p HTTP/1.1\r\n$H\r\n")
    st=$(head -n1 <<<"$out" | awk '{print $2}')
    if [[ "$st" =~ ^(400|404)$ ]] && ! grep -q 'root:' <<<"$out" && ! grep -q secret <<<"$out"
    then pass "blocked: $p"; else fail "NOT blocked: $p (status $st)"; fi
done

echo "-- response hygiene"
out=$(raw "GET /nope HTTP/1.1\r\n$H\r\n")
grep -qi '^X-Content-Type-Options: nosniff' <<<"$out"  && pass "404 carries nosniff" || fail "404 missing nosniff"
grep -qi '^Content-Security-Policy:'        <<<"$out"  && pass "404 carries CSP"     || fail "404 missing CSP"
out=$(raw "GET /%0d%0aSet-Cookie:%20x=1 HTTP/1.1\r\n$H\r\n")
grep -qi '^Set-Cookie' <<<"$out" && fail "CRLF injection reached the response" || pass "no header injection via path"

echo "-- log hygiene (trust_proxy off)"
curl -s -o /dev/null -A 'evil"agent' "http://127.0.0.1:$PORT/"
curl -s -o /dev/null -H 'CF-Connecting-IP: 1.2.3.4' "http://127.0.0.1:$PORT/"
raw "GET /a%0aFAKE HTTP/1.1\r\n$H\r\n" >/dev/null
sleep 0.3
grep -q '^FAKE' build/test-access.log       && fail "log injection via path"        || pass "path cannot forge a log line"
grep -q 'evil_agent' build/test-access.log  && pass "quotes in User-Agent neutralized" || fail "quote not neutralized"
grep -q '1\.2\.3\.4' build/test-access.log  && fail "forwarded IP trusted while off" || pass "forwarded IP ignored while off"

echo "-- log hygiene (trust_proxy on)"
make_conf "trust_proxy = on"; restart_server
curl -s -o /dev/null -H 'CF-Connecting-IP: 1.2.3.4' "http://127.0.0.1:$PORT/"
curl -s -o /dev/null -H 'CF-Connecting-IP: 999.1.1.1' "http://127.0.0.1:$PORT/"
curl -s -o /dev/null -H 'CF-Connecting-IP: <script>' "http://127.0.0.1:$PORT/"
sleep 0.3
grep -q ' 1\.2\.3\.4 ' build/test-access.log  && pass "valid forwarded IP is used from loopback" || fail "forwarded IP not used"
grep -q -e '999\.1\.1\.1' -e '<script>' build/test-access.log && fail "invalid forwarded value logged" || pass "invalid forwarded values ignored"

finish