#!/usr/bin/env bash
#
# mayhem/build.sh — build the WebUI fuzz harnesses (fuzz_ws, fuzz_http), their standalone
# reproducers, and the normal-flag selftest that mayhem/test.sh runs. Offline and re-runnable.
#
# WebUI is a C library: src/webui.c plus the vendored civetweb server. The harnesses start the
# real in-process server (mayhem/webui_glue.c includes src/webui.c) and talk to it over loopback.
set -euo pipefail

[ -n "${SOURCE_DATE_EPOCH:-}" ] || unset SOURCE_DATE_EPOCH

: "${SANITIZER_FLAGS=-fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer}"
: "${DEBUG_FLAGS:=-g -gdwarf-3}"
: "${CC:=clang}" ; : "${CXX:=clang++}" ; : "${LIB_FUZZING_ENGINE:=-fsanitize=fuzzer}"
: "${MAYHEM_JOBS:=$(nproc)}"
: "${COVERAGE_FLAGS=}"
export SANITIZER_FLAGS DEBUG_FLAGS CC CXX LIB_FUZZING_ENGINE MAYHEM_JOBS COVERAGE_FLAGS

cd "$SRC"

OUT="$SRC"
B="$SRC/build/mayhem"
mkdir -p "$B/fuzz" "$B/test"

# Background compiles run in parallel; a failure of any one fails the build.
pids=()
wait_all() {
    local p
    for p in "${pids[@]}"; do wait "$p"; done
    pids=()
}

CIVET_DEFS="-DNO_SSL -DNDEBUG -DNO_CACHING -DNO_CGI -DUSE_WEBSOCKET"
INC="-Iinclude -Isrc/civetweb"

# 1) The project with sanitizers + SanCov (fuzzer-no-link) + DWARF 3: civetweb and webui.c (via the glue).
# shellcheck disable=SC2086
$CC $SANITIZER_FLAGS $DEBUG_FLAGS -O1 -fsanitize=fuzzer-no-link -c src/civetweb/civetweb.c \
    $INC $CIVET_DEFS -w -o "$B/fuzz/civetweb.o" & pids+=($!)
# shellcheck disable=SC2086
$CC $SANITIZER_FLAGS $DEBUG_FLAGS -O1 -fsanitize=fuzzer-no-link -c mayhem/webui_glue.c \
    $INC -DNO_SSL -w -o "$B/fuzz/glue.o" & pids+=($!)
# shellcheck disable=SC2086
$CXX $SANITIZER_FLAGS $DEBUG_FLAGS -c mayhem/lsan_off.cc -o "$B/fuzz/lsan_off.o" & pids+=($!)
# shellcheck disable=SC2086
$CC $SANITIZER_FLAGS $DEBUG_FLAGS -c "$STANDALONE_FUZZ_MAIN" -o "$B/fuzz/standalone_main.o" & pids+=($!)
wait_all

# 2) Each harness twice: libFuzzer engine, and the standalone run-once reproducer.
for t in fuzz_ws fuzz_http; do
    # shellcheck disable=SC2086
    $CC $SANITIZER_FLAGS $DEBUG_FLAGS -O1 $LIB_FUZZING_ENGINE mayhem/$t.c \
        "$B/fuzz/glue.o" "$B/fuzz/civetweb.o" "$B/fuzz/lsan_off.o" \
        -lpthread -ldl -lm -o "$OUT/$t" & pids+=($!)
    # shellcheck disable=SC2086
    $CC $SANITIZER_FLAGS $DEBUG_FLAGS -O1 mayhem/$t.c "$B/fuzz/standalone_main.o" \
        "$B/fuzz/glue.o" "$B/fuzz/civetweb.o" "$B/fuzz/lsan_off.o" \
        -lpthread -ldl -lm -o "$OUT/$t-standalone" & pids+=($!)
done
wait_all

# 3) The selftest oracle for mayhem/test.sh: the same server built with the project's normal flags.
# shellcheck disable=SC2086
$CC -O1 $COVERAGE_FLAGS -c src/civetweb/civetweb.c $INC $CIVET_DEFS -w -o "$B/test/civetweb.o" & pids+=($!)
# shellcheck disable=SC2086
$CC -O1 $COVERAGE_FLAGS -c mayhem/webui_glue.c $INC -DNO_SSL -w -o "$B/test/glue.o" & pids+=($!)
wait_all
# shellcheck disable=SC2086
$CC -O1 $COVERAGE_FLAGS mayhem/selftest.c "$B/test/glue.o" "$B/test/civetweb.o" \
    -lpthread -ldl -lm -o "$OUT/webui_selftest"
