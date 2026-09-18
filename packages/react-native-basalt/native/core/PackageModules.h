// How a capability that does not live in core reaches the runtime.
//
// The host is one binary. There is no dynamic native module loading here, so a
// capability shipped as its own package is still *compiled in* -- which means
// the question is not how it loads but how core learns it exists without
// naming it.
//
// It does not. CMake writes the answer. When the build discovers a package that
// declares native code, it generates a translation unit defining the function
// below to call that package's installer; when it discovers none, it generates
// one with an empty body. Core calls it either way and never mentions a
// package.
//
// Generated rather than registered at static initialisation, which is the
// obvious alternative and the wrong one here: a registrar in a static library
// is only linked in if something already references its translation unit, so
// the capability would vanish from a release build and stay in a debug one.
// That failure is silent and platform-specific, which is the worst shape a
// build problem can have.

#pragma once

#include <jsi/jsi.h>

namespace basalt {

// Sets each discovered package's Expo modules on `modules`, which is
// `globalThis.expo.modules`. Defined by the generated translation unit; see
// cmake/BasaltPackages.cmake.
void installPackageExpoModules(facebook::jsi::Runtime &runtime, facebook::jsi::Object &modules);

} // namespace basalt
