# Design

## The two halves are independent

Windows and CI fail for unrelated reasons, and doing them together is what has
kept both open. They are separable: the Windows half is a missing launcher, and
the CI half is a diagnosis nobody has finished.

## Windows: Metro without a shell

`scripts/metro.sh` execs `metro serve`. The scenario needs the same thing on a
platform with no `/bin/sh`, so the launcher becomes a small Node script and the
shell script keeps working for people who already use it.

Worth noting so it is not rediscovered: `pkill -f "cli.js start"` does not match
these processes, because the script execs `metro serve` rather than the CLI.
Whatever the Windows path is, it needs its own way to stop what it started.

## CI: finish the diagnosis before changing anything

The standing theory was watchman, and it was tried and **ruled out** on the `ci`
branch: the upstream build ran, `watch-project` reported an inotify watcher over
exactly the root Metro was given, a `.watchmanconfig` was added -- and a freshly
requested bundle still carried the old text. Metro had not seen the change.

What that run did not establish is whether *metro-file-map* used watchman at
all, because the `watch-project` call was made by the workflow rather than by
Metro. That is the open question, and it is the cheapest one:

1. `DEBUG=metro:*` on the CI Metro, to see what the watcher thinks it is doing
   rather than inferring it from the bundle.
2. Shrink `watchFolders`. The React Native checkout is the bulk of eight
   thousand directories; volume would show here.
3. Have the scenario poll Metro for the edit, which separates "Metro never saw
   it" from "Metro saw it and the client missed it" without another CI round
   trip per hypothesis.

Already ruled out, and not to be repeated: `fs.watch` sees the same edit on the
same runner; inotify limits are 655360 watches and 1280 instances; both sides
run Node 24.20.0; CI's layout reproduces green in a local VM.

## If the runner cannot be fixed

Splitting the assertion is better than skipping. "Metro served the edit" and
"the running app applied it" are two claims, and only the first depends on the
runner's file watching. Asserting the second against a bundle the test itself
edited would still guard the host half, which is the half this project owns.
