# Phase 31 — `Blob`, `File` and `FileReader`

> **Done, 2026-09-11.** Eleven checks pass on both desktops. A stock Expo app's
> missing-module list is down from eight to six, and everything left on it is
> something a desktop does not need.

`BlobModule` was the last entry on phase 29's list that a real app would miss.
It backs `Blob`, `File`, `FileReader` and `URL.createObjectURL` -- the web APIs
React Native implements on top of a native byte store, so that a 20MB file a
user picked never enters the JavaScript heap.

## All of it is portable

A blob is a byte array with a name, on every platform. `core/BlobRegistry.cpp`
is the store, `core/BlobModule.cpp` is the JavaScript-facing half, and neither
is behind a seam because there is nothing here an operating system could have an
opinion about.

That makes this the second module in a row -- after `Appearance` -- where the
work was almost entirely in core and both desktops got it at once. Which is the
shape phase 17 predicted and the reason to keep checking: a module that turns
out *not* to be portable is the interesting one, and this was not it.

Two details worth keeping.

**A missing blob is absent, not empty.** `blobBytes` returns an optional, and
`""` is a legal blob. An app that read a released blob as an empty string would
get a silent wrong answer instead of a loud one, and there is a check for it in
`js/blob.js` as well as a unit test.

**A slice past the end is clamped, not rejected.** JavaScript computes offsets
from sizes it was told, and a rounding disagreement should produce a short read
rather than an exception three layers from the cause.

## What `js/blob.js` proves

Eleven checks, on both hosts: blobs from strings, from several parts, from other
blobs; slicing; `readAsText` of a blob and of a slice; `readAsDataURL`; base64
padding at all three tail lengths; `File`'s name and type; `URL.createObjectURL`;
and that a released blob *rejects* rather than reading as empty.

The base64 tail is the one worth having. Lengths 1, 2 and 3 take all three paths
through the end of that algorithm, and it is always wrong there if it is wrong
anywhere.

It also caught something about the harness rather than the code: the first run
reported ten passes with no names, because the bundler minifies `Function.name`
away. The checks now carry their names as data, which would have mattered had
any of them failed.

## The upstream bug, measured rather than assumed

A `Blob` cannot be used as a request body, and the reason is upstream.

`convertRequestBody` sends `{blob: blob.data}`, where `blob.data` is
`{blobId, offset, size}` -- an object. ReactCxxPlatform's `Bridging<http::Body>`
reads that field as `std::optional<std::string>`. So the request never reaches
any platform's http client:

```
probe: blob upload threw Value is an object, expected a String
```

Identically on both desktops, which is how it was confirmed rather than
reasoned about. `js/blob.js` keeps that probe, outside the pass count and
labelled as expected to fail, so the day it starts working somebody notices.

`core/HttpClient.cpp` now resolves a blob body through the registry anyway,
treating `body.blob` as an id -- which is what the type says it is, and which
becomes correct the moment upstream fixes the type rather than needing to be
written then. It also closes the "only string request bodies are supported"
warning for the case that mattered.

The fix upstream is either a structured `http::Body::blob` or bridging that
resolves the handle. Worth reporting alongside the other ReactCxxPlatform
findings in `plan/backlog.md`.

`responseType: 'blob'` is missing for a separate reason: the cxx
`NetworkingModule` does not mention blobs at all, so there is no response path
to hook. `addNetworkingHandler` is therefore a deliberate no-op -- silent rather
than warning, because every app calls it once at startup and a warning nobody
can act on is noise.

## Where the Expo app stands now

```
DeviceEventManager  EXDevLauncher  ExpoGo
ExpoUpdates  NativeUnimoduleProxy  SoundManager
```

Six, and every one of them is something a desktop does not need: a hardware back
button, UI click sounds, Expo's superseded legacy bridge, and three modules
about running inside Expo's own clients. There is nothing left on that list to
implement -- which is a different and better state than "five more to go".

## What is missing

**Blob request bodies**, above, which is upstream's to fix.

**`responseType: 'blob'`**, which needs a seam ReactCxxPlatform's networking
module does not have.

**Binary websocket frames.** `sendOverSocket` needs the socket, and
`WebSocketModule` is upstream's. That one warns rather than vanishing, because
an app calls it deliberately.

**`readAsText` only understands UTF-8**, and rejects anything else by name
rather than guessing.

**`readAsArrayBuffer` does not exist** -- and neither does it in React Native's
own JavaScript, which has no `FileReader.readAsArrayBuffer` at all. Worth
knowing before somebody looks for the native half of it.

**Nothing evicts.** A blob lives until JavaScript releases it, which is the
contract, but a leak in an app is a leak in the process with nothing to notice
it. `blobCount()` exists for a future check.
