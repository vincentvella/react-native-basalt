# Fast Refresh, guarded on every desktop

## Why

Fast Refresh works on all three desktops and is tested on none of them by any
machine that is not somebody's laptop.

Two separate things stand in the way, and they are usually discussed as one:

- **On Windows the scenario cannot run at all.** `integration_test.py` skips it
  because `scripts/metro.sh` is a shell script with no Windows path. So the
  desktop that most recently gained development mode is the one with no way to
  exercise it.
- **In CI it is switched off everywhere.** Both jobs set
  `BASALT_SKIP_FAST_REFRESH`, because Metro on a GitHub runner never notices an
  edit: the file changes on disk with a fresh mtime, a freshly requested bundle
  still carries the old text, and Metro logs nothing.

The result is a working feature with nothing protecting it. It passes on macOS
and on Linux in a VM, so what is unguarded is the whole of development mode
against any change anyone makes.

## What Changes

- The Fast Refresh scenario runs on Windows, by starting Metro through something
  that is not a shell script.
- The reason CI cannot see an edit is established rather than theorised, and the
  scenario is turned on wherever it can be made to pass.
- If the runner turns out to be immovable, the scenario asserts what it *can* --
  that Metro served the edit -- rather than staying off entirely, so a
  regression in the host half is still caught.

## Capabilities

### Modified Capabilities
- `developer-tools`

## Impact

- A Node entry point for Metro, replacing `scripts/metro.sh` in the test path;
  the shell script can stay for people.
- `scripts/integration_test.py` loses the two `Skipped("scripts/metro.sh has no
  Windows path")` branches.
- `.github/workflows/ci.yml` loses `BASALT_SKIP_FAST_REFRESH` on whichever jobs
  can be made to pass.
- `docs/backlog/testing.md` carries the investigation so far, including what has
  already been ruled out -- watchman, inotify limits, Node version, the CI
  layout -- and the next three things to try. That list is the starting point
  and should not be re-run from the top.
