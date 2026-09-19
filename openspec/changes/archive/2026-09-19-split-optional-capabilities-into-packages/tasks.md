# Tasks

## 1. Settle the rule

- [x] Agree the three tests, or amend them -- agreed unamended. Notifications
      was moved by them and the move held, which is the evidence they were
      going to get before the first dependant exists
- [x] Ensure every capability package reports whether it is supported here, not
      only no-ops -- the one that exists does: `notificationSupport()` answers
      with a reason, and `getPermissionsAsync` carries it to where a developer
      sees it. Written into the rule so the next package inherits it
- [x] Decide which half of spell checking this platform implements -- both, in
      core. Applying the three tests answers it the other way from the
      intuition that raised the question: fetching a dictionary asks nobody for
      consent, touches no hardware and no other application, and acts inside
      the app's own windows. What it needs is a cache-directory seam, which is
      a question about a seam and not about a package
- [x] Record the outcome in `docs/DECISIONS.md`

## 2. Generalise discovery

- [x] Define the manifest key and the CMake entry point it names
- [x] Make `optionalNativeModules` scan dependencies for it
- [x] Keep the Expo, worklets and Reanimated special cases, and say why
- [x] Fail loudly, naming the package, when a contributed build fails.
      `explainContributedFailure` adds what was in the build to the error.
      It does not claim *which* package broke it: the build output is
      streamed rather than captured, because a person watching a
      twenty-minute compile should see it happen, and buffering it to grep
      for a directory would trade that for a guess

## 3. Apply it

- [x] Move the notifications seam out of core first -- it is the only shipped
      capability the rule catches, and proving it does not break the
      expo-notifications proxy is what makes the rest safe
- [x] Finish that move: `core/portability_probe.cpp` still included
      `Notifications.h` unconditionally, so core did not compile in an app
      that had not installed the package -- which is every app, since `init`
      does not add optional capabilities. A package brings its own probe stub
      now, through `BASALT_PACKAGE_PROBE_SOURCES`, which is one more property
      of the same kind as the four it already contributes
- [x] Nothing else moves: every seam left in `native/core/` was checked
      against the three tests and none fails one. Camera, tray, location and
      the rest of the package side of the rule are catalogued, not built
- [x] Move what the rule moves, in one commit per package -- which turned out
      to be no commits. Every seam in `native/core/` was checked against the
      three tests and none fails one; the package side of the rule is
      catalogued rather than built, so notifications was the whole of it
- [x] Have `init` install what an app needs so the split is invisible to it --
      the host packages, which are not optional and which `init` was not
      adding. Capability packages deliberately stay out of that: an app gets
      one by asking, which is the whole point of the boundary, and a command
      that installed notifications into every app would have moved the seam
      without moving the dependency
- [x] Update the specs whose capability moved: `desktop-notifications` says
      it ships as its own package, and `distribution` gains the requirement
      today's bug proved was missing -- core builds with no capability
      package installed, and a package satisfies its own seam
