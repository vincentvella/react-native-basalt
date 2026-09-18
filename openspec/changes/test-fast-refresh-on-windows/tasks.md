# Tasks

## 1. Windows can run the scenario

- [ ] A Node entry point for Metro, usable from `integration_test.py` on any platform
- [ ] Keep `scripts/metro.sh` working for people; the test path stops using it
- [ ] A way to stop what it started that works on Windows (`pkill -f "cli.js start"` does not match `metro serve`)
- [ ] Remove both `Skipped("scripts/metro.sh has no Windows path")` branches
- [ ] Run the scenario on Windows and record the result

## 2. Finish the CI diagnosis

- [ ] `DEBUG=metro:*` on the CI Metro; establish whether metro-file-map chose watchman or fell back
- [ ] If volume is implicated, shrink `watchFolders` away from the React Native checkout
- [ ] Have the scenario poll Metro for the edit, separating "never saw it" from "saw it, client missed it"
- [ ] Record each result in `docs/backlog/testing.md`, whether or not it fixes anything

## 3. Turn it on

- [ ] Drop `BASALT_SKIP_FAST_REFRESH` from whichever jobs can pass
- [ ] If the runner cannot be fixed, split the assertion so the host half is still guarded
- [ ] Note in the workflow why any remaining skip is there

## 4. Records

- [ ] Update `docs/backlog/testing.md`
- [ ] Update the README's "Next"
