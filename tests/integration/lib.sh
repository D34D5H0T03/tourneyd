#!/bin/bash
# Sourced by every suite. Run from the repo root.
PORT=${PORT:-18080}
BIN=${BIN:-./build/tourneyd}
PASSES=0; FAILS=0; SERVER_PID=
trap '' PIPE                       # writing to a closed socket must not kill the test script
trap 'stop_server' EXIT
mkdir -p build

pass()      { PASSES=$((PASSES+1)); echo "  ok    $1"; }
fail()      { FAILS=$((FAILS+1));   echo "  FAIL  $1"; }
assert_eq() { if [ "$1" = "$2" ]; then pass "$3"; else fail "$3 (got '$1', want '$2')"; fi; }
finish()    { stop_server; echo "$PASSES passed, $FAILS failed"; [ "$FAILS" -eq 0 ]; }

setup_docroot() {
    rm -rf build/test-public; mkdir -p build/test-public/css
    echo '<!doctype html><title>t</title><p>hello</p>' > build/test-public/index.html
    echo 'body{}'  > build/test-public/css/style.css
    echo secret    > build/test-public/.hidden
    ln -s /etc/passwd build/test-public/leak.txt
    head -c 6291456 /dev/urandom > build/test-public/big.bin
}

make_conf() {                      # extra "key = value" lines may be passed as arguments
    { printf '%s\n' "port = $PORT" "bind_addr = 127.0.0.1" "doc_root = build/test-public" \
        "max_children = 8" "idle_timeout_sec = 2" "request_timeout_sec = 3" \
        "max_requests_per_conn = 5" "shutdown_grace_sec = 6"
      printf '%s\n' "$@"; } > build/test.conf
}

start_server() {
    : > build/test-access.log; : > build/test-error.log
    "$BIN" -c build/test.conf >> build/test-access.log 2>> build/test-error.log &
    SERVER_PID=$!
    for _ in $(seq 50); do nc -z 127.0.0.1 "$PORT" 2>/dev/null && return 0; sleep 0.1; done
    echo "server did not start:"; cat build/test-error.log; return 1
}
stop_server()    { [ -n "$SERVER_PID" ] || return 0; kill -TERM "$SERVER_PID" 2>/dev/null
                   wait "$SERVER_PID" 2>/dev/null; SERVER_PID=; }
restart_server() { stop_server; start_server; }

# raw "<payload>" [seconds]: sends bytes exactly as written (printf %b escapes), prints the reply.
raw() {
    local fd
    exec {fd}<>"/dev/tcp/127.0.0.1/$PORT" || return 1
    printf '%b' "$1" >&"$fd"
    timeout "${2:-2}" cat <&"$fd" || true
    exec {fd}>&-
}
raw_status() { local s; s=$(raw "$@" | head -n1 | awk '{print $2}'); echo "${s:-none}"; }

open_idle()  { IDLE_FDS=(); for _ in $(seq "$1"); do exec {fd}<>"/dev/tcp/127.0.0.1/$PORT" && IDLE_FDS+=("$fd"); done; }
close_idle() { for fd in "${IDLE_FDS[@]}"; do exec {fd}>&-; done; IDLE_FDS=(); }