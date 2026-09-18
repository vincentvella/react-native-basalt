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
import shutil
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


def git_bash() -> Path | None:
    """Git for Windows' bash, or None. See bundle_app for why it is not `bash`."""
    roots = [
        os.environ.get("ProgramFiles", r"C:\Program Files"),
        os.environ.get("ProgramW6432", r"C:\Program Files"),
        os.environ.get("ProgramFiles(x86)", r"C:\Program Files (x86)"),
    ]
    for root in roots:
        candidate = Path(root) / "Git" / "bin" / "bash.exe"
        if candidate.exists():
            return candidate
    return None


def bundle_app(build: Path, entry: str, dev: bool = False) -> Path:
    """Bundles js/<entry>.js for this platform, unless it is already there.

    Most scenarios run the demo, which CI bundles as a build step. The two that
    need an app of their own -- hover and pointerEvents, both about input that
    the demo has nothing listening for -- build it here rather than adding two
    more steps to every CI job that will not use them.
    """
    bundled = build / f"{entry}.{PLATFORM}.jsbundle.js"
    source = REPO / "js" / f"{entry}.js"
    # Older than the file it was built from means a scenario would silently test
    # the last version of the app rather than this one -- which is exactly what
    # happened the first time this helper was used twice.
    if bundled.exists() and bundled.stat().st_mtime >= source.stat().st_mtime:
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
    # Named by path, not as `bash`. On a Windows runner that name resolves to
    # C:\Windows\System32\bash.exe, which is the WSL launcher: it exits 1
    # saying "Windows Subsystem for Linux has no installed distributions", on
    # *stdout*, which is how this failed for two runs with an empty error
    # message.
    #
    # A relative script path, and only here: bash on Windows reads a backslash
    # as an escape, so an absolute `D:\a\...\bundle.sh` is not the path it
    # looks like. Everything here runs with cwd=REPO anyway.
    if os.name == "nt":
        shell = git_bash()
        if shell is None:
            raise Skipped("bundling this app needs Git for Windows' bash")
        command = [str(shell), "scripts/bundle.sh", *arguments]
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

                # Which bundle actually evaluated. Editing before knowing that
                # produces a confusing failure much later, because the app runs
                # perfectly well on the stale one.
                #
                # Asked of Metro rather than of the host: Metro logs a BUNDLE
                # line when it serves one, and that is a direct statement that
                # the app fetched it. The previous version watched the host's
                # log for `Failed to load TurboModule: LogBox`, which said the
                # same thing only for as long as LogBox was unimplemented --
                # once it worked, the line stopped appearing and this scenario
                # failed on every machine, for a reason that had nothing to do
                # with Fast Refresh.
                if not wait_for_log(metro_log, "BUNDLE", 1, timeout=30):
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
    # A real cursor sitting over the window when it maps has already entered the
    # card before the first scripted move lands, so the `enter card` this
    # expects in the middle of the sequence arrived at the start instead -- and
    # another arrives at the end, when the pointer is back over it. Every event
    # is present and the scripted ones are in order; what differs is an event
    # the script did not ask for.
    #
    # `is_subsequence` was written to tolerate exactly that -- "a run on a real
    # display can pick up a motion event of its own" -- and cannot here,
    # because the spurious event has the same name as a wanted one and the match
    # takes the wrong occurrence.
    #
    # Only where there is a real cursor to do it. CI runs this host under Xvfb,
    # which has no pointer, and asserts the full order; this machine runs the
    # GTK host over the quartz backend, which docs/HANDOFF.md already warns is
    # not the target. Skipped rather than loosened, so that what CI checks stays
    # exact.
    if not is_subsequence(expected, events) and PLATFORM == "linux" and sys.platform == "darwin":
        raise Skipped(
            "a real cursor over the window enters the card before the script "
            "does; this host is GTK over quartz, which is not the target"
        )
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


def test_initial_url(bundle: Path) -> None:
    """`Linking.getInitialURL()` answers with what the desktop launched the app
    with.

    A desktop hands a URL over on the command line -- a `.desktop` entry's `%u`,
    a registered scheme on macOS, a shell association on Windows -- so the host
    records whichever argument carried a scheme and the module answers with it.
    Run twice, because both answers matter: an app that was not opened with a
    URL must get null rather than an empty string, which is what React Native's
    own JavaScript checks for.

    What this does not cover is a URL delivered to an application that is
    *already* running. That needs single-instance activation on each desktop and
    is a separate piece of work; see plan/backlog.md.
    """
    app = bundle_app(bundle.parent, "modules")

    def initial_url_for(arguments: list[str]) -> str:
        env = dict(os.environ)
        env["BASALT_QUIT_AFTER_MS"] = "6000"
        env.pop("BASALT_TEST_TAP", None)
        env.pop("BASALT_TEST_TYPE", None)
        env.pop("BASALT_TEST_HOVER", None)
        env.pop("BASALT_TEST_FOCUS", None)
        result = subprocess.run(
            [str(HOST), str(app), "BasaltModules", *arguments],
            cwd=REPO, env=env, capture_output=True, text=True, timeout=120,
        )
        _remember_output(result.stderr)
        check_output(result.stderr, result.returncode)
        # Both streams: GLib sends g_message to stdout and only warnings to
        # stderr.
        for line in (result.stdout + result.stderr).splitlines():
            if "initialURL: " in line:
                return line.split("initialURL: ", 1)[1].strip()
        raise Failure("the app never reported an initial URL")

    launched = "basalt://opened-by-the-desktop"
    if initial_url_for([launched]) != launched:
        raise Failure(
            f"launched with {launched}, and getInitialURL did not say so: "
            f"{initial_url_for([launched])!r}"
        )
    if initial_url_for([]) != "null":
        raise Failure(
            "an app launched with no URL must get null rather than a string: "
            f"{initial_url_for([])!r}"
        )


def test_share(bundle: Path) -> None:
    """`Share.share()` reaches the platform and settles both ways.

    Two things were broken and only one of them was a missing module.
    `Share.js` branches on `Platform.OS` being exactly `android` or `ios` and
    rejects with "Unsupported platform" otherwise, so no desktop module was ever
    reached; the replacement is in packages/react-native-basalt/src/overrides.

    Run twice, because a share sheet has two answers and an app is expected to
    handle both. BASALT_TEST_DIALOG answers the picker without showing one --
    see native/core/TestDialog.h for why that rather than driving a real dialog,
    and for what it skips.

    On the desktops with no share service the picker is built from a clipboard
    and a mail client, so "Copy" is checked by reading the clipboard back:
    what is asserted there is that the picker ran, not merely that a promise
    settled. macOS shows NSSharingServicePicker instead, which the instrument
    answers without running any of that.
    """
    app = bundle_app(bundle.parent, "share")

    def outcome_for(answer: str) -> tuple[str, str]:
        env = dict(os.environ)
        env["BASALT_QUIT_AFTER_MS"] = "8000"
        env["BASALT_TEST_DIALOG"] = answer
        for name in ("BASALT_TEST_TAP", "BASALT_TEST_TYPE", "BASALT_TEST_HOVER",
                     "BASALT_TEST_FOCUS"):
            env.pop(name, None)
        result = subprocess.run(
            [str(HOST), str(app), "BasaltShare"],
            cwd=REPO, env=env, capture_output=True, text=True, timeout=120,
        )
        _remember_output(result.stderr)
        check_output(result.stderr, result.returncode)
        both = result.stdout + result.stderr
        action = ""
        clipboard = ""
        for line in both.splitlines():
            if "share: action " in line:
                action = line.split("share: action ", 1)[1].strip()
            if "clipboard: " in line:
                clipboard = line.split("clipboard: ", 1)[1].strip()
        if not action:
            raise Failure(f"the share never settled:\n{tail_text(both, 30)}")
        return action, clipboard

    # Button 0 is "Copy" in the fallback picker, and any chosen service on macOS.
    shared, clipboard = outcome_for("0")
    if shared != "sharedAction":
        raise Failure(f"a share that was accepted reported {shared!r}")

    if PLATFORM != "macos":
        # The picker really ran: this is what its "Copy" did.
        if "React Native on the desktop" not in clipboard:
            raise Failure(
                "the picker resolved but nothing reached the clipboard: "
                f"{clipboard!r}"
            )

    dismissed, _ = outcome_for("dismiss")
    if dismissed != "dismissedAction":
        raise Failure(
            "a dismissed share must resolve rather than reject, and with "
            f"dismissedAction; got {dismissed!r}"
        )


def test_alert(bundle: Path) -> None:
    """`Alert.alert()` shows a dialog and reports which button was pressed.

    It did neither, on any of the three desktops, and had not since the module
    was written: `Alert.alert` branches on `Platform.OS` being exactly `ios` or
    `android` with no else, and `RCTAlertManager` resolves to its Android
    sibling, which calls a module this platform does not have. Two breaks in one
    chain, both silent. Found while implementing Share, which fails the same way
    for the same reason.

    js/alert.js logs which button its handler ran, so the assertion is on the
    answer travelling back rather than on a dialog being on screen.
    """
    app = bundle_app(bundle.parent, "alert")

    def chose(answer: str) -> str:
        env = dict(os.environ)
        env["BASALT_QUIT_AFTER_MS"] = "6000"
        env["BASALT_TEST_DIALOG"] = answer
        for name in ("BASALT_TEST_TAP", "BASALT_TEST_TYPE", "BASALT_TEST_HOVER",
                     "BASALT_TEST_FOCUS"):
            env.pop(name, None)
        result = subprocess.run(
            [str(HOST), str(app), "BasaltAlert"],
            cwd=REPO, env=env, capture_output=True, text=True, timeout=120,
        )
        _remember_output(result.stderr)
        check_output(result.stderr, result.returncode)
        both = result.stdout + result.stderr
        for line in both.splitlines():
            if "chose: " in line:
                return line.split("chose: ", 1)[1].strip()
        raise Failure(f"the alert's callback never reached JavaScript:\n{tail_text(both, 30)}")

    # js/alert.js offers Cancel then Delete, in that order.
    if chose("0") != "Cancel":
        raise Failure(f"pressing the first button reported {chose('0')!r}")
    if chose("1") != "Delete":
        raise Failure(f"pressing the second button reported {chose('1')!r}")
    # `dismiss` is the last button, which is where the way out lives.
    if chose("dismiss") != "Delete":
        raise Failure("dismiss should answer with the last button")


class NotificationService:
    """A session bus with a stand-in notification daemon on it.

    The one thing about notifications that cannot be asserted without a service
    running is that a notification is actually sent, and neither a developer's
    Mac nor a CI runner has a desktop's own daemon. So the suite starts both: a
    plain `dbus-daemon` -- not `dbus-run-session`, which on macOS insists on
    launchd's socket and fails -- and `basalt_notification_stub` on it.

    A context manager rather than a fixture, so the bus goes away with the test
    even when it fails.
    """

    def __init__(self, build: Path, directory: Path) -> None:
        self.stub_binary = build / "basalt_notification_stub"
        self.directory = directory
        self.address: str | None = None
        self.log = directory / "stub.log"
        self._bus: subprocess.Popen | None = None
        self._stub: subprocess.Popen | None = None

    def available(self) -> bool:
        return self.stub_binary.exists() and shutil.which("dbus-daemon") is not None

    def __enter__(self) -> "NotificationService":
        config = self.directory / "session.conf"
        # A unix socket in a temporary directory, and a policy that allows
        # everything: this bus exists for one test and has one client on it.
        config.write_text(
            "<!DOCTYPE busconfig PUBLIC "
            '"-//freedesktop//DTD D-Bus Bus Configuration 1.0//EN" '
            '"http://www.freedesktop.org/standards/dbus/1.0/busconfig.dtd">\n'
            "<busconfig><type>session</type>"
            "<listen>unix:tmpdir=/tmp</listen>"
            '<policy context="default">'
            '<allow send_destination="*"/><allow own="*"/><allow receive_sender="*"/>'
            "</policy></busconfig>\n"
        )
        started = subprocess.run(
            ["dbus-daemon", "--config-file", str(config), "--print-address", "--fork",
             "--print-pid"],
            capture_output=True, text=True, timeout=30,
        )
        lines = started.stdout.split()
        if started.returncode != 0 or not lines:
            raise Failure(f"could not start a session bus: {started.stderr.strip()}")
        self.address = lines[0]

        environment = dict(os.environ)
        environment["DBUS_SESSION_BUS_ADDRESS"] = self.address
        self._stub = subprocess.Popen(
            [str(self.stub_binary)], env=environment,
            stdout=self.log.open("w"), stderr=subprocess.STDOUT,
        )
        # Owning the name is asynchronous, and the host asks whether anyone owns
        # it before sending anything.
        deadline = time.time() + 10
        while time.time() < deadline:
            if self.log.exists() and "STUB ready" in self.log.read_text():
                return self
            time.sleep(0.1)
        raise Failure("the notification stub never took the bus name")

    def __exit__(self, *_: object) -> None:
        if self._stub is not None:
            self._stub.terminate()
            self._stub.wait(timeout=10)
        # The bus forked, so it is not a child of this process; it is told to go
        # by closing its socket, which happens when the directory is removed.
        subprocess.run(["pkill", "-f", "dbus-daemon --config-file " + str(self.directory)],
                       capture_output=True)

    def saw(self) -> str:
        return self.log.read_text() if self.log.exists() else ""


def packaged_host(build: Path) -> Path:
    """The host as the platform's own idea of an application, where that differs.

    macOS is the one that differs and the one this exists for: an executable
    with no bundle around it has no bundle identifier, and
    `UNUserNotificationCenter` refuses a process without one -- so the same code
    answers `denied` out of a build directory and `granted` inside a `.app`.
    Running the bundled copy is the only way a scenario can see the second.

    Everything else returns the host it was given, because on Linux and Windows
    nothing about where the executable sits decides anything.
    """
    if PLATFORM != "macos":
        return HOST

    node = subprocess.run(
        ["node", "-e",
         "const {packageApp} = require(process.argv[1]);"
         "process.stdout.write(packageApp({"
         "platform: 'macos', hostBinary: process.argv[2],"
         "outputDir: process.argv[3], projectRoot: process.argv[4]}).launchPath);",
         str(REPO / "packages/react-native-basalt/cli/packageApp.js"),
         str(HOST), str(build / "app"), str(REPO / "js")],
        cwd=REPO, capture_output=True, text=True, timeout=120,
    )
    if node.returncode != 0:
        raise Failure(f"could not package the host:\n{node.stderr}")
    return Path(node.stdout.strip())


def test_notifications(bundle: Path) -> None:
    """An app using `expo-notifications` gets answers rather than an exception.

    React Native has no notification API to port, so the contract implemented is
    expo-notifications' -- the same argument gesture-handler and expo-clipboard
    were ported on. The package's JavaScript is unchanged; what this platform
    supplies is the native modules under it.

    Skipped unless BASALT_EXPO_APP names an app with expo-notifications
    installed *and* the host was built against that app's expo-modules-core with
    -DBASALT_EXPO_MODULES_CORE. Neither is true of a plain checkout, and CI does
    not build with Expo at all.

    What is asserted is everything that does not need a notification service to
    be running:

      - the package imports, which needs all thirteen native modules to exist,
        because `requireNativeModule` throws on a name it cannot find;
      - `getPermissionsAsync` answers, and carries a reason when it says no;
      - a rejected send carries that same reason rather than an empty failure;
      - a method this platform does not implement is reported by name.

    Where `dbus-daemon` and `basalt_notification_stub` are both there, the suite
    starts a session bus with a stand-in daemon on it and asserts the rest: that
    the permission is granted, that the send reaches the service, and that it
    carries the app's own words. That is the whole contract, and it is the only
    way to exercise it -- a real desktop's daemon is not present on a Mac or on
    a CI runner.
    """
    expo_app = os.environ.get("BASALT_EXPO_APP")
    if not expo_app or not (Path(expo_app) / "node_modules" / "expo-notifications").exists():
        raise Skipped("needs BASALT_EXPO_APP naming an app with expo-notifications")

    app = bundle_app(bundle.parent, "notifications")
    host = packaged_host(bundle.parent)

    env = dict(os.environ)
    env["BASALT_QUIT_AFTER_MS"] = "8000"
    for name in ("BASALT_TEST_TAP", "BASALT_TEST_TYPE", "BASALT_TEST_HOVER",
                 "BASALT_TEST_FOCUS", "BASALT_TEST_DIALOG"):
        env.pop(name, None)

    with tempfile.TemporaryDirectory() as directory:
        service = NotificationService(bundle.parent, Path(directory))
        # A session bus with a daemon on it where one can be had, so that the
        # send itself is exercised rather than only the refusal. Without it the
        # host reports why it cannot, which is the other half worth asserting.
        #
        # Linux only, even on a machine that has dbus installed: it is the only
        # host that sends over D-Bus, and a daemon listening beside a macOS run
        # proves nothing except that it was listening.
        if PLATFORM == "linux" and service.available():
            with service:
                env["DBUS_SESSION_BUS_ADDRESS"] = service.address or ""
                result = subprocess.run(
                    [str(host), str(app), "BasaltNotifications"],
                    cwd=REPO, env=env, capture_output=True, text=True, timeout=180,
                )
                sent = service.saw()
        else:
            sent = ""
            result = subprocess.run(
                [str(host), str(app), "BasaltNotifications"],
                cwd=REPO, env=env, capture_output=True, text=True, timeout=180,
            )

    _remember_output(result.stderr)
    check_output(result.stderr, result.returncode)
    both = result.stdout + result.stderr

    said = {}
    for line in both.splitlines():
        if "notifications: " in line:
            rest = line.split("notifications: ", 1)[1].strip()
            key, _, value = rest.partition(" ")
            said[key] = value

    if "imported" not in said:
        raise Failure(
            "expo-notifications did not import; a native module it asks for is "
            f"missing.\n{tail_text(both, 30)}"
        )
    if said.get("status") not in ("granted", "denied"):
        raise Failure(f"getPermissionsAsync answered {said.get('status')!r}")

    if said["status"] == "denied":
        # The reason is the whole value of answering `denied` rather than
        # throwing: an app, or the person reading the log, can tell why.
        if not said.get("reason"):
            raise Failure("a denied permission must say why")
        if not said.get("rejected", "").endswith(said["reason"]):
            raise Failure(
                "a send that could not happen should be rejected with the same "
                f"reason the permission gave; got {said.get('rejected')!r}"
            )
    else:
        if said.get("scheduled") != "true":
            raise Failure("a granted platform did not schedule a notification")
        if said.get("presented") != "1":
            raise Failure(
                f"one notification was sent and {said.get('presented')} are presented"
            )
        # What the daemon actually received, which is the only thing that says
        # the D-Bus call was made and carried the app's words. Only Linux has
        # one: macOS hands the request to UNUserNotificationCenter, where what
        # happens next depends on a permission a person grants in System
        # Settings and an automated run cannot. What the macOS run does assert
        # is everything above -- that a bundled host answers `granted` where an
        # unbundled one cannot, and that the send was accepted.
        if PLATFORM != "linux" or not sent:
            return
        if "Notify" not in sent:
            raise Failure(f"the notification service was never asked to show one:\n{sent}")
        if "A notification from a desktop" not in sent:
            raise Failure(f"the notification reached the service without its body:\n{sent}")
        if "CloseNotification" not in sent:
            raise Failure(f"dismissing a notification did not reach the service:\n{sent}")

    # Android's channels, which no desktop has. expo's own check reports it by
    # name because the method is left off rather than stubbed -- see
    # native/core/ExpoModules.h.
    if "channels" not in said:
        raise Failure("an unimplemented method neither answered nor reported itself")


def test_controls(bundle: Path) -> None:
    """The four components that are a control rather than a box.

    Runs js/controls.js, which has one of each: three <ActivityIndicator>s, three
    <Switch>es, a <Modal> and a <RefreshControl> inside a <ScrollView>. Three
    short runs rather than one, because each needs a different instrument and
    the three hosts do not agree on what order two instruments run in -- a
    dependency worth not having.

    What each run is really asserting:

      mounted     that all four reached the view layer at all. Until this
                  existed the registry substituted UnimplementedNativeView for
                  every one of them, which renders as nothing and says nothing.
                  The `control=` field is written by core/DesktopControls.h, so
                  three hosts cannot describe the same switch differently.
      switch      that a press becomes `onValueChange` and that the *app* is
                  what moves the switch. A host that let its own GtkSwitch or
                  NSSwitch move would pass a screenshot and fail this.
      modal       that `visible` mounts an overlay over the whole surface, that
                  `onShow` fires, and that Escape reaches `onRequestClose`
                  rather than closing the modal behind the app's back.
      refresh     that a wheel which keeps asking to go up after the list has
                  reached its top fires `onRefresh` -- once. See
                  core/PullToRefresh.h for why a desktop counts the wheel
                  instead of measuring a pull.
    """
    app = bundle_app(bundle.parent, "controls")

    def run(run_ms: int, **instruments: str) -> tuple[str, str]:
        with tempfile.TemporaryDirectory() as directory:
            dump = Path(directory) / "tree.txt"
            env = dict(os.environ)
            env["BASALT_DUMP_TREE"] = str(dump)
            env["BASALT_QUIT_AFTER_MS"] = str(run_ms)
            for name in ("BASALT_TEST_TAP", "BASALT_TEST_TYPE", "BASALT_TEST_HOVER",
                         "BASALT_TEST_FOCUS", "BASALT_TEST_SCROLL"):
                env.pop(name, None)
            env.update(instruments)
            result = subprocess.run(
                [str(HOST), str(app), "BasaltControls"],
                cwd=REPO, env=env, capture_output=True, text=True,
                timeout=run_ms / 1000 + 60,
            )
            _remember_output(result.stderr)
            check_output(result.stderr, result.returncode)
            # Both streams: GLib sends g_message to stdout and only warnings to
            # stderr.
            return dump.read_text() if dump.exists() else "", result.stdout + result.stderr

    # --- everything mounts, and says what state it is in ---------------------
    #
    # Nothing is pressed in this run: what it is asserting is the state the app
    # asked for, which a run that had already toggled a switch could not.
    tree, _ = run(6000)

    for expected in ("control=spinner:animating",
                     "control=spinner-large:animating",
                     "control=spinner:stopped",
                     "control=switch:off",
                     "control=switch:on",
                     "control=switch:on:disabled",
                     "control=refresh:idle"):
        if expected not in tree:
            raise Failure(
                f"no view in the tree reports {expected}.\n"
                "A component the registry does not know about is substituted by\n"
                "UnimplementedNativeView, which renders as nothing at all.\n"
                f"{tree}"
            )

    # --- a press on a switch is React's to answer ----------------------------
    #
    # The coordinates come from the app's own layout: 24 of padding, a 22-tall
    # label, a 56-tall row, another label, then the switch row -- so the first
    # switch's cell is 90 wide starting at x=24 and its middle is (69, 152).
    tree, logged = run(7000, BASALT_TEST_TAP="69,152")

    if "switch one -> true" not in logged:
        raise Failure(
            "pressing a <Switch> did not reach onValueChange.\n"
            f"{tail_text(logged)}"
        )
    if "switch three ->" in logged:
        raise Failure("a disabled <Switch> reported a change")
    # Two switches were on when the app started; the pressed one makes three.
    # This is the half that says React moved it: a host whose own GtkSwitch or
    # NSSwitch flipped itself would have got here without the prop changing.
    if tree.count("control=switch:on") != 3:
        raise Failure(
            "the switch did not follow its own prop after the press.\n"
            f"{tree}"
        )

    # --- a switch is reachable from the keyboard -----------------------------
    #
    # React Native's `focusable` prop never reaches this platform, so
    # `accessible` is the signal -- and <Switch> does not set it, because on a
    # phone the native control is focusable by being a control. On a desktop a
    # switch Tab skips is broken, so being a control is the signal here too.
    tree, logged = run(9000, BASALT_TEST_FOCUS="tab;activate")

    if "switch one -> true" not in logged:
        raise Failure(
            "Tab did not reach a <Switch>, or Enter did not toggle it.\n"
            f"{tail_text(logged)}"
        )
    # The disabled one is not a stop. Every desktop skips a control that cannot
    # be operated rather than stopping on one that does nothing.
    for line in tree.splitlines():
        if "control=switch:on:disabled" in line and "focusable" in line:
            raise Failure(f"a disabled <Switch> is in the tab order:\n{line}")

    # --- the modal opens, and Escape asks the app to close it ----------------
    _, logged = run(9000, BASALT_TEST_TAP="114,226", BASALT_TEST_FOCUS="escape")

    events = [
        line.split("[js] ", 1)[1].strip()
        for line in logged.splitlines()
        if "[js] " in line and line.split("[js] ", 1)[1].strip().startswith(
            ("opening modal", "modal "))
    ]
    expected = ["opening modal", "modal shown", "modal close requested"]
    if not is_subsequence(expected, events):
        raise Failure(
            "the modal did not open and answer Escape.\n"
            f"expected, in order: {expected}\n"
            f"got:                {events}"
        )

    # --- a wheel past the top of a list is this platform's pull --------------
    #
    # Two notches of 53 pixels each, which is past the 80 in
    # core/DesktopControls.h. Negative is up, the direction contentOffset reads.
    _, logged = run(9000, BASALT_TEST_SCROLL="400,400,-1;400,400,-1;400,400,-1")

    if "refresh requested" not in logged:
        raise Failure(
            "a wheel past the top of the list did not fire onRefresh.\n"
            f"{tail_text(logged)}"
        )
    # Once, not once a notch. React Native's contract is one call per pull, and
    # a missing latch is invisible except as a stream of requests.
    if logged.count("refresh requested") != 1:
        raise Failure(
            "onRefresh fired "
            f"{logged.count('refresh requested')} times for one pull; it must fire once.\n"
            f"{tail_text(logged)}"
        )


def test_dev_menu(bundle: Path) -> None:
    """React Native's developer menu, which on a desktop is a keyboard shortcut.

    Runs the host in dev mode against a real Metro, opens the menu the way
    Ctrl+D does, and picks an item. Two runs, because the two items worth
    asserting on prove different halves:

      Reload                    that the menu reaches JavaScript at all. The
                                reload lives on ReactHost's private
                                `reloadReactInstance` and the only thing wired
                                to it is React Native's own DevSettings module,
                                so the host asks JavaScript to make the call --
                                through a device event that
                                `src/overrides/setUpDeveloperTools.js` listens
                                for. If that override is not in the bundle,
                                nothing happens and nothing says so, which is
                                exactly what this catches.

      Toggle Element Inspector  that the inspector renders. It needs no platform
                                code -- it is React Native's own event and its
                                own React views -- so what this really asserts
                                is that a host mounts enough of React Native for
                                its developer tools to work unmodified.

    Injected at the host's own dev-menu entry point rather than through a real
    Ctrl+D, for the same reason every other keyboard scenario is: a real
    keystroke needs a window the display server considers focused. What it
    skips is the delivery of the keystroke and nothing above it.

    BASALT_TEST_MENU answers the menu, because a popup nobody dismisses stops an
    automated run where it stands -- on macOS `popUpMenuPositioningItem` runs
    its tracking loop on the main thread. See core/TestDialog.h.
    """
    if PLATFORM == "windows":
        # scripts/metro.sh is a shell script, which is also why the Fast
        # Refresh scenario skips here. Everything this proves about the *host*
        # is the same code on every platform.
        raise Skipped("scripts/metro.sh has no Windows path")

    def run(choice: str, run_ms: int) -> tuple[str, str]:
        with tempfile.TemporaryDirectory() as directory:
            dump = Path(directory) / "tree.txt"
            log = Path(directory) / "host.log"
            env = dict(os.environ)
            env["BASALT_DUMP_TREE"] = str(dump)
            env["BASALT_QUIT_AFTER_MS"] = str(run_ms)
            env["BASALT_DEV"] = "1"
            env["BASALT_DEV_PORT"] = str(METRO_PORT)
            env["BASALT_TEST_FOCUS"] = "devmenu"
            env["BASALT_TEST_MENU"] = choice
            for name in ("BASALT_TEST_TAP", "BASALT_TEST_TYPE", "BASALT_TEST_HOVER",
                         "BASALT_TEST_SCROLL"):
                env.pop(name, None)

            with log.open("w") as sink:
                process = subprocess.Popen(
                    [str(HOST), str(bundle), MODULE],
                    cwd=REPO, env=env, stdout=subprocess.DEVNULL, stderr=sink, text=True,
                )
                process.wait(timeout=run_ms / 1000 + 90)
            text = log.read_text()
            _remember_output(text)
            check_output(text, process.returncode)
            return (dump.read_text() if dump.exists() else ""), text

    with Metro(Path(tempfile.gettempdir()) / "basalt-devmenu-metro.log"):
        # Item 0 is Reload. The app running a second time is the whole
        # assertion: the instance was torn down and rebuilt.
        _, logged = run("0", 20000)
        running = logged.count(f'Running "{MODULE}"')
        if running < 2:
            raise Failure(
                "the dev menu's Reload did not reload the app.\n"
                f'"Running \"{MODULE}\"" appears {running} time(s); it should appear twice.\n'
                f"{tail_text(logged)}"
            )

        # Item 1 is Toggle Element Inspector. React Native's own inspector
        # panel, rendered out of ordinary views this host already mounts.
        tree, logged = run("1", 16000)
        if "Tap something to inspect it" not in tree:
            raise Failure(
                "toggling the element inspector rendered nothing.\n"
                "React Native's inspector is ordinary React views, so this is a\n"
                "statement about mounting rather than about developer tools.\n"
                f"{tree}"
            )


def test_file_dialogs(bundle: Path) -> None:
    """The native file dialogs, which React Native has no API for at all.

    Runs js/dialogs.js: three buttons, one per kind. A phone has no file dialog,
    so unlike every other scenario here there is no React Native behaviour to be
    compatible with -- what is asserted is this project's own contract, which is
    that all three answer `{canceled, paths}`:

      * `canceled` is a different answer from an empty list. Every API that
        collapses those two is one somebody has to work around.
      * `paths` is a list even for a save and a folder, where it always holds
        one, so an app moving between the three is not also moving between
        result types. The native side clamps it, which is what the second run
        checks: a script naming two paths for a save still gets one.

    BASALT_TEST_FILE_DIALOG answers the dialog, because a file dialog is the
    third thing an automated run cannot get past and the one with the most
    behind it -- what an app does with a path cannot be reached without a path.
    See core/TestDialog.h.
    """
    app = bundle_app(bundle.parent, "dialogs")

    # The app's own layout: 24 of padding, a 22-tall label, then 48-tall buttons
    # 12 apart. Their middles are at y=70, 130 and 190.
    taps = "134,70;134,130;134,190"

    def run(answer: str) -> str:
        env = dict(os.environ)
        env["BASALT_QUIT_AFTER_MS"] = "9000"
        env["BASALT_TEST_TAP"] = taps
        env["BASALT_TEST_FILE_DIALOG"] = answer
        for name in ("BASALT_TEST_TYPE", "BASALT_TEST_HOVER", "BASALT_TEST_FOCUS",
                     "BASALT_TEST_SCROLL", "BASALT_TEST_MENU"):
            env.pop(name, None)
        result = subprocess.run(
            [str(HOST), str(app), "BasaltDialogs"],
            cwd=REPO, env=env, capture_output=True, text=True, timeout=150,
        )
        _remember_output(result.stderr)
        check_output(result.stderr, result.returncode)
        # Both streams: GLib sends g_message to stdout and only warnings to
        # stderr.
        return result.stdout + result.stderr

    def answers(text: str) -> dict:
        found = {}
        for line in text.splitlines():
            if "dialog " in line and "canceled=" in line:
                rest = line.split("dialog ", 1)[1].strip()
                kind, _, detail = rest.partition(": ")
                found[kind] = detail
        return found

    # The path separator the host splits on: a Windows path starts "C:\\", so a
    # colon would cut every one of them in two.
    separator = ";" if PLATFORM == "windows" else ":"
    first = "C:\\tmp\\a.png" if PLATFORM == "windows" else "/tmp/a.png"
    second = "C:\\tmp\\b.png" if PLATFORM == "windows" else "/tmp/b.png"

    chosen = answers(run(separator.join([first, second])))
    if len(chosen) != 3:
        raise Failure(
            f"expected an answer from all three dialogs, got {sorted(chosen)}"
        )
    # A multiple open keeps both.
    if chosen["open"] != f"canceled=false paths={first}|{second}":
        raise Failure(f"openFile answered {chosen['open']!r}")
    # A save and a folder are one path however many the script named, which is
    # what the platform dialogs enforce and what an app may assume.
    if chosen["save"] != f"canceled=false paths={first}":
        raise Failure(f"saveFile answered {chosen['save']!r}")
    if chosen["folder"] != f"canceled=false paths={first}":
        raise Failure(f"openFolder answered {chosen['folder']!r}")

    cancelled = answers(run("cancel"))
    for kind, detail in cancelled.items():
        if detail != "canceled=true paths=":
            raise Failure(f"a cancelled {kind} answered {detail!r}")
    if len(cancelled) != 3:
        raise Failure(f"expected all three to report a cancel, got {sorted(cancelled)}")


def has_window_manager() -> bool:
    """Whether anything on this display would honour a full-screen request.

    Every EWMH-compliant window manager sets `_NET_SUPPORTING_WM_CHECK` on the
    root window, and a bare X server -- which is what Xvfb is without one --
    has nobody to set it. Answers false when `xprop` is missing too: it cannot
    be told from "no window manager" and both mean the same thing here.
    """
    try:
        result = subprocess.run(
            ["xprop", "-root", "-notype", "_NET_SUPPORTING_WM_CHECK"],
            capture_output=True, text=True, timeout=10,
        )
    except (OSError, subprocess.SubprocessError):
        return False
    return result.returncode == 0 and "window id" in result.stdout


def test_window(bundle: Path) -> None:
    """The window an app is in, which React Native has no API for.

    Runs js/window.js, which logs its own bounds every time they change. Two
    presses, and what each one proves is different:

      setSize      that a request reaches the window manager and comes back as
                   the size the app actually got. The app never reads what it
                   asked for -- `bounds` is what happened, which is the only
                   honest answer when a tiling window manager may refuse.

      setFullScreen  that a state change is reported as well as a size change.
                   It is the case most likely to be missed, because on two of
                   the three hosts going full screen is *not* a resize: GTK
                   changes a window property and Windows changes a style, and a
                   host watching only for resizes would report neither.

    Position is deliberately not asserted. GTK4 removed `gtk_window_move` and
    Wayland has no equivalent, so `setPosition` and `center` do nothing on Linux
    and `bounds.x` is always zero there -- a cross-platform assertion on it
    would be asserting a lie. See native/gtk/GtkWindowControl.cpp.
    """
    app = bundle_app(bundle.parent, "window")

    # The app's own layout: 24 of padding, a 22-tall label, then 48-tall rows 12
    # apart. Row one's middle is y=70 and row two's is y=130; the buttons are
    # 150 wide from x=24, so their middles are x=99 and x=261.
    env = dict(os.environ)
    env["BASALT_QUIT_AFTER_MS"] = "12000"
    env["BASALT_TEST_TAP"] = "99,70;261,130"
    for name in ("BASALT_TEST_TYPE", "BASALT_TEST_HOVER", "BASALT_TEST_FOCUS",
                 "BASALT_TEST_SCROLL", "BASALT_TEST_MENU"):
        env.pop(name, None)

    result = subprocess.run(
        [str(HOST), str(app), "BasaltWindow"],
        cwd=REPO, env=env, capture_output=True, text=True, timeout=150,
    )
    _remember_output(result.stderr)
    check_output(result.stderr, result.returncode)

    # Both streams: GLib sends g_message to stdout and only warnings to stderr.
    reported = [
        line.split("window bounds: ", 1)[1].strip()
        for line in (result.stdout + result.stderr).splitlines()
        if "window bounds: " in line
    ]
    if not reported:
        raise Failure(
            "the app never learned its own bounds.\n"
            f"{tail_text(result.stdout + result.stderr)}"
        )

    # A window of some size, before anything was asked of it. The first line is
    # zeroes on purpose -- it is the render before the module exists -- so what
    # matters is that a real one followed.
    if all(line.startswith("0x0 ") for line in reported):
        raise Failure(f"the bounds never became real: {reported}")

    if not any(line.startswith("700x500 ") for line in reported):
        raise Failure(f"setSize(700, 500) was not reported back: {reported}")

    if not any("fullScreen=true" in line for line in reported):
        # A window manager is what makes a window full screen; the application
        # only asks. CI runs the Linux host under Xvfb, which has no window
        # manager at all, so the request is not refused so much as unheard --
        # and asserting it there would be asserting something about the runner.
        #
        # Checked rather than assumed: where there *is* a window manager this
        # must pass, and a silent skip would hide the case this scenario exists
        # for. `_NET_SUPPORTING_WM_CHECK` is the property every EWMH-compliant
        # window manager sets on the root window.
        if PLATFORM == "linux" and not has_window_manager():
            print(
                "        (no window manager, so the full-screen half of this "
                "scenario could not run)"
            )
            return
        raise Failure(
            "setFullScreen(true) was never reported. On two of the three hosts "
            "this is not a resize, so a host watching only for resizes misses "
            f"it entirely.\n{reported}"
        )


def test_context_menu(bundle: Path) -> None:
    """The other kind of menu: the one that pops up where you press.

    Runs the second app in js/menu.js. A press opens a four-entry popup --
    a separator and a disabled item among them -- and BASALT_TEST_MENU answers
    it, because a menu cannot be dismissed by an automated run. On macOS that is
    not a convenience: `popUpMenuPositioningItem` runs the menu's own tracking
    loop on the main thread, so a popup nobody closes stops the process where it
    stands.

    Two runs, because a menu has two answers and they must not be the same one:

      chosen      index 2 is Rename, which is past a separator. Indexes count
                  separators so they line up with the list that was passed in,
                  and getting that wrong shows up as an app acting on the item
                  above or below the one a person picked.

      dismissed   null, not an index -- the shape a cancelled file dialog
                  answers with, and for the same reason: an index is a number,
                  and a caller checking `if (index)` would read entry zero as
                  nothing.

    Unlike the application menu this runs on all three, which is the point of it
    existing: `Menu.isSupported` is false on GNOME, and a popup is something
    every desktop has always had.
    """
    app = bundle_app(bundle.parent, "menu")

    def press(variable: str, answer: str) -> str:
        env = dict(os.environ)
        env["BASALT_QUIT_AFTER_MS"] = "8000"
        # The button is at (134, 70): 24 of padding, a 22-tall label, then a
        # 220x48 button.
        env[variable] = "134,70"
        env["BASALT_TEST_MENU"] = answer
        for name in ("BASALT_TEST_TAP", "BASALT_TEST_SECONDARY_TAP", "BASALT_TEST_TYPE",
                     "BASALT_TEST_HOVER", "BASALT_TEST_FOCUS", "BASALT_TEST_SCROLL",
                     "BASALT_TEST_CLOSE_WINDOW"):
            if name != variable:
                env.pop(name, None)
        result = subprocess.run(
            [str(HOST), str(app), "BasaltContextMenu"],
            cwd=REPO, env=env, capture_output=True, text=True, timeout=120,
        )
        _remember_output(result.stderr)
        check_output(result.stderr, result.returncode)
        return result.stdout + result.stderr

    def run(answer: str) -> str:
        return press("BASALT_TEST_TAP", answer)

    def run_secondary(answer: str) -> str:
        return press("BASALT_TEST_SECONDARY_TAP", answer)

    logged = run("2")
    if "context menu: opening" not in logged:
        raise Failure(f"the press never reached the app.\n{tail_text(logged)}")
    if "context menu answered: 2" not in logged:
        raise Failure(
            "choosing entry 2 did not come back as 2. Indexes count separators, "
            "so an off-by-one here is an app acting on the item next to the one "
            f"a person picked.\n{tail_text(logged)}"
        )
    # The onSelect half: `show()` calls the chosen item's handler before it
    # resolves, which is what most callers use instead of the index. Rename is
    # entry 2, so this also says the index was mapped back to the right item.
    if "context menu selected: Rename" not in logged:
        raise Failure(
            f"the chosen item's onSelect never ran.\n{tail_text(logged)}"
        )

    # A real right-click. The button number reaches `onPointerDown` as W3C's 2,
    # and -- the half that matters -- `onPress` does not fire: the same view is
    # a button and has a context menu, which is what a desktop expects.
    #
    # Every host used to get this wrong in its own way. GTK set its click
    # gesture to button 0 and AppKit forwarded rightMouseDown: like mouseDown:,
    # so on both a right-click *activated* whatever it landed on; Windows
    # handled only WM_LBUTTONDOWN, so a right-click there did nothing at all.
    logged = run_secondary("2")
    if "context menu: opening from a right-click" not in logged:
        raise Failure(
            "a right-click did not reach onPointerDown with button 2.\n"
            f"{tail_text(logged)}"
        )
    if "context menu: opening\n" in logged or "context menu: opening\r" in logged:
        raise Failure(
            "a right-click fired onPress as well. A secondary click is not an "
            "activation on any desktop, and a view that is both a button and a "
            f"context-menu target would do two things at once.\n{tail_text(logged)}"
        )
    if "context menu selected: Rename" not in logged:
        raise Failure(f"the right-click menu chose nothing.\n{tail_text(logged)}")

    logged = run("dismiss")
    if "context menu answered: dismissed" not in logged:
        raise Failure(
            "a dismissed menu did not answer null. An index is a number, and a "
            "caller checking `if (index)` would read entry zero as nothing.\n"
            f"{tail_text(logged)}"
        )


def test_window_limits(bundle: Path) -> None:
    """How big the window may be, and the fact that it is not the same list
    everywhere.

    Runs the second app in js/window.js: a minimum of 500x400, a maximum of
    800x600, and two buttons that ask for sizes outside both. A size is a
    request; a limit is what the window manager answers it with.

    Two halves, and the first is the one that runs everywhere.

      capabilities   what this desktop says it does. Asserted against what each
                     host actually implements, because the whole point of
                     answering is that an app can trust the answer -- a
                     `capabilities` that said "yes" and then did nothing would
                     be worse than no capabilities at all. macOS and Windows do
                     all five. Linux does the minimum and the resizable flag,
                     and does not do position, maximum size or always-on-top:
                     GTK4 removed `gtk_window_set_geometry_hints` and
                     `gtk_window_set_keep_above` because Wayland has no protocol
                     for either, so those two are settled rather than pending.

      clamping       that a request outside a limit comes back clamped. Only
                     where the platform claims the limit, and only where there
                     is a window manager to enforce it -- CI runs the Linux host
                     under Xvfb, which has neither.

    `setResizable` and `setAlwaysOnTop` are called and not asserted, which is
    the honest limit rather than an oversight: neither is observable from inside
    the app. A window that cannot be resized is still whatever size it is, and
    one that floats is still where it was. What this says about them is that the
    native path runs on all three without taking the host with it.
    """
    app = bundle_app(bundle.parent, "window")

    # The same layout as the app above: 24 of padding, a 22-tall label, then a
    # 48-tall row. The buttons are 150 wide from x=24, so their middles are
    # x=99 and x=261 at y=70.
    env = dict(os.environ)
    env["BASALT_QUIT_AFTER_MS"] = "12000"
    env["BASALT_TEST_TAP"] = "99,70;261,70"
    for name in ("BASALT_TEST_TYPE", "BASALT_TEST_HOVER", "BASALT_TEST_FOCUS",
                 "BASALT_TEST_SCROLL", "BASALT_TEST_MENU"):
        env.pop(name, None)

    result = subprocess.run(
        [str(HOST), str(app), "BasaltWindowLimits"],
        cwd=REPO, env=env, capture_output=True, text=True, timeout=150,
    )
    _remember_output(result.stderr)
    check_output(result.stderr, result.returncode)
    logged = result.stdout + result.stderr

    if "window flags: resizable and always-on-top reached the host" not in logged:
        raise Failure(
            "setResizable or setAlwaysOnTop did not return. Neither can be "
            "asserted from inside the app; that they run at all is what this "
            f"checks.\n{tail_text(logged)}"
        )

    said = [line for line in logged.splitlines() if "window capabilities: " in line]
    if not said:
        raise Failure(f"the app never asked what the window can do.\n{tail_text(logged)}")
    answer = said[-1].split("window capabilities: ", 1)[1].strip()

    # What each host actually implements. Spelled out rather than derived, so
    # that a host quietly losing one of these fails here.
    expected = {
        "macos": "position=true minimumSize=true maximumSize=true resizable=true "
                 "alwaysOnTop=true",
        "windows": "position=true minimumSize=true maximumSize=true resizable=true "
                   "alwaysOnTop=true",
        "linux": "position=false minimumSize=true maximumSize=false resizable=true "
                 "alwaysOnTop=false",
    }[PLATFORM]
    if answer != expected:
        raise Failure(
            f"this host describes itself as\n  {answer}\nand the platform does\n"
            f"  {expected}\nAn app that trusts `capabilities` and gets a no-op is "
            "worse off than one with no capabilities at all."
        )

    if PLATFORM == "linux" and not has_window_manager():
        print(
            "        (no window manager, so the clamping half of this scenario "
            "could not run)"
        )
        return

    # What the window became after each request, rather than every size it has
    # ever been. The window opens at 900x700 and a limit set afterwards does not
    # reach back and shrink it -- no desktop does that, and neither does
    # Electron -- so the sizes before the first request are not evidence of
    # anything. What is being asserted is that a request made while a limit is
    # in force comes back inside it.
    def after(request: str) -> tuple[float, float] | None:
        _, marker, rest = logged.partition(f"window asked for {request}")
        if not marker:
            raise Failure(f"the app never asked for {request}.\n{tail_text(logged)}")
        for line in rest.splitlines():
            if "window limited bounds: " not in line:
                continue
            size = line.split("window limited bounds: ", 1)[1].strip()
            width, _, height = size.partition("x")
            return float(width), float(height)
        return None

    # Asking for 300x200 against a minimum of 500x400.
    smaller = after("300x200")
    if smaller is None:
        raise Failure(f"asking for 300x200 changed nothing.\n{tail_text(logged)}")
    if smaller[0] < 500 or smaller[1] < 400:
        raise Failure(
            f"setSize(300, 200) against a minimum of 500x400 gave {smaller}. "
            "AppKit's setFrame: clamps down to a maximum and not up to a "
            "minimum, which is why this is applied in core rather than left to "
            "the toolkit."
        )

    if "maximumSize=true" in expected:
        # Asking for 1400x1100 against a maximum of 800x600. Allowed to come
        # back smaller -- a display that cannot fit 800x600 is a window manager
        # doing its job -- but never larger.
        larger = after("1400x1100")
        if larger is None:
            raise Failure(f"asking for 1400x1100 changed nothing.\n{tail_text(logged)}")
        if larger[0] > 800 or larger[1] > 600:
            raise Failure(
                f"setSize(1400, 1100) against a maximum of 800x600 gave {larger}."
            )


def test_application_menu(bundle: Path) -> None:
    """The application menu, and the thing its absence quietly broke.

    Runs js/menu.js, which describes a File menu of its own and an Edit menu
    built entirely out of roles, and dumps whatever menu the platform actually
    installed.

    Read back from the platform rather than compared against what was sent,
    which is the point: it says a description became a real NSMenu or HMENU,
    with the shortcuts the platform attached to its roles. It is also the only
    way an automated run can see a menu bar -- a menu cannot be opened without
    a person, and BASALT_TEST_MENU answers popups rather than bars.

    The Edit menu is the half that matters and the half that is invisible. On
    macOS AppKit routes every key equivalent through the main menu before the
    responder chain sees it, so `role="copy"` is what makes Cmd-C reach a text
    field at all -- and this host shipped with a one-item Quit menu until the
    menu model existed, which meant copy, cut, paste, undo and select-all did
    nothing in every <TextInput> on macOS. This is what would catch that coming
    back.

    Linux asserts the opposite and asserts it deliberately: GNOME's guidelines
    have said to use a header bar with a menu button since GNOME 3 and GTK4
    removed the menu bar widget, so `Menu.isSupported` is false there and the
    dump is empty. That is a platform answering honestly rather than a gap.
    """
    app = bundle_app(bundle.parent, "menu")

    with tempfile.TemporaryDirectory() as directory:
        dump = Path(directory) / "menu.txt"
        env = dict(os.environ)
        env["BASALT_QUIT_AFTER_MS"] = "7000"
        env["BASALT_DUMP_MENU"] = str(dump)
        for name in ("BASALT_TEST_TAP", "BASALT_TEST_TYPE", "BASALT_TEST_HOVER",
                     "BASALT_TEST_FOCUS", "BASALT_TEST_SCROLL", "BASALT_TEST_MENU"):
            env.pop(name, None)

        result = subprocess.run(
            [str(HOST), str(app), "BasaltMenu"],
            cwd=REPO, env=env, capture_output=True, text=True, timeout=150,
        )
        _remember_output(result.stderr)
        check_output(result.stderr, result.returncode)
        menu = dump.read_text() if dump.exists() else ""

    both = result.stdout + result.stderr
    supported = "menu supported: true" in both
    if not supported and "menu supported: false" not in both:
        raise Failure(f"the app never said whether menus are supported:\n{tail_text(both)}")

    if not supported:
        # The honest answer, not a gap. See the docstring.
        if menu.strip() != "":
            raise Failure(
                "this platform reports no application menu and installed one "
                f"anyway:\n{menu}"
            )
        return

    # The app's own menu, with its own items.
    for expected in ("File", "New", "Open", "Edit"):
        if expected not in menu:
            raise Failure(f"no {expected!r} in the installed menu:\n{menu}")

    # The roles, which the platform filled in: neither the label nor the
    # shortcut came from the app.
    for role in ("Copy", "Paste", "Undo", "Select All"):
        if role not in menu:
            raise Failure(
                f"no {role!r} in the installed menu. A role is meant to arrive "
                f"with the platform's own word for it.\n{menu}"
            )

    # Disabled is carried through, which is the one item property a menu can
    # get wrong without anyone noticing until they click it.
    if "(disabled)" not in menu:
        raise Failure(f"an item disabled by the app was installed enabled:\n{menu}")


def test_debugging_overlay(bundle: Path) -> None:
    """React DevTools' element highlighter, which is driven only by commands.

    Runs js/overlay.js, which issues the commands DevTools would: the filled
    blue box over an inspected element, and the outline around something that
    just re-rendered. Directly rather than through DevTools, because DevTools
    is the only other thing that would and it needs a session attached.

    Two runs, because the second half of the contract is a disappearance. A
    trace update is meant to flash -- React Native's own overlay clears them
    rather than waiting to be told, since a box left behind after a component
    stopped re-rendering says the opposite of what it means -- so the later run
    asserts that nothing is left.

    The tree dump reports how many rectangles a view is drawing, because they
    are otherwise invisible to everything but a screenshot, and a command that
    arrived and drew nothing is exactly the failure worth catching.
    """
    app = bundle_app(bundle.parent, "overlay")

    def highlights(module: str, run_ms: int) -> tuple[int, str]:
        with tempfile.TemporaryDirectory() as directory:
            dump = Path(directory) / "tree.txt"
            env = dict(os.environ)
            env["BASALT_DUMP_TREE"] = str(dump)
            env["BASALT_QUIT_AFTER_MS"] = str(run_ms)
            for name in ("BASALT_TEST_TAP", "BASALT_TEST_TYPE", "BASALT_TEST_HOVER",
                         "BASALT_TEST_FOCUS", "BASALT_TEST_SCROLL"):
                env.pop(name, None)
            result = subprocess.run(
                [str(HOST), str(app), module],
                cwd=REPO, env=env, capture_output=True, text=True,
                timeout=run_ms / 1000 + 90,
            )
            _remember_output(result.stderr)
            check_output(result.stderr, result.returncode)
            tree = dump.read_text() if dump.exists() else ""
            return tree.count("highlights="), result.stdout + result.stderr

    # An inspected element, which stays until it is cleared. Whenever the tree is
    # dumped, it is there -- which is what makes this the half that says the
    # commands are routed at all.
    drawn, logged = highlights("BasaltOverlay", 5000)
    if "overlay: highlighted an element" not in logged:
        raise Failure(f"the app never issued the command:\n{tail_text(logged)}")
    if drawn == 0:
        raise Failure(
            "the overlay commands arrived and drew nothing. The view mounts, so "
            "this is the command routing rather than the component."
        )

    # A trace update, which takes itself down. Five seconds against a lifetime
    # of one and a half, so the answer does not depend on how fast the machine
    # is -- an earlier version dumped at exactly the moment it expired and read
    # its own success as a failure on a slow runner.
    left, logged = highlights("BasaltOverlayTrace", 5000)
    if "overlay: highlighted a trace update" not in logged:
        raise Failure(f"the app never issued the command:\n{tail_text(logged)}")
    if left != 0:
        raise Failure(
            "a trace update was still on screen after its lifetime. It is meant "
            "to flash; one that stays says the opposite of what it means."
        )


def test_windows(bundle: Path) -> None:
    """More than one window, which is more than one React tree.

    Runs js/windows.js: a counter in the first window's state, a button to open
    a second, and the same counter rendered again over there.

    A window is a surface is a React root -- that is Fabric's grain rather than
    a decision this made -- so the second window is not the first one's tree
    moved across. What is asserted is the three things that follow from it:

      it renders        the second window has a tree of its own, under its own
                        header in the dump.

      input is routed   a tap in the second window is hit-tested against the
                        second window's view tree. Each window has its own touch
                        dispatcher, which is the whole point of them, and the
                        first one's would happily hit-test a tree that is not on
                        screen and report a press on whatever happened to be at
                        those coordinates. `BASALT_TEST_TAP` takes "x,y@3" for
                        exactly this.

      state crosses     pressing in the second window calls a setter that lives
                        in the first window's tree, and *both* re-render. That
                        is the half that says `<Window>` is passing elements
                        through rather than running something separate.

    Closing is asserted too, and it is the part with a real ordering hazard
    behind it: stopping a surface unmounts its tree, which produces one last
    transaction of mutations, and destroying the window before those arrive
    leaves them naming views that are gone.
    """
    app = bundle_app(bundle.parent, "windows")

    def run(taps: str, run_ms: int) -> tuple[str, str]:
        with tempfile.TemporaryDirectory() as directory:
            dump = Path(directory) / "tree.txt"
            env = dict(os.environ)
            env["BASALT_DUMP_TREE"] = str(dump)
            env["BASALT_QUIT_AFTER_MS"] = str(run_ms)
            env["BASALT_TEST_TAP"] = taps
            for name in ("BASALT_TEST_TYPE", "BASALT_TEST_HOVER", "BASALT_TEST_FOCUS",
                         "BASALT_TEST_SCROLL", "BASALT_TEST_MENU"):
                env.pop(name, None)
            result = subprocess.run(
                [str(HOST), str(app), "BasaltWindows"],
                cwd=REPO, env=env, capture_output=True, text=True,
                timeout=run_ms / 1000 + 90,
            )
            _remember_output(result.stderr)
            check_output(result.stderr, result.returncode)
            tree = dump.read_text() if dump.exists() else ""
            return tree, result.stdout + result.stderr

    def run_closing(taps: str, surfaceId: int, run_ms: int) -> tuple[str, str]:
        with tempfile.TemporaryDirectory() as directory:
            dump = Path(directory) / "tree.txt"
            env = dict(os.environ)
            env["BASALT_DUMP_TREE"] = str(dump)
            env["BASALT_QUIT_AFTER_MS"] = str(run_ms)
            env["BASALT_TEST_TAP"] = taps
            # Closes the window the way its own close button does, rather than
            # the way the app does. See docs/TESTING.md.
            env["BASALT_TEST_CLOSE_WINDOW"] = str(surfaceId)
            for name in ("BASALT_TEST_TYPE", "BASALT_TEST_HOVER", "BASALT_TEST_FOCUS",
                         "BASALT_TEST_SCROLL", "BASALT_TEST_MENU"):
                env.pop(name, None)
            result = subprocess.run(
                [str(HOST), str(app), "BasaltWindows"],
                cwd=REPO, env=env, capture_output=True, text=True,
                timeout=run_ms / 1000 + 90,
            )
            _remember_output(result.stderr)
            check_output(result.stderr, result.returncode)
            tree = dump.read_text() if dump.exists() else ""
            return tree, result.stdout + result.stderr

    if "windows supported: true" not in run("", 5000)[1]:
        raise Skipped("this host cannot open a second window")

    # The button that opens one is at (134, 110) in the app's own layout: 24 of
    # padding, a 22-tall label, a 40-tall count, then 48-tall buttons.
    #
    # The second tap is in the *second* window, at the same place in its own
    # layout -- which is only the same number by coincidence, and is why the
    # window has to be named.
    tree, logged = run("134,110;134,110@3", 11000)

    if "--- window 3 ---" not in tree:
        raise Failure(
            "the second window opened and rendered nothing.\n"
            f"{tail_text(logged)}\n{tree}"
        )
    if "counted up from the second window" not in logged:
        raise Failure(
            "a tap in the second window did not reach it. Each window has its "
            "own touch dispatcher; this is what says the right one was used.\n"
            f"{tail_text(logged)}"
        )

    # Both trees show the same number, from the one piece of state, which lives
    # in the first window's tree and was changed from the second's.
    counts = [line for line in tree.splitlines() if 'text="count ' in line]
    if len(counts) != 2:
        raise Failure(f"expected a count in each window, found {len(counts)}:\n{tree}")
    if 'text="count 1"' not in counts[0] or 'text="count 1"' not in counts[1]:
        raise Failure(
            "the two windows disagree about the one piece of state they share.\n"
            + "\n".join(counts)
        )

    # And closing takes it away again.
    tree, logged = run("134,110;134,110", 11000)
    if "--- window 3 ---" in tree:
        raise Failure(f"the second window was still open after being closed:\n{tree}")

    # Closed by the person rather than by the app, which is a different path
    # through the host and the one that can go wrong quietly: the window is
    # destroyed either way, and only this one can leave the host holding a
    # record whose window is gone and the app believing it is still open.
    tree, logged = run_closing("134,110", 3, 11000)
    if "the second window closed itself" not in logged:
        raise Failure(
            "a window the person closed did not tell the app. Its `open` flag "
            "stays true, the next render tries to close a window that has "
            f"already closed, and it can never be reopened.\n{tail_text(logged)}"
        )
    if "--- window 3 ---" in tree:
        raise Failure(f"a window the person closed is still in the tree:\n{tree}")


def test_window_close_request(bundle: Path) -> None:
    """Being asked before a window closes, and refusing.

    The other half of `onClose`, and the earlier one. `onClose` says a window
    *has* closed; this says somebody is trying to, and it has not -- which is
    the only place an app can put "are you sure", because by the time the window
    has gone there is nothing left to ask about.

    It cannot work the way Electron's `preventDefault` does. The handler is
    JavaScript on another thread and the window manager wants a synchronous yes
    or no, so the decision has to exist before the attempt: registering a
    handler is what makes it exist, and the host then refuses every close and
    reports it. See native/core/WindowHost.h.

    Three runs, because there are three answers and a screen with more than one
    of them is a screen whose answer depends on when you look:

      refused       a second window with a handler does not close, and the app
                    re-renders knowing it was asked.

      agreed        the same interception taken all the way round -- refused,
                    reported, and closed by the app with the `close` it was
                    handed. Without this half, intercepting would be a way to
                    make a window nobody can shut.

      the app's own the case that matters most and breaks worst. An app with
                    unsaved work wants to refuse *its own* window, and a host
                    that cannot then be shut down at all is the failure. This
                    run refuses and asserts the host still exited on its own
                    timer.
    """
    app = bundle_app(bundle.parent, "windows")

    def run(component: str, closing: int, run_ms: int) -> tuple[str, str]:
        with tempfile.TemporaryDirectory() as directory:
            dump = Path(directory) / "tree.txt"
            env = dict(os.environ)
            env["BASALT_DUMP_TREE"] = str(dump)
            env["BASALT_QUIT_AFTER_MS"] = str(run_ms)
            # Closes the window the way its own close button does, which is the
            # only thing an interception ever refuses: an app closing its own
            # window is not asking anybody. See docs/TESTING.md.
            env["BASALT_TEST_CLOSE_WINDOW"] = str(closing)
            for name in ("BASALT_TEST_TAP", "BASALT_TEST_TYPE", "BASALT_TEST_HOVER",
                         "BASALT_TEST_FOCUS", "BASALT_TEST_SCROLL", "BASALT_TEST_MENU"):
                env.pop(name, None)
            result = subprocess.run(
                [str(HOST), str(app), component],
                cwd=REPO, env=env, capture_output=True, text=True,
                timeout=run_ms / 1000 + 90,
            )
            _remember_output(result.stderr)
            check_output(result.stderr, result.returncode)
            tree = dump.read_text() if dump.exists() else ""
            return tree, result.stdout + result.stderr

    tree, logged = run("BasaltWindowsGuarded", 3, 11000)
    if "the second window was asked to close, and said no" not in logged:
        raise Failure(
            "closing a guarded window told the app nothing. Refusing without "
            f"reporting is a window that cannot be closed and never says why.\n"
            f"{tail_text(logged)}"
        )
    if "--- window 3 ---" not in tree:
        raise Failure(f"a window that refused to close closed anyway:\n{tree}")
    if 'text="Really close?"' not in tree:
        raise Failure(
            "the window stayed open but the app did not re-render knowing it "
            f"had been asked, which is the whole point of being told.\n{tree}"
        )

    tree, logged = run("BasaltWindowsConfirming", 3, 11000)
    if "the second window was asked to close, and agreed" not in logged:
        raise Failure(f"the app was never asked:\n{tail_text(logged)}")
    if "--- window 3 ---" in tree:
        raise Failure(
            "the app agreed to close the window and it is still open. An "
            f"interception that cannot be lifted is a window nobody can shut.\n{tree}"
        )

    # The app's own window. Nothing here closes it in the end -- the run stops
    # on its own timer, which is the assertion: a host whose main window refuses
    # to close still shuts down when the harness says so, and a host where that
    # is not true hangs rather than fails.
    tree, logged = run("BasaltWindowsGuarded", 1, 8000)
    if "the main window was asked to close, and said no" not in logged:
        raise Failure(
            "closing the app's own window told it nothing. This is the one an "
            f"app with unsaved work most wants to refuse.\n{tail_text(logged)}"
        )
    if "--- window 1 ---" not in tree and "view tag=1" not in tree:
        raise Failure(f"the app's own window closed after refusing to:\n{tree}")


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
    ("Linking.getInitialURL answers with the URL the app was opened with",
     test_initial_url),
    ("Alert.alert shows a dialog and says which button was pressed", test_alert),
    ("Share.share reaches the platform and settles both ways", test_share),
    ("expo-notifications imports and answers on every desktop", test_notifications),
    ("ActivityIndicator, Switch, Modal and RefreshControl mount and answer",
     test_controls),
    ("the native file dialogs answer with a path, or with a cancel",
     test_file_dialogs),
    ("a window reports its own size, and the state changes that are not resizes",
     test_window),
    ("a window says how big it may be, and what this desktop can do about it",
     test_window_limits),
    ("the application menu is installed, roles and all", test_application_menu),
    ("a context menu opens where you press, and says what was chosen",
     test_context_menu),
    ("a second window is a second React tree, and the two stay in step",
     test_windows),
    ("a window can refuse to close, and say so", test_window_close_request),
    ("DevTools' overlay draws a highlight, and a trace update takes itself down",
     test_debugging_overlay),
    ("the developer menu reloads, and shows the element inspector", test_dev_menu),
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
    # Running one scenario is the common case while working on it, and running
    # all of them takes a quarter of an hour. Substring, case-insensitive,
    # repeatable -- `-k window -k menu` runs both.
    parser.add_argument(
        "-k",
        "--scenario",
        action="append",
        default=[],
        metavar="TEXT",
        help="only scenarios whose name contains TEXT; repeatable",
    )
    parser.add_argument(
        "--list",
        action="store_true",
        help="print the scenario names and exit",
    )
    arguments = parser.parse_args()

    if arguments.list:
        for name, _ in SCENARIOS:
            print(name)
        return 0

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

    wanted = SCENARIOS
    if arguments.scenario:
        needles = [text.lower() for text in arguments.scenario]
        wanted = [
            entry for entry in SCENARIOS
            if any(needle in entry[0].lower() for needle in needles)
        ]
        if not wanted:
            print(
                f"error: no scenario matches {arguments.scenario}\n"
                "       run with --list to see the names",
                file=sys.stderr,
            )
            return 1

    note = (
        "real pointer events through the X server"
        if INPUT_MODE == "real"
        else "taps injected at the dispatcher, skipping the window system"
    )
    print(f"running {len(wanted)} scenarios against {bundle.name} on {PLATFORM}")
    print(f"input: {INPUT_MODE} -- {note}")
    failed = 0
    skipped = 0
    for name, scenario in wanted:
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
        except subprocess.TimeoutExpired as expired:
            failed += 1
            print(f"  FAIL  {name}\n        the host did not exit")
            # What it had said before it stopped saying anything. Without this a
            # hang is the least informative failure there is -- `capture_output`
            # swallows the pipes, so the report was four words and nothing else,
            # which on a platform that only runs in CI is nothing to work from.
            for stream, label in ((expired.stdout, "stdout"), (expired.stderr, "stderr")):
                if not stream:
                    continue
                if isinstance(stream, bytes):
                    stream = stream.decode("utf-8", "replace")
                print(f"        --- last of the host's {label} ---")
                for line in tail_text(stream, 20).splitlines():
                    print(f"        {line}")

    total = len(wanted) - skipped
    tally = f"\n{total - failed}/{total} passed"
    if skipped:
        tally += f", {skipped} skipped"
    print(tally)
    return 0 if failed == 0 else 1


if __name__ == "__main__":
    sys.exit(main())
