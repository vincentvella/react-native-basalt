#!/usr/bin/env python3
"""End-to-end tests for the host.

The unit suite (build/rn_tests) covers everything that can be reached without a
JavaScript runtime. What it cannot cover is the path this project actually
exists to provide: JavaScript, React, Fabric, the mounting manager, and GTK
widgets, all in one process. Until there was a way to read the resulting widget
tree, the only way to check that path was to look at a screenshot.

RN_LINUX_DUMP_TREE makes it assertable. Each scenario below runs the real host
against the real bundle, optionally injects taps, and asserts on the tree it
wrote on the way out.

Usage:  scripts/integration_test.py [--bundle build/main.jsbundle.js]

Needs a display, like any GTK program. On a headless Linux box:

    xvfb-run -a scripts/integration_test.py
"""

import argparse
import os
import re
import subprocess
import sys
import tempfile
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent
HOST = REPO / "build" / "rn_linux_host"
MODULE = "RNLinuxDemo"

# Coordinates are in surface-root points, and depend on the demo's layout. They
# are computed from js/index.js rather than measured from a screenshot: the
# button row sits at the bottom of a 900x700 window, inside 24pt of padding.
WINDOW = (900, 700)
BUTTON_Y = 650
SCROLL_TO_TOP = (233, BUTTON_Y)
# Deliberately on the label's glyphs rather than the button's background, so
# this also proves a touch on a child bubbles to the Pressable that handles it.
SCROLL_TO_END_LABEL = (657, BUTTON_Y)


class Failure(Exception):
    pass


def run_host(bundle: Path, taps: str = "", run_ms: int = 4000) -> str:
    """Runs the host once and returns the widget tree it dumped."""
    with tempfile.TemporaryDirectory() as directory:
        dump = Path(directory) / "tree.txt"
        env = dict(os.environ)
        env["RN_LINUX_DUMP_TREE"] = str(dump)
        env["RN_LINUX_QUIT_AFTER_MS"] = str(run_ms)
        if taps:
            env["RN_LINUX_TEST_TAP"] = taps

        result = subprocess.run(
            [str(HOST), str(bundle), MODULE],
            cwd=REPO,
            env=env,
            capture_output=True,
            text=True,
            timeout=run_ms / 1000 + 60,
        )
        if result.returncode != 0:
            raise Failure(f"host exited {result.returncode}\n{result.stderr[-2000:]}")
        for line in result.stderr.splitlines():
            # A JS error does not fail the process, so it has to be looked for.
            if "onJsError" in line or "Invariant Violation" in line:
                raise Failure(f"javascript error: {line}")
        if not dump.exists():
            raise Failure("host wrote no widget tree")
        return dump.read_text()


def expect_contains(tree: str, needle: str, why: str) -> None:
    if needle not in tree:
        raise Failure(f"{why}: expected to find {needle!r} in the widget tree")


def scroll_offset(tree: str) -> float:
    """The vertical scroll offset of the only scrolling view in the tree."""
    matches = re.findall(r"scroll=\(([-0-9.]+),([-0-9.]+)\)", tree)
    if not matches:
        return 0.0
    return float(matches[0][1])


def offset_label(tree: str) -> float:
    """The number the demo renders next to 'contentOffset.y'."""
    for line in tree.splitlines():
        if 'text="contentOffset.y"' in line:
            continue
        match = re.search(r'text="(\d+)"$', line.strip())
        if match:
            return float(match.group(1))
    raise Failure("no numeric offset label in the widget tree")


# --------------------------------------------------------------------------
# Scenarios
# --------------------------------------------------------------------------


def test_initial_render(bundle: Path) -> None:
    tree = run_host(bundle)

    expect_contains(tree, 'text="React Native on GTK4"', "React rendered no text")
    expect_contains(tree, "texture=160x100", "the image never loaded or decoded")
    expect_contains(tree, 'text="row 0"', "the list did not render")
    expect_contains(tree, 'text="row 23"', "the list is short of rows")

    # The ScrollView must clip, or its content paints over its siblings.
    scroller = [line for line in tree.splitlines() if "bg=#1a1e28ff" in line]
    if not scroller or "clip" not in scroller[0]:
        raise Failure("the ScrollView is not clipping")

    if scroll_offset(tree) != 0.0:
        raise Failure("a freshly mounted ScrollView should be at the top")


def test_scroll_to_end(bundle: Path) -> None:
    # The tap lands on the button's *label*, so a pass also means a touch on a
    # child bubbled to the Pressable that handles it.
    tree = run_host(bundle, taps=f"{SCROLL_TO_END_LABEL[0]},{SCROLL_TO_END_LABEL[1]}", run_ms=5000)

    offset = scroll_offset(tree)
    if offset <= 0:
        raise Failure(f"scrollToEnd left the offset at {offset}")

    # onScroll has to have reached JavaScript, or the label would still say 0.
    label = offset_label(tree)
    if label <= 0:
        raise Failure(f"onScroll never reached React; the label reads {label}")
    if abs(label - offset) > 2.0:
        raise Failure(f"the label ({label}) disagrees with the widget tree ({offset})")


def test_scroll_round_trip(bundle: Path) -> None:
    taps = (
        f"{SCROLL_TO_END_LABEL[0]},{SCROLL_TO_END_LABEL[1]};"
        f"{SCROLL_TO_TOP[0]},{SCROLL_TO_TOP[1]}"
    )
    tree = run_host(bundle, taps=taps, run_ms=6000)

    if scroll_offset(tree) != 0.0:
        raise Failure("scrollTo({y: 0}) did not return to the top")
    if offset_label(tree) != 0.0:
        raise Failure("the offset label did not follow the scroll back to zero")


SCENARIOS = [
    ("initial render", test_initial_render),
    ("scrollToEnd, and a tap that bubbles from a label", test_scroll_to_end),
    ("scroll away and back", test_scroll_round_trip),
]


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--bundle", default="build/main.jsbundle.js")
    arguments = parser.parse_args()

    bundle = (REPO / arguments.bundle).resolve()
    if not HOST.exists():
        print(f"error: {HOST} not built", file=sys.stderr)
        return 1
    if not bundle.exists():
        print(
            f"error: no bundle at {bundle}\n"
            "       run scripts/bundle.sh ../react-native --prod first",
            file=sys.stderr,
        )
        return 1

    print(f"running {len(SCENARIOS)} scenarios against {bundle.name}")
    failed = 0
    for name, scenario in SCENARIOS:
        try:
            scenario(bundle)
            print(f"  ok    {name}")
        except Failure as failure:
            failed += 1
            print(f"  FAIL  {name}\n        {failure}")
        except subprocess.TimeoutExpired:
            failed += 1
            print(f"  FAIL  {name}\n        the host did not exit")

    print(f"\n{len(SCENARIOS) - failed}/{len(SCENARIOS)} passed")
    return 0 if failed == 0 else 1


if __name__ == "__main__":
    sys.exit(main())
