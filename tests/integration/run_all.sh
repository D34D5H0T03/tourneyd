#!/bin/bash
cd "$(dirname "$0")/../.." || exit 1
fails=0
for t in tests/integration/test_*.sh; do 
    echo "== $t"
    bash "$t" || fails=$((fails+1))
done
[ "$fails" -eq 0 ] && echo "all integration suites passed" || { echo "$fails suite(s) failed"; exit 1; }