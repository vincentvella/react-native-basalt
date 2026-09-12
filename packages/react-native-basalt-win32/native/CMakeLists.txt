# react-native-basalt-win32 — the Windows view layer, over Win32 and Direct2D.
#
# A view layer, a mounting manager and a host. Everything behind them is in
# react-native-basalt, which this adds as a subdirectory.
#
# `win32/` rather than `windows/`: the toolkit is what differs, and the name
# would be wrong the moment WinUI or something after it mattered. The npm
# package is `-win32` for the same reason `-appkit` is not `-macos`, and neither
# name is the JavaScript platform's -- that one is `windows`, matching
# react-native-windows, and lives in the shared package.

cmake_minimum_required(VERSION 3.13)
project(basalt_win32 CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

# Executables at the top of the build tree whether this is configured directly
# or added by a wrapper, so `build/basalt_win32` means the same thing either
# way. Every script and document here names that path.
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR})
# ...including under the multi-config generators, which are the default on
# Windows: Visual Studio and Ninja Multi-Config both append Debug/ or Release/
# to the directory above unless told otherwise, and a document that says
# `build/basalt_win32_tests` would be wrong on the platform it was written for.
foreach(config ${CMAKE_CONFIGURATION_TYPES})
  string(TOUPPER ${config} config_upper)
  set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_${config_upper} ${CMAKE_BINARY_DIR})
endforeach()

# ---------------------------------------------------------------------------
# Finding react-native-basalt, the shared half.
#
# The default works in both layouts this can be built from: a checkout of this
# repository, where the packages are siblings under packages/, and an app's
# node_modules, where npm hoists them side by side.
#
# Guarded on the target rather than on the variable: a build that configures
# more than one platform package would otherwise add the same directory twice,
# which CMake refuses.
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

if(NOT WIN32)
  # Not an error. A checkout builds every package it finds, and on anything but
  # Windows this one has nothing it could compile.
  message(STATUS "react-native-basalt-win32 skipped: not Windows")
  return()
endif()

# ---------------------------------------------------------------------------
# What this project's own sources are held to.
#
# The GTK and AppKit halves say `-Wall -Wextra`, which MSVC does not speak.
# /W4 is the nearest equivalent and /permissive- is the part that matters more:
# without it MSVC accepts things no other compiler in this project would, and
# the first anyone would hear of it is the Linux build failing on code that
# compiled here. Set on targets rather than globally, so React Native's own
# sources -- which set their own flags and are not warning-clean under /W4 --
# never inherit them.
#
# Note that clang-cl reports as MSVC here, which is intended: it takes the same
# flags, and `plan/decisions.md` prefers clang where there is a choice.
# ---------------------------------------------------------------------------
if(MSVC)
  set(BASALT_WIN32_WARNINGS /W4 /permissive-)
else()
  set(BASALT_WIN32_WARNINGS -Wall -Wextra)
endif()

# UNICODE, because every Win32 entry point in this project takes wide strings
# and the ANSI variants silently lose anything outside the active code page --
# which for React Native, where a string is UTF-8 from JavaScript, is most of
# the world. NOMINMAX and WIN32_LEAN_AND_MEAN because windows.h otherwise
# defines `min` and `max` as macros and breaks <algorithm> at a distance.
#
# _CRT_SECURE_NO_WARNINGS is not laziness about a real hazard: what it silences
# here is C4996 on `std::getenv`, which the other two platforms call in the same
# place for the same reason and which has no portable replacement. Anything that
# genuinely wants the checked CRT can ask for it on its own.
add_compile_definitions(UNICODE _UNICODE NOMINMAX WIN32_LEAN_AND_MEAN _CRT_SECURE_NO_WARNINGS)

# ---------------------------------------------------------------------------
# basalt_win32_view — the Win32 view layer.
#
# No React Native dependency, so it builds and is tested on its own, on a
# Windows box with nothing installed but a compiler and the SDK. Direct2D, WIC
# and USER32 all ship with the operating system; there is nothing to fetch.
# ---------------------------------------------------------------------------
add_library(basalt_win32_view STATIC
  win32/RnWin32View.cpp
  # The paragraph object. DirectWrite only, no React Native, which is what keeps
  # this target buildable and testable with nothing else installed.
  win32/RnWin32TextLayout.cpp
  # <Image> pixels: the WIC decode and the four resize modes. The fetch that
  # feeds it is core/ImageBytes.cpp in the shared half, because a URI means the
  # same thing on every desktop.
  win32/RnWin32Image.cpp
  # Offscreen rendering, to a PNG or to pixels a test can read. The second is
  # why this is in the view target rather than beside the demo: it is what the
  # rendering assertions in tests/test_win32_paint.cpp are built on.
  win32/Win32Snapshot.cpp)
target_compile_options(basalt_win32_view PRIVATE ${BASALT_WIN32_WARNINGS})
target_include_directories(basalt_win32_view PUBLIC win32)
target_link_libraries(basalt_win32_view PUBLIC d2d1 dwrite windowscodecs ole32 user32)

add_executable(demo_layout_win32 win32/demo_layout_win32.cpp)
target_compile_options(demo_layout_win32 PRIVATE ${BASALT_WIN32_WARNINGS})
target_link_libraries(demo_layout_win32 PRIVATE basalt_win32_view)

# The runner comes from the shared half, which knows about no toolkit: the test
# infrastructure is shared even though the views are not. Unlike the GTK suite
# this one brings no separate entry point -- there is nothing to initialise
# before a view exists -- so `main` sits at the bottom of test_win32_view.cpp,
# as it does on the AppKit side.
set(BASALT_WIN32_TEST_SOURCES
  ${BASALT_TEST_HARNESS}
  tests/test_win32_view.cpp
  # What actually reached the pixels. Needs no display and no window, which is
  # why this platform can assert it and the other two cannot; see the file.
  tests/test_win32_paint.cpp
  # Picking. In the view layer rather than beside a touch dispatcher, so that
  # testing it needs no React Native -- the same arrangement AppKit settled on.
  tests/test_win32_hittest.cpp
  # DirectWrite measurement, against RnWin32TextLayout alone.
  tests/test_win32_text.cpp
  # The resize modes, against a real bitmap and real pixels.
  tests/test_win32_image.cpp)

# ---------------------------------------------------------------------------
# Still to come, in the order the macOS port took them:
#
#   basalt_win32_mounting   Win32MountingManager (the seven operations
#                           core/MountingWalk.h asks for), ComponentRegistryWin32,
#                           FontRegistryDirectWrite, ColorSchemeWin32,
#                           PlatformServicesWin32, DirectWriteLayoutManager.
#   mount_harness_win32     real ShadowViewMutations, no JavaScript.
#   basalt_win32            a ReactHost, Hermes, and one surface in an HWND.
#
# None of them is guarded here yet, because none exists. The shape to copy when
# they do is packages/react-native-basalt-appkit/native/CMakeLists.txt, where
# each is inside `if(TARGET basalt_core)` so the view layer above still builds
# and tests on a machine that was never pointed at a React Native.
# ---------------------------------------------------------------------------

add_executable(basalt_win32_tests ${BASALT_WIN32_TEST_SOURCES})
target_compile_options(basalt_win32_tests PRIVATE ${BASALT_WIN32_WARNINGS})
target_include_directories(basalt_win32_tests PRIVATE ${BASALT_TEST_INCLUDE})
target_link_libraries(basalt_win32_tests PRIVATE basalt_win32_view)
