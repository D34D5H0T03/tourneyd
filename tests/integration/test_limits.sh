#!/bin/bash
cd "$(dirname "$0")/../.." || exit 1
source tests/integration/lib.sh
setup_docroot; make_conf; start_server || exit 1
URL="http://127.0.0.1:$PORT"

echo "-- connection cap"
open_idle 14; sleep 1
kids=$(pgrep -P "$SERVER_PID" | wc -l)
[ "$kids" -le 8 ] && pass "child cap holds ($kids <= 8)" || fail "child cap exceeded ($kids children)"
code=$(curl -s -o /dev/null -m 20 -w '%{http_code}' "$URL/")
assert_eq "$code" 200 "a queued client is served once idle children expire"
close_idle
kill -0 "$SERVER_PID" 2>/dev/null && pass "server survived the flood" || fail "server died"

echo "-- timeouts"
before=$(wc -l < build/test-access.log)
exec {fd}<>"/dev/tcp/127.0.0.1/$PORT"; sleep 3
data=$(timeout 1 cat <&"$fd"); exec {fd}>&-
assert_eq "$data" "" "idle connection is closed without a response"
sleep 0.3
assert_eq "$(wc -l < build/test-access.log)" "$before" "idle connection writes no access line"

start=$SECONDS
raw 'GET / HT' 8 | grep -q 408 && pass "half-sent request gets 408" || fail "no 408 for half request"
[ $((SECONDS-start)) -le 6 ] && pass "...within the request deadline" || fail "408 came too late"

slowloris() {
    local fd out=build/slow.out
    exec {fd}<>"/dev/tcp/127.0.0.1/$PORT"
    timeout 10 cat <&"$fd" > "$out" &            # read concurrently so a late RST cannot eat the reply
    local reader=$!
    printf 'GET / HTTP/1.1\r\nHost: x\r\n' >&"$fd"
    for _ in $(seq 8); do sleep 1; printf 'X-Pad: 1\r\n' >&"$fd" 2>/dev/null || break; done
    wait "$reader" 2>/dev/null; exec {fd}>&-; cat "$out"
}
start=$SECONDS
slowloris | grep -q 408 && pass "slowloris is cut off with 408" || fail "slowloris not cut off"
[ $((SECONDS-start)) -le 7 ] && pass "...well before the attacker finished" || fail "slowloris held a child too long"

echo "-- keep-alive rules"
req='GET / HTTP/1.1\r\nHost: x\r\n\r\n'
out=$(raw "$(for _ in 1 2 3 4 5 6; do printf '%s' "$req"; done)" 4)
assert_eq "$(grep -c '^HTTP/1.1 200' <<<"$out")" 5 "max_requests_per_conn = 5 closes after the fifth"
grep -c '^Connection: close' <<<"$out" | grep -q '^1$' && pass "the last response announces close" || fail "no Connection: close on the last"
out=$(raw "GET /nope HTTP/1.1\r\nHost: x\r\n\r\nGET / HTTP/1.1\r\nHost: x\r\nConnection: close\r\n\r\n")
assert_eq "$(grep -c '^HTTP/1.1 ' <<<"$out")" 2 "a routed 404 keeps the connection open"

echo "-- leaks and zombies"
fds_before=$(ls /proc/"$SERVER_PID"/fd | wc -l)
for _ in $(seq 300); do curl -s -o /dev/null "$URL/"; done
sleep 1
assert_eq "$(ls /proc/"$SERVER_PID"/fd | wc -l)" "$fds_before" "parent fd count unchanged after 300 requests"
assert_eq "$(ps -o stat= --ppid "$SERVER_PID" | grep -c '^Z')" 0 "no zombie children"

echo "-- crash reporting"
open_idle 1; sleep 0.5
child=$(pgrep -P "$SERVER_PID" | head -n1)
kill -KILL "$child"; sleep 1
grep -q 'killed by signal 9' build/test-error.log && pass "a killed child is reported" || fail "child death not logged"
kill -0 "$SERVER_PID" 2>/dev/null && pass "server survived a child crash" || fail "server died"
close_idle

echo "-- graceful shutdown"
open_idle 2; sleep 0.5
start=$SECONDS; kill -TERM "$SERVER_PID"; wait "$SERVER_PID"; rc=$?; SERVER_PID=
assert_eq "$rc" 0 "exit status 0 with idle connections open"
[ $((SECONDS-start)) -le 4 ] && pass "idle connections do not delay shutdown" || fail "shutdown too slow"
close_idle
start_server && pass "immediate restart binds the port" || fail "restart failed"

curl -s --limit-rate 3M -o build/dl.bin -w '%{size_download}' "$URL/big.bin" > build/dl.size &
dl=$!; sleep 1; kill -TERM "$SERVER_PID"; wait "$dl"
assert_eq "$(cat build/dl.size)" 6291456 "in-flight download completes during shutdown"
wait "$SERVER_PID"; assert_eq $? 0 "server exits cleanly afterwards"; SERVER_PID=

make_conf "shutdown_grace_sec = 2"; start_server
curl -s --limit-rate 50k -o /dev/null -m 15 "$URL/big.bin" &
dl=$!; sleep 1
start=$SECONDS; kill -TERM "$SERVER_PID"; wait "$SERVER_PID"; SERVER_PID=
[ $((SECONDS-start)) -le 5 ] && pass "stragglers are killed after the grace period" || fail "grace period not enforced"
grep -q 'killing' build/test-error.log && pass "straggler kill is logged" || fail "no straggler log line"
kill "$dl" 2>/dev/null

finish