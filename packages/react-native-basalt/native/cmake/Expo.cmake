# Expo's core runtime, from the app's own expo-modules-core.
#
# Nothing is vendored and nothing is fetched. expo-modules-core ships its
# portable C++ in the npm package, under common/cpp, and it depends on JSI and a
# call invoker and nothing else -- no JNI, no Objective-C. So the app's own copy
# compiles, at the app's own Expo version, which keeps Expo's C++ and JavaScript
# in step for the same reason building against the app's React Native does.
#
# See src/ExpoRuntime.cpp for what is installed and what is deliberately not.

if(NOT DEFINED BASALT_EXPO_MODULES_CORE)
  return()
endif()

get_filename_component(EXPO_CORE_DIR "${BASALT_EXPO_MODULES_CORE}" ABSOLUTE)
set(EXPO_CPP_DIR ${EXPO_CORE_DIR}/common/cpp)

if(NOT EXISTS ${EXPO_CPP_DIR}/EventEmitter.cpp)
  message(FATAL_ERROR
          "no expo-modules-core C++ at ${EXPO_CPP_DIR}.\n"
          "BASALT_EXPO_MODULES_CORE should name an installed "
          "expo-modules-core, the one in the app's node_modules.")
endif()

# The runtime classes and their JSI helpers, and deliberately not everything
# under common/cpp. `fabric/` holds Expo's own Fabric view components, which are
# a separate port with a separate purpose: they need the whole renderer include
# set, and nothing installs them here because no Expo view exists on this
# platform. `tests/` brings a test framework this build has no reason to have.
file(GLOB EXPO_CORE_SRC CONFIGURE_DEPENDS
        ${EXPO_CPP_DIR}/*.cpp
        ${EXPO_CPP_DIR}/JSI/*.cpp)
list(FILTER EXPO_CORE_SRC EXCLUDE REGEX "/tests?/")

add_library(expo_core OBJECT ${EXPO_CORE_SRC})
# SYSTEM, and warnings off: these are somebody else's sources held to somebody
# else's warning set, and this project's -Wall -Wextra is about its own code.
target_include_directories(expo_core SYSTEM PUBLIC
        ${EXPO_CPP_DIR} ${EXPO_CPP_DIR}/JSI
        ${RN_DIR}/ReactCommon ${RN_DIR}/ReactCommon/jsi ${FOLLY_DIR})
target_compile_options(expo_core PRIVATE -w)
target_link_libraries(expo_core folly_runtime glog)

message(STATUS "Expo core from ${EXPO_CORE_DIR}")
add_compile_definitions(BASALT_HAS_EXPO=1)
