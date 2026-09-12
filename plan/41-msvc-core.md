# Phase 41 — React Native's C++ core, on MSVC

> **Started, 2026-09-12.** `basalt_core.lib` builds on Windows: 442 objects,
> every translation unit in ReactCommon, ReactCxxPlatform, folly and Yoga.
> Nothing links yet, because Hermes does not build from source here and should
> not have to.

`plan/backlog.md` called this "the real unknown in the Windows port", on the
grounds that folly, glog, boost and ReactCommon all build on Windows for
react-native-windows, so it is known-possible rather than speculative. That was
the right framing and the estimate was about right: five distinct problems, four
of them cleared in an afternoon, and the fifth turning out to be a question
about packaging rather than about compiling.

## Where the dependencies come from

On Linux they come from apt and on macOS from Homebrew, which is why
`ThirdParty.cmake` reaches for them with `find_library`. Windows has no system
to install them into, and vcpkg is the equivalent that everyone else uses.

Eight packages: glog, fmt, double-conversion, boost-regex, boost-beast,
boost-asio, boost-thread, OpenSSL and curl. It builds them from source and takes
about forty minutes the first time.

The Windows branch uses `find_package(CONFIG)` rather than `find_library`, and
that is a better answer here rather than merely a different one: a config
package carries its include directories, its transitive dependencies and its
debug/release split, and a bare `find_library(boost_regex)` would not even match
the name vcpkg gives it -- `boost_regex-vc143-mt-x64-1_92.lib`.

`boost-thread` is not in React Native's dependency list and is needed anyway,
because folly's Windows `PThread.cpp` -- the shim that provides pthreads on a
platform that has none -- is implemented over `boost::thread`.

## folly

Compiles, 51 translation units, once two things are true.

The three `FOLLY_HAVE_*` defines React Native passes are **claims about the
platform**, not requests: `FOLLY_HAVE_RECVMMSG`, `FOLLY_HAVE_PTHREAD` and
`FOLLY_HAVE_XSI_STRERROR_R` tell folly the system provides those, and on Windows
it does not. Leaving them defined does not fail the configure or the compile; it
fails at the link, thirty symbols at a time. `FOLLY_HAVE_CLOCK_GETTIME` is the
interesting exception and stays defined, because folly's own portability layer
*provides* clock_gettime and the flag is how it is told to use it.

And seventeen of folly's portability sources have to be built. This was the
pleasant surprise: nineteen of folly's twenty-one `portability/*.cpp` already
carry `_WIN32` branches. React Native's source list omits them because React
Native does not build folly on Windows -- it consumes a prebuilt one -- so
nothing here is a shim this project wrote.

## React Native's own CMake flags

`ReactCommon/cmake-utils/react-native-flags.cmake` puts this on every target it
touches:

    -Wall -Werror -fexceptions -frtti -std=c++20

and carries `TODO T228344694 improve this so that it works for all platforms`
directly beneath. That TODO is this phase.

Three of the five are clang command-line spellings MSVC's front end does not
have. clang-cl ignores them with a warning -- which `-Werror` promotes to an
error, so the build fails on the flags rather than on any code. And `-Wall` is
worse than ignored: to clang-cl the token `-Wall` *is* MSVC's `/Wall`, which it
maps to clang's `-Weverything`. Not "the usual warnings" but every warning clang
has, `-Wc++98-compat` included, which fires on every nested namespace and
defaulted constructor in a C++20 codebase. React Native's `EventBeat.h` alone
produces about forty errors from that.

**Counteracting the flags was tried first and cannot work.** `-Wno-everything`
does undo `-Wall`, and it moved the failure count from 73 to 72 -- because React
Native also applies `-Wpedantic`, and applies it `INTERFACE` on `callinvoker`
and `react_cxxstableapi`. An INTERFACE option arrives from the link graph, after
every PRIVATE option on the target, and re-enables
`-Wlanguage-extension-token` on top of whatever came before. There is no flag
that reliably lands last, so there is no counteracting flag.

Removing them is order-independent and is what works. Not by patching the
checkout -- nothing here modifies React Native, which is the property that lets
this platform track it -- but by walking every target in the build graph after
`add_subdirectory` and stripping the five from both `COMPILE_OPTIONS` and
`INTERFACE_COMPILE_OPTIONS`. The same after-the-fact surgery
`ReactNativeCore.cmake` already does to drop the stub `TextLayoutManager`.

A list was tried before a walk, and covered ReactCommon but not
ReactCxxPlatform: seventy-three translation units failed later on the same
`-Weverything`, arriving by a route the list did not know about. The walk needs
no updating when upstream adds a directory.

Nothing is lost by the removal. `/std:c++20` comes from CMake's `CXX_STANDARD`,
and exceptions and RTTI are re-added in MSVC's spelling. What *is* lost is
`-Werror` over React Native's own code, so upstream warnings are not enforced on
Windows and Linux stays the build that guards that. That is a real gap and the
backlog says so.

## One more upstream portability bug

`react/renderer/components/view/conversions.h` uses `M_PI` seven times, to
convert degrees for `transform: rotate`. `M_PI` is a POSIX extension rather than
standard C++, and MSVC's `<cmath>` defines it only behind `_USE_MATH_DEFINES`.

Exactly the same shape as the missing `<cstdint>` in `HttpUtils.h` that this
build already force-includes around, and the same kind of fix: a define on the
command line rather than an edit in their tree. Both are worth reporting
together; the backlog already carries the first.

## Hermes, which is the part that is not solved

It does not build from source on Windows, and the interesting finding is that it
probably should not have to.

Two compilers, two different failures, and both are real:

- **cl** stops at 260 of 415 on `__builtin_expect` in Static Hermes'
  `static_h.h` -- a GCC and Clang builtin MSVC does not have.
- **clang-cl** gets further and stops on static assertions in `VM/Callable.h`
  requiring `hermes::vm::Environment` and the C struct `SHEnvironment` to have
  identical field offsets. `sizeof` matches and the offsets do not: the MSVC ABI
  lays out that base-class-plus-trailing-objects combination differently. No
  build flag fixes a struct layout.

One thing along the way *was* a supported knob rather than a wall, and is worth
recording because it looked like a wall: clang-cl also failed on
`StackExecutor.cpp`, where Hermes' vendored boost::context throws
`std::bad_alloc` from its Windows stack allocator while Hermes compiles with
exceptions off. The POSIX allocator does not throw, which is why this is a
Windows-only failure. `HERMES_ALLOW_BOOST_CONTEXT=0` is Hermes' own option for
it -- the one its ASAN and Emscripten builds use -- and the whole path is behind
`#if HERMES_USE_BOOST_CONTEXT`, so disabling it is a clean fallback rather than
a hole.

The answer is in this repository already, in `supported-versions.json`:

> Hermes is pinned here rather than read from React Native because this platform
> builds it from source. Windows, macOS, iOS and Android all consume a prebuilt
> Hermes; Meta publishes one for each of them and none for Linux. **That single
> difference is where every version problem here comes from.**

Building Hermes from source is a *Linux* necessity. Windows is one of the four
platforms that has a prebuilt, and react-native-windows consumes one rather than
compiling facebook/hermes with MSVC. So the next step is not to make this build
work; it is to consume a prebuilt Hermes on Windows, and the pinned-triple
machinery in `supported-versions.json` is already the right shape to say which
one.

## Where it gets to

    442 objects, and these libraries:
      basalt_core.lib
      folly_runtime.lib
      yogacore.lib
      ...and ten more

Every translation unit in ReactCommon, ReactCxxPlatform, folly, Yoga and this
project's own shared half compiles under clang-cl. The one thing that fails is
the link of `basalt_core_probe`, on `makeHermesRuntime` and about forty JSI
symbols -- which is precisely and only the missing Hermes.

That probe is the milestone to aim at next, and it is worth remembering what it
is for: `core/portability_probe.cpp` links the core and no toolkit, so that the
claim "the shared half needs no view layer" is a build failure rather than a
paragraph. On Windows it will additionally be the thing that says the core
*links* here, not merely that it compiles.

## Toolchain notes

The core build needs **clang-cl**, not cl -- cl does not understand `-Werror` or
clang's `-Wall`, and warns rather than erroring on the flags that get stripped
above, which is survivable but noisier. The Windows *view layer* builds fine
with either, and CI uses cl for it.

Node had to be replaced. The machine had 18.19.1 and React Native 0.87 requires
`^22.13 || ^24.3 || >= 26`; `bootstrap.sh` checks exactly this and refuses.
Node 24.21.0 is installed portably rather than over the system one.

`bootstrap.sh` has not been taught any of this yet. Every step above was run by
hand, which is the right order -- the script should encode what worked rather
than what was guessed -- but it means there is currently no single command that
reproduces this, and that is the first thing to fix.
