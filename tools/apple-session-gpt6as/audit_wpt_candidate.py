"""Independently compare frozen native WPT results without hiding missing rows.

The output is a provenance and regression gate, never an overall browser score.
Both inputs retain every scheduled variant, even when no result was recorded.
"""
from collections import Counter
from datetime import datetime, timezone
from pathlib import Path
import argparse
import hashlib
import json
import re


SIGNATURE_FIELDS = (
    "engine", "wpt_revision", "manifest_sha256", "inventory_sha256",
    "source_fixture", "server_config_sha256", "fixture_manifest_sha256",
    "runner_sha256", "reporter_sha256", "monitor_sha256", "origin",
    "secure_origin", "h2_origin", "timeout_s", "long_timeout_s",
    "strict_infrastructure", "native_fixture",
)


def sha(path):
    with Path(path).open("rb") as stream:
        return hashlib.file_digest(stream, "sha256").hexdigest()


def read(path):
    return json.loads(Path(path).read_text())


def key(row):
    return row["type"], row["test"]


def evidence_error(row):
    metrics = row.get("metrics") or {}
    cleanup = row.get("cleanup") or {}
    return (metrics.get("errors") or row.get("telemetry_errors") or
            cleanup.get("error") or cleanup.get("profile_error"))


def qualified(row):
    return row.get("status") == "OK" and not evidence_error(row)


def assertions(row):
    if row is None:
        return {}
    result = {}
    for assertion in row.get("subtests", []):
        name = assertion["name"]
        if name in result:
            raise ValueError("Duplicate assertion name in " + row["test"])
        result[name] = assertion
    return result


def fully_passes(row):
    checks = assertions(row)
    return bool(row and qualified(row) and checks and
                all(check["status"] == "PASS" for check in checks.values()))


def indexed(rows):
    result = {}
    for row in rows:
        identity = key(row)
        if identity in result:
            raise ValueError("Duplicate variant result: " + row["test"])
        assertions(row)
        result[identity] = row
    return result


def assertion_status(row, name, checks=None):
    if row is None:
        return "MISSING_RESULT"
    check = (assertions(row) if checks is None else checks).get(name)
    if check is None:
        return "MISSING_ASSERTION"
    if row.get("status") != "OK":
        return "UNQUALIFIED_HARNESS_" + str(row.get("status"))
    if evidence_error(row):
        return "UNQUALIFIED_EVIDENCE"
    return check["status"]


def summarize_rows(rows, scheduled):
    unique = indexed(rows)
    if len(unique) > scheduled:
        raise ValueError("Recorded variants exceed frozen schedule")
    fully = sum(fully_passes(row) for row in unique.values())
    count = len(unique)
    return {
        "scheduled": scheduled, "recorded": count, "fullyPassing": fully,
        "unrun": scheduled - count,
        "passPercentOfRecorded": 100 * fully / count if count else None,
        "harnessStatuses": dict(Counter(row["status"] for row in rows)),
        "assertions": dict(Counter(test["status"] for row in rows for test in row.get("subtests", []))),
        "qualifiedPassingAssertions": sum(test["status"] == "PASS" for row in rows if qualified(row) for test in row.get("subtests", [])),
        "measurementErrorVariants": sum(bool(evidence_error(row)) for row in rows),
    }


def audit_rows(baseline, candidate):
    before, after = indexed(baseline), indexed(candidate)
    lost, gained, changed, lost_variants = [], [], [], []
    for identity in sorted(set(before) | set(after)):
        a, b = before.get(identity), after.get(identity)
        if a is not None and b is not None and a.get("url") != b.get("url"):
            raise ValueError("Actual URL differs for " + identity[1])
        if fully_passes(a) and not fully_passes(b):
            lost_variants.append(identity[1])
        am, bm = assertions(a), assertions(b)
        for name in sorted(set(am) | set(bm)):
            av, bv = assertion_status(a, name, am), assertion_status(b, name, bm)
            if av == bv:
                continue
            entry = {"test": identity[1], "name": name, "baselineStatus": av, "candidateStatus": bv}
            changed.append(entry)
            if av == "PASS" and bv != "PASS":
                lost.append(entry)
            if bv == "PASS" and av != "PASS":
                gained.append(entry)
    missing = sorted(identity[1] for identity in set(before) - set(after))
    added = sorted(identity[1] for identity in set(after) - set(before))
    new_bad_harnesses = [identity[1] for identity in sorted(set(before) & set(after))
                        if qualified(before[identity]) and not qualified(after[identity])]
    return {
        "missingCandidateVariants": missing,
        "newlyRecordedVariants": added,
        "lostFullyPassingVariants": lost_variants,
        "newUnqualifiedHarnesses": new_bad_harnesses,
        "lostPassingAssertions": lost,
        "gainedPassingAssertions": gained,
        "changedAssertions": changed,
        "hasComparableEvidence": bool(set(before) & set(after)),
        "noRegression": bool(set(before) & set(after)) and not (missing or lost_variants or new_bad_harnesses or lost),
    }


def group_paths(directory, name):
    runtime = directory / name
    if (runtime / "run.json").is_file():
        return runtime / "run.json", runtime / "schedule.json", runtime / "results.jsonl"
    return (directory / (name + "-run.json"), directory / (name + "-schedule.json"),
            directory / (name + "-results.jsonl"))


def load_cohort(plan_path, require_binary=True):
    plan_path = Path(plan_path)
    plan = read(plan_path)
    if not re.fullmatch(r"[a-f0-9]{64}", plan["binary"]["sha256"]):
        raise ValueError("Invalid native binary identity")
    if require_binary and sha(plan["binary"]["path"]) != plan["binary"]["sha256"]:
        raise ValueError("Native binary changed")
    schedule = plan["frozenSchedule"]
    frozen = indexed(schedule)
    flattened = [row for group in plan["groups"].values() for row in group]
    if sorted(flattened, key=key) != sorted(schedule, key=key) or len(indexed(flattened)) != len(frozen):
        raise ValueError("Execution groups changed the frozen schedule")
    if len(frozen) != plan["fullFrozenVariantCount"]:
        raise ValueError("Frozen denominator is inconsistent")
    rows, signatures, evidence = [], {}, {}
    for name, group in plan["groups"].items():
        meta_path, schedule_path, results_path = group_paths(plan_path.parent, name)
        if not results_path.is_file():
            continue
        signature = read(meta_path)["signature"]
        if signature.get("engine") != "lumen" or signature.get("binary_sha256") != plan["binary"]["sha256"]:
            raise ValueError("Raw native artifact identity differs: " + name)
        if signature.get("schedule_sha256") != plan["groupScheduleSHA256"][name]:
            raise ValueError("Recorded group schedule identity differs: " + name)
        scheduled = read(schedule_path)
        if isinstance(scheduled, dict):
            scheduled = scheduled.get("schedule", scheduled.get("tests"))
        if scheduled != group:
            raise ValueError("Raw group schedule differs: " + name)
        records = [json.loads(line) for line in results_path.read_text().splitlines() if line.strip()]
        allowed = indexed(group)
        if not set(indexed(records)).issubset(allowed):
            raise ValueError("Foreign variant result in " + name)
        rows.extend(records)
        signatures[name] = signature
        evidence[name] = {"resultsSHA256": sha(results_path), "runSHA256": sha(meta_path),
                          "scheduleSHA256": sha(schedule_path), "recorded": len(records)}
    indexed(rows)
    return {"plan": plan, "rows": rows, "signatures": signatures, "evidence": evidence,
            "planSHA256": sha(plan_path), "summary": summarize_rows(rows, len(frozen))}


def compare_cohorts(baseline, candidate):
    for field in ("wptRevision", "fullFrozenScheduleSHA256", "fullFrozenVariantCount", "frozenSchedule", "groups"):
        if baseline["plan"][field] != candidate["plan"][field]:
            raise ValueError("Frozen comparison input differs: " + field)
    for group in set(baseline["signatures"]) & set(candidate["signatures"]):
        a, b = baseline["signatures"][group], candidate["signatures"][group]
        for field in SIGNATURE_FIELDS:
            if field not in a or field not in b or a[field] != b[field]:
                raise ValueError("Paired protocol differs: " + group + ":" + field)
    result = audit_rows(baseline["rows"], candidate["rows"])
    result.update(
        createdUTC=datetime.now(timezone.utc).isoformat(),
        scope="Native frozen-cohort regression audit. Unknown variants remain unrun; this is not full WPT or Chrome parity.",
        fullOfficialWptPassPercent=None,
        baseline={"binary": baseline["plan"]["binary"], "planSHA256": baseline["planSHA256"],
                  "summary": baseline["summary"], "evidence": baseline["evidence"]},
        candidate={"binary": candidate["plan"]["binary"], "planSHA256": candidate["planSHA256"],
                   "summary": candidate["summary"], "evidence": candidate["evidence"]},
    )
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--baseline-plan", type=Path, required=True)
    parser.add_argument("--candidate-plan", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    if args.output.exists():
        raise SystemExit("Preserve existing audit; choose a new output path")
    result = compare_cohorts(load_cohort(args.baseline_plan), load_cohort(args.candidate_plan))
    args.output.parent.mkdir(parents=True, exist_ok=True)
    with args.output.open("x") as stream:
        stream.write(json.dumps(result, indent=2) + "\n")
    print(json.dumps({"audit": str(args.output), "noRegression": result["noRegression"],
                      "baseline": result["baseline"]["summary"], "candidate": result["candidate"]["summary"],
                      "lostAssertions": len(result["lostPassingAssertions"]),
                      "gainedAssertions": len(result["gainedPassingAssertions"]),
                      "missingVariants": len(result["missingCandidateVariants"])}, indent=2))
    raise SystemExit(0 if result["noRegression"] else 1)


if __name__ == "__main__":
    main()
