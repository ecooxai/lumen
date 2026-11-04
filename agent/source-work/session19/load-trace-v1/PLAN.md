# Diagnostic-only namespace load trace

Root approved native-only instrumentation after candidate1904 build seal.
This is not a lifecycle fix, compatibility qualification, or performance improvement.
The test owner will freeze a fixed OFF/ON/OFF/ON sequence using the existing namespace2
cohort,15-second timeout,2workers, identical profile and monitored resource controls.
OFF means LUMEN_TRACE_LOAD unset; ON means exactly1. No runner, guard, WPT or oracle changes.

The trace reads existing native fields only. It does not invoke JS, query DOM through
author APIs, re-run host blocker callbacks, alter scheduling, or change gate conditions.
It records numeric context IDs, parent IDs, monotonic timestamps, realm-call and host-sync
duration, first active blocking reason, existing pending counts, and load dispatch boundaries.
Repeated identical blocker snapshots are deduplicated; output is capped at2048records per
thread (including a final cap marker). No page text, URLs, script strings or capabilities
are logged. Logging overhead is expected and ON results are diagnostic only.

Interpretation must separate final owner-load completion before/after parent readyState and
Window load from a remaining blocker. A late successful parent load supports time-pressure
analysis; absence of load with lingering state calls for examining that concrete blocker.
Neither alone proves candidate1903 source causality. Preserve all original held runs and
the unchanged six-run alternating1808/1903 diagnostic before any proposed fix.
