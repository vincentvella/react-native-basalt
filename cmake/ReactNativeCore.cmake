# Adds the React Native C++ targets a host needs in order to run Fabric's
# mounting layer.
#
# This is the transitive closure of react_renderer_mounting, computed from the
# target_link_libraries in RN's own CMakeLists. Notably it contains no Hermes:
# constructing and applying mutations does not require a JS runtime. Adding one
# (react/runtime + react/runtime/hermes) is a separate, later step.

set(REACT_COMMON_DIR ${RN_DIR}/ReactCommon)
# jsinspector-modern's CMakeLists include SoMerging-utils.cmake from here. It
# is an Android .so-merging helper that no-ops off-Android, but the include
# path must still resolve.
set(REACT_ANDROID_DIR ${RN_DIR}/ReactAndroid)

# Several RN CMakeLists call react_native_android_selector() without including
# the file that defines it, relying on a parent scope having done so.
include(${REACT_COMMON_DIR}/cmake-utils/internal/react-native-platform-selector.cmake)
set(REACT_CXX_PLATFORM_DIR ${RN_DIR}/ReactCxxPlatform)

function(add_react_common_subdir relative_path)
  add_subdirectory(${REACT_COMMON_DIR}/${relative_path} ReactCommon/${relative_path})
endfunction()

set(RN_CORE_SUBDIRS
        callinvoker
        logger
        oscompat
        reactperflogger
        runtimeexecutor
        jsi/jsi
        jsinspector-modern
        jsinspector-modern/cdp
        jsinspector-modern/network
        jsinspector-modern/tracing
        jserrorhandler
        react/cxxstableapi
        react/debug
        react/featureflags
        react/timing
        react/utils
        react/performance/timeline
        react/renderer/componentregistry
        react/renderer/consistency
        react/renderer/core
        react/renderer/css
        react/renderer/debug
        react/renderer/graphics
        react/renderer/mapbuffer
        react/renderer/mounting
        react/renderer/runtimescheduler
        react/renderer/telemetry
        react/renderer/components/root
        react/renderer/components/view
        react/renderer/components/scrollview
        react/renderer/components/legacyviewmanagerinterop
        yoga)

# Seams with per-platform variants: a host must choose one of each, globally,
# before any RN target is added. Individual RN CMakeLists only add the variant
# dirs they own, so a target like componentregistry -- which includes ViewProps
# transitively -- otherwise cannot find HostPlatformViewProps.h.
include_directories(
        ${REACT_COMMON_DIR}/react/renderer/graphics/platform/cxx
        ${REACT_COMMON_DIR}/react/renderer/components/view/platform/cxx
        ${REACT_COMMON_DIR}/react/renderer/textlayoutmanager/platform/cxx
        ${REACT_COMMON_DIR}/react/renderer/imagemanager/platform/cxx
        ${REACT_COMMON_DIR}/react/utils/platform/cxx
        ${REACT_COMMON_DIR}/runtimeexecutor/platform/cxx)

foreach(subdir ${RN_CORE_SUBDIRS})
  add_react_common_subdir(${subdir})
endforeach()

# yoga/CMakeLists.txt descends into yoga/yoga, which names the target
# `yogacore`. RN's other CMakeLists link against `yoga`. Fantom bridges the
# two the same way.
add_library(yoga ALIAS yogacore)

# Every RN target above is an OBJECT library; collect them into one thing that
# is convenient to link.
set(RN_CORE_OBJECT_TARGETS
        callinvoker logger oscompat reactperflogger
        jsi jsinspector jsinspector_cdp jsinspector_network jsinspector_tracing
        jserrorhandler
        react_cxxstableapi react_debug react_featureflags react_timing
        react_utils react_performance_timeline
        react_renderer_componentregistry
        react_renderer_consistency react_renderer_core react_renderer_css
        react_renderer_debug react_renderer_graphics react_renderer_mapbuffer
        react_renderer_mounting react_renderer_runtimescheduler
        react_renderer_telemetry
        rrc_root rrc_view rrc_scrollview rrc_legacyviewmanagerinterop)

# Upstream portability bug: ReactCommon/jsinspector-modern/network/HttpUtils.h
# uses uint16_t without including <cstdint>. It compiles on Meta's toolchains
# only via transitive includes. Force-including <cstdint> fixes it without
# patching the React Native checkout, and is harmless everywhere else.
foreach(target ${RN_CORE_OBJECT_TARGETS} yogacore)
  if(TARGET ${target})
    # A few RN targets are INTERFACE libraries, which reject PRIVATE options.
    get_target_property(target_type ${target} TYPE)
    if(NOT target_type STREQUAL "INTERFACE_LIBRARY")
      target_compile_options(${target} PRIVATE -include cstdint)
    endif()
  endif()
endforeach()

add_library(rn_core INTERFACE)
foreach(target ${RN_CORE_OBJECT_TARGETS})
  # target_link_libraries carries usage requirements (include dirs, defines);
  # OBJECT libraries additionally need their objects named explicitly, or the
  # final link comes up short on every symbol they define.
  target_link_libraries(rn_core INTERFACE ${target})
  # Some of these (e.g. callinvoker) are header-only INTERFACE libraries with
  # no objects at all; $<TARGET_OBJECTS:> is an error on those.
  get_target_property(target_type ${target} TYPE)
  if(target_type STREQUAL "OBJECT_LIBRARY")
    target_sources(rn_core INTERFACE $<TARGET_OBJECTS:${target}>)
  endif()
endforeach()

# ReactCommon declares jsi::dynamicFromValue (used by RawProps) but no
# CMakeLists anywhere in the tree compiles JSIDynamic.cpp. Hosts are expected
# to build it themselves.
add_library(rn_jsidynamic OBJECT ${REACT_COMMON_DIR}/jsi/jsi/JSIDynamic.cpp)
target_include_directories(rn_jsidynamic PUBLIC ${REACT_COMMON_DIR} ${REACT_COMMON_DIR}/jsi ${FOLLY_DIR})
target_link_libraries(rn_jsidynamic folly_runtime glog)
target_sources(rn_core INTERFACE $<TARGET_OBJECTS:rn_jsidynamic>)
target_link_libraries(rn_core INTERFACE yogacore folly_runtime glog boost fmt
        double-conversion fast_float)

# std::atomic<std::optional<double>> in ReactNativeFeatureFlagsAccessor is
# 16 bytes wide; on x86-64 those operations are out-of-line calls in libatomic.
# Fantom's tester CMakeLists carries the same `if(UNIX AND NOT APPLE)` branch.
if(UNIX AND NOT APPLE)
  target_link_libraries(rn_core INTERFACE atomic)
endif()
