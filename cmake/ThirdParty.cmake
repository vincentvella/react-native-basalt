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
add_library(glog INTERFACE)
target_link_libraries(glog INTERFACE ${GLOG_LIBRARY})
target_compile_definitions(glog INTERFACE GLOG_USE_GLOG_EXPORT)

# RN's Android build has a glog_init target that installs signal handlers.
# Nothing on a host build needs it; Fantom stubs it the same way.
add_library(glog_init INTERFACE)

# --- boost (headers only) --------------------------------------------------
find_path(BOOST_INCLUDE_DIR boost/version.hpp REQUIRED)
add_library(boost INTERFACE)
target_include_directories(boost INTERFACE ${BOOST_INCLUDE_DIR})

# --- double-conversion -----------------------------------------------------
find_library(DOUBLE_CONVERSION_LIBRARY double-conversion REQUIRED)
add_library(double-conversion INTERFACE)
target_link_libraries(double-conversion INTERFACE ${DOUBLE_CONVERSION_LIBRARY})

# --- fmt -------------------------------------------------------------------
find_library(FMT_LIBRARY fmt REQUIRED)
add_library(fmt INTERFACE)
target_link_libraries(fmt INTERFACE ${FMT_LIBRARY})

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
