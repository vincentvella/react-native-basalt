// This platform's Expo modules, and the thing that was missing until now: a way
// to have one at all.
//
// `requireNativeModule('ExpoClipboard')` looks in three places, in order:
// `globalThis.expo.modules[name]`, the legacy bridge proxy, and a TurboModule
// of the same name wrapped by expo-modules-core. The third looks tempting --
// this project already has a TurboModule for the clipboard -- but the wrapper
// copies methods off `Object.getPrototypeOf(turboModule)`, and a C++ TurboModule
// is a jsi host object whose prototype is `Object.prototype`, so what it copies
// is nothing. The first is the real seam, and the one every native platform
// uses.
//
// So an Expo module here is a `expo.NativeModule` instance -- expo's own class,
// installed by ExpoRuntime, which is where `addListener` and the event
// machinery come from -- with host functions set on it. `moduleWithFunctions`
// below is the whole of the framework; everything else is one module's worth of
// behaviour, and each of them is a handful of lines because the behaviour is
// already written. `ExpoClipboard` is core/PlatformServices.h's clipboard, the
// same one React Native's own `Clipboard` module uses.
//
// **What a missing method does is part of the design.** Expo's JavaScript
// checks for each function before calling it -- `if (!ExpoClipboard.getImageAsync)
// throw new UnavailabilityError(...)` -- and reports "not available on this
// platform" naming the method. That is a better answer than a stub returning
// null, so a method this platform cannot implement is left off rather than
// faked.

#pragma once

#include <jsi/jsi.h>

#include <string>

namespace basalt {

// Adds this platform's Expo modules to `modules`, the object that becomes
// `globalThis.expo.modules`. Called from installExpoRuntime; there is no reason
// for anything else to call it.
//
// A no-op when the build was not pointed at an expo-modules-core, like
// everything else Expo here.
void installExpoModules(facebook::jsi::Runtime &runtime, facebook::jsi::Object &modules);

// The app's Expo config -- `app.json` or `app.config.js`, resolved -- which is
// what `Constants.expoConfig` is and what expo-linking reads to find the app's
// URI scheme. Without it `Constants.expoConfig` is null and an app that reads
// `Constants.expoConfig.extra.something` crashes on the property access.
//
// iOS and Android embed it in the app bundle at build time, from a script Expo
// runs during the native build. This platform's equivalent moment is bundling:
// the bundler writes `app.config.json` beside the bundle, which is why this
// takes the bundle's path rather than the config's. Nothing being there is not
// an error -- a plain React Native app has no Expo config and never asks for
// one.
//
// Call before the runtime is installed; the hosts do it as they start.
void loadExpoAppConfigBeside(const std::string &bundlePath);

} // namespace basalt
