# 48. Fast Refresh, and the graph nobody built

Fast Refresh did not work on macOS. `scripts/integration_test.py` reported it
as *"Metro never pushed an update after the edit"*, and that was exactly true:
Metro had nothing to push it to.

## What was actually wrong

React Native's `HMRClient.setup()` registers an entry point with Metro over the
`/hot` websocket, and the URL it registers is `getDevServer().fullBundleUrl` --
which is `NativeSourceCode.getConstants().scriptURL`, verbatim. Metro resolves
that URL to a **graph**: entry file plus transform options. Not a path, a graph.

This project supplied its own `SourceCode` module on every platform, reporting a
URL synthesised by `core/SourceCodeModule.cpp`:

    http://localhost:8081/index.bundle?platform=macos&dev=true

But that is not the URL the bundle came from. `DevServerHelper` fetched:

    http://localhost:8081/index.bundle?platform=macos&dev=true&lazy=true
      &minify=false&app=basalt-macos&modulesOnly=false&runModule=true&...

Those are two different graphs. Metro had built the second and never the first,
so registration was answered with

    GraphNotFoundError: The graph `{"entryFile":".../js/index.js","options":{...
      "lazy":false,...,"platform":"macos",...}}` was not found.

and no update was ever sent. The websocket was connected, the client was set up,
the URL was well-formed, the platform was right. The graph did not exist.

## Why Linux looked fine

It was not fine. It passed because the test harness accidentally built the
missing graph: `Metro.prewarm()` fetches

    http://localhost:8081/index.bundle?platform=linux&dev=true&minify=false

which is, to Metro, the same graph the short URL resolves to. The platform in
that URL is **hardcoded** `linux`, so on a macOS run the prewarm built the Linux
graph and the macOS one stayed missing.

So the scenario was passing on Linux for a reason that had nothing to do with
the Linux host being correct, and a developer running `scripts/metro.sh` by hand
-- with no prewarm -- had the same broken Fast Refresh there.

## The fix

Serve this project's `SourceCode` module only outside dev mode, exactly as
`DevSettings` already was, and for the same reason: ReactCxxPlatform provides a
real one when a dev server exists. Its `SourceCodeModule` reports `sourceURL_`,
which `ReactHost` sets to the URL the bundle was actually downloaded from --
which is, by construction, a graph that exists.

One line per host, and the synthesised URL is still what a release build
reports, where it is right and where assets need it.

## The second half: SIGTERM

With the graph found, the scenario got far enough to fail differently: `host
exited -15`. The macOS host installed no signal handlers, so the test's
`terminate()` killed it where it stood, skipping `applicationWillTerminate` and
the teardown under it. The GTK host has had `g_unix_signal_add` since phase 32.

macOS gets the counterpart: a `dispatch_source_t` per signal, which runs on the
main queue rather than in signal context. Two things are easy to get wrong and
both look identical to "the signal was not caught":

- the signal must be `SIG_IGN`'d first, because a dispatch source *observes* a
  signal rather than replacing its disposition, and the default disposition
  still kills the process; and
- the source must be kept alive. Under ARC a `dispatch_source_t` is an object
  like any other, and letting it go out of scope releases it before it can fire.

## What this cost to find

Two false trails, both worth recording because both looked conclusive.

A `sed` used to shorten one host's log -- and not the other's -- stripped
`localhost:8081`, which read as the macOS host registering `http:///` with an
empty host. It was not. Both hosts always registered well-formed URLs.

Then `js/metro.config.js`, instrumented to log every rewritten URL, slowed Metro
enough that the macOS host held no HMR websocket at all. That produced a clean,
stable, reproducible measurement -- `established=0` at 2, 4, 6, 9, 12, 16 and 20
seconds -- of something that was entirely an artifact of the instrument.

A TCP proxy was no better: it closed every connection at ~2.4s, which is when
HMR connects, so both hosts looked identically broken.

What finally worked was neither logging nor proxying but asking Metro directly:
a twenty-line websocket client that registers one entry point and prints what
comes back. `GraphNotFoundError` was the first honest answer in the whole
investigation.
