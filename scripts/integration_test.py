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

One scenario is different: the Fast Refresh one starts its own Metro, runs the
host in dev mode against it, and edits the demo while it is on screen. It needs
a React Native checkout -- scripts/metro.sh looks for one beside the repo, and
RN_DIR overrides that -- and it restores the file it edits once the host has
exited.

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
import socket
import tempfile
import time
import urllib.error
import urllib.request
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent
HOST = REPO / "build" / "rn_linux_host"
MODULE = "RNLinuxDemo"

# Coordinates are in surface-root points, and depend on the demo's layout. They
# are computed from js/index.js rather than measured from a screenshot: the
# button row sits at the bottom of a 900x700 window, inside 24pt of padding.
WINDOW = (900, 700)
BUTTON_Y = 650
# Three buttons share the 852pt content row with 12pt gaps, so each is 272 wide.
SCROLL_TO_TOP = (160, BUTTON_Y)
# Deliberately on the label's glyphs rather than the button's background, so
# this also proves a touch on a child bubbles to the Pressable that handles it.
SCROLL_TO_END_LABEL = (444, BUTTON_Y)
FOCUS_THE_FIELD = (728, BUTTON_Y)
# The middle of the text field itself, for a tap that focuses it directly.
TEXT_FIELD = (184, 207)

# The string the Fast Refresh scenario swaps in the demo's heading, and puts
# back. Chosen to be unmistakable in a widget tree and unique in the file.
BEFORE = "React Native on GTK4"
AFTER = "Fast Refresh reached the window"


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


def type_with_xdotool(text: str) -> None:
    """Types through the X server, so GDK and the input method see the keys."""
    subprocess.run(["xdotool", "type", "--delay", "80", text], check=True)
    time.sleep(1.5)


def run_host(bundle: Path, taps: str = "", run_ms: int = 4000, typing: str = "") -> str:
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
        env.pop("RN_LINUX_TEST_TYPE", None)

        if points and INPUT_MODE == "injected":
            env["RN_LINUX_TEST_TAP"] = taps
        if typing and INPUT_MODE == "injected":
            env["RN_LINUX_TEST_TYPE"] = typing

        command = [str(HOST), str(bundle), MODULE]
        timeout = run_ms / 1000 + 60

        if (points or typing) and INPUT_MODE == "real":
            process = subprocess.Popen(
                command, cwd=REPO, env=env, stdout=subprocess.PIPE,
                stderr=subprocess.PIPE, text=True,
            )
            # The window has to exist before it can be clicked.
            time.sleep(4)
            try:
                click_with_xdotool(points)
                if typing:
                    type_with_xdotool(typing)
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

    expect_contains(tree, "React Native on GTK4", "React rendered no text")

    # The platform package, end to end: an app built for `linux` has to see
    # Platform.OS === 'linux'. Getting this wrong is quiet -- React Native's
    # Platform shim resolves to itself and yields undefined rather than
    # complaining -- so the demo renders it and this asserts on it.
    expect_contains(
        tree,
        "Platform.OS is linux",
        "the app did not see Platform.OS === 'linux'; was it bundled for linux?",
    )
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


# --------------------------------------------------------------------------
# Fast Refresh
# --------------------------------------------------------------------------

# Not 8081: a developer running Metro for real should not have to stop it, and
# a test that silently talks to someone else's dev server proves nothing.
METRO_PORT = 8099


class Metro:
    """Metro on its own port, for the Fast Refresh scenario."""

    def __init__(self, log: Path) -> None:
        self.process = None
        self.log = log

    def __enter__(self) -> "Metro":
        # Kept rather than discarded: when this scenario fails it is almost
        # always Metro doing something, and a CI run that only says "no update
        # arrived" costs another four minutes to learn anything from.
        self.sink = self.log.open("w")
        self.process = subprocess.Popen(
            [str(REPO / "scripts" / "metro.sh"), "--port", str(METRO_PORT)],
            cwd=REPO,
            stdout=self.sink,
            stderr=subprocess.STDOUT,
        )
        # Readiness is the port accepting a connection, not any particular
        # endpoint: Metro's /status is not served on this version. Polling beats
        # a fixed sleep -- a cold Metro on a slow machine takes a while.
        deadline = time.time() + 90
        while time.time() < deadline:
            if self.process.poll() is not None:
                raise Failure("metro exited before it started serving")
            try:
                with socket.create_connection(("localhost", METRO_PORT), timeout=2):
                    return self
            except OSError:
                time.sleep(1)
        raise Failure("metro did not start listening")

    def prewarm(self) -> None:
        """Builds the bundle before the host asks for it.

        The host tries Metro and falls back to the on-disk bundle, which is a
        production one. A cold Metro takes longer to answer than that fallback
        is willing to wait, so without this the app quietly runs the *release*
        bundle and no edit will ever reach it -- which is what CI saw, reported
        as "Metro never pushed an update".
        """
        url = (
            f"http://localhost:{METRO_PORT}/index.bundle"
            "?platform=linux&dev=true&minify=false"
        )
        try:
            with urllib.request.urlopen(url, timeout=300) as response:
                if response.status != 200:
                    raise Failure(f"metro answered {response.status} for the bundle")
                response.read()
        except urllib.error.URLError as error:
            raise Failure(f"metro could not build the bundle: {error}") from error

    def __exit__(self, *_) -> None:
        if self.process is not None:
            self.process.terminate()
            try:
                self.process.wait(timeout=15)
            except subprocess.TimeoutExpired:
                self.process.kill()
        self.sink.close()


def tail(log: Path, lines: int = 25) -> str:
    if not log.exists():
        return "(no log)"
    return "\n".join(log.read_text().splitlines()[-lines:])


def wait_for_log(log: Path, needle: str, count: int, timeout: float) -> bool:
    """Waits until `needle` has appeared in `log` at least `count` times."""
    deadline = time.time() + timeout
    while time.time() < deadline:
        if log.exists() and log.read_text().count(needle) >= count:
            return True
        time.sleep(0.5)
    return False


def test_fast_refresh(bundle: Path) -> None:
    """Edits the demo while it runs and checks the change lands in the window.

    This is the scenario that would have caught a wrong claim in the README:
    nothing else here runs the host in dev mode at all, so "development still
    works" was being taken on trust.

    A __DEV__ bundle needs the DevSettings TurboModule, which
    ReactCxxTurboModuleProvider serves only when a DevServerHelper exists. So
    this also pins the one configuration where that is true.

    It waits on the host's own log rather than on sleeps. A fixed budget was
    the first version and it failed in CI, where Metro is cold and a rebuild
    takes longer than a developer's warm one -- which is the same class of
    flake as any other "should be long enough".
    """
    source = REPO / "js" / "index.js"
    original = source.read_text()
    if original.count(BEFORE) != 1:
        raise Failure(f"the demo does not contain exactly one {BEFORE!r} to edit")

    running = f'Running "{MODULE}"'

    with tempfile.TemporaryDirectory() as directory:
        dump = Path(directory) / "tree.txt"
        log = Path(directory) / "host.log"
        env = dict(os.environ)
        env["RN_LINUX_DUMP_TREE"] = str(dump)
        # A backstop, not the schedule: the host is asked to quit by signal as
        # soon as the refresh shows up.
        env["RN_LINUX_QUIT_AFTER_MS"] = "180000"
        env["RN_LINUX_DEV"] = "1"
        env["RN_LINUX_DEV_PORT"] = str(METRO_PORT)
        env.pop("RN_LINUX_TEST_TAP", None)
        env.pop("RN_LINUX_TEST_TYPE", None)

        metro_log = Path(directory) / "metro.log"

        def diagnose(message: str) -> Failure:
            """Fails with what the two processes were saying, not just a verdict."""
            return Failure(
                f"{message}\n"
                f"--- last of metro ---\n{tail(metro_log)}\n"
                f"--- last of the host ---\n{tail(log)}"
            )

        with Metro(metro_log) as metro, log.open("w") as sink:
            metro.prewarm()
            process = subprocess.Popen(
                [str(HOST), str(bundle), MODULE],
                cwd=REPO, env=env, stdout=subprocess.DEVNULL, stderr=sink, text=True,
            )
            try:
                if not wait_for_log(log, running, 1, timeout=120):
                    raise diagnose("the app never started")

                # A __DEV__ bundle asks for the LogBox TurboModule and a release
                # one does not, so this is how to tell which bundle actually
                # evaluated. Editing before knowing that produces a confusing
                # failure much later.
                if not wait_for_log(log, "TurboModule: LogBox", 1, timeout=10):
                    raise diagnose(
                        "the app is running the on-disk release bundle, not Metro's"
                    )

                source.write_text(original.replace(BEFORE, AFTER))

                # The demo has no refresh boundary, so React Native reloads the
                # whole surface and the app runs a second time. Waiting on
                # either the reload or that second run keeps this from depending
                # on which of the two the demo happens to provoke.
                if not (
                    wait_for_log(log, running, 2, timeout=90)
                    or wait_for_log(log, "Fast Refresh", 1, timeout=1)
                ):
                    raise diagnose("Metro never pushed an update after the edit")

                # Rendering follows the reload; the tree is dumped on the way out.
                time.sleep(3)
                process.terminate()
                process.wait(timeout=60)
            finally:
                source.write_text(original)
                if process.poll() is None:
                    process.kill()
                    process.wait(timeout=15)

        stderr = log.read_text()
        check_output(stderr, process.returncode)

        # The exact line, not "DevSettings" anywhere: the module logs its own
        # name at INFO when it works, and plenty of other TurboModules fail to
        # load harmlessly, so a conjunction of the two substrings reports
        # success as failure. It did.
        if "Failed to load TurboModule: DevSettings" in stderr:
            raise Failure("DevSettings was not served, so no __DEV__ bundle can run")
        if not dump.exists():
            raise Failure("host wrote no widget tree")
        if AFTER not in dump.read_text():
            raise Failure(
                "the edit never reached the running app; Fast Refresh did not apply it"
            )


def editable_value(tree: str) -> str:
    """The text inside the only <TextInput> in the tree."""
    match = re.search(r'editable="([^"]*)"', tree)
    if match is None:
        raise Failure("no text field in the widget tree; is the TextInput mounted?")
    return match.group(1)


def test_text_input(bundle: Path) -> None:
    # The field is focused through the third button rather than by tapping it,
    # so this covers the focus command as well as the typing. The value is
    # controlled by React, so what ends up in the widget is only there because
    # onChange reached JavaScript and the new value came back down.
    tree = run_host(
        bundle,
        taps=f"{FOCUS_THE_FIELD[0]},{FOCUS_THE_FIELD[1]}",
        typing="Ada",
        run_ms=9000,
    )

    if "focused" not in tree:
        raise Failure("the focus command did not move focus to the field")

    value = editable_value(tree)
    if value != "Ada":
        raise Failure(f"the field holds {value!r}, not the text that was typed")

    # The echo label is rendered from React state, so it only says this if
    # onChangeText fired. Without it the widget could hold the right text
    # purely because GtkText kept it, with JavaScript none the wiser.
    expect_contains(
        tree,
        'text="hello, Ada"',
        "onChangeText never reached React; the field is not controlled",
    )


SCENARIOS = [
    ("initial render", test_initial_render),
    ("scrollToEnd, and a tap that bubbles from a label", test_scroll_to_end),
    ("scroll away and back", test_scroll_round_trip),
    ("focus a TextInput, type, and see it round-trip through React", test_text_input),
    ("edit the demo and watch Fast Refresh apply it", test_fast_refresh),
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
