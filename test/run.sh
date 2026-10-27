#!/bin/sh
# Host tests: C++ rules shared with the firmware, plus the portal script on macOS.
set -eu
cd "$(dirname "$0")/.."
out="${TMPDIR:-/tmp}/onx-tests"; mkdir -p "$out"
flags="-std=c++17 -Wall -Wextra -Werror -Iinclude"
if command -v xcrun >/dev/null 2>&1; then
    flags="$flags -isystem $(xcrun --show-sdk-path)/usr/include/c++/v1"
fi
cjson="${IDF_PATH:-$HOME/esp/esp-idf}/components/json/cJSON"
for test in test/*_test.cpp; do
    name=$(basename "$test" .cpp); extra=""
    if grep -q EntityTracker "$test"; then
        if [ ! -f "$cjson/cJSON.c" ]; then echo "$name: SKIP (no cJSON, set IDF_PATH)"; continue; fi
        # cJSON is C; build it separately so -Werror applies only to project code.
        cc -c -O1 -w "$cjson/cJSON.c" -o "$out/cJSON.o"; extra="-I$cjson $out/cJSON.o"
    fi
    c++ $flags $extra "$test" -o "$out/$name"
    "$out/$name"; echo "$name: OK"
done
if command -v swift >/dev/null 2>&1; then swift test/portal_flow_test.swift; fi
