#!/usr/bin/env python3
"""Checks on the end-to-end harness itself, for the failures that point at green.

A bug in a test harness is not like a bug in the code under test. If it makes a
wait return early or a count match something it should not, every scenario using
it passes while checking less than its name says, and nothing in the output
looks wrong. That is the class of bug here.

Both checks below are regressions:

- `wait_for_log` counted from the start of the file, and the Fast Refresh
  scenario used it to wait for Metro to serve the running app a bundle. But
  `Metro.prewarm` is itself a request and Metro logs two BUNDLE lines for it, so
  the count was already satisfied before the host was started. The wait returned
  in about a hundredth of a second, on every platform, for as long as it had
  existed. What hid it is that the edit assertion after it carried the scenario
  on the machines where Metro's file watching works.

- `stop_host` replaced a bare `terminate()`. GTK and AppKit handle SIGTERM and
  exit 0; Windows has no SIGTERM, so `terminate()` is `TerminateProcess` and the
  exit code is always 1. Asserting on it reported our own kill as the app
  crashing, the first time that scenario ran on Windows.

Usage:  scripts/test_harness.py
"""

import importlib.util
import pathlib
import subprocess
import sys
import tempfile

HARNESS = pathlib.Path(__file__).with_name("integration_test.py")


def load():
    spec = importlib.util.spec_from_file_location("integration_test", HARNESS)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def main() -> int:
    harness = load()
    failures = []

    def check(label, got, want):
        if got != want:
            failures.append(f"{label}: got {got!r}, wanted {want!r}")

    with tempfile.TemporaryDirectory() as directory:
        log = pathlib.Path(directory) / "metro.log"

        # What prewarm leaves behind: Metro logs a progress line and a final one.
        log.write_text(" BUNDLE  js/index.js 0% (0/1)\n BUNDLE  js/index.js\n")
        served = log.stat().st_size

        # The bug, kept as a check so the distinction cannot quietly go away: read
        # from the start and the needle is already there.
        check("counting from the start still sees prewarm",
              harness.wait_for_log(log, "BUNDLE", 1, timeout=1), True)

        # The fix. Nothing has happened since `served`, so this must not match.
        check("an offset ignores what prewarm wrote",
              harness.wait_for_log(log, "BUNDLE", 1, timeout=1, start=served), False)

        # And it must still see what happens after it.
        log.write_text(log.read_text() + " BUNDLE  js/index.js\n")
        check("an offset sees the host's own request",
              harness.wait_for_log(log, "BUNDLE", 1, timeout=1, start=served), True)

        # Counts past the offset, not in total: two more lines, asked for two.
        log.write_text(log.read_text() + " BUNDLE  js/index.js\n")
        check("counts are relative to the offset",
              harness.wait_for_log(log, "BUNDLE", 2, timeout=1, start=served), True)
        check("and do not borrow from before it",
              harness.wait_for_log(log, "BUNDLE", 3, timeout=1, start=served), False)

        # A log that does not exist yet is a wait, not a crash: the host writes
        # it, and the harness starts watching before the host has opened it.
        missing = pathlib.Path(directory) / "not-yet.log"
        check("a missing log times out rather than raising",
              harness.wait_for_log(missing, "anything", 1, timeout=1), False)

        # An offset past the end of the file reads nothing rather than raising.
        check("an offset past the end reads nothing",
              harness.wait_for_log(log, "BUNDLE", 1, timeout=1,
                                   start=log.stat().st_size + 4096), False)

        # Undecodable bytes: Metro's progress lines carry control characters, and
        # a host can die mid-write. The wait must survive its own input.
        raw = pathlib.Path(directory) / "raw.log"
        raw.write_bytes(b"\xff\xfe BUNDLE  js/index.js\n")
        check("invalid utf-8 does not stop the search",
              harness.wait_for_log(raw, "BUNDLE", 1, timeout=1), True)

    # stop_host against a real process that ignores nothing: it must end it, and
    # must say whether the exit code it leaves behind is the app's or the kill's.
    sleeper = subprocess.Popen(
        [sys.executable, "-c", "import time; time.sleep(120)"],
        stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL,
    )
    meaningful = harness.stop_host(sleeper)
    check("stop_host ends the process", sleeper.poll() is not None, True)
    check("stop_host trusts the exit code everywhere but Windows",
          meaningful, harness.PLATFORM != "windows")
    # The claim the Windows branch rests on, asserted rather than assumed: a
    # terminate() there is a kill with its own exit code, and on POSIX it is a
    # signal the process can act on. If a future Python changed either, this is
    # where it would be noticed rather than in a scenario.
    if harness.PLATFORM == "windows":
        check("a killed process on Windows exits nonzero", sleeper.returncode != 0, True)
    else:
        check("a signalled process on POSIX reports the signal",
              sleeper.returncode < 0, True)

    if failures:
        print("harness checks failed:", file=sys.stderr)
        for failure in failures:
            print(f"  {failure}", file=sys.stderr)
        return 1
    print("all harness checks passed")
    return 0


if __name__ == "__main__":
    sys.exit(main())
