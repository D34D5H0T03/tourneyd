#!/bin/bash
cd "$(dirname "$0")/../.." || exit 1
source tests/integration/lib.sh

rm -f build/test-api.db build/test-api.db-wal build/test-api.db-shm
make_conf "db_path = build/test-api.db" "migrations_dir = migrations"
CTL=(./build/tourneyctl -c build/test.conf)
FX=build/api-fixtures; mkdir -p "$FX"
mkdir -p build/test-public

# ---- fixtures ----
printf 'First challenge.\nSecond line with "quotes".\n' > "$FX/d1.txt"
printf 'Another one.\n' > "$FX/d2.txt"
printf 'Café "quoted" line\nsecond line\\backslash\n' > "$FX/w.md"
"${CTL[@]}" migrate > /dev/null
add() { printf '%s' "$2" | "${CTL[@]}" challenge add --slug "$1" --title "$3" --category "$4" --points "$5" \
          --desc-file "$FX/d1.txt" --flag-stdin > /dev/null; }
add api-one    'flag{api-test-one}' "API One" web 100
add api-two    'flag{api-test-two}' "API Two" crypto 200
add hidden-one 'flag{hidden-one}'   "Hidden"  misc 10
add rate-limit 'flag{rate-limit}'   "Rate"    misc 10
add burst      'flag{burst}'        "Burst"   misc 10
"${CTL[@]}" challenge hide --slug hidden-one > /dev/null
for s in wu-a wu-b wu-c wu-hidden; do
    "${CTL[@]}" writeup add --slug "$s" --title "Writeup $s" --author tester --file "$FX/w.md" > /dev/null
done
"${CTL[@]}" writeup hide --slug wu-hidden > /dev/null

start_server || exit 1
URL="http://127.0.0.1:$PORT"
status() { curl -s -o /dev/null -w '%{http_code}' "$@"; }
headers() { curl -s -D - -o /dev/null "$@"; }
submit() { curl -s -w '\n%{http_code}' -X POST -H 'Content-Type: text/plain' --data-binary "$2" "$URL/api/challenges/$1/submit"; }
code_of() { echo "${1##*$'\n'}"; }
json_of() { echo "${1%$'\n'*}"; }

echo "-- challenge list and detail"
out=$(curl -s "$URL/api/challenges")
grep -q '"slug":"api-one"' <<<"$out"            && pass "list contains a published challenge" || fail "list missing api-one"
grep -q 'hidden-one' <<<"$out"                  && fail "hidden challenge is listed" || pass "hidden challenge is not listed"
grep -qiE 'hash|salt|flag\{' <<<"$out"          && fail "list leaks flag material" || pass "list contains no flag material"
headers "$URL/api/challenges" | grep -qi '^content-type: application/json' && pass "JSON content type" || fail "content type"
headers "$URL/api/challenges" | grep -qi '^cache-control: no-store'         && pass "no-store"          || fail "cache-control"
out=$(curl -s "$URL/api/challenges/api-one")
grep -qF '"description":"First challenge.\nSecond line with \"quotes\".\n"' <<<"$out" \
                                                && pass "description is escaped correctly" || fail "description escaping: $out"
grep -qiE 'hash|salt|flag\{' <<<"$out"          && fail "detail leaks flag material" || pass "detail contains no flag material"
for p in nope hidden-one Bad_Slug "%27%20OR%201%3D1" "api-one/extra"; do
    [ "$(status "$URL/api/challenges/$p")" = 404 ] && pass "404 for /api/challenges/$p" || fail "expected 404 for $p"
done

echo "-- flag submission"
out=$(submit api-one 'flag{api-test-one}');   assert_eq "$(code_of "$out") $(json_of "$out")" '200 {"correct":true}'  "correct flag"
out=$(submit api-one 'flag{wrong}');          assert_eq "$(code_of "$out") $(json_of "$out")" '200 {"correct":false}' "wrong flag"
out=$(submit api-one $'flag{api-test-one}\n'); assert_eq "$(json_of "$out")" '{"correct":true}' "trailing newline is trimmed"
assert_eq "$(code_of "$(submit api-one '')")" 400 "empty body"
assert_eq "$(code_of "$(submit api-one "$(head -c 300 /dev/zero | tr '\0' a)")")" 400 "300-byte flag"
code=$(printf 'a\0b' | curl -s -o /dev/null -w '%{http_code}' -X POST --data-binary @- "$URL/api/challenges/api-one/submit")
assert_eq "$code" 400 "NUL byte in the flag"
assert_eq "$(code_of "$(submit nope 'x')")" 404 "unknown challenge"
assert_eq "$(code_of "$(submit hidden-one 'flag{hidden-one}')")" 404 "hidden challenge cannot be solved"
assert_eq "$(status "$URL/api/challenges/api-one/submit")" 404 "GET on the submit path"
assert_eq "$(status -X DELETE "$URL/api/challenges")" 405 "DELETE on the list"
headers -X DELETE "$URL/api/challenges" | grep -qi '^allow:.*GET' && pass "Allow header on 405" || fail "no Allow header"
assert_eq "$(status -X POST "$URL/api/writeups")" 405 "POST on writeups"
assert_eq "$(status -I "$URL/api/challenges")" 200 "HEAD works"

submit api-one 'flag{NEEDLE-in-the-log}' > /dev/null; sleep 0.3
grep -q 'NEEDLE' build/test-access.log && fail "a submitted flag reached the access log" || pass "access log never contains flags"
grep -q 'flag{api-test-one}' build/test-access.log && fail "a real flag reached the log" || pass "real flag not in the log either"

echo "-- rate limit"
for i in $(seq 30); do submit rate-limit "wrong$i" > /dev/null; done
out=$(submit rate-limit 'wrong31');           assert_eq "$(code_of "$out")" 429 "31st attempt in a minute is limited"
out=$(submit rate-limit 'flag{rate-limit}');  assert_eq "$(code_of "$out")" 429 "even the right flag waits (global limit)"
curl -s -D - -o /dev/null -X POST --data-binary x "$URL/api/challenges/rate-limit/submit" | grep -qi '^retry-after: 60' \
                                                && pass "Retry-After header" || fail "no Retry-After"

echo "-- concurrent writers (SQLite writer lock)"
codes=$(seq 100 | xargs -P 20 -I{} curl -s -o /dev/null -w '%{http_code}\n' -X POST --data-binary "x{}" \
        "$URL/api/challenges/burst/submit" | sort -u | tr '\n' ' ')
grep -qE '(^| )5[0-9][0-9]( |$)' <<<"$codes" && fail "server errors under concurrent submits: $codes" || pass "no 5xx under 20 parallel writers ($codes)"

echo "-- writeups"
out=$(curl -s "$URL/api/writeups")
assert_eq "$(grep -o '"slug":"[a-z0-9-]*"' <<<"$out" | head -n1)" '"slug":"wu-c"' "newest first"
grep -q '"total":3' <<<"$out"                   && pass "total counts only published" || fail "total: $out"
grep -q 'wu-hidden' <<<"$out"                   && fail "hidden writeup is listed" || pass "hidden writeup is not listed"
grep -q '"challenge":null' <<<"$out"            && pass "unset links are null" || fail "null links"
out=$(curl -s "$URL/api/writeups?limit=1&offset=1")
grep -q '"slug":"wu-b"' <<<"$out" && grep -q '"limit":1' <<<"$out" && pass "pagination" || fail "pagination: $out"
curl -s "$URL/api/writeups?limit=1000" | grep -q '"limit":50' && pass "limit is clamped to 50" || fail "limit clamp"
curl -s "$URL/api/writeups?limit=abc"  | grep -q '"limit":20' && pass "bad limit uses the default" || fail "bad limit"
curl -s "$URL/api/writeups?offset=-5"  | grep -q '"offset":0' && pass "negative offset is clamped" || fail "offset clamp"
out=$(curl -s "$URL/api/writeups/wu-a")
grep -q 'Café' <<<"$out"                        && pass "UTF-8 is preserved" || fail "UTF-8 lost"
grep -qF '\"quoted\"' <<<"$out" && grep -qF '\\backslash' <<<"$out" && pass "quotes and backslashes escaped" || fail "escaping: $out"
assert_eq "$(status "$URL/api/writeups/wu-hidden")" 404 "hidden writeup"
assert_eq "$(status "$URL/api/writeups/Bad_Slug")" 404 "bad writeup slug"

grep -q ERROR build/test-error.log && fail "ERROR lines in the server log" || pass "no errors logged"
finish