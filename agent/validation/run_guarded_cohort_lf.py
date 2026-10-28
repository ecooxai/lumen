#!/usr/bin/env python3
"""Run a cohort and drain real sealed-capture-guard cleanup workers.

Browser launch, refusal, handshake and assertions remain in the unchanged
runners. This adapter prevents interpreter exit from cutting off audit cleanup.
"""
from __future__ import annotations
import argparse
import hashlib
import json
import pathlib
import runpy
import sys
import threading
import time

ROOT = pathlib.Path(__file__).resolve().parents[2]
VALIDATION = ROOT / 'agent/validation'
CHROMIUM = pathlib.Path('/home/a/.local/archpkg/pacman/usr/bin/chromium').resolve()


def option(arguments: list[str], name: str) -> str | None:
    value = None
    for index, argument in enumerate(arguments):
        if argument == name and index + 1 < len(arguments):
            value = arguments[index + 1]
        elif argument.startswith(name + '='):
            value = argument.split('=', 1)[1]
    return value


def source_hashes(paths: list[pathlib.Path]) -> dict[str, str]:
    return {str(p.relative_to(ROOT)): hashlib.sha256(p.read_bytes()).hexdigest() for p in paths}


def cleanup_workers() -> list[threading.Thread]:
    result = []
    for thread in threading.enumerate():
        target = getattr(thread, '_target', None)
        if (getattr(target, '__module__', '') == 'native_capture_guard'
                and getattr(target, '__qualname__', '') == 'guarded_popen.<locals>.cleanup'):
            result.append(thread)
    return result


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--runner', choices=('paired_wpt_lf',), required=True)
    parser.add_argument('--audit-report', type=pathlib.Path, required=True)
    parser.add_argument('--cleanup-timeout', type=float, default=5)
    parser.add_argument('arguments', nargs=argparse.REMAINDER)
    config = parser.parse_args()
    if not 0 < config.cleanup_timeout <= 60:
        parser.error('cleanup-timeout must be positive and at most60 seconds')
    arguments = config.arguments[1:] if config.arguments[:1] == ['--'] else config.arguments
    kind = option(arguments, '--kind')
    binary_argument = option(arguments, '--binary')
    binary = pathlib.Path(binary_argument).resolve() if binary_argument else None
    # --binary selects this runner's browser, not its comparison reference.
    if kind in ('chromium', 'chrome-cdp') and binary != CHROMIUM:
        parser.error('Chromium kind requires the pinned installed Chromium executable')
    if kind == 'lumen' and binary == CHROMIUM:
        parser.error('Lumen kind cannot use the Chromium executable')
    target = VALIDATION / (config.runner + '.py')
    paths = list(dict.fromkeys([pathlib.Path(__file__).resolve(), target,
        VALIDATION / 'native_capture_guard.py', VALIDATION / 'paired_wpt.py', VALIDATION / 'wpt_console_frames.py',
        ROOT / 'tests/wpt/lumen_wpt.py']))
    before = source_hashes(paths)
    failure = None
    started = time.monotonic()
    sys.path.insert(0, str(VALIDATION))
    sys.argv = [str(target), *arguments]
    try:
        runpy.run_path(str(target), run_name='__main__')
    except BaseException as error:
        failure = error
    cleanup_started = time.monotonic()
    workers = cleanup_workers()
    deadline = cleanup_started + config.cleanup_timeout
    for worker in workers:
        worker.join(max(0, deadline - time.monotonic()))
    remaining = cleanup_workers()
    after = source_hashes(paths)
    report = {
        'runner': str(target.relative_to(ROOT)), 'arguments': arguments,
        'binary': str(binary) if binary else None,
        'source_hashes_before': before, 'source_hashes_after': after,
        'sources_unchanged': before == after,
        'joined_cleanup_threads': len(workers),
        'remaining_cleanup_threads': [t.name for t in remaining],
        'cleanup_elapsed_s': time.monotonic() - cleanup_started,
        'cohort_elapsed_s': time.monotonic() - started,
        'runner_exception': repr(failure) if failure is not None else None,
        'method': 'Join actual trusted guard cleanup workers; never synthesize audit records.',
    }
    config.audit_report.parent.mkdir(parents=True, exist_ok=True)
    config.audit_report.write_text(json.dumps(report, indent=2) + '\n')
    print(json.dumps({'guard_cleanup': {'report': str(config.audit_report),
        'joined': len(workers), 'remaining': len(remaining),
        'sources_unchanged': before == after}}), flush=True)
    if remaining or before != after:
        raise RuntimeError('Cohort cleanup/provenance gate failed; see retained report') from failure
    if failure is not None:
        raise failure


if __name__ == '__main__':
    main()
