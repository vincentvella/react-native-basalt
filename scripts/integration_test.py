#!/usr/bin/env python3
"""End-to-end tests for the host.

The unit suite (build/basalt_gtk_tests) covers everything that can be reached without a
JavaScript runtime. What it cannot cover is the path this project actually
exists to provide: JavaScript, React, Fabric, the mounting manager, and GTK
widgets, all in one process. Until there was a way to read the resulting widget
tree, the only way to check that path was to look at a screenshot.

BASALT_DUMP_TREE makes it assertable. Each scenario below runs the real host
against the real bundle, taps something, and asserts on the tree it wrote on
the way out.

Taps arrive one of two ways:

  real       xdotool moves the pointer and clicks, so the event goes through
             the X server and GDK exactly as a person's click would. This is
             the only mode that exercises event delivery itself.
  injected   BASALT_TEST_TAP enters at the touch dispatcher, skipping the
             window system. The fallback where a real event cannot be
             synthesised: macOS needs accessibility permission an automated run
             does not have, and Windows needs SendInput, which moves the real
             cursor and so cannot run beside anything else on the machine.

The default picks real input when a display and xdotool are both present.

One scenario is different: the Fast Refresh one starts its own Metro, runs the
host in dev mode against it, and edits the demo while it is on screen. It needs
a React Native checkout -- scripts/metro.sh looks for one beside the repo, and
RN_DIR overrides that -- and it restores the file it edits once the host has
exited.

Usage:  scripts/integration_test.py [--bundle build/main.jsbundle.js]
                                    [--input auto|real|injected]
                                    [--platform auto|linux|macos|windows]
                                    [--build-dir build]

Runs whichever host is built. The scenarios are the same on all three, because
they are about React and Fabric rather than about a toolkit; what differs is
which binary is launched and how a tap is delivered.

Needs a display, like any GTK program. On a headless Linux box:

    Xvfb :99 -screen 0 1400x1000x24 &
    DISPLAY=:99 scripts/integration_test.py

On Windows and macOS nothing has to be arranged -- the host makes its own
window -- and input is injected, because neither can synthesise a real click
without taking over the machine's cursor.
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
# Which host, and therefore which binary and which bundle. Rebound by
# --platform and --build-dir: testing against a second React Native version
# means a second build tree, and the suite has to run the host from it.
#
# One suite for three platforms rather than three, because the scenarios are
# about React and Fabric rather than about a toolkit -- the tree a tap produces
# is the same tree on all three, which is the claim scripts/compare_hosts.sh
# makes and this one relies on.
HOSTS = {
    "linux": "basalt_gtk",
    "macos": "basalt_appkit",
    "windows": "basalt_win32.exe",
}
PLATFORM = "linux"
HOST = REPO / "build" / "basalt_gtk"
MODULE = "BasaltDemo"

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

# js/hover.js, which the hover scenario runs instead of the demo: a card at
# y=24..244 holding two 140pt boxes, at x=44..184 and x=204..344. The third
# point is inside the card and outside both boxes, which is the one that
# distinguishes enter from over.
HOVER_LEFT_BOX = (100, 130)
HOVER_RIGHT_BOX = (270, 130)
HOVER_CARD_ONLY = (700, 130)

# The string the Fast Refresh scenario swaps in the demo's heading, and puts
# back. Chosen to be unmistakable in a widget tree and unique in the file.
BEFORE = "React Native on the desktop"
AFTER = "Fast Refresh reached the window"


# What the host last printed. Kept here rather than threaded through every
# caller of run_host, which only ever wants the tree.
LAST_HOST_OUTPUT = ""


def _remember_output(text: str) -> None:
    global LAST_HOST_OUTPUT
    LAST_HOST_OUTPUT = text or ""


class Failure(Exception):
    pass


class Skipped(Exception):
    """A scenario that cannot run here, and says why rather than passing."""


# The demo's <TextInput>, from js/index.js: frame (24,183 320x44). Clicked
# rather than tapped, so this is a point inside it in surface coordinates.
FIELD_POINT = (120, 205)

# styles.fieldFocused sets borderColor to PALETTE[0], and describeTree prints
# per-edge colours -- so focus is visible in the tree without the demo needing
# to render anything new for the test's benefit.
FOCUSED_BORDER = "borderc=(#4285f4ff,#4285f4ff,#4285f4ff,#4285f4ff)"

INPUT_MODE = "injected"


def real_input_available() -> bool:
    return bool(os.environ.get("DISPLAY")) and shutil.which("xdotool") is not None


def check_output(stderr: str, returncode: int, allow_js_errors: bool = False) -> None:
    if returncode != 0:
        raise Failure(f"host exited {returncode}\n{stderr[-2000:]}")
    if allow_js_errors:
        # For the one scenario whose whole point is an error: js/logbox.js calls
        # console.error deliberately, and LogBox is what is being tested.
        return
    for line in stderr.splitlines():
        # A JS error does not fail the process, so it has to be looked for.
        if "onJsError" in line or "Invariant Violation" in line:
            raise Failure(f"javascript error: {line}")


def click_with_xdotool(points: list[tuple[int, int]]) -> None:
    """Clicks through the X server, so GDK delivers the event itself."""
    window = subprocess.run(
        ["xdotool", "search", "--name", "react-native-basalt"],
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


# --- A real click, which is the only kind that can focus a field -------------


def click_field_with_cgevent(surface_height: int) -> None:
    """Clicks the demo's <TextInput> with a real mouse event, on macOS.

    Not `BASALT_TEST_TAP`, and not System Events' `click at`. The first enters
    at the touch dispatcher, below the window system, so it moves React
    Native's responder and never reaches the peer that takes focus. The second
    performs an accessibility *press*, which a text field does nothing with --
    and it answers with the name of the element it found, which makes it look
    like it worked. Both were tried, both reported the feature broken, and the
    feature was fine.

    The field is located through accessibility rather than by arithmetic on the
    window's origin: the title bar's height is the host's business and nothing
    here should have to know it. That also means this clicks the control the
    system believes is there, which is worth something on its own.
    """
    # Derived rather than looked up. The field is exposed to accessibility as
    # an AXGroup rather than an AXTextField -- see plan/backlog.md -- so there
    # is no role to search for, and hunting the only group in the window would
    # break the first time the demo grows another.
    #
    # So: ask the window where it is and how big it is, and take the title
    # bar's height as the difference between that and the surface, which the
    # caller read out of a tree dump. Nothing here has to know what AppKit's
    # title bar measures.
    script = """
    tell application "System Events"
      set procs to (every process whose name contains "basalt")
      if (count of procs) = 0 then error "no host process"
      set w to first window of (item 1 of procs)
      set {wx, wy} to position of w
      set {ww, wh} to size of w
      return (wx as text) & "," & (wy as text) & "," & (ww as text) & "," & (wh as text)
    end tell
    """
    found = subprocess.run(
        ["osascript", "-e", script], capture_output=True, text=True
    )
    if found.returncode != 0 or found.stdout.count(",") != 3:
        raise Failure(f"could not find the host window: {found.stderr.strip()}")
    wx, wy, _ww, wh = (int(part) for part in found.stdout.strip().split(","))

    chrome = wh - surface_height
    x = wx + FIELD_POINT[0]
    y = wy + chrome + FIELD_POINT[1]

    # CoreGraphics through ctypes, so nothing has to be compiled to run the
    # suite. A post that goes nowhere is what a missing accessibility
    # permission looks like, which the scenario's failure names.
    import ctypes

    core = ctypes.cdll.LoadLibrary(
        "/System/Library/Frameworks/ApplicationServices.framework/ApplicationServices"
    )

    class CGPoint(ctypes.Structure):
        _fields_ = [("x", ctypes.c_double), ("y", ctypes.c_double)]

    core.CGEventCreateMouseEvent.restype = ctypes.c_void_p
    core.CGEventCreateMouseEvent.argtypes = [
        ctypes.c_void_p, ctypes.c_uint32, CGPoint, ctypes.c_uint32
    ]
    core.CGEventPost.argtypes = [ctypes.c_uint32, ctypes.c_void_p]

    point = CGPoint(x, y)
    for event_type in (5, 1, 2):  # mouseMoved, leftMouseDown, leftMouseUp
        event = core.CGEventCreateMouseEvent(None, event_type, point, 0)
        core.CGEventPost(0, event)  # kCGHIDEventTap
        time.sleep(0.15)


def click_field_for_real(surface_height: int) -> None:
    """The platform's way of producing a click a window system believes in."""
    if PLATFORM == "macos":
        click_field_with_cgevent(surface_height)
        return
    if PLATFORM == "linux":
        # The demo's field, from js/index.js. xdotool goes through the X server,
        # so GDK delivers the press itself.
        click_with_xdotool([(FIELD_POINT[0], FIELD_POINT[1])])
        return
    raise Skipped(f"no real click on {PLATFORM}")


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
        env["BASALT_DUMP_TREE"] = str(dump)
        env["BASALT_QUIT_AFTER_MS"] = str(run_ms)
        env.pop("BASALT_TEST_TAP", None)
        env.pop("BASALT_TEST_TYPE", None)

        if points and INPUT_MODE == "injected":
            env["BASALT_TEST_TAP"] = taps
        if typing and INPUT_MODE == "injected":
            env["BASALT_TEST_TYPE"] = typing

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
            _remember_output(stderr)
            check_output(stderr, process.returncode)
        else:
            result = subprocess.run(
                command, cwd=REPO, env=env, capture_output=True, text=True, timeout=timeout,
            )
            _remember_output(result.stderr)
            check_output(result.stderr, result.returncode)

        if not dump.exists():
            raise Failure("host wrote no widget tree")
        return dump.read_text()


def expect_logged(needle: str, why: str) -> None:
    """Asserts on what the host printed, rather than on what it rendered.

    Some facts are not in the widget tree and should not be. `Platform.OS` is
    one: the demo used to render it, which made the tree differ between the two
    desktops for a reason that was not a bug, so scripts/compare_all.sh could
    never compare the richest app there is. Logging it keeps the check and lets
    the trees match.
    """
    if needle not in LAST_HOST_OUTPUT:
        raise Failure(f"{why}: expected {needle!r} in the host's output")


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

    expect_contains(tree, "React Native on the desktop", "React rendered no text")

    # The platform package, end to end: an app built for `linux` has to see
    # Platform.OS === 'linux'. Getting this wrong is quiet -- React Native's
    # Platform shim resolves to itself and yields undefined rather than
    # complaining -- so the demo logs it and this asserts on the log.
    #
    # The log rather than the tree, since phase 28: rendering it made the demo's
    # tree differ between the two desktops for a reason that was not a bug, and
    # that tree is the thing scripts/compare_all.sh compares.
    expect_logged(
        f"Platform.OS is {PLATFORM}",
        f"the app did not see Platform.OS === {PLATFORM!r}; was it bundled for {PLATFORM}?",
    )
    expect_contains(tree, "texture=160x100", "the image never loaded or decoded")
    expect_contains(tree, 'text="row 0"', "the list did not render")
    expect_contains(tree, 'text="row 23"', "the list is short of rows")

    # Accessibility: what a screen reader would be told. A <Text> should call
    # itself a label and an <Image> an image without the app saying so, and an
    # explicit accessibilityRole should win.
    expect_contains(tree, "role=text", "no <Text> reported itself as text")
    expect_contains(tree, "role=image", "no <Image> reported itself as an image")
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


def is_port_taken(port: int, host: str = "localhost") -> bool:
    try:
        with socket.create_connection((host, port), timeout=1.5):
            return True
    except OSError:
        return False


class Metro:
    """Metro on its own port, for the Fast Refresh scenario."""

    def __init__(self, log: Path) -> None:
        self.process = None
        self.log = log

    def __enter__(self) -> "Metro":
        # Refuse to share the port. A packager already listening here is not
        # ours, and talking to it means testing against whatever React Native
        # *it* was started with. That happened: a Metro left over from a run
        # against another version served its bundle to this one for an hour,
        # and the symptom was a version mismatch nobody had introduced. Note
        # that `pkill -f "cli.js start"` does not match these -- scripts/metro.sh
        # execs `metro serve` -- which is why they accumulate unnoticed.
        if is_port_taken(METRO_PORT):
            raise Failure(
                f"something is already listening on port {METRO_PORT}.\n"
                "This scenario needs its own packager, and reusing a stranger's "
                "would test whatever React Native that one was started with.\n"
                'Stop it with: pkill -f "metro serve"'
            )

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

    def serves_edit(self) -> str:
        """Whether a fresh bundle from Metro contains the edit.

        This is what separates the two ways this scenario can fail. If Metro
        serves the new text, its file watching is fine and the Fast Refresh
        client never subscribed. If it serves the old text, Metro never saw the
        file change.
        """
        try:
            return "yes" if AFTER in self.fetch_bundle() else "no"
        except Exception as error:  # diagnostics must not raise
            return f"could not tell ({error})"

    def fetch_bundle(self) -> str:
        # The platform of the run, not a fixed one. Both callers care:
        # `prewarm` is warming the bundle *this host* is about to ask for, and
        # `serves_edit` is diagnosing the bundle it actually ran. Hardcoding
        # `linux` made a macOS run prewarm the wrong bundle -- leaving the real
        # one cold, which is the race prewarm exists to lose -- and then report
        # on a bundle nothing had loaded.
        url = (
            f"http://localhost:{METRO_PORT}/index.bundle"
            f"?platform={PLATFORM}&dev=true&minify=false"
        )
        with urllib.request.urlopen(url, timeout=300) as response:
            if response.status != 200:
                raise Failure(f"metro answered {response.status} for the bundle")
            return response.read().decode("utf-8", "replace")

    def prewarm(self) -> None:
        """Builds the bundle before the host asks for it.

        The host tries Metro and falls back to the on-disk bundle, which is a
        production one. A cold Metro takes longer to answer than that fallback
        is willing to wait, so without this the app quietly runs the *release*
        bundle and no edit will ever reach it -- which is what CI saw, reported
        as "Metro never pushed an update".

        Warming alone is all this is for. It used to do a second job by
        accident: the graph it built was the one HMRClient subscribed to, so on
        Linux Fast Refresh worked because of this call rather than because the
        host was right. See plan/48-fast-refresh.md.
        """
        try:
            self.fetch_bundle()
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


def bundle_app(build: Path, entry: str, dev: bool = False) -> Path:
    """Bundles js/<entry>.js for this platform, unless it is already there.

    Most scenarios run the demo, which CI bundles as a build step. The two that
    need an app of their own -- hover and pointerEvents, both about input that
    the demo has nothing listening for -- build it here rather than adding two
    more steps to every CI job that will not use them.
    """
    bundled = build / f"{entry}.{PLATFORM}.jsbundle.js"
    if bundled.exists():
        return bundled
    arguments = [
        "--dev" if dev else "--prod",
        "--platform", PLATFORM,
        "--entry", f"{entry}.js",
        "--out", f"{entry}.{PLATFORM}.jsbundle",
        "--build-dir", build.name,
    ]
    # Windows cannot exec a shell script, and answers an attempt with
    # "%1 is not a valid Win32 application" -- which says nothing about shells.
    # Git for Windows brings bash, and CI's own bundle step already runs this
    # same script through it.
    #
    # A relative path, and only here: bash on Windows reads a backslash as an
    # escape, so an absolute `D:\a\...\bundle.sh` is not the path it looks
    # like. Everything below runs with cwd=REPO anyway.
    if os.name == "nt":
        command = ["bash", "scripts/bundle.sh", *arguments]
    else:
        command = [str(REPO / "scripts" / "bundle.sh"), *arguments]

    built = subprocess.run(command, cwd=REPO, capture_output=True, text=True, timeout=600)
    if built.returncode != 0 or not bundled.exists():
        # Both streams and the exit code: bundle.sh puts Metro's own output on
        # stdout, and a failure that reports neither is a failure nobody can act
        # on -- which is exactly how this first failed on a runner.
        raise Failure(
            f"could not bundle js/{entry}.js (exit {built.returncode}, "
            f"{'wrote' if bundled.exists() else 'no'} bundle):\n"
            f"--- stdout ---\n{tail_text(built.stdout, 30)}\n"
            f"--- stderr ---\n{tail_text(built.stderr, 30)}"
        )
    return bundled


def tail_text(text: str, lines: int = 25) -> str:
    return "\n".join(text.splitlines()[-lines:])


def is_subsequence(wanted: list, got: list) -> bool:
    """Whether `wanted` appears in `got` in order, with anything in between.

    Not equality: a run on a real display can pick up a motion event of its own
    -- a window mapping under the cursor is one -- and what is being asserted is
    the order these arrive in, not that nothing else ever happens.
    """
    iterator = iter(got)
    return all(item in iterator for item in wanted)


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
    if os.environ.get("BASALT_SKIP_FAST_REFRESH"):
        raise Skipped(
            "Metro does not notice file edits on this machine; see docs/TESTING.md"
        )
    if PLATFORM == "windows":
        # scripts/metro.sh is a shell script, and this scenario is the only one
        # that shells out at all. Everything it would prove about the *host* --
        # dev mode, the DevSettings TurboModule, the websocket -- is the same
        # code on every platform; what would be platform-specific is Metro's
        # file watching, which is Metro's.
        raise Skipped("scripts/metro.sh has no Windows path")

    source = REPO / "js" / "index.js"
    original = source.read_text()
    if original.count(BEFORE) != 1:
        raise Failure(f"the demo does not contain exactly one {BEFORE!r} to edit")

    running = f'Running "{MODULE}"'

    with tempfile.TemporaryDirectory() as directory:
        dump = Path(directory) / "tree.txt"
        log = Path(directory) / "host.log"
        env = dict(os.environ)
        env["BASALT_DUMP_TREE"] = str(dump)
        # A backstop, not the schedule: the host is asked to quit by signal as
        # soon as the refresh shows up.
        env["BASALT_QUIT_AFTER_MS"] = "180000"
        env["BASALT_DEV"] = "1"
        env["BASALT_DEV_PORT"] = str(METRO_PORT)
        env.pop("BASALT_TEST_TAP", None)
        env.pop("BASALT_TEST_TYPE", None)

        metro_log = Path(directory) / "metro.log"

        def file_state() -> str:
            """What is actually on disk, since everything else is inference."""
            try:
                stat = source.stat()
                text = source.read_text()
                return (
                    f"{source}: {stat.st_size} bytes, mtime {stat.st_mtime}, "
                    f"contains the edit: {AFTER in text}"
                )
            except Exception as error:
                return f"could not stat the demo ({error})"

        def diagnose(message: str) -> Failure:
            """Fails with what the two processes were saying, not just a verdict."""
            return Failure(
                f"{message}\n"
                f"--- the file on disk ---\n{file_state()}\n"
                f"--- all of metro ---\n{tail(metro_log, 400)}\n"
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

                edited = original.replace(BEFORE, AFTER)
                source.write_text(edited)

                # The demo has no refresh boundary, so React Native reloads the
                # whole surface and the app runs a second time. Waiting on
                # either the reload or that second run keeps this from depending
                # on which of the two the demo happens to provoke.
                #
                # Rewritten on each attempt rather than written once, because a
                # watcher that is not up yet does not queue anything: the event
                # is simply never delivered and nothing re-crawls. Metro watches
                # the React Native checkout, some eight thousand directories,
                # and attaching all of that takes markedly longer on a cold CI
                # machine than on a warm laptop. Everything else was ruled out
                # first -- inotify works there, the limits are generous, and
                # neither side has watchman, so both run the same node watcher.
                deadline = time.time() + 150
                applied = False
                while time.time() < deadline and not applied:
                    applied = wait_for_log(log, running, 2, timeout=10)
                    if not applied:
                        # A rewrite is enough: the file map keys on size and
                        # mtime, and mtime moves.
                        source.write_text(edited)
                if not applied:
                    raise diagnose(
                        "Metro never pushed an update after the edit "
                        f"(does a fresh bundle contain it? {metro.serves_edit()})"
                    )

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



def test_click_focuses_a_field(bundle: Path) -> None:
    """A real click into a <TextInput> focuses it.

    The one interaction nothing else here covers. "focus a TextInput, type, and
    see it round-trip" looks like it does, and does not: it taps the demo's
    *button*, which calls focus() -- so every path through this suite focuses
    programmatically, and a regression in focus-by-click would be invisible.

    The demo's field takes a different border colour when it is focused, and
    `describeTree` prints per-edge border colours, so the tree is the assertion
    and nothing new has to be rendered to check it.
    """
    if PLATFORM == "windows":
        raise Skipped("SendInput moves the runner's real cursor")
    if PLATFORM == "linux" and not real_input_available():
        raise Skipped("needs a display and xdotool; a click has to be real")

    # The surface's height, so the click can be aimed without anything here
    # knowing what a title bar measures. One short run with no input, which is
    # cheaper than guessing and cannot drift.
    root = re.search(r"view tag=\d+ frame=\(0,0 [\d.]+x([\d.]+)\)", run_host(bundle, run_ms=3000))
    if root is None:
        raise Failure("could not read the surface height from a tree dump")
    surface_height = int(float(root.group(1)))

    with tempfile.TemporaryDirectory() as directory:
        dump = Path(directory) / "tree.txt"
        env = dict(os.environ)
        env["BASALT_DUMP_TREE"] = str(dump)
        env["BASALT_QUIT_AFTER_MS"] = "12000"
        env.pop("BASALT_TEST_TAP", None)
        env.pop("BASALT_TEST_TYPE", None)

        process = subprocess.Popen(
            [str(HOST), str(bundle), MODULE], cwd=REPO, env=env,
            stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True,
        )
        # The window has to exist, and be where the system thinks it is.
        time.sleep(5)
        try:
            click_field_for_real(surface_height)
            time.sleep(2)
        finally:
            _, stderr = process.communicate(timeout=90)
        _remember_output(stderr)
        check_output(stderr, process.returncode)

        if not dump.exists():
            raise Failure("host wrote no widget tree")
        tree = dump.read_text()

    # PALETTE[0] on every edge, which styles.fieldFocused sets and nothing else
    # in the demo uses as a border.
    if FOCUSED_BORDER not in tree:
        raise Failure(
            "the field did not take focus from a real click; its border is still "
            f"unfocused.\nOn macOS the usual cause is that the run has no "
            f"accessibility permission, so CGEventPost silently posts nothing.\n"
            f"{tree[:1200]}"
        )


def test_hover(bundle: Path) -> None:
    """A cursor that presses nothing still reaches JavaScript.

    Runs js/hover.js rather than the demo, because hover is the one part of the
    input path with no equivalent in React Native's touch model and the demo has
    nothing listening for it.

    What this actually proves is a division of labour. A host emits one
    `pointerMove` per motion and nothing else; React Native's own
    PointerEventsProcessor turns that into enter, leave, over and out. So the
    assertion that matters is the third move, from one box to the other: `over`
    and `out` fire at the boxes, and the card -- which contains both -- hears
    neither a leave nor an enter, because the cursor never left it. A host that
    emitted enter and leave itself would get that wrong, and would also see
    every event twice. See core/HoverTracker.h.

    Injected on every platform, unlike the tap scenarios. A real hover means
    moving the machine's actual cursor over the window and leaving it there,
    which no automated run can do without taking the pointer away from whoever
    is using the machine.
    """
    hover_bundle = bundle_app(bundle.parent, "hover")

    points = ";".join(
        f"{x},{y}" for x, y in (HOVER_LEFT_BOX, HOVER_RIGHT_BOX, HOVER_CARD_ONLY)
    )
    env = dict(os.environ)
    env["BASALT_QUIT_AFTER_MS"] = "9000"
    # A negative point is the cursor leaving the surface, which no move can
    # express and which the host reports as a pointerLeave.
    env["BASALT_TEST_HOVER"] = f"{points};-1,-1"
    env.pop("BASALT_TEST_TAP", None)
    env.pop("BASALT_TEST_TYPE", None)

    result = subprocess.run(
        [str(HOST), str(hover_bundle), "BasaltHover"],
        cwd=REPO, env=env, capture_output=True, text=True, timeout=120,
    )
    _remember_output(result.stderr)
    check_output(result.stderr, result.returncode)

    # Both streams: GLib sends g_message to stdout and only warnings to stderr,
    # so on the GTK host the app's own logging is not where the other scenarios
    # look for it.
    events = [
        line.split("hover: ", 1)[1].strip()
        for line in (result.stdout + result.stderr).splitlines()
        if "hover: " in line
    ]

    expected = [
        # Into the left box: it is hovered, and so is the card around it.
        "over left", "enter card", "enter left",
        # Across to the right box.
        "out left", "leave left", "over right", "enter right",
        # Into the card, outside both boxes.
        "out right", "leave right",
        # Off the surface entirely.
        "leave card",
    ]
    if not is_subsequence(expected, events):
        raise Failure(
            "hover did not reach JavaScript in the expected order.\n"
            f"expected, in order: {expected}\n"
            f"got:                {events}"
        )

    # The claim the order alone does not make: crossing from one box to the
    # other must not leave the card, because the cursor stayed inside it.
    crossing = events[events.index("leave left"):events.index("enter right")]
    if "leave card" in crossing:
        raise Failure(
            "the card was left while the cursor moved between its own children; "
            "enter and leave are being treated as if they bubbled.\n"
            f"got: {events}"
        )


def test_pointer_events(bundle: Path) -> None:
    """What a press can land on, which is not the same as what is drawn.

    Runs js/pointerevents.js: four rows, one per value of the prop, each a
    panel with a button inside it and a backdrop behind it. Every one of the
    three reports which view it thinks was pressed, so each tap has exactly one
    right answer and a wrong implementation says which way it is wrong.

    `box-none` and `box-only` are the pair worth the app. They are the two
    everyone gets the wrong way round, and the difference between `box-none`
    and simply falling back to the parent is only visible when something is
    underneath -- which is what the backdrop is for.
    """
    app = bundle_app(bundle.parent, "pointerevents")

    # Panel-sized coordinates from the app's own layout: rows 110 tall with 12
    # between them under 24 of padding, and a panel inset 14 inside each. x=500
    # is over the panel and clear of the button; x=100 is over the button.
    taps = [(500, 80), (500, 200), (500, 320), (100, 320), (500, 440), (100, 440)]
    expected = [
        # auto: the ordinary case.
        "auto: panel",
        # none: the panel and its button are out of hit testing entirely.
        "none: backdrop",
        # box-none: transparent to a press that misses its children...
        "box-none: backdrop",
        # ...and its children are still targets.
        "box-none: button",
        # box-only: the panel takes the press wherever it lands, including
        # over the button.
        "box-only: panel",
        "box-only: panel",
    ]

    env = dict(os.environ)
    env["BASALT_QUIT_AFTER_MS"] = "12000"
    env["BASALT_TEST_TAP"] = ";".join(f"{x},{y}" for x, y in taps)
    env.pop("BASALT_TEST_TYPE", None)
    env.pop("BASALT_TEST_HOVER", None)

    result = subprocess.run(
        [str(HOST), str(app), "BasaltPointerEvents"],
        cwd=REPO, env=env, capture_output=True, text=True, timeout=150,
    )
    _remember_output(result.stderr)
    check_output(result.stderr, result.returncode)

    # Both streams: GLib sends g_message to stdout and only warnings to stderr.
    pressed = [
        line.split("pressed ", 1)[1].strip()
        for line in (result.stdout + result.stderr).splitlines()
        if "pressed " in line
    ]
    if pressed != expected:
        raise Failure(
            "a press landed on the wrong view.\n"
            f"expected: {expected}\n"
            f"got:      {pressed}"
        )


def test_keyboard_focus(bundle: Path) -> None:
    """An app driven entirely from the keyboard.

    Runs js/focus.js: three buttons and a text field, in that order. The field
    is the interesting neighbour -- it takes focus because it is a real GtkText,
    NSTextField or EDIT control, and it has to sit in the same Tab order as the
    buttons around it without either side knowing about the other.

    Tab reaching a `<Pressable>` is not something React Native gives a platform
    for free: its `focusable` prop is parsed only into Android's and tvOS's
    props, and the C++ host's are a bare alias of the base ones. So `accessible`
    is the signal on all three hosts, and this is what says they agree about it.

    Activating a focused button dispatches `topClick` with an empty payload --
    what React Native for Android sends from a focusable view -- which
    Pressability turns into the `onPress` the app already handles. So the last
    assertion is that a keyboard press and a mouse press are the same press.

    Injected on every platform, unlike the tap scenarios. A real Tab needs a
    window the display server considers focused, which an automated run does not
    reliably have on any of the three.
    """
    app = bundle_app(bundle.parent, "focus")

    env = dict(os.environ)
    env["BASALT_QUIT_AFTER_MS"] = "14000"
    env["BASALT_TEST_FOCUS"] = "tab;activate;tab;tab;tab;shift-tab"
    env.pop("BASALT_TEST_TAP", None)
    env.pop("BASALT_TEST_TYPE", None)
    env.pop("BASALT_TEST_HOVER", None)

    result = subprocess.run(
        [str(HOST), str(app), "BasaltFocus"],
        cwd=REPO, env=env, capture_output=True, text=True, timeout=180,
    )
    _remember_output(result.stderr)
    check_output(result.stderr, result.returncode)

    # Both streams: GLib sends g_message to stdout and only warnings to stderr.
    events = [
        line.split("[js] ", 1)[1].strip()
        for line in (result.stdout + result.stderr).splitlines()
        if "[js] " in line and line.split("[js] ", 1)[1].strip().split(" ")[0]
        in ("focus", "blur", "press")
    ]

    expected = [
        "focus first",
        # Enter on a focused button is the same press a click produces.
        "press first",
        "blur first",
        "focus second",
        "blur second",
        "focus third",
        # The text field is a stop like any other, in tree order.
        "blur third",
        "focus field",
        # And back the way it came.
        "blur field",
        "focus third",
    ]
    if events != expected:
        raise Failure(
            "the keyboard did not move focus where it should have.\n"
            f"expected: {expected}\n"
            f"got:      {events}"
        )


def test_logbox(bundle: Path) -> None:
    """A console error opens React Native's own inspector.

    The red box is not something a host draws. It is
    `LogBoxInspectorContainer`, registered by AppRegistry under the name
    "LogBox" exactly as an app registers its own component -- so what a host
    provides is a second surface, started when the LogBox TurboModule asks. The
    toasts, which sit inside the app's own surface, have worked since <View> and
    <Text> did; the box they open needed the surface.

    A development bundle, because that is the only kind that has LogBox at all:
    a production one registers a component that renders nothing.

    The scenario taps the error toast and asserts on the inspector's own tree,
    which each host appends to the dump under `--- LogBox ---`.
    """
    app = bundle_app(bundle.parent, "logbox", dev=True)

    env = dict(os.environ)
    env["BASALT_QUIT_AFTER_MS"] = "12000"
    # The first tap is a miss, and is there only to let the error arrive: the
    # app logs it a second and a half in, and taps fire a second apart. The
    # second lands on the error toast, which is the lower of the two.
    env["BASALT_TEST_TAP"] = "5,5;400,650"
    env.pop("BASALT_TEST_TYPE", None)
    env.pop("BASALT_TEST_HOVER", None)
    env.pop("BASALT_TEST_FOCUS", None)

    with tempfile.TemporaryDirectory() as directory:
        dump = Path(directory) / "tree.txt"
        env["BASALT_DUMP_TREE"] = str(dump)
        result = subprocess.run(
            [str(HOST), str(app), "BasaltLogBox"],
            cwd=REPO, env=env, capture_output=True, text=True, timeout=180,
        )
        _remember_output(result.stderr)
        check_output(result.stderr, result.returncode, allow_js_errors=True)
        if not dump.exists():
            raise Failure("host wrote no widget tree")
        tree = dump.read_text()

    if "--- LogBox ---" not in tree:
        raise Failure(
            "tapping the error toast did not open the inspector; no second "
            f"surface was started.\n{tree[-1500:]}"
        )
    inspector = tree.split("--- LogBox ---", 1)[1]

    # The inspector's own furniture, which nothing else in the tree has.
    for needle, why in (
        ('text="Console Error"', "the inspector did not name the log's level"),
        ('text="an error that should raise a red box"', "the message is missing"),
        ('text="Call Stack"', "the stack section is missing"),
    ):
        if needle not in inspector:
            raise Failure(f"{why}: expected {needle} in\n{inspector[:1500]}")

    # LogBox's icons are `require()`d images, and Metro's `build` has no
    # --assets-dest -- so without scripts/copy_assets.js they lay out at the
    # right size and draw nothing. `texture=` is what says one arrived.
    if "texture=" not in inspector:
        raise Failure(
            "the inspector's icons did not load; were the bundle's assets copied?\n"
            f"{inspector[:1500]}"
        )


SCENARIOS = [
    ("initial render", test_initial_render),
    ("scrollToEnd, and a tap that bubbles from a label", test_scroll_to_end),
    ("scroll away and back", test_scroll_round_trip),
    ("focus a TextInput, type, and see it round-trip through React", test_text_input),
    ("click a TextInput with a real mouse and see it focus", test_click_focuses_a_field),
    ("hover across nested views and see enter, leave, over and out", test_hover),
    ("pointerEvents decides what four taps land on", test_pointer_events),
    ("Tab reaches a Pressable, and Enter presses it", test_keyboard_focus),
    ("a console error opens LogBox's inspector", test_logbox),
    ("edit the demo and watch Fast Refresh apply it", test_fast_refresh),
]


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--build-dir", default="build")
    parser.add_argument("--bundle", default=None)
    parser.add_argument("--input", choices=["auto", "real", "injected"], default="auto")
    parser.add_argument(
        "--platform",
        choices=["auto", *HOSTS],
        default="auto",
        help="which host to run; auto picks the one that is built",
    )
    arguments = parser.parse_args()

    global INPUT_MODE, HOST, PLATFORM
    build = REPO / arguments.build_dir

    if arguments.platform == "auto":
        # Whichever is built. No machine has more than one -- a host needs its
        # toolkit -- so there is nothing to disambiguate in practice, and
        # --platform is there for the case where there somehow is.
        built = [name for name, binary in HOSTS.items() if (build / binary).exists()]
        if not built:
            print(
                f"error: no host built in {build}\n"
                f"       looked for {', '.join(HOSTS.values())}",
                file=sys.stderr,
            )
            return 1
        PLATFORM = built[0]
    else:
        PLATFORM = arguments.platform

    HOST = build / HOSTS[PLATFORM]
    if arguments.bundle is None:
        arguments.bundle = f"{arguments.build_dir}/main.jsbundle.js"
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
        else "taps injected at the dispatcher, skipping the window system"
    )
    print(f"running {len(SCENARIOS)} scenarios against {bundle.name} on {PLATFORM}")
    print(f"input: {INPUT_MODE} -- {note}")
    failed = 0
    skipped = 0
    for name, scenario in SCENARIOS:
        try:
            scenario(bundle)
            print(f"  ok    {name}")
        except Skipped as reason:
            skipped += 1
            print(f"  skip  {name}")
            print(f"        {reason}")
        except Failure as failure:
            failed += 1
            print(f"  FAIL  {name}\n        {failure}")
        except subprocess.TimeoutExpired:
            failed += 1
            print(f"  FAIL  {name}\n        the host did not exit")

    total = len(SCENARIOS) - skipped
    tally = f"\n{total - failed}/{total} passed"
    if skipped:
        tally += f", {skipped} skipped"
    print(tally)
    return 0 if failed == 0 else 1


if __name__ == "__main__":
    sys.exit(main())
