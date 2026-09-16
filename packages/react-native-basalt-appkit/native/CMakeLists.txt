# react-native-basalt-appkit — the macOS view layer, over AppKit.
#
# A view layer, a mounting manager and a host. Everything behind them is in
# react-native-basalt, which this adds as a subdirectory.
#
# `appkit/` rather than `mac/`: the toolkit is what differs, and the name would
# be wrong the moment something else on macOS mattered.

cmake_minimum_required(VERSION 3.13)
project(basalt_appkit CXX)

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

if(NOT APPLE)
  # Not an error. A checkout builds every package it finds, and on anything but
  # a Mac this one has nothing it could compile.
  message(STATUS "react-native-basalt-appkit skipped: not a Mac")
  return()
endif()

# Objective-C++ is enabled here rather than in the shared half, so that a Linux
# build never asks for a compiler it has no reason to have.
enable_language(OBJCXX)

# ---------------------------------------------------------------------------
# basalt_appkit_view — the AppKit view layer.
#
# No React Native dependency, so it builds and is tested on its own, on a Mac
# with nothing else installed.
# ---------------------------------------------------------------------------
add_library(basalt_appkit_view STATIC
  appkit/RnAppKitView.mm
  # The <TextInput> peer seam. In the *view* target rather than the mounting
  # one because RnAppKitView reads the peer's text for its tree dump, and this
  # target has to keep linking on its own -- the same reason GtkTextPeer.cpp
  # sits in basalt_gtk_view.
  appkit/AppKitTextPeer.mm
  # The paragraph object. Core Text only, no React Native, which is what keeps
  # this target buildable on a Mac with nothing else installed.
  appkit/RnTextLayout.mm
  appkit/AppKitSnapshot.mm)
target_compile_options(basalt_appkit_view PRIVATE -Wall -Wextra -fobjc-arc)
target_include_directories(basalt_appkit_view PUBLIC appkit)
# core/FocusRing.h, and nothing else from there. It is a header of four
# constants with no React Native in it, so taking it does not make this target
# depend on the half that does -- and the alternative is a focus ring that is a
# different blue on each desktop.
target_include_directories(basalt_appkit_view PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/../../react-native-basalt/native/core)
target_link_libraries(basalt_appkit_view PUBLIC
  "-framework Cocoa" "-framework QuartzCore" "-framework CoreText")

add_executable(demo_layout_appkit appkit/demo_layout_appkit.mm)
target_compile_options(demo_layout_appkit PRIVATE -Wall -Wextra -fobjc-arc)
target_link_libraries(demo_layout_appkit PRIVATE basalt_appkit_view)

# The runner comes from the shared half, which knows about no toolkit: the test
# infrastructure is shared even though the views are not.
set(BASALT_APPKIT_TEST_SOURCES
  ${BASALT_TEST_HARNESS}
  tests/test_appkit_view.mm
  # Core Text, against RnTextLayout alone. No React Native, so these run in the
  # toolkit-only build too.
  tests/test_appkit_text.mm
  # The resize modes, against a real bitmap. No React Native, so these run in
  # the toolkit-only build too.
  tests/test_appkit_image.mm
  # The NSAccessibility mapping. The React Native role name is what the
  # cross-platform diff compares; this is where the platform half is checked.
  tests/test_appkit_accessibility.mm
  # macOS's half of the platform-services seam. The alert is the reason: a
  # sheet is its own window and never appears in a snapshot.
  tests/test_appkit_services.mm
  tests/test_appkit_titlebar.mm)

# ---------------------------------------------------------------------------
# basalt_appkit_mounting — the AppKit half of the Fabric mounting manager.
#
# The mutation walk is not here: it is in the shared half, in
# core/MountingWalk.h. What is here is what is genuinely AppKit about mounting.
#
# Skipped when the build was not pointed at a React Native, so the view layer
# above still builds and tests on its own.
# ---------------------------------------------------------------------------
if(TARGET basalt_core)
  add_library(basalt_appkit_mounting STATIC
    appkit/AppKitMountingManager.mm
    # Which components this platform claims. See core/ComponentRegistry.h.
    appkit/ComponentRegistryAppKit.mm
    # macOS's implementation of core/FontRegistry.h -- the seam phase 17 named.
    # Core links against whichever implementation the platform half provides,
    # so without this nothing that reaches expo-font links.
    appkit/FontRegistryCoreText.mm
    # ...and of core/ColorScheme.h.
    appkit/ColorSchemeAppKit.mm
    # ...and of core/PlatformServices.h.
    appkit/PlatformServicesAppKit.mm
    # AttributedString -> Core Text. Shared by measurement and painting, so they
    # agree, which is the whole reason it is one file.
    appkit/CoreTextLayout.mm
    # Replaces React Native's stub TextLayoutManager; see the shared half's
    # cmake/ReactNativeCore.cmake for why the stub has to leave the build.
    appkit/CoreTextLayoutManager.mm
    # <ScrollView>: offset, clipping, onScroll, state and commands.
    appkit/AppKitScrollView.mm
    # <Image> pixels. RN's cxx ImageManager produces none; the fetch is in the
    # shared half, and only the decode and the cache are here.
    appkit/AppKitImageLoader.mm
    # <TextInput>, over a real NSTextField.
    appkit/AppKitTextInput.mm
    appkit/AppKitTitleBar.mm
    appkit/AppKitWindowModule.mm)
  target_compile_options(basalt_appkit_mounting PRIVATE -Wall -Wextra -fobjc-arc)
  target_include_directories(basalt_appkit_mounting PUBLIC appkit)
  target_link_libraries(basalt_appkit_mounting PUBLIC basalt_appkit_view basalt_core)
  target_link_libraries(basalt_appkit_mounting PUBLIC "-framework CoreText" "-framework ImageIO")

  # -------------------------------------------------------------------------
  # basalt_appkit — a ReactHost, Hermes, and one surface in an NSWindow.
  # -------------------------------------------------------------------------
  add_executable(basalt_appkit
    appkit/main_appkit.mm
    appkit/AppKitRunLoopObserver.mm
    appkit/AppKitAnimationChoreographer.mm
    appkit/AppKitFocus.mm
    appkit/AppKitTouchDispatcher.mm)
  target_compile_options(basalt_appkit PRIVATE -Wall -Wextra -fobjc-arc)
  target_link_libraries(basalt_appkit PRIVATE basalt_appkit_mounting)

  # Real mutations into real views, with no JavaScript anywhere. The checkpoint
  # between "the mounting manager compiles" and "React Native runs": a mistake
  # in the Create/Insert/Update/Remove/Delete handling shows up in a picture
  # rather than in review.
  add_executable(mount_harness_appkit appkit/mount_harness_appkit.mm)
  target_compile_options(mount_harness_appkit PRIVATE -Wall -Wextra -fobjc-arc)
  target_link_libraries(mount_harness_appkit PRIVATE basalt_appkit_mounting)

  list(APPEND BASALT_APPKIT_TEST_SOURCES
    ${BASALT_CORE_TESTS}
    tests/test_appkit_mounting.mm
    # Hit testing lives with the touch dispatcher, which the host builds; the
    # tests need it too, and it pulls in nothing else.
    appkit/AppKitFocus.mm
    appkit/AppKitTouchDispatcher.mm
    tests/test_appkit_input.mm
    # <TextInput>'s controlled-value loop, through real mutations.
    tests/test_appkit_textinput.mm)
endif()

add_executable(basalt_appkit_tests ${BASALT_APPKIT_TEST_SOURCES})
target_compile_options(basalt_appkit_tests PRIVATE -Wall -Wextra -fobjc-arc)
target_include_directories(basalt_appkit_tests PRIVATE ${BASALT_TEST_INCLUDE})
# basalt_appkit_mounting already carries basalt_appkit_view; naming both makes
# the linker warn about a duplicate library rather than doing anything useful.
if(TARGET basalt_appkit_mounting)
  target_link_libraries(basalt_appkit_tests PRIVATE basalt_appkit_mounting)
else()
  target_link_libraries(basalt_appkit_tests PRIVATE basalt_appkit_view)
endif()
