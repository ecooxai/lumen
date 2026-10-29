#!/bin/sh
set -eu
cd /Users/devin/repos/lumen
if [ "$#" -ne 2 ]; then
    printf '%s\n' 'Usage: run_native_resources_10292019r.sh BUILD_SUMMARY OUTPUT_DIRECTORY' >&2
    exit 2
fi
exec python3 tools/apple-session-gpt6as/run_native_resources_20261029_cpu30.py --build-summary "$1" --output "$2"
