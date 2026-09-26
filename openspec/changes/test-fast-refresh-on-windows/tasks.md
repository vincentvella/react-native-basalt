# Tasks

## 1. Windows can run the scenario

- [x] A Node entry point for Metro, usable from `integration_test.py` on any platform
      -- `scripts/metro.js`, resolving `RN_DIR` from an argument, the environment,
      or the same three candidate directories the shell script looked in
- [x] Keep `scripts/metro.sh` working for people; the test path stops using it
      -- it `exec`s the Node one, so there is one implementation rather than two
      that resolve `RN_DIR` differently
- [x] A way to stop what it started that works on Windows (`pkill -f "cli.js start"` does not match `metro serve`)
      -- by not starting anything: `metro.js` calls `loadConfig` and `runServer`
      in-process, which is what `metro serve` does anyway. One process, so the
      caller's `terminate()` is the whole of the cleanup. A wrapper that spawned
      the packager would have leaked it on Windows, where terminating a process
      does not terminate its children
- [x] Remove both `Skipped("scripts/metro.sh has no Windows path")` branches
- [x] Run the scenario on Windows and record the result
      -- the **developer menu** scenario runs there now and passes, and so does
      the host half of Fast Refresh. Two harness bugs had to go first, neither
      of them Windows': a bundle wait that counted `prewarm`'s own output and so
      could not fail, and `terminate()` meaning a handled SIGTERM on two hosts
      and an unhandleable `TerminateProcess` on the third. Both guarded by
      `scripts/test_harness.py`
- [x] A portable way to ask a host to quit *now*, so the edit half can be run on
      Windows by hand
      -- `BASALT_TEST_QUIT_FILE`, on all three hosts, polled at 250ms on the
      timer each already runs for `BASALT_QUIT_AFTER_MS`. The scenario's last
      assertion reads the dumped tree, every host writes that on the way out, and
      a `TerminateProcess` kill reaches neither -- so this was the thing standing
      between a Windows machine and a meaningful run. Shared spellings in
      `core/TestQuitFile.h`, tested by `native/tests/test_quit_file.cpp`, measured
      at 0.24s on GTK and AppKit
- [ ] **Run the edit half on a Windows machine** -- the one step that needs a
      Windows machine rather than CI, since CI's Metro never notices an edit.
      `python scripts\integration_test.py --platform windows -k "Fast Refresh"`

## 2. Finish the CI diagnosis

- [ ] `DEBUG=metro:*` on the CI Metro; establish whether metro-file-map chose watchman or fell back
- [ ] If volume is implicated, shrink `watchFolders` away from the React Native checkout
- [ ] Have the scenario poll Metro for the edit, separating "never saw it" from "saw it, client missed it"
- [ ] Record each result in `docs/backlog/testing.md`, whether or not it fixes anything

## 3. Turn it on

- [ ] Drop `BASALT_SKIP_FAST_REFRESH` from whichever jobs can pass
- [x] If the runner cannot be fixed, split the assertion so the host half is still guarded
      -- done ahead of section 2 rather than after it, because it does not depend
      on the answer: `BASALT_SKIP_FAST_REFRESH` now drops the edit rather than the
      scenario, so all three jobs check dev mode, the dev server helper, the
      websocket, `DevSettings`, and that the app runs Metro's bundle instead of
      the release one on disk. The scenario passes with a note naming what it did
      not check
- [ ] Note in the workflow why any remaining skip is there

## 4. Records

- [ ] Update `docs/backlog/testing.md`
- [ ] Update the README's "Next"
