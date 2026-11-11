#!/usr/bin/env bash
#
# mayhem/test.sh — run the WebUI selftest (built by mayhem/build.sh with the project's normal flags).
# It starts the real server in-process and asserts, over HTTP and the binary WebSocket protocol,
# the served bytes/status codes, the token check + bind list, and the result of a bound function
# call. Each assertion prints PASS:/FAIL:; this script maps them to CTRF.
set -uo pipefail
[ -n "${SOURCE_DATE_EPOCH:-}" ] || unset SOURCE_DATE_EPOCH
: "${MAYHEM_JOBS:=$(nproc)}"
cd "$SRC"

emit_ctrf() {
  local tool="$1" passed="$2" failed="$3" skipped="${4:-0}" pending="${5:-0}" other="${6:-0}"
  local tests=$(( passed + failed + skipped + pending + other ))
  cat > "${CTRF_REPORT:-$SRC/ctrf-report.json}" <<JSON
{
  "results": {
    "tool": { "name": "$tool" },
    "summary": {
      "tests": $tests,
      "passed": $passed,
      "failed": $failed,
      "pending": $pending,
      "skipped": $skipped,
      "other": $other
    }
  }
}
JSON
  printf 'CTRF {"results":{"tool":{"name":"%s"},"summary":{"tests":%d,"passed":%d,"failed":%d,"pending":%d,"skipped":%d,"other":%d}}}\n' \
    "$tool" "$tests" "$passed" "$failed" "$pending" "$skipped" "$other"
  [ "$failed" -eq 0 ]
}

EXPECTED=12   # assertions in mayhem/selftest.c; fewer PASS lines means the program did not run them all

if [ ! -x "$SRC/webui_selftest" ]; then
  echo "webui_selftest missing: mayhem/build.sh must build it" >&2
  emit_ctrf "webui-selftest" 0 1 0
  exit 1
fi

out="$(timeout 120 "$SRC/webui_selftest" 2>&1)"
rc=$?
printf '%s\n' "$out"

passed=$(printf '%s\n' "$out" | grep -c '^PASS: ')
failed=$(printf '%s\n' "$out" | grep -c '^FAIL: ')
# A run that ended early, crashed, or exited non-zero cannot have passed everything it should have.
missing=$(( EXPECTED - passed - failed ))
[ "$missing" -lt 0 ] && missing=0
failed=$(( failed + missing ))
[ "$rc" -ne 0 ] && [ "$failed" -eq 0 ] && failed=1

emit_ctrf "webui-selftest" "$passed" "$failed" 0
