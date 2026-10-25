#!/bin/bash
cd "$(dirname "$0")/../.." || exit 1
source tests/integration/lib.sh

echo "-- CSP lint (the server forbids inline code, so find it before the browser does)"
inline=$(grep -nE '<style|[[:space:]]style=|[[:space:]]on[a-z]+=' public/*.html || true)
[ -z "$inline" ] && pass "no inline styles or event handlers" || fail "inline code: $inline"
noscript=$(grep -nE '<script' public/*.html | grep -v 'src=' || true)
[ -z "$noscript" ] && pass "no inline scripts" || fail "inline script: $noscript"
inner=$(grep -n 'innerHTML\|outerHTML\|insertAdjacentHTML\|document.write' public/js/*.js || true)
[ -z "$inner" ] && pass "no HTML-string injection in the JS" || fail "HTML injection sinks: $inner"
for f in public/*.html; do
    for ref in $(grep -oE '(src|href)="/[^"]*"' "$f" | sed -E 's/^(src|href)="//; s/"$//'); do
        [ "$ref" = "/" ] && continue
        [ -e "public$ref" ] || fail "$f references a missing file: $ref"
    done
done
pass "all referenced files exist"

echo "-- served by the real server"
make_conf "doc_root = public"
start_server || exit 1
for page in / /event/ctf/challenges.html /event/ctf/challenge.html /blog/ctf_writeups/writeups.html /blog/ctf_writeups/writeup.html /css/style.css /js/common.js; do
    assert_eq "$(curl -s -o /dev/null -w '%{http_code}' "http://127.0.0.1:$PORT$page")" 200 "GET $page"
done
curl -sI "http://127.0.0.1:$PORT/event/ctf/challenges.html" | grep -qi '^content-security-policy:'    && pass "CSP header on pages" || fail "no CSP"
curl -sI "http://127.0.0.1:$PORT/js/common.js" | grep -qi '^content-type: text/javascript' && pass "JS content type" || fail "JS content type"
curl -sI "http://127.0.0.1:$PORT/css/style.css" | grep -qi '^content-type: text/css'       && pass "CSS content type" || fail "CSS content type"
curl -sI "http://127.0.0.1:$PORT/challenges.html" | grep -qi '^content-security-policy:'    && pass "CSP header on pages" || fail "no CSP"
finish