# react-native-basalt — everything that is not about a particular toolkit.
#
# The bedrock the platform packages stand on: React Native's C++ core, Hermes,
# the Expo runtime, the core TurboModules, networking, and the Fabric mutation
# walk. No GTK, no AppKit, no window. react-native-basalt-gtk and
# react-native-basalt-appkit each add a view layer on top of this and nothing
# else.
#
# Configured on its own it builds `basalt_core` and the probe that proves the
# claim. A platform package adds it as a subdirectory; see BASALT_CORE_DIR in
# either of theirs.

cmake_minimum_required(VERSION 3.13)
project(basalt_core CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

# Executables at the top of the build tree whether this is configured directly
# or added by a wrapper, so `build/basalt_gtk` means the same thing either way.
# Every script and document here names that path.
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR})

# Where folly, fast_float, nlohmann, Hermes and the codegen output live.
#
# Not inside this directory. In a development checkout they sit at the root of
# the repository, and when an app builds a host they belong to that app, not to
# a copy of this package inside its node_modules -- npm would delete them on the
# next install, and rebuilding Hermes is not a thing to lose by accident.
# bootstrap.sh takes the same path and fills it.
if(NOT DEFINED BASALT_THIRD_PARTY)
  set(BASALT_THIRD_PARTY ${CMAKE_CURRENT_SOURCE_DIR}/third_party)
endif()
get_filename_component(BASALT_THIRD_PARTY "${BASALT_THIRD_PARTY}" ABSOLUTE)

# The test runner, which knows about no toolkit. Each platform package compiles
# it alongside its own tests and brings its own `main`; see docs/TESTING.md.
set(BASALT_TEST_HARNESS ${CMAKE_CURRENT_SOURCE_DIR}/tests/TestHarness.cpp
    CACHE INTERNAL "the toolkit-free test runner")
set(BASALT_TEST_INCLUDE ${CMAKE_CURRENT_SOURCE_DIR}/tests
    CACHE INTERNAL "where TestHarness.h lives")

# Tests for core's own behaviour, compiled into every platform's suite.
#
# They belong to neither platform, so asserting them in one would leave the
# other unchecked and asserting them in both would be the same file twice. The
# seam they sit on top of -- what the system is actually set to -- is never
# asked about, because it differs by machine.
set(BASALT_CORE_TESTS
    ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_appearance.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_blobs.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_devbundle.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_gestures.cpp
    CACHE INTERNAL "core's own tests")

# ---------------------------------------------------------------------------
# React Native's C++ core, and the half of this project that is not a toolkit.
#
# Everything from here to the end of the block is platform-independent, which is
# why it comes before either view layer rather than inside one. Needs React
# Native's C++ core and its third-party dependencies (folly, glog, boost, fmt,
# double-conversion). Enable with:
#
#   cmake -B build -DRN_DIR=/path/to/react-native/packages/react-native
#
# See README.md for the current state of these targets.
# ---------------------------------------------------------------------------

if(DEFINED RN_DIR)
  # RN's own CMakeLists include files by ${REACT_COMMON_DIR}/..., which a
  # relative path resolves against the wrong directory. Anchor it to this repo.
  get_filename_component(RN_DIR "${RN_DIR}" ABSOLUTE BASE_DIR "${CMAKE_SOURCE_DIR}")
  if(NOT DEFINED FOLLY_DIR)
    set(FOLLY_DIR ${BASALT_THIRD_PARTY}/folly)
  endif()
  if(NOT DEFINED FAST_FLOAT_DIR)
    set(FAST_FLOAT_DIR ${BASALT_THIRD_PARTY}/fast_float)
  endif()
  if(NOT DEFINED NLOHMANN_JSON_DIR)
    set(NLOHMANN_JSON_DIR ${BASALT_THIRD_PARTY}/nlohmann_json)
  endif()
  if(NOT DEFINED CODEGEN_DIR)
    set(CODEGEN_DIR ${BASALT_THIRD_PARTY}/codegen)
  endif()
  if(NOT DEFINED HERMES_DIR)
    set(HERMES_DIR ${BASALT_THIRD_PARTY}/hermes)
  endif()
  if(NOT DEFINED HERMES_BUILD_DIR)
    set(HERMES_BUILD_DIR ${BASALT_THIRD_PARTY}/hermes-build)
  endif()

  # React Native's npm package omits ReactCxxPlatform, so with an installed
  # React Native it is fetched into third_party at the matching version instead.
  # bootstrap.sh does the fetching; this just says where to look.
  if(NOT EXISTS ${RN_DIR}/ReactCxxPlatform AND EXISTS ${BASALT_THIRD_PARTY}/ReactCxxPlatform)
    set(RN_CXX_PLATFORM_OVERRIDE ${BASALT_THIRD_PARTY}/ReactCxxPlatform)
    message(STATUS "ReactCxxPlatform from ${RN_CXX_PLATFORM_OVERRIDE}")
  endif()

  # Expo's core, compiled from the app's own expo-modules-core when the build
  # was pointed at one. Point BASALT_EXPO_MODULES_CORE at
  # <app>/node_modules/expo-modules-core; without it an Expo app fails at its
  # first import exactly as before, and a plain React Native app is unaffected.
  include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/Expo.cmake)

  include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/ThirdParty.cmake)
  include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/ReactNativeCore.cmake)

  # ---------------------------------------------------------------------------
  # basalt_core — everything that is not about a particular toolkit.
  #
  # A desktop platform on React Native's C++ host divides in two. One half is
  # the view layer: widgets, text shaping, input, the frame clock. That half is
  # GTK here and would be something else on macOS or Windows, and there is no
  # sense pretending otherwise. The other half is the host wiring, the Expo
  # runtime, the core modules and networking, and none of it is about Linux at
  # all -- it was simply written here first.
  #
  # This target is the second half, and it deliberately does not link GTK. That
  # is the whole point: the split is asserted by the compiler rather than by a
  # directory name, so a toolkit dependency that creeps into core fails the
  # build rather than quietly making core unshareable.
  # ---------------------------------------------------------------------------
  add_library(basalt_core STATIC
    # `Appearance`, and the portable half of light-vs-dark.
    core/AppearanceModule.cpp
    # Blob, File and FileReader -- portable in full.
    core/BlobModule.cpp
    core/BlobRegistry.cpp
    core/ColorScheme.cpp
    # Clipboard, Vibration, Alert, Linking, I18nManager, AccessibilityInfo --
    # the core modules ReactCxxPlatform does not provide.
    core/CoreModules.cpp
    core/DevBundle.cpp
    core/ExpoImageComponent.cpp
    core/GestureHandlerModule.cpp
    core/Gestures.cpp
    core/ExpoModules.cpp
    core/ExpoRuntime.cpp
    core/HttpClient.cpp
    # The bytes behind an <Image>, which is the half of image loading that is
    # the same on every desktop.
    core/ImageBytes.cpp
    core/PlatformConstantsModule.cpp
    core/SourceCodeModule.cpp
    core/StatusBarModule.cpp)
  target_compile_options(basalt_core PRIVATE -Wall -Wextra)
  # The React Native version, for the version guards and PlatformConstants.
  #
  # ReactNativeCore.cmake also states these with add_compile_definitions, which
  # is how React Native's own targets get them -- but that is directory-scoped,
  # and the platform packages are sibling directories rather than children. On
  # the target they travel with the link, so anything using the core has them.
  target_compile_definitions(basalt_core PUBLIC
    BASALT_REACT_NATIVE_VERSION="${RN_VERSION}"
    BASALT_RN_MINOR=${RN_MINOR})
  target_include_directories(basalt_core PUBLIC core)
  target_include_directories(basalt_core SYSTEM PUBLIC
    ${RN_DIR}/ReactCommon
    ${RN_DIR}/ReactCommon/jsi
    ${REACT_CXX_PLATFORM_DIR}
    ${FOLLY_DIR}
    ${CODEGEN_DIR}
    ${CODEGEN_DIR}/react/renderer/components/FBReactNativeSpec
    ${CODEGEN_DIR}/react/renderer/components)
  # libcurl, because core/HttpClient.cpp is here. It used to be named only on
  # basalt_gtk_mounting, which worked for exactly as long as the GTK host was the only
  # thing linking core: the archive member goes unreferenced until something
  # asks for an http client, so the missing dependency stayed invisible until
  # the macOS host asked.
  target_link_libraries(basalt_core PUBLIC rn_core CURL::libcurl)
  if(TARGET expo_core)
    target_link_libraries(basalt_core PUBLIC expo_core)
    target_sources(basalt_core PUBLIC $<TARGET_OBJECTS:expo_core>)
  endif()

  # Links the core and no toolkit. See core/portability_probe.cpp: what it
  # cannot find is precisely what a new desktop platform has to provide.
  add_executable(basalt_core_probe core/portability_probe.cpp)
  target_link_libraries(basalt_core_probe PRIVATE basalt_core)

endif()
