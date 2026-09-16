# react-native-basalt-gtk — the Linux view layer, over GTK4.
#
# A view layer, a mounting manager and a host. Everything behind them is in
# react-native-basalt, which this adds as a subdirectory; nothing here is
# duplicated from it.

cmake_minimum_required(VERSION 3.13)
project(basalt_gtk CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

# Executables at the top of the build tree whether this is configured directly
# or added by a wrapper, so `build/basalt_gtk` means the same thing either way.
# Every script and document here names that path.
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR})

# ---------------------------------------------------------------------------
# Finding react-native-basalt, the shared half.
#
# The default works in both layouts this can be built from: a checkout of this
# repository, where the packages are siblings under packages/, and an app's
# node_modules, where npm hoists them side by side. `react-native run-linux --build`
# passes it explicitly anyway, because a pnpm store is neither.
#
# Guarded on the target rather than on the variable: a build that configures
# both platform packages would otherwise add the same directory twice, which
# CMake refuses.
# ---------------------------------------------------------------------------
if(NOT TARGET basalt_core)
  if(NOT DEFINED BASALT_CORE_DIR)
    set(BASALT_CORE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../../react-native-basalt/native)
  endif()
  get_filename_component(BASALT_CORE_DIR "${BASALT_CORE_DIR}" ABSOLUTE)
  if(NOT EXISTS ${BASALT_CORE_DIR}/CMakeLists.txt)
    message(FATAL_ERROR
      "react-native-basalt not found at ${BASALT_CORE_DIR}.\n"
      "Pass -DBASALT_CORE_DIR=<path to react-native-basalt/native>.")
  endif()
  add_subdirectory(${BASALT_CORE_DIR} basalt_core)
endif()

# Found here rather than inherited from the shared half. find_package sets
# PKG_CONFIG_VERSION as an ordinary variable, which is scoped to the directory
# that ran it, so a package that only inherits it gets pkg_check_modules failing
# on a malformed `if` deep inside FindPkgConfig rather than on anything legible.
#
# Neither of the first two is REQUIRED, and that is the same courtesy the AppKit
# and Win32 packages extend: a checkout builds every package it finds, and on a
# machine with no GTK -- Windows, or a Mac without brew's gtk4 -- this one has
# nothing it could compile and should say so rather than fail the configure for
# the platform that machine *can* build. Everything after these two stays
# REQUIRED: a box with gtk4 and no pangoft2 is a broken installation, not an
# absent one.
find_package(PkgConfig QUIET)
if(NOT PKG_CONFIG_FOUND)
  message(STATUS "react-native-basalt-gtk skipped: no pkg-config")
  return()
endif()

pkg_check_modules(GTK4 IMPORTED_TARGET gtk4)
if(NOT GTK4_FOUND)
  message(STATUS "react-native-basalt-gtk skipped: no gtk4")
  return()
endif()
# For fonts an app loads at runtime. Pango finds fonts through fontconfig, so
# adding one to fontconfig is what makes it available to text this platform
# renders. See gtk/FontRegistryFontconfig.cpp.
pkg_check_modules(FONTCONFIG REQUIRED IMPORTED_TARGET fontconfig)
# pango_fc_font_map_config_changed lives here rather than in pango proper. It is
# how Pango is told that the set of fonts changed underneath it.
pkg_check_modules(PANGOFT2 REQUIRED IMPORTED_TARGET pangoft2)

# ---------------------------------------------------------------------------
# basalt_gtk_view — the GTK4 widget layer.
#
# No React Native dependency, so it builds and runs on its own.
# ---------------------------------------------------------------------------

add_library(basalt_gtk_view STATIC gtk/RnView.cpp gtk/GtkTextPeer.cpp)
# This project's own sources are held to -Wall -Wextra. React Native's are not:
# its targets set their own flags, and these are PRIVATE so they never reach it.
target_compile_options(basalt_gtk_view PRIVATE -Wall -Wextra)
target_include_directories(basalt_gtk_view PUBLIC gtk)
target_link_libraries(basalt_gtk_view PUBLIC PkgConfig::GTK4 PkgConfig::FONTCONFIG PkgConfig::PANGOFT2)

# ---------------------------------------------------------------------------
# demo_layout_gtk — exercises the widget layer with hand-written frames.
# ---------------------------------------------------------------------------

add_executable(demo_layout_gtk gtk/demo_layout_gtk.cpp)
target_link_libraries(demo_layout_gtk PRIVATE basalt_gtk_view)

# ---------------------------------------------------------------------------
# basalt_gtk_mounting — the GTK half of the Fabric mounting manager.
#
# The mutation walk is not here: it is in core/MountingWalk.h, shared with every
# other desktop platform. What is here is what is genuinely GTK about mounting.
# ---------------------------------------------------------------------------

if(TARGET basalt_core)
  add_library(basalt_gtk_mounting STATIC
    gtk/GtkMountingManager.cpp
    # Which components this platform claims. See core/ComponentRegistry.h.
    gtk/ComponentRegistryGtk.cpp
    # The mounting manager loads <Image> pixels itself, so the loader lives
    # here rather than with the host: mount_harness_gtk links this too.
    gtk/GtkImageLoader.cpp
    gtk/GtkScrollView.cpp
    gtk/GtkTextInput.cpp
    gtk/GtkTitleBar.cpp
    gtk/GtkWindowModule.cpp
    # Linux's implementation of core/FontRegistry.h.
    gtk/FontRegistryFontconfig.cpp
    # ...and of core/ColorScheme.h.
    gtk/ColorSchemeGtk.cpp
    # ...and of core/PlatformServices.h.
    gtk/PlatformServicesGtk.cpp
    gtk/PangoTextLayout.cpp
    # Replaces React Native's stub; see cmake/ReactNativeCore.cmake.
    gtk/PangoTextLayoutManager.cpp)

  target_include_directories(basalt_gtk_mounting PUBLIC gtk)
  target_link_libraries(basalt_gtk_mounting PUBLIC basalt_core)

  # SYSTEM, so -Wall -Wextra on this project's own sources does not drown in
  # diagnostics from React Native's headers, which are not -Wextra clean (its
  # own targets build with a narrower warning set).
  target_include_directories(basalt_gtk_mounting SYSTEM PUBLIC
    ${RN_DIR}/ReactCommon
    ${RN_DIR}/ReactCommon/jsi
    # Not ${RN_DIR}/ReactCxxPlatform: when React Native is an installed package
    # that directory does not exist and this one is the fetched copy. Latent
    # until a version whose headers are not -Wextra clean, which 0.81 is.
    ${REACT_CXX_PLATFORM_DIR}
    ${FOLLY_DIR}
    # Out-of-tree platforms pick a variant for each seam that has one. The cxx
    # variants are the generic implementations; textlayoutmanager/cxx is a stub
    # that measures nothing and is the first thing a Linux platform replaces.
    ${RN_DIR}/ReactCommon/react/renderer/graphics/platform/cxx
    ${RN_DIR}/ReactCommon/react/renderer/components/view/platform/cxx
    ${RN_DIR}/ReactCommon/react/renderer/textlayoutmanager/platform/cxx
    ${RN_DIR}/ReactCommon/react/renderer/imagemanager/platform/cxx
  )

  target_compile_definitions(basalt_gtk_mounting PUBLIC
    FOLLY_NO_CONFIG=1
    FOLLY_CFG_NO_COROUTINES=1
    FOLLY_MOBILE=0
    FOLLY_HAVE_PTHREAD=1
    FOLLY_HAVE_CLOCK_GETTIME=1
    GLOG_USE_GLOG_EXPORT
  )

  # RN headers carry Xcode's #pragma mark, which GCC does not recognise.
  target_compile_options(basalt_gtk_mounting PRIVATE -Wall -Wextra -Wno-unknown-pragmas)

  target_link_libraries(basalt_gtk_mounting PUBLIC basalt_gtk_view rn_core)
  target_link_libraries(basalt_gtk_mounting PUBLIC hermes-engine::hermesvm)

  # The seams point both ways, and GNU ld has to be told.
  #
  # `basalt_core` declares `core/PlatformServices.h`, `core/ColorScheme.h` and
  # `core/FontRegistry.h`; this library defines them. So `CoreModules.o`, inside
  # libbasalt_core.a, has undefined references that only libbasalt_gtk_mounting.a
  # can satisfy -- while this library's own objects call into core. That is a
  # genuine cycle between two static archives.
  #
  # It links anywhere the linker resolves across the whole command line, which
  # is why Windows and macOS never noticed. GNU ld scans left to right exactly
  # once, and CMake orders a dependency *after* its dependent, so libbasalt_core.a
  # arrives with libbasalt_gtk_mounting.a already behind it and the seams come
  # out undefined.
  #
  # Declaring the cycle is CMake's own answer: for mutually dependent STATIC
  # libraries it repeats the whole connected component on the link line. A
  # `--start-group` would work too and would say less about why.
  target_link_libraries(basalt_core PUBLIC basalt_gtk_mounting)

  # ---------------------------------------------------------------------------
  # mount_harness_gtk — drives real ShadowViewMutations through the real mounting
  # manager into real GTK widgets, with no JS runtime involved.
  # ---------------------------------------------------------------------------
  add_executable(mount_harness_gtk gtk/mount_harness_gtk.cpp)
  target_link_libraries(mount_harness_gtk PRIVATE basalt_gtk_mounting)
  target_compile_options(mount_harness_gtk PRIVATE -Wno-unknown-pragmas)

  # ---------------------------------------------------------------------------
  # basalt_gtk -- the real thing: ReactHost, Hermes and a live surface, with
  # the mutation stream coming from Fabric rather than from a harness.
  # ---------------------------------------------------------------------------
  add_executable(basalt_gtk
    gtk/main_gtk.cpp
    gtk/GtkAnimationChoreographer.cpp
    gtk/GtkRunLoopObserver.cpp
    gtk/GtkTouchDispatcher.cpp)
  target_link_libraries(basalt_gtk PRIVATE basalt_gtk_mounting)
  target_compile_options(basalt_gtk PRIVATE -Wall -Wextra -Wno-unknown-pragmas)

  # ---------------------------------------------------------------------------
  # basalt_gtk_tests -- the test suite.
  #
  # Needs a display: every test touches widgets, and GTK cannot create one
  # before gtk_init succeeds. It presents no window. On a headless machine run
  # it under Xvfb; see docs/TESTING.md.
  # ---------------------------------------------------------------------------
  add_executable(basalt_gtk_tests
    ${BASALT_TEST_HARNESS}
    ${BASALT_CORE_TESTS}
    tests/TestMainGtk.cpp
    tests/test_mounting.cpp
    tests/test_view.cpp
    tests/test_text.cpp
    tests/test_hittest.cpp
    tests/test_image.cpp
    tests/test_accessibility.cpp
    tests/test_viewprops.cpp
    tests/test_textinput.cpp
    tests/test_titlebar.cpp
    tests/test_version.cpp
    # Hit testing lives with the touch dispatcher, which the host builds; the
    # tests need it too, and it pulls in nothing else.
    gtk/GtkTouchDispatcher.cpp)
  target_link_libraries(basalt_gtk_tests PRIVATE basalt_gtk_mounting)
  target_include_directories(basalt_gtk_tests PRIVATE tests ${BASALT_TEST_INCLUDE})
  target_compile_options(basalt_gtk_tests PRIVATE -Wall -Wextra -Wno-unknown-pragmas)

  message(STATUS "basalt_gtk_mounting enabled (RN_DIR=${RN_DIR})")
else()
  message(STATUS "basalt_gtk_mounting skipped (pass -DRN_DIR=... to enable)")
endif()
