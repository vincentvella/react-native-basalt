#!/usr/bin/env python3
"""Fails if the compiler did not go through ccache at all.

This exists because of a bug that took months to notice and cost six minutes
of every macOS run. The job installed ccache, restored a ccache and saved a
ccache, and never passed `CMAKE_CXX_COMPILER_LAUNCHER` -- so the compiler was
invoked directly and the cache stayed empty. **A cache that is never written
restores in 0 seconds and saves in 0 seconds, and both steps report success.**
The only symptom was a slow build, which looks like the machine being slow.

The distinction this draws is the one that matters, and it is not the hit rate:

  cacheable calls == 0   ccache was not in the compile at all. Always a bug in
                         the workflow, on a cold cache or a warm one.
  cacheable calls > 0    ccache ran. How many of them hit is a property of the
                         cache and of what changed, so it is reported and not
                         asserted -- a legitimately cold cache misses
                         everything, and a first run after a dependency bump
                         nearly everything.

Asserting a hit rate would fail on exactly the runs where a cold cache is
correct, and a check that cries wolf gets deleted.

Usage:  scripts/check_ccache.py
"""

import shutil
import subprocess
import sys

# Every cacheable call lands in one of these, so their sum is how many times
# ccache was asked about a compile. `--print-stats` is the machine-readable
# form and is stable across ccache 4.x; `--show-stats` is laid out for people
# and has changed between versions.
HIT_FIELDS = ("direct_cache_hit", "preprocessed_cache_hit")
CACHEABLE_FIELDS = HIT_FIELDS + ("cache_miss",)


def main() -> int:
    if shutil.which("ccache") is None:
        print("check_ccache: no ccache on PATH, so nothing was cached", file=sys.stderr)
        return 1

    printed = subprocess.run(
        ["ccache", "--print-stats"], capture_output=True, text=True,
    )
    if printed.returncode != 0:
        print(
            "check_ccache: `ccache --print-stats` failed:\n"
            f"{printed.stdout}{printed.stderr}",
            file=sys.stderr,
        )
        return 1

    stats = {}
    for line in printed.stdout.splitlines():
        parts = line.split()
        if len(parts) == 2 and parts[1].lstrip("-").isdigit():
            stats[parts[0]] = int(parts[1])

    known = [field for field in CACHEABLE_FIELDS if field in stats]
    if not known:
        # Not treated as zero: a renamed field would then read as the bug this
        # exists to catch, and the fix would be to the wrong thing. Louder and
        # more specific is better than wrong and confident.
        print(
            "check_ccache: none of "
            f"{', '.join(CACHEABLE_FIELDS)} appeared in `ccache --print-stats`.\n"
            "This is a version difference in ccache rather than a caching "
            "problem; the field names need updating here. Raw output:\n"
            f"{printed.stdout}",
            file=sys.stderr,
        )
        return 1

    cacheable = sum(stats.get(field, 0) for field in CACHEABLE_FIELDS)
    hits = sum(stats.get(field, 0) for field in HIT_FIELDS)

    if cacheable == 0:
        print(
            "check_ccache: ccache saw no cacheable compiles.\n"
            "The compiler did not go through it. Check that the configure step "
            "passes -DCMAKE_C_COMPILER_LAUNCHER=ccache and "
            "-DCMAKE_CXX_COMPILER_LAUNCHER=ccache, and that CCACHE_DIR is the "
            "directory the workflow caches.\n"
            f"Raw output:\n{printed.stdout}",
            file=sys.stderr,
        )
        return 1

    share = 100 * hits / cacheable
    print(f"ccache: {hits}/{cacheable} cacheable compiles hit ({share:.0f}%)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
