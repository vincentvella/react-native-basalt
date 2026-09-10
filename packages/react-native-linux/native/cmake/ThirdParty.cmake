# Provides the third-party CMake target *names* React Native's own CMakeLists
# link against, backed by system packages where possible and by vendored
# sources where not.
#
# React Native normally gets these from its Android build (gradle downloads and
# unpacks each one under third-party-ndk). A host build has no such step, so we
# supply equivalents here. Fantom does the same thing for its tester.

# --- glog ------------------------------------------------------------------
# RN pins glog 0.3.5; distros ship 0.7.x, which added a guard requiring
# GLOG_USE_GLOG_EXPORT when the headers are not consumed via glog's own CMake
# target. Without it every glog header hard-errors.
find_library(GLOG_LIBRARY glog REQUIRED)
find_path(GLOG_INCLUDE_DIR glog/logging.h REQUIRED)
add_library(glog INTERFACE)
target_link_libraries(glog INTERFACE ${GLOG_LIBRARY})
# Distros put headers in /usr/include; Homebrew does not, so carry the path.
target_include_directories(glog INTERFACE ${GLOG_INCLUDE_DIR})
target_compile_definitions(glog INTERFACE GLOG_USE_GLOG_EXPORT)

# RN's Android build has a glog_init target that installs signal handlers.
# Nothing on a host build needs it; Fantom stubs it the same way.
add_library(glog_init INTERFACE)

# --- boost -----------------------------------------------------------------
# Headers only, with one exception: folly's Uri parser uses boost::regex, which
# is a compiled library. Uri is pulled in by React Native's websocket client
# (see rn_websocket in ReactNativeCore.cmake), so it is needed as soon as the
# dev server is. boost::asio and boost::beast, which that client also uses, are
# header-only.
find_path(BOOST_INCLUDE_DIR boost/version.hpp REQUIRED)
find_library(BOOST_REGEX_LIBRARY NAMES boost_regex boost_regex-mt REQUIRED)
add_library(boost INTERFACE)
target_include_directories(boost INTERFACE ${BOOST_INCLUDE_DIR})
target_link_libraries(boost INTERFACE ${BOOST_REGEX_LIBRARY})

# --- double-conversion -----------------------------------------------------
find_library(DOUBLE_CONVERSION_LIBRARY double-conversion REQUIRED)
find_path(DOUBLE_CONVERSION_INCLUDE_DIR double-conversion/double-conversion.h REQUIRED)
add_library(double-conversion INTERFACE)
target_link_libraries(double-conversion INTERFACE ${DOUBLE_CONVERSION_LIBRARY})
target_include_directories(double-conversion INTERFACE ${DOUBLE_CONVERSION_INCLUDE_DIR})

# --- fmt -------------------------------------------------------------------
find_library(FMT_LIBRARY fmt REQUIRED)
find_path(FMT_INCLUDE_DIR fmt/format.h REQUIRED)
add_library(fmt INTERFACE)
target_link_libraries(fmt INTERFACE ${FMT_LIBRARY})
target_include_directories(fmt INTERFACE ${FMT_INCLUDE_DIR})

# --- fast_float (vendored, header-only) ------------------------------------
add_library(fast_float INTERFACE)
target_include_directories(fast_float INTERFACE ${FAST_FLOAT_DIR}/include)

# --- folly_runtime ---------------------------------------------------------
# The trimmed folly RN actually builds: ~30 translation units, no config step,
# no coroutines. Source list and flags mirror
# private/react-native-fantom/tester/third-party/folly/CMakeLists.txt.
set(folly_FLAGS
        -DFOLLY_NO_CONFIG=1
        -DFOLLY_HAVE_CLOCK_GETTIME=1
        -DFOLLY_CFG_NO_COROUTINES=1
        -DFOLLY_MOBILE=0
        -DFOLLY_HAVE_RECVMMSG=1
        -DFOLLY_HAVE_PTHREAD=1
        -DFOLLY_HAVE_XSI_STRERROR_R=1)

set(folly_runtime_SRC
        Conv.cpp Demangle.cpp FileUtil.cpp Format.cpp ScopeGuard.cpp
        SharedMutex.cpp String.cpp Unicode.cpp
        concurrency/CacheLocality.cpp
        container/detail/F14Table.cpp
        detail/FileUtilDetail.cpp detail/Futex.cpp
        detail/SplitStringSimd.cpp detail/UniqueInstance.cpp
        hash/SpookyHashV2.cpp
        io/IOBuf.cpp
        json/dynamic.cpp json/json_pointer.cpp json/json.cpp
        # Not in React Native's own folly source list. Its websocket client
        # parses the packager URL with folly::Uri, so a host that talks to the
        # dev server needs it.
        Uri.cpp
        lang/CString.cpp lang/Exception.cpp lang/SafeAssert.cpp lang/ToAscii.cpp
        memory/SanitizeAddress.cpp memory/detail/MallocImpl.cpp
        net/NetOps.cpp
        portability/SysUio.cpp
        synchronization/SanitizeThread.cpp synchronization/ParkingLot.cpp
        system/AtFork.cpp system/ThreadId.cpp system/ThreadName.cpp)
list(TRANSFORM folly_runtime_SRC PREPEND ${FOLLY_DIR}/folly/)

add_library(folly_runtime STATIC ${folly_runtime_SRC})
target_compile_options(folly_runtime PRIVATE
        -fexceptions -fno-omit-frame-pointer -frtti -Wno-sign-compare ${folly_FLAGS})
target_compile_options(folly_runtime PUBLIC ${folly_FLAGS})
target_include_directories(folly_runtime PUBLIC ${FOLLY_DIR})
target_link_libraries(folly_runtime glog double-conversion boost fmt fast_float)

# --- nlohmann_json (vendored, header-only) ---------------------------------
# Needed by ReactCxxPlatform's devsupport (PackagerConnection).
add_library(nlohmann_json INTERFACE)
target_include_directories(nlohmann_json INTERFACE ${NLOHMANN_JSON_DIR}/include)

# --- OpenSSL ---------------------------------------------------------------
# Also devsupport: the packager connection speaks WebSocket over TLS.
find_package(OpenSSL REQUIRED)

# --- libcurl ---------------------------------------------------------------
# Backs src/LinuxNetworking.cpp's IHttpClient: the dev server bundle fetch and
# NetworkingModule (fetch/XHR) both go through it. Present on macOS in the SDK;
# on Linux it is the curl development package.
find_package(CURL REQUIRED)

# --- Hermes ----------------------------------------------------------------
# Built separately from RN's pinned tarball; see README. Fantom locates the
# same library out of its Android hermes-engine build dir.
# Two names, because Hermes renamed the library. React Native 0.81's Hermes
# builds `libhermes`; the one 0.87 and main use builds `libhermesvm`. Which one
# is present follows from the version in supported-versions.json, so this looks
# for either rather than being told.
find_library(HERMES_VM_LIBRARY NAMES hermesvm hermes
  HINTS ${HERMES_BUILD_DIR}/API/hermes ${HERMES_BUILD_DIR}/lib
  REQUIRED)
# Both names, for the same reason the find_library above takes both: React
# Native 0.81's own CMakeLists link hermes-engine::libhermes, and 0.87 and main
# link hermes-engine::hermesvm. Only one of them is ever referenced by a given
# React Native, and defining the other costs nothing.
foreach(hermes_alias hermesvm libhermes)
  add_library(hermes-engine::${hermes_alias} INTERFACE IMPORTED)
  set_target_properties(hermes-engine::${hermes_alias} PROPERTIES
    INTERFACE_LINK_LIBRARIES ${HERMES_VM_LIBRARY})
endforeach()

# Hermes' public headers must be globally visible: RN's own hermes/executor and
# hermes/inspector-modern targets include <hermes/hermes.h> without declaring a
# dependency that would carry the path. Fantom does the same with a global
# include_directories() over its prefab-headers dir.
#
# RN's jsi is used rather than Hermes' vendored copy, so API/ and public/ only.
include_directories(
  ${HERMES_DIR}/API
  ${HERMES_DIR}/public
  ${HERMES_BUILD_DIR}/lib/config)
