# Realm initialization phase diagnostic

This separate diagnostic follows the measured80-realm hotspot. It adds seven native milestones while preserving the existing V8 call expression and ordering. Compare adjacent monotonic and thread CPU timestamps per context to separate context/security setup, native API installation, cached-code binding/compile, prelude execution, API/CSP setup, and microtasks.

Use the immutable diagnostic binary with the existing fixed namespace2 cohort,15s/2workers, and OFF/ON controls preregistered by the test lane. Do not launch a competing build during the full1248 replay. The shared2048-record cap applies to all load and phase records. These logs provide profiling evidence only; they do not qualify a lifecycle or performance fix.
