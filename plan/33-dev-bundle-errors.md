# 33 — what Metro said

The last phase went looking for crashes by importing everything an app might
import. This one is the crash that has nothing to do with imports: the app never
gets as far as running, because what Metro sent was not JavaScript.

    Compiling JS failed: 1:8:';' expected

That is the whole message, and every part of it is a lie. There is no line 1
column 8 in your code; there is a line 1 column 8 in Metro's JSON error report,
which is what got compiled. The actual error -- a missing import, a typo, a
transform that threw -- is in that report, and it is thrown away.

## Two failures that look the same and are not

`ReactHost::loadScript` tries Metro first in dev mode and falls back to the
on-disk bundle if the fetch "fails". Whether the fetch failed is decided by
`DevServerHelper`, which sets the promise's value from `onBody` and its
exception from `onResponseComplete` -- and `onBody` runs first, so any response
with a body wins. The status code is never read.

That makes two very different things indistinguishable:

- **Nothing is listening.** curl reports a connection error, there is no body,
  the promise takes the exception and the on-disk bundle loads. Verified by
  running the host against a dead port: it falls back and the app comes up. The
  note in the backlog claiming otherwise was wrong.
- **Metro is running and answered 500**, which is how it reports every error in
  your app. There is a body, so the promise takes it, and Hermes is handed a
  JSON document.

The second is the common one. Every syntax error anyone makes goes through it.

## Where to intervene

Neither `DevServerHelper` nor `ReactHost` is ours -- they are React Native's cxx
platform, and the point of this project is not to fork them. What *is* ours is
the http client: `getHttpClientFactory()` is declared by ReactCxxPlatform and
defined by each host, like the component registry and the text layout manager.
So `core/HttpClient.cpp` is where the status code can be looked at, and
`core/DevBundle.h` is the small piece of shared state it needs.

The rule is narrow on purpose: for the bundle request, and only for it, a
non-2xx status means the body is not delivered. `onResponseComplete` gets an
error instead, upstream's promise takes the exception, and nothing tries to
compile anything. Everything else keeps the behaviour it had -- an app's own
`fetch()` of a 404 is entitled to read the body, which is what the status code
is for.

"The bundle request" is a GET, to the dev server's own origin, for a path ending
in `.bundle`. The origin matters: matching on the path alone would swallow the
body of any request an app made to a URL that happened to end that way. The
tests in `core/tests/test_devbundle.cpp` pin both directions, including
`http://localhost:80811/`, which a prefix match on the host would accept.

## Falling back is the wrong kindness

Upstream's fallback still runs after the error: the on-disk bundle loads and the
app would come up. That is worse than not coming up. The developer saved a file,
saw an error they never read, and is now looking at a running app that is
executing the last bundle that built -- editing code that is not what is
running, and wondering why nothing changes.

So the hosts read `devBundleError()` after `loadScript` returns and stop. The
message they print is Metro's own, which for a transform error is the code frame
with the caret under the offending token, colours and all:

    Metro could not build the bundle (HTTP 500):
    SyntaxError: /.../js/broken.js: Unexpected token (2:10)

       1 | import {AppRegistry} from 'react-native';
    >  2 | const x = ;
         |           ^
       3 | AppRegistry.registerComponent('BasaltDemo', () => null);

Both hosts exit non-zero, so a script that starts one can tell a failed build
from a run that ended. GTK needs the status carried on the host struct and
returned from `main`: `g_application_set_exit_status` is gone from GLib.

Stopping through the normal shutdown path rather than returning from `main`
matters more than it looks. The fallback bundle has already started evaluating
on the JS thread by then, and dropping `ReactHost` without joining it aborts in
a destructor -- `mutex lock failed: Invalid argument`, exit 134, after the real
error has already been printed and while the developer is reading it.

## What this is not

It is not a red box. The error is printed and the process exits; there is no
overlay, and an app already running when a *reload* fails keeps the code it has
with the error only in the log. Both are in the backlog. What is fixed is the
part that was actively misleading: the message you get is now the message Metro
wrote.
