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
        cxxreact
        devtoolsruntimesettings
        hermes/executor
        hermes/inspector-modern
        jserrorhandler
        jsi
        jsiexecutor
        jsinspector-modern
        jsinspector-modern/cdp
        jsinspector-modern/network
        jsinspector-modern/tracing
        jsitooling
        logger
        oscompat
        react/bridging
        react/cxxstableapi
        react/debug
        react/featureflags
        react/nativemodule/core
        react/nativemodule/cputime
        react/nativemodule/defaults
        react/nativemodule/devtoolsruntimesettings
        react/nativemodule/dom
        react/nativemodule/featureflags
        react/nativemodule/idlecallbacks
        react/nativemodule/intersectionobserver
        react/nativemodule/microtasks
        react/nativemodule/mutationobserver
        react/nativemodule/resizeobserver
        react/nativemodule/viewtransition
        react/nativemodule/webperformance
        react/performance/cdpmetrics
        react/performance/timeline
        react/renderer/animated
        react/renderer/animationbackend
        react/renderer/attributedstring
        react/renderer/bridging
        react/renderer/componentregistry
        react/renderer/componentregistry/native
        react/renderer/components/image
        react/renderer/components/legacyviewmanagerinterop
        react/renderer/components/modal
        react/renderer/components/root
        react/renderer/components/scrollview
        react/renderer/components/text
        react/renderer/components/view
        react/renderer/consistency
        react/renderer/core
        react/renderer/css
        react/renderer/debug
        react/renderer/dom
        react/renderer/graphics
        react/renderer/imagemanager
        react/renderer/leakchecker
        react/renderer/mapbuffer
        react/renderer/mounting
        react/renderer/observers/events
        react/renderer/observers/intersection
        react/renderer/observers/mutation
        react/renderer/observers/resize
        react/renderer/runtimescheduler
        react/renderer/scheduler
        react/renderer/telemetry
        react/renderer/textlayoutmanager
        react/renderer/uimanager
        react/renderer/uimanager/consistency
        react/renderer/viewtransition
        react/runtime
        react/runtime/hermes
        react/timing
        react/utils
        reactperflogger
        runtimeexecutor
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

# ---------------------------------------------------------------------------
# ReactCxxPlatform — the generic C++ platform: ReactHost, the scheduler
# delegate, http, io, logging, threading, devsupport, coremodules and the
# TurboModule host. This is the layer that makes a non-Apple, non-Android host
# possible at all.
# ---------------------------------------------------------------------------

# React Native's generated codegen artifacts. 24 targets depend on
# react_codegen_rncore, including ReactCxxPlatform's react/runtime -- where
# ReactHost lives -- so this is not optional for a host that runs JS.
add_subdirectory(${CODEGEN_DIR} codegen)

function(add_react_cxx_platform_subdir relative_path)
  add_subdirectory(${REACT_CXX_PLATFORM_DIR}/${relative_path} ReactCxxPlatform/${relative_path})
endfunction()

set(RN_CXX_PLATFORM_SUBDIRS
        react/coremodules
        react/devsupport
        react/http
        react/io
        react/logging
        react/nativemodule
        react/profiling
        react/renderer/scheduler
        react/renderer/uimanager
        react/runtime
        react/threading
        react/utils)

foreach(subdir ${RN_CXX_PLATFORM_SUBDIRS})
  add_react_cxx_platform_subdir(${subdir})
endforeach()

set(RN_CXX_PLATFORM_TARGETS
        react_cxx_platform_react_coremodules
        react_cxx_platform_react_devsupport
        react_cxx_platform_react_http
        react_cxx_platform_react_io
        react_cxx_platform_react_logging
        react_cxx_platform_react_nativemodule
        react_cxx_platform_react_profiling
        react_cxx_platform_react_renderer_scheduler
        react_cxx_platform_react_renderer_uimanager
        react_cxx_platform_react_runtime
        react_cxx_platform_react_threading
        react_cxx_platform_react_utils)

# Every RN target above is an OBJECT library; collect them into one thing that
# is convenient to link.
set(RN_CORE_OBJECT_TARGETS
        bridgeless
        bridgelesshermes
        callinvoker
        devtoolsruntimesettings
        hermes_executor_common
        hermes_inspector_modern
        jserrorhandler
        jsi
        jsinspector
        jsinspector_cdp
        jsinspector_network
        jsinspector_tracing
        jsireact
        jsitooling
        logger
        oscompat
        react_bridging
        react_cxxreact
        react_cxxstableapi
        react_debug
        react_featureflags
        react_nativemodule_core
        react_nativemodule_cpu
        react_nativemodule_defaults
        react_nativemodule_devtoolsruntimesettings
        react_nativemodule_dom
        react_nativemodule_featureflags
        react_nativemodule_idlecallbacks
        react_nativemodule_intersectionobserver
        react_nativemodule_microtasks
        react_nativemodule_mutationobserver
        react_nativemodule_resizeobserver
        react_nativemodule_viewtransition
        react_nativemodule_webperformance
        react_performance_cdpmetrics
        react_performance_timeline
        react_renderer_animated
        react_renderer_animationbackend
        react_renderer_attributedstring
        react_renderer_bridging
        react_renderer_componentregistry
        react_renderer_consistency
        react_renderer_core
        react_renderer_css
        react_renderer_debug
        react_renderer_dom
        react_renderer_graphics
        react_renderer_imagemanager
        react_renderer_leakchecker
        react_renderer_mapbuffer
        react_renderer_mounting
        react_renderer_observers_events
        react_renderer_observers_intersection
        react_renderer_observers_mutation
        react_renderer_observers_resize
        react_renderer_runtimescheduler
        react_renderer_scheduler
        react_renderer_telemetry
        react_renderer_textlayoutmanager
        react_renderer_uimanager
        react_renderer_uimanager_consistency
        react_renderer_viewtransition
        react_timing
        react_utils
        reactperflogger
        rrc_image
        rrc_legacyviewmanagerinterop
        rrc_modal
        rrc_native
        rrc_root
        rrc_scrollview
        rrc_text
        rrc_view
        runtimeexecutor)

# Must come after the set() above, which would otherwise overwrite it.
list(APPEND RN_CORE_OBJECT_TARGETS ${RN_CXX_PLATFORM_TARGETS})

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
