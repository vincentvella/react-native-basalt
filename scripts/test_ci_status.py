#!/usr/bin/env python3
"""Checks on scripts/ci_status.py, whose whole job is to not read comfortably.

The bug this tooling exists to catch pointed at green: five red runs looked like
four cancellations and a mystery, because a cancelled job is not a failed one and
nothing said so. A bug *in* the tool would point the same way -- it would report a
stale pass as a current one -- so the logic is a pure function over records and it
is checked here rather than trusted.

Usage:  scripts/test_ci_status.py
"""

import importlib.util
import pathlib
import sys

spec = importlib.util.spec_from_file_location(
    "ci_status", pathlib.Path(__file__).with_name("ci_status.py"))
ci = importlib.util.module_from_spec(spec)
spec.loader.exec_module(ci)

failures = []


def check(label, got, want):
    if got != want:
        failures.append(f"{label}:\n    got  {got!r}\n    want {want!r}")


def run(sha, **jobs):
    """A record: job name -> conclusion, newest-first ordering supplied by caller."""
    return {
        "sha": sha,
        "run_id": int(sha, 16) if all(c in "0123456789abcdef" for c in sha) else 0,
        "created_at": f"2026-09-26T00:00:0{sha[-1]}Z",
        "jobs": [{"name": n.replace("_", " "), "conclusion": c} for n, c in jobs.items()],
    }


# --- folding shards together -------------------------------------------------

check("three green shards are a green job",
      ci.fold_shards([{"name": "linux (shard 1)", "conclusion": "success"},
                      {"name": "linux (shard 2)", "conclusion": "success"},
                      {"name": "linux (shard 3)", "conclusion": "success"}]),
      {"linux": "success"})

check("one red shard is a red job",
      ci.fold_shards([{"name": "linux (shard 1)", "conclusion": "success"},
                      {"name": "linux (shard 2)", "conclusion": "failure"},
                      {"name": "linux (shard 3)", "conclusion": "success"}]),
      {"linux": "failure"})

# The case that matters most. Two green shards and one cancelled is not a tested
# commit, and calling it green is exactly the mistake this tool exists to stop.
check("a cancelled shard makes the job undecided, not green",
      ci.fold_shards([{"name": "linux (shard 1)", "conclusion": "success"},
                      {"name": "linux (shard 2)", "conclusion": "cancelled"},
                      {"name": "linux (shard 3)", "conclusion": "success"}]),
      {"linux": None})

check("a job still running is undecided",
      ci.fold_shards([{"name": "linux (shard 1)", "conclusion": None}]),
      {"linux": None})

check("a wholly skipped job is skipped, not undecided",
      ci.fold_shards([{"name": "drift", "conclusion": "skipped"}]),
      {"drift": "skipped"})

check("a timeout is a verdict",
      ci.fold_shards([{"name": "win", "conclusion": "timed_out"}]),
      {"win": "failure"})

check("unsharded jobs keep their names",
      ci.fold_shards([{"name": "the AppKit host", "conclusion": "success"}]),
      {"the AppKit host": "success"})

check("shard suffixes are stripped however they are spaced",
      sorted({ci.base_name("x (shard 1)"), ci.base_name("x  (shard 12) "),
              ci.base_name("x")}),
      ["x"])

# --- the blind spot count ----------------------------------------------------

# The day this is modelled on: Windows failing under a Linux job that kept being
# cancelled. Newest first.
records = [
    run("aaaaaa5", linux="cancelled", windows="failure"),
    run("aaaaaa4", linux="cancelled", windows="failure"),
    run("aaaaaa3", linux="cancelled", windows="failure"),
    run("aaaaaa2", linux="cancelled", windows="failure"),
    run("aaaaaa1", linux="cancelled", windows="failure"),
    run("aaaaaa0", linux="success", windows="success"),
]
verdicts = ci.last_verdicts(records)

check("windows' last verdict is the newest failure",
      (verdicts["windows"]["conclusion"], verdicts["windows"]["sha"]),
      ("failure", "aaaaaa5"))
check("windows has no blind spot: it answered every time",
      verdicts["windows"]["runs_without_a_verdict"], 0)
check("linux' last verdict is the old pass",
      (verdicts["linux"]["conclusion"], verdicts["linux"]["sha"]),
      ("success", "aaaaaa0"))
# 5 is the number that would have exposed this on the day.
check("linux reports the five runs that never finished",
      verdicts["linux"]["runs_without_a_verdict"], 5)

# A job that has never reached a verdict is absent rather than invented.
check("a job with no verdict at all is not reported",
      "macos" in ci.last_verdicts([run("bbbbbb1", macos="cancelled")]), False)

# Skipped runs must not inflate the blind spot: a skipped job is not an untested
# one, and counting it would cry wolf on every run of the drift job.
skipped = ci.last_verdicts([
    run("cccccc2", drift="skipped"),
    run("cccccc1", drift="skipped"),
    run("cccccc0", drift="success"),
])
check("skipped runs are not counted as a blind spot",
      skipped["drift"]["runs_without_a_verdict"], 0)

# A newer verdict wins over an older one, whatever it says.
newer = ci.last_verdicts([run("dddddd1", linux="failure"), run("dddddd0", linux="success")])
check("the newest verdict is the one reported",
      (newer["linux"]["conclusion"], newer["linux"]["runs_without_a_verdict"]),
      ("failure", 0))

check("no records means nothing to report", ci.last_verdicts([]), {})

if failures:
    print("ci_status checks failed:", file=sys.stderr)
    for f in failures:
        print(f"  {f}", file=sys.stderr)
    sys.exit(1)
print(f"all {14} ci_status checks passed")
