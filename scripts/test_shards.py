"""Does sharding the end-to-end suite still run all of it?

The dangerous bug in a test harness points towards green: a shard boundary
that drops a scenario makes CI pass while checking less, and nothing about the
output looks wrong. So this asserts the one property that matters -- every
scenario lands in exactly one shard, for every shard count -- rather than
trusting a slice expression to be right.

Run with:  python3 scripts/test_shards.py
"""

import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

import integration_test as suite


def check(condition: bool, why: str) -> None:
    if not condition:
        print(f"FAIL {why}")
        check.failed += 1
    else:
        print(f"ok   {why}")


check.failed = 0

names = [name for name, _ in suite.SCENARIOS]
check(len(names) == len(set(names)), "the scenario names are unique")

# Every count from one to a few past the number of scenarios: the interesting
# boundaries are 1 (everything in one shard), exactly the scenario count (one
# each), and more shards than scenarios (some empty).
for count in range(1, len(names) + 3):
    seen = []
    for index in range(1, count + 1):
        seen.extend(name for name, _ in suite.shard_of(suite.SCENARIOS, index, count))

    check(
        sorted(seen) == sorted(names),
        f"{count} shards cover every scenario exactly once",
    )

    # And no shard is more than one scenario larger than another, which is what
    # striding buys over slicing and the reason to prefer it.
    sizes = [
        len(suite.shard_of(suite.SCENARIOS, index, count)) for index in range(1, count + 1)
    ]
    check(max(sizes) - min(sizes) <= 1, f"{count} shards are within one of each other")

# The parser, including the mistakes a CI matrix makes.
check(suite.parse_shard("2/4") == (2, 4), "2/4 parses")
for bad in ("0/4", "5/4", "-1/4", "2/0", "2", "2/4/8", "a/4", ""):
    try:
        suite.parse_shard(bad)
        check(False, f"{bad!r} is refused")
    except ValueError:
        check(True, f"{bad!r} is refused")

print()
if check.failed:
    print(f"{check.failed} failed")
    sys.exit(1)
print("all shard checks passed")
