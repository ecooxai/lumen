#!/bin/bash
set -euo pipefail

if [ "$#" -ne 2 ]; then
    printf 'Usage: bash ios/ui-tests/run.sh build-sim/Lumen.app SIMULATOR-UDID\n' >&2
    exit 2
fi
test_root=$(cd "$(dirname "$0")/../.." && pwd)
test_app=$(python3 -c 'from pathlib import Path; import sys; print(Path(sys.argv[1]).resolve(strict=True))' "$1")
test_device=$2
test_port=${LUMEN_UI_TEST_PORT:-8789}
test_output="$test_root/build-ui-tests/gpt-6-astra-chatgpt-xctest-$(date -u +%Y%m%d-%H%M%S)"
mkdir -p "$test_output"
test_bundle=com.ecooxai.lumen
test_server_pid=

finish() {
    trap - EXIT INT TERM
    xcrun simctl terminate "$test_device" "$test_bundle" >/dev/null 2>&1 || true
    # Let the per-PID recorder write its final JSON after the app exits.
    sleep 2
    if [ -n "$test_server_pid" ]; then
        kill "$test_server_pid" 2>/dev/null || true
        wait "$test_server_pid" 2>/dev/null || true
    fi
}
trap finish EXIT INT TERM

xcrun simctl bootstatus "$test_device" -b >"$test_output/simulator-boot.log" 2>&1
xcrun simctl install "$test_device" "$test_app"
test_container=$(xcrun simctl get_app_container "$test_device" "$test_bundle" data)
test_installed=$(xcrun simctl get_app_container "$test_device" "$test_bundle" app)
python3 "$test_root/ios/ui-tests/fixture_server.py" --port "$test_port" --app-binary "$test_installed/Lumen" --output "$test_output" >"$test_output/fixture.log" 2>&1 &
test_server_pid=$!
python3 - "$test_port" <<'PY'
import sys, time, urllib.request
url = 'http://127.0.0.1:' + sys.argv[1] + '/health'
for attempt in range(10):
    try:
        with urllib.request.urlopen(url, timeout=1) as response:
            if response.status == 200:
                break
    except OSError:
        time.sleep(0.3)
else:
    raise SystemExit('UI fixture server did not start')
PY

test_status=0
xcodebuild test -project "$test_root/ios/ui-tests/LumenUITests.xcodeproj" -scheme LumenUITests \
    -sdk iphonesimulator -destination "platform=iOS Simulator,id=$test_device" \
    -derivedDataPath "$test_root/build-ui-tests/DerivedData" -resultBundlePath "$test_output/LumenUI.xcresult" \
    -parallel-testing-enabled NO -maximum-concurrent-test-simulator-destinations 1 -jobs 2 \
    -collect-test-diagnostics never \
    CODE_SIGNING_ALLOWED=NO "LUMEN_UI_TEST_BASE=http://127.0.0.1:$test_port" \
    "LUMEN_UI_TEST_PROFILE_ROOT=$test_container/tmp/lumen-ui-tests" >"$test_output/xcodebuild.log" 2>&1 || test_status=$?

finish
printf 'XCTest result: %s\nEvidence directory: %s\n' "$test_status" "$test_output"
if [ "$test_status" -ne 0 ]; then
    tail -n 60 "$test_output/xcodebuild.log"
    exit "$test_status"
fi
python3 - "$test_output" <<'PY'
import csv, json, sys
from pathlib import Path
root = Path(sys.argv[1])
files = sorted(root.glob('resource-*.json'))
if not files:
    raise SystemExit('UI assertions passed, but no CPU/RSS report was produced; verification is incomplete')
for path in files:
    report = json.loads(path.read_text())
    if not report.get('samples'):
        raise SystemExit('UI assertions passed, but a resource report has no samples')
    print(path.name + ': ' + json.dumps(report))
PY
