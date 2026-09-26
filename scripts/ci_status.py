#!/usr/bin/env python3
"""What each CI job last actually said, rather than what the latest run shows.

`gh run list` shows the latest run. When that run was cancelled -- by a newer
push, which is what `cancel-in-progress` is for -- the latest run says nothing,
and nothing distinguishes "not tested" from "tested and fine". A cancelled job
is not a failed one.

This is not a hypothetical failure mode. The Linux job was cancelled on five
consecutive pushes to main while Windows failed on every one of them, and what
exposed it was a sixth push adding a macOS job, whose failure was then assumed to
be new. Five red runs read as four cancellations and a mystery.

So: for every job, the most recent run that reached a verdict, and how many newer
runs did not. That second number is the one that matters -- it is the size of the
blind spot, and on the day above it would have read 5.

Shards are folded together: `the AppKit host (shard 2)` is the AppKit host. A
run only counts as a verdict for a job when every one of that job's shards
reached one, because three green shards and one cancelled shard is not a tested
commit.

Usage:  scripts/ci_status.py [--limit 15] [--json]

Exits non-zero when any job's last verdict was a failure, so it can be a check
and not only a report.
"""

import argparse
import json
import re
import subprocess
import sys

# A verdict. `cancelled` and `skipped` are not: one means nobody looked, the
# other means there was nothing to look at.
VERDICTS = ("success", "failure", "timed_out")

SHARD = re.compile(r"\s*\(shard\s+\d+\)\s*$")


def base_name(job_name: str) -> str:
    """`the AppKit host (shard 2)` -> `the AppKit host`."""
    return SHARD.sub("", job_name).strip()


def fold_shards(jobs: list) -> dict:
    """One conclusion per job, from however many shards it ran as.

    A job has a verdict for this run only when all of its shards do. Failure
    wins over success, because one red shard is a red job -- `fail-fast: false`
    means the others keep going and say nothing about it.
    """
    grouped = {}
    for job in jobs:
        grouped.setdefault(base_name(job["name"]), []).append(job.get("conclusion"))
    folded = {}
    for name, conclusions in grouped.items():
        if any(c == "skipped" for c in conclusions) and all(
            c == "skipped" for c in conclusions
        ):
            folded[name] = "skipped"
        elif not all(c in VERDICTS for c in conclusions):
            folded[name] = None  # cancelled, still running, or partly so
        elif any(c != "success" for c in conclusions):
            folded[name] = "failure"
        else:
            folded[name] = "success"
    return folded


def last_verdicts(records: list) -> dict:
    """Per job: its last real result, and how many newer runs gave none.

    `records` is newest first. Each is {"sha", "run_id", "created_at", "jobs"}.
    """
    out = {}
    blind = {}
    for record in records:
        for name, conclusion in fold_shards(record["jobs"]).items():
            if name in out:
                continue
            if conclusion in VERDICTS:
                out[name] = {
                    "conclusion": conclusion,
                    "sha": record["sha"],
                    "run_id": record["run_id"],
                    "created_at": record["created_at"],
                    "runs_without_a_verdict": blind.get(name, 0),
                }
            elif conclusion is None:
                # Counted only for jobs still waiting for a verdict; a skipped
                # job is not a blind spot, it is a job with nothing to do.
                blind[name] = blind.get(name, 0) + 1
    return out


def fetch(limit: int) -> list:
    runs = json.loads(
        subprocess.run(
            ["gh", "run", "list", "--limit", str(limit), "--json",
             "databaseId,headSha,createdAt,workflowName"],
            capture_output=True, text=True, check=True,
        ).stdout
    )
    records = []
    for run in runs:
        jobs = json.loads(
            subprocess.run(
                ["gh", "run", "view", str(run["databaseId"]), "--json", "jobs"],
                capture_output=True, text=True, check=True,
            ).stdout
        )["jobs"]
        records.append({
            "sha": run["headSha"][:7],
            "run_id": run["databaseId"],
            "created_at": run["createdAt"],
            "jobs": [{"name": j["name"], "conclusion": j.get("conclusion")} for j in jobs],
        })
    return records


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--limit", type=int, default=15,
                        help="how many recent runs to look back through")
    parser.add_argument("--json", action="store_true")
    args = parser.parse_args()

    verdicts = last_verdicts(fetch(args.limit))
    if args.json:
        print(json.dumps(verdicts, indent=2, sort_keys=True))
    else:
        if not verdicts:
            print(f"no job reached a verdict in the last {args.limit} runs")
            return 1
        width = max(len(name) for name in verdicts)
        for name in sorted(verdicts):
            v = verdicts[name]
            blind = v["runs_without_a_verdict"]
            # Spelled out rather than left as a number, because the number being
            # non-zero is the whole point and a bare column is easy to skim past.
            trailer = (
                ""
                if blind == 0
                else f"  <- and {blind} newer run(s) never finished, so this is older than it looks"
            )
            print(f"  {name:<{width}}  {v['conclusion']:<8}  {v['sha']}  {v['created_at']}{trailer}")

    failed = sorted(n for n, v in verdicts.items() if v["conclusion"] != "success")
    if failed:
        print(f"\nlast verdict was not a pass for: {', '.join(failed)}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
