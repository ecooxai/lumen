#!/bin/sh
# Run web-platform-tests through build/jsrun and summarise per directory.
# usage: tests/wpt/run.sh OUTDIR DIR... (paths relative to the WPT root)
# Needs tests/wpt/serve.py running (WPT_URL, default http://127.0.0.1:8799).
set -u
WPT=${WPT:-$HOME/src/wpt}; URL=${WPT_URL:-http://127.0.0.1:8799}; TO=${WPT_TIMEOUT:-6000}
out=$1; shift; mkdir -p "$out"; : > "$out/results.tsv"
cd "$(dirname "$0")/../.." || exit 1
for dir in "$@"; do
  (cd "$WPT" && find "$dir" \( -name '*.html' -o -name '*.htm' -o -name '*.any.js' -o -name '*.window.js' \) \
     ! -path '*/resources/*' ! -path '*/support/*' ! -path '*/crashtests/*' ! -name '*-ref.htm*' ! -name '*-manual*' ! -name '*.sub.*' ! -name '*.https.*' \
     ! -name '*.tentative.*' ! -path '*/tentative/*' | sort) | while read -r f; do
    case $f in *.js) p=${f%.js}.html ;; *) p=$f ;; esac
    grep -qs 'testharness.js' "$WPT/$f" || case $f in *.js) ;; *) continue ;; esac
    if grep -qs 'testdriver' "$WPT/$f"; then printf '%s\t0\t0\tSKIP_TESTDRIVER\n' "$p" >> "$out/results.tsv"; continue; fi
    log="$out/$(echo "$p" | tr '/' '_').log"
    JSRUN_UNTIL_ATTR=data-wpt-done LUMEN_CONSOLE=1 LUMEN_JS_TIMEOUT_MS=5000 timeout 20 ./build/jsrun "$URL/$p" "$TO" > "$log" 2>&1
    pass=$(grep -c 'WPT-SUB PASS' "$log"); tot=$(grep -c 'WPT-SUB ' "$log")
    h=$(grep -o 'WPT-DONE harness=[A-Z_]*' "$log" | head -1 | cut -d= -f2); [ -n "$h" ] || h=NO_RESULT
    printf '%s\t%s\t%s\t%s\n' "$p" "$pass" "$tot" "$h" >> "$out/results.tsv"
  done
done
awk -F'\t' '{ split($1, a, "/"); d = a[1] "/" a[2]; P[d] += $2; T[d] += $3; F[d]++; if ($4 == "SKIP_TESTDRIVER") S[d]++; else if ($4 != "OK") B[d]++; tp += $2; tt += $3 }
  END { for (d in P) printf "%-40s files=%4d subtests=%6d pass=%6d (%5.1f%%) harness-errors=%d\n", d, F[d], T[d], P[d], T[d] ? 100 * P[d] / T[d] : 0, B[d];
        printf "TOTAL subtests=%d pass=%d (%.1f%%)\n", tt, tp, tt ? 100 * tp / tt : 0 }' "$out/results.tsv" | sort
