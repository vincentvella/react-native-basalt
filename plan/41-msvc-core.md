# Phase 41 — React Native's C++ core, on MSVC

> **Done, 2026-09-12.** `basalt_core_probe.exe` links and runs on Windows.
> React Native's C++ core, ReactCxxPlatform, folly, Yoga, Hermes and this
> project's shared half all compile, link and execute under clang-cl.

`plan/backlog.md` called this "the real unknown in the Windows port", on the
grounds that folly, glog, boost and ReactCommon all build on Windows for
react-native-windows, so it is known-possible rather than speculative. That was
the right framing. What it did not anticipate is that the *prebuilt* Hermes
everyone else on Windows consumes cannot be used here at all, and that building
it from source -- which looked like the hard path -- needs three small fixes.
The sections below are in the order they were found, including the one that
concluded the prebuilt was the answer, because being wrong in a legible order is
what these notes are for.

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

## Hermes: the prebuilt does not exist, so the source build it is

The first instinct was that Windows should consume a prebuilt Hermes, because
`supported-versions.json` says Windows is one of the four platforms Meta
publishes one for and that building from source is the Linux exception. That is
true in general and false for this React Native, and the measurement is worth
keeping.

`Microsoft.JavaScript.Hermes` is the package react-native-windows consumes,
built from microsoft/hermes-windows -- a fork, not facebook/hermes. Its stable
is 0.1.27, from August 2024; it also publishes dated prereleases, the most
recent being 0.0.0-2608.24001, three weeks old. Recent enough to be worth
checking, so it was checked.

It ships its own `jsi/` headers, and that is the compatibility test, because
this project deliberately uses React Native's jsi rather than Hermes' vendored
copy -- `-DJSI_DIR` points the Hermes build at React Native's sources, so the
jsi symbols the whole application resolves against come out of libhermesvm.

    IRuntime in the NuGet's jsi.h:   0
    IRuntime in React Native 0.87's: 186

React Native renamed `jsi::Runtime` to `jsi::IRuntime` after that fork last
synced. Every JSI symbol mangles differently, so the package cannot link against
0.87 at all -- not a version-skew risk to be careful about, an impossibility.

The reason is structural rather than bad luck, and it is this project's own
thesis arriving as a build failure: **react-native-windows 0.84.0 targets React
Native 0.84.1, react-native-macos is at 0.81.9, and this is built against
0.87.1.** Being ahead of both forks is the thing the README claims this
architecture can offer. The cost of being ahead is that nobody has built the
prebuilt yet.

So Windows has to build Hermes from source after all, for the same reason Linux
does, and the entry in `supported-versions.json` explaining why Linux is special
needs a second sentence.

## What the source build actually needs

Three things, none of them large, and one of them an upstream bug worth
reporting.

**`HERMES_ALLOW_BOOST_CONTEXT=0`.** Hermes' vendored boost::context throws
`std::bad_alloc` from its *Windows* stack allocator; the POSIX one does not,
which is why this is a Windows-only failure, and Hermes compiles with exceptions
off. This is Hermes' own option for exactly that -- the one its ASAN and
Emscripten builds use -- and the whole path is behind
`#if HERMES_USE_BOOST_CONTEXT`, so it is a clean fallback rather than a hole.

**`HERMES_EMPTY_BASES` on `VM::Environment`.** Static Hermes asserts that the
C++ `Environment` and the C `SHEnvironment` are layout-identical. `sizeof`
matches and the offsets do not, because `Environment` multiply-inherits from
`VariableSizeRuntimeCell` and an *empty* base, `llvh::TrailingObjects`, and the
MSVC ABI does not collapse empty bases the way the Itanium ABI does.

The fix is one word, and Hermes already has it. `Support/Compiler.h` defines

    #define HERMES_EMPTY_BASES __declspec(empty_bases)

with the comment "Force MSVC to enable empty base class optimization; this is
necessary for PointerBase alignment requirements in some cases when using
HERMESVM_CONTIGUOUS_HEAP" -- which is the mode this builds in. The macro is
defined and applied to nothing at all in the entire tree. It is dead code that
was presumably attached to something once. Applying it to `Environment` clears
the assertion.

**Exceptions, for React Native's jsi.** This one is the most interesting of the
three, because the mechanism is a POSIX assumption hiding inside a difference
between two compilers' defaults.

`jsi.cpp` throws `JSError`. Inside the Hermes build it is compiled with Hermes'
flags, and `cmake/modules/Hermes.cmake` does two things on MSVC: it strips
`/EHsc` out of `CMAKE_CXX_FLAGS` wholesale --

    string(REPLACE "/EHsc" "" CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS}")

-- to avoid a D9025 warning, and then puts `/EHsc` or `/EHs-c-` back on each of
*its own* targets through `hermes_update_compile_flags()`, according to
`HERMES_ENABLE_EH`.

React Native's `jsi` is not one of its own targets. It arrives through
`JSI_DIR`, never goes through that helper, and so ends up with **no `/EH` flag
at all** -- CMake's default having been removed on its behalf. On Linux that is
harmless, because clang with no flag defaults exceptions *on*. clang-cl with no
flag defaults them *off*. The strip is safe on every platform Hermes is tested
on and fatal on this one.

`HERMES_ENABLE_EH=ON` is the supported knob and covers Hermes' own targets, and
it has to be `HERMES_ENABLE_RTTI=ON` alongside it -- `API/hermes/CMakeLists.txt`
refuses the mixed case outright with "Currently only support having exceptions
and RTTI having the same enable status". Worth knowing because the failure
arrives as a configure error about ICU further down the log, which is
collateral and sends you looking in the wrong place.

For the jsi target the flag has to come from `CMAKE_CXX_FLAGS` instead, which
means spelling it as `/EHs /EHc` -- exactly equivalent, and not the literal
string their `REPLACE` looks for.

One trap while getting there, recorded because it cost a full rebuild:
`-DCMAKE_CXX_FLAGS=...` **replaces** CMake's platform defaults rather than
adding to them, and those defaults are where `/DWIN32 /D_WINDOWS` come from.
Hermes' `OSCompat.h` keys its `<unistd.h>` include on `_WINDOWS`, so overriding
the variable makes a Windows build start looking for POSIX headers. Pass the
defaults back with it.

## Which compiler, and why it is clang-cl

The two front ends fail differently on Hermes, and the difference decided the
toolchain for the whole core build:

- **cl** stops at 260 of 415 on `__builtin_expect` in Static Hermes'
  `static_h.h` -- a GCC and Clang builtin MSVC does not have, and not something
  a flag can supply.
- **clang-cl** has that builtin and gets past it, which is what makes the three
  fixes above sufficient rather than the first of a long list.

## Where it gets to

    core links.
      modules           PlatformConstants, SourceCode, StatusBarManager, Appearance,
                        Clipboard, AlertManager, LinkingManager, I18nManager,
                        AccessibilityInfo
      scriptURLFor      file://C:\Users\vince\Workspace\react-native-basalt\bundle.js
      expo runtime      not compiled in
      worklets          not compiled in
      font seam         isFontRegistered=0 generation=0
      colour scheme     light
      services seam     canOpenUrl=0 clipboard=""

That is `core/portability_probe.cpp` running on Windows. Phase 17 built it so
that "the shared half needs no view layer" would be a build failure rather than
a paragraph; here it does a second job, which is to say that the core *links*
and *runs*, not merely that it compiles.

The last four link errors before it did were all consequences of static archives
not behaving like shared libraries, and none of them is visible on a platform
with `.so` files:

- **jsi has to be named separately.** `libhermesvm.so` absorbs the jsi it was
  built with; `hermesvm_a.lib` does not.
- **The static archive, not the import library.** Hermes' `hermesvm` target
  produces a DLL, and a Windows DLL exports nothing without
  `__declspec(dllexport)`, which Hermes does not annotate. `hermesvm.lib` is a
  one-kilobyte import library for an empty DLL; the engine is the
  thirty-megabyte `hermesvm_a.lib` beside it. ELF exporting everything by
  default is the only reason `find_library(hermesvm)` finds the right file
  anywhere else.
- **`icu.lib`.** Hermes reports "Using Windows 10 built-in ICU" and calls
  `u_strToUpper` and `ucol_strcoll` out of the operating system. On Linux that
  dependency would have been recorded inside the shared object.
- **`winmm` and `Boost::thread`**, for folly: `timeBeginPeriod`, and the
  thread-local storage its Windows `PThread.cpp` shim is written over.

## One thing only a fresh build tree finds

`CMAKE_BUILD_TYPE` is optional on Linux and macOS and mandatory here. Left
unset, MSVC selects the *debug* C runtime -- and the vcpkg toolchain follows it
to the debug packages -- while Hermes was built Release. Linking the two is
refused:

    lld-link: error: /failifmismatch: mismatch detected for '_ITERATOR_DEBUG_LEVEL':
    >>> basalt_core.lib(SourceCodeModule.cpp.obj) has value 2
    >>> hermesvm_a.lib(CDPAgent.cpp.obj)          has value 0

which names neither the build type nor the decision that caused it. Every build
in this phase passed `-DCMAKE_BUILD_TYPE=RelWithDebInfo` by habit, so it took
running the line `bootstrap.sh` prints, in an empty directory, to find that the
line was wrong. Worth remembering as an argument for the script printing a
command and someone actually running it, rather than the two drifting.

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
