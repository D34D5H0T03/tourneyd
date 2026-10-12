#!/bin/bash
set -e # Exit immediately if any compilation or test fails

mkdir -p build/tests

echo "--- Compiling Unit Tests ---"
# Parser Test
gcc -Iinclude -Wall -Wextra -g tests/unit/test_parser.c \
    src/http/parser.c src/http/status.c \
    -o build/tests/test_parser

# Response Test
gcc -Iinclude -Wall -Wextra -g tests/unit/test_response.c \
    src/http/response.c src/http/status.c src/http/parser.c \
    -o build/tests/test_response

# Router Test
gcc -Iinclude -Wall -Wextra -g tests/unit/test_router.c \
    src/http/router.c src/http/response.c src/http/status.c \
    src/http/parser.c src/util/log.c \
    -o build/tests/test_router

echo -e "\n--- Running Parser Tests ---"
./build/tests/test_parser

echo -e "\n--- Running Response Tests ---"
./build/tests/test_response

echo -e "\n--- Running Router Tests ---"
./build/tests/test_router

echo -e "\n✅ All test suites passed successfully!"