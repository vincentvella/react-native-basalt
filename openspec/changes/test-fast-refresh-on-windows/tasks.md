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
      -- the **developer menu** scenario runs there now and passes. **Fast
      Refresh itself still does not run on any CI machine**, because
      `BASALT_SKIP_FAST_REFRESH` is set on all three jobs for a reason that is
      section 2's. Removing the skip is what would exercise the Windows path,
      and that is the one thing section 1 cannot finish on its own

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
