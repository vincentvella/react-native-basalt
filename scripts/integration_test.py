#!/usr/bin/env python3
"""End-to-end tests for the host.

The unit suite (build/rn_tests) covers everything that can be reached without a
JavaScript runtime. What it cannot cover is the path this project actually
exists to provide: JavaScript, React, Fabric, the mounting manager, and GTK
widgets, all in one process. Until there was a way to read the resulting widget
tree, the only way to check that path was to look at a screenshot.

RN_LINUX_DUMP_TREE makes it assertable. Each scenario below runs the real host
against the real bundle, taps something, and asserts on the tree it wrote on
the way out.

Taps arrive one of two ways:

  real       xdotool moves the pointer and clicks, so the event goes through
             the X server and GDK exactly as a person's click would. This is
             the only mode that exercises event delivery itself.
  injected   RN_LINUX_TEST_TAP calls the gesture callback directly, skipping
             GDK. The fallback where a real event cannot be synthesised --
             notably macOS, where it needs accessibility permission an
             automated run does not have.

The default picks real input when a display and xdotool are both present.

Usage:  scripts/integration_test.py [--bundle build/main.jsbundle.js]
                                    [--input auto|real|injected]

Needs a display, like any GTK program. On a headless Linux box:

    Xvfb :99 -screen 0 1400x1000x24 &
    DISPLAY=:99 scripts/integration_test.py
"""

import argparse
import os
import re
import shutil
import subprocess
import sys
import tempfile
import time
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


INPUT_MODE = "injected"


def real_input_available() -> bool:
    return bool(os.environ.get("DISPLAY")) and shutil.which("xdotool") is not None


def check_output(stderr: str, returncode: int) -> None:
    if returncode != 0:
        raise Failure(f"host exited {returncode}\n{stderr[-2000:]}")
    for line in stderr.splitlines():
        # A JS error does not fail the process, so it has to be looked for.
        if "onJsError" in line or "Invariant Violation" in line:
            raise Failure(f"javascript error: {line}")


def click_with_xdotool(points: list[tuple[int, int]]) -> None:
    """Clicks through the X server, so GDK delivers the event itself."""
    window = subprocess.run(
        ["xdotool", "search", "--name", "react-native-linux"],
        capture_output=True,
        text=True,
    ).stdout.split()
    if not window:
        raise Failure("could not find the host window with xdotool")

    geometry = subprocess.run(
        ["xdotool", "getwindowgeometry", "--shell", window[0]],
        capture_output=True,
        text=True,
    ).stdout
    origin = dict(
        line.split("=", 1) for line in geometry.splitlines() if "=" in line
    )
    x0, y0 = int(origin.get("X", 0)), int(origin.get("Y", 0))

    for x, y in points:
        subprocess.run(["xdotool", "mousemove", str(x0 + x), str(y0 + y)], check=True)
        time.sleep(0.4)
        subprocess.run(["xdotool", "click", "1"], check=True)
        time.sleep(1.0)


def run_host(bundle: Path, taps: str = "", run_ms: int = 4000) -> str:
    """Runs the host once and returns the widget tree it dumped."""
    points = [
        (int(part.split(",")[0]), int(part.split(",")[1]))
        for part in taps.split(";")
        if part
    ]

    with tempfile.TemporaryDirectory() as directory:
        dump = Path(directory) / "tree.txt"
        env = dict(os.environ)
        env["RN_LINUX_DUMP_TREE"] = str(dump)
        env["RN_LINUX_QUIT_AFTER_MS"] = str(run_ms)
        env.pop("RN_LINUX_TEST_TAP", None)

        if points and INPUT_MODE == "injected":
            env["RN_LINUX_TEST_TAP"] = taps

        command = [str(HOST), str(bundle), MODULE]
        timeout = run_ms / 1000 + 60

        if points and INPUT_MODE == "real":
            process = subprocess.Popen(
                command, cwd=REPO, env=env, stdout=subprocess.PIPE,
                stderr=subprocess.PIPE, text=True,
            )
            # The window has to exist before it can be clicked.
            time.sleep(4)
            try:
                click_with_xdotool(points)
            finally:
                _, stderr = process.communicate(timeout=timeout)
            check_output(stderr, process.returncode)
        else:
            result = subprocess.run(
                command, cwd=REPO, env=env, capture_output=True, text=True, timeout=timeout,
            )
            check_output(result.stderr, result.returncode)

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
        # Not anchored to the end of the line: a line may carry a role or
        # other attributes after its text.
        match = re.search(r'text="(\d+)"', line)
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

    # Accessibility: what a screen reader would be told. A <Text> should call
    # itself a label and an <Image> an image without the app saying so, and an
    # explicit accessibilityRole should win.
    expect_contains(tree, "role=label", "no <Text> reported itself as a label")
    expect_contains(tree, "role=img", "no <Image> reported itself as an image")
    expect_contains(tree, "role=button", "the buttons did not take their accessibilityRole")
    expect_contains(tree, "role=list", "the ScrollView did not take its accessibilityRole")

    # The ScrollView must clip, or its content paints over its siblings. It is
    # found by its accessibilityRole rather than its colour: more than one view
    # in the demo shares a background, and matching on that picked the wrong
    # one as soon as the demo grew.
    scroller = [line for line in tree.splitlines() if "role=list" in line]
    if not scroller:
        raise Failure("no view reported itself as a list; is the ScrollView mounted?")
    if "clip" not in scroller[0]:
        raise Failure("the ScrollView is not clipping")

    if scroll_offset(tree) != 0.0:
        raise Failure("a freshly mounted ScrollView should be at the top")


def test_scroll_to_end(bundle: Path) -> None:
    # The tap lands on the button's *label*, so a pass also means a touch on a
    # child bubbled to the Pressable that handles it. Under real input it also
    # means the X server and GDK delivered the event.
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
    parser.add_argument("--input", choices=["auto", "real", "injected"], default="auto")
    arguments = parser.parse_args()

    global INPUT_MODE
    if arguments.input == "auto":
        INPUT_MODE = "real" if real_input_available() else "injected"
    else:
        INPUT_MODE = arguments.input
    if INPUT_MODE == "real" and not real_input_available():
        print("error: --input real needs DISPLAY set and xdotool installed", file=sys.stderr)
        return 1

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

    note = (
        "real pointer events through the X server"
        if INPUT_MODE == "real"
        else "taps injected at the gesture callback, skipping GDK"
    )
    print(f"running {len(SCENARIOS)} scenarios against {bundle.name}")
    print(f"input: {INPUT_MODE} -- {note}")
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
