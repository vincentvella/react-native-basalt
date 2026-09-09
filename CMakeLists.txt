cmake_minimum_required(VERSION 3.13)
project(react_native_linux CXX)

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
    set(FOLLY_DIR ${CMAKE_CURRENT_SOURCE_DIR}/third_party/folly)
  endif()
  if(NOT DEFINED FAST_FLOAT_DIR)
    set(FAST_FLOAT_DIR ${CMAKE_CURRENT_SOURCE_DIR}/third_party/fast_float)
  endif()
  if(NOT DEFINED NLOHMANN_JSON_DIR)
    set(NLOHMANN_JSON_DIR ${CMAKE_CURRENT_SOURCE_DIR}/third_party/nlohmann_json)
  endif()
  if(NOT DEFINED CODEGEN_DIR)
    set(CODEGEN_DIR ${CMAKE_CURRENT_SOURCE_DIR}/third_party/codegen)
  endif()
  if(NOT DEFINED HERMES_DIR)
    set(HERMES_DIR ${CMAKE_CURRENT_SOURCE_DIR}/third_party/hermes)
  endif()
  if(NOT DEFINED HERMES_BUILD_DIR)
    set(HERMES_BUILD_DIR ${CMAKE_CURRENT_SOURCE_DIR}/third_party/hermes-build)
  endif()

  include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/ThirdParty.cmake)
  include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/ReactNativeCore.cmake)

  add_library(rn_mounting STATIC src/GtkMountingManager.cpp)

  target_include_directories(rn_mounting PUBLIC
    src
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
  target_compile_options(rn_mounting PRIVATE -Wno-unknown-pragmas)

  target_link_libraries(rn_mounting PUBLIC rn_view rn_core)
target_link_libraries(rn_mounting PUBLIC hermes-engine::hermesvm)

  # ---------------------------------------------------------------------------
  # mount_harness — drives real ShadowViewMutations through the real mounting
  # manager into real GTK widgets, with no JS runtime involved.
  # ---------------------------------------------------------------------------
  add_executable(mount_harness src/mount_harness.cpp)
  target_link_libraries(mount_harness PRIVATE rn_mounting)
  target_compile_options(mount_harness PRIVATE -Wno-unknown-pragmas)

  message(STATUS "rn_mounting enabled (RN_DIR=${RN_DIR})")
else()
  message(STATUS "rn_mounting skipped (pass -DRN_DIR=... to enable)")
endif()
