cmake_minimum_required(VERSION 3.13)
project(react_native_linux CXX)

# Where folly, fast_float, nlohmann, Hermes and the codegen output live.
#
# Not inside this directory. In a development checkout they sit at the root of
# the repository, and when an app builds the host they belong to that app, not
# to a copy of this package inside its node_modules -- npm would delete them on
# the next install, and rebuilding Hermes is not a thing to lose by accident.
# bootstrap.sh takes the same path and fills it.
if(NOT DEFINED RN_LINUX_THIRD_PARTY)
  set(RN_LINUX_THIRD_PARTY ${CMAKE_CURRENT_SOURCE_DIR}/third_party)
endif()
get_filename_component(RN_LINUX_THIRD_PARTY "${RN_LINUX_THIRD_PARTY}" ABSOLUTE)

# Executables at the top of the build tree whether this is configured directly
# or added by the wrapper at the repository root, so `build/rn_linux_host` means
# the same thing either way. Every script and document here names that path.
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR})

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

find_package(PkgConfig REQUIRED)
pkg_check_modules(GTK4 REQUIRED IMPORTED_TARGET gtk4)

# ---------------------------------------------------------------------------
# rn_view — the GTK4 widget layer.
#
# No React Native dependency, so it builds and runs on its own.
# ---------------------------------------------------------------------------

add_library(rn_view STATIC src/RnView.cpp)
# This project's own sources are held to -Wall -Wextra. React Native's are not:
# its targets set their own flags, and these are PRIVATE so they never reach it.
target_compile_options(rn_view PRIVATE -Wall -Wextra)
target_include_directories(rn_view PUBLIC src)
target_link_libraries(rn_view PUBLIC PkgConfig::GTK4)

# ---------------------------------------------------------------------------
# demo_layout — exercises the widget layer with hand-written frames.
# ---------------------------------------------------------------------------

add_executable(demo_layout src/demo_layout.cpp)
target_link_libraries(demo_layout PRIVATE rn_view)

# ---------------------------------------------------------------------------
# rn_mounting — the Fabric mounting manager.
#
# Needs React Native's C++ core and its third-party dependencies (folly, glog,
# boost, fmt, double-conversion). Enable with:
#
#   cmake -B build -DRN_DIR=/path/to/react-native/packages/react-native
#
# See README.md for the current state of this target.
# ---------------------------------------------------------------------------

if(DEFINED RN_DIR)
  # RN's own CMakeLists include files by ${REACT_COMMON_DIR}/..., which a
  # relative path resolves against the wrong directory. Anchor it to this repo.
  get_filename_component(RN_DIR "${RN_DIR}" ABSOLUTE BASE_DIR "${CMAKE_SOURCE_DIR}")
  if(NOT DEFINED FOLLY_DIR)
    set(FOLLY_DIR ${RN_LINUX_THIRD_PARTY}/folly)
  endif()
  if(NOT DEFINED FAST_FLOAT_DIR)
    set(FAST_FLOAT_DIR ${RN_LINUX_THIRD_PARTY}/fast_float)
  endif()
  if(NOT DEFINED NLOHMANN_JSON_DIR)
    set(NLOHMANN_JSON_DIR ${RN_LINUX_THIRD_PARTY}/nlohmann_json)
  endif()
  if(NOT DEFINED CODEGEN_DIR)
    set(CODEGEN_DIR ${RN_LINUX_THIRD_PARTY}/codegen)
  endif()
  if(NOT DEFINED HERMES_DIR)
    set(HERMES_DIR ${RN_LINUX_THIRD_PARTY}/hermes)
  endif()
  if(NOT DEFINED HERMES_BUILD_DIR)
    set(HERMES_BUILD_DIR ${RN_LINUX_THIRD_PARTY}/hermes-build)
  endif()

  # React Native's npm package omits ReactCxxPlatform, so with an installed
  # React Native it is fetched into third_party at the matching version instead.
  # bootstrap.sh does the fetching; this just says where to look.
  if(NOT EXISTS ${RN_DIR}/ReactCxxPlatform AND EXISTS ${RN_LINUX_THIRD_PARTY}/ReactCxxPlatform)
    set(RN_CXX_PLATFORM_OVERRIDE ${RN_LINUX_THIRD_PARTY}/ReactCxxPlatform)
    message(STATUS "ReactCxxPlatform from ${RN_CXX_PLATFORM_OVERRIDE}")
  endif()

  include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/ThirdParty.cmake)
  include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/ReactNativeCore.cmake)

  add_library(rn_mounting STATIC
    src/GtkMountingManager.cpp
    # The mounting manager loads <Image> pixels itself, so the loader lives
    # here rather than with the host: mount_harness links this too.
    src/GtkImageLoader.cpp
    src/GtkScrollView.cpp
    src/GtkTextInput.cpp
    src/LinuxPlatformConstants.cpp
    src/PangoTextLayout.cpp
    # Replaces React Native's stub; see cmake/ReactNativeCore.cmake.
    src/PangoTextLayoutManager.cpp)

  target_include_directories(rn_mounting PUBLIC src)

  # SYSTEM, so -Wall -Wextra on this project's own sources does not drown in
  # diagnostics from React Native's headers, which are not -Wextra clean (its
  # own targets build with a narrower warning set).
  target_include_directories(rn_mounting SYSTEM PUBLIC
    ${RN_DIR}/ReactCommon
    ${RN_DIR}/ReactCommon/jsi
    ${RN_DIR}/ReactCxxPlatform
    ${FOLLY_DIR}
    # Out-of-tree platforms pick a variant for each seam that has one. The cxx
    # variants are the generic implementations; textlayoutmanager/cxx is a stub
    # that measures nothing and is the first thing a Linux platform replaces.
    ${RN_DIR}/ReactCommon/react/renderer/graphics/platform/cxx
    ${RN_DIR}/ReactCommon/react/renderer/components/view/platform/cxx
    ${RN_DIR}/ReactCommon/react/renderer/textlayoutmanager/platform/cxx
    ${RN_DIR}/ReactCommon/react/renderer/imagemanager/platform/cxx
  )

  target_compile_definitions(rn_mounting PUBLIC
    FOLLY_NO_CONFIG=1
    FOLLY_CFG_NO_COROUTINES=1
    FOLLY_MOBILE=0
    FOLLY_HAVE_PTHREAD=1
    FOLLY_HAVE_CLOCK_GETTIME=1
    GLOG_USE_GLOG_EXPORT
  )

  # RN headers carry Xcode's #pragma mark, which GCC does not recognise.
  target_compile_options(rn_mounting PRIVATE -Wall -Wextra -Wno-unknown-pragmas)

  target_link_libraries(rn_mounting PUBLIC rn_view rn_core CURL::libcurl)
target_link_libraries(rn_mounting PUBLIC hermes-engine::hermesvm)

  # ---------------------------------------------------------------------------
  # mount_harness — drives real ShadowViewMutations through the real mounting
  # manager into real GTK widgets, with no JS runtime involved.
  # ---------------------------------------------------------------------------
  add_executable(mount_harness src/mount_harness.cpp)
  target_link_libraries(mount_harness PRIVATE rn_mounting)
  target_compile_options(mount_harness PRIVATE -Wno-unknown-pragmas)

  # ---------------------------------------------------------------------------
  # rn_linux_host -- the real thing: ReactHost, Hermes and a live surface, with
  # the mutation stream coming from Fabric rather than from a harness.
  # ---------------------------------------------------------------------------
  add_executable(rn_linux_host
    src/main.cpp
    src/GtkAnimationChoreographer.cpp
    src/GtkRunLoopObserver.cpp
    src/GtkTouchDispatcher.cpp
    src/LinuxNetworking.cpp)
  target_link_libraries(rn_linux_host PRIVATE rn_mounting CURL::libcurl)
  target_compile_options(rn_linux_host PRIVATE -Wall -Wextra -Wno-unknown-pragmas)

  # ---------------------------------------------------------------------------
  # rn_tests -- the test suite.
  #
  # Needs a display: every test touches widgets, and GTK cannot create one
  # before gtk_init succeeds. It presents no window. On a headless machine run
  # it under Xvfb; see docs/TESTING.md.
  # ---------------------------------------------------------------------------
  add_executable(rn_tests
    tests/TestHarness.cpp
    tests/test_mounting.cpp
    tests/test_view.cpp
    tests/test_text.cpp
    tests/test_hittest.cpp
    tests/test_image.cpp
    tests/test_accessibility.cpp
    tests/test_viewprops.cpp
    tests/test_textinput.cpp
    tests/test_version.cpp
    # Hit testing lives with the touch dispatcher, which the host builds; the
    # tests need it too, and it pulls in nothing else.
    src/GtkTouchDispatcher.cpp)
  target_link_libraries(rn_tests PRIVATE rn_mounting)
  target_include_directories(rn_tests PRIVATE tests)
  target_compile_options(rn_tests PRIVATE -Wall -Wextra -Wno-unknown-pragmas)

  message(STATUS "rn_mounting enabled (RN_DIR=${RN_DIR})")
else()
  message(STATUS "rn_mounting skipped (pass -DRN_DIR=... to enable)")
endif()
