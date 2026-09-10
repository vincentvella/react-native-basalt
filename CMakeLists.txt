# The development build of this repository.
#
# Everything native now lives in the npm package, at
# packages/react-native-linux/native, so that installing the package brings the
# host's sources with it and `react-native run-linux` can build one. That is the
# same shape react-native-windows uses, where the package *is* the vnext
# directory.
#
# This file exists so that the way this repository has always been built keeps
# working:
#
#   cmake -B build -DRN_DIR=../react-native/packages/react-native
#   cmake --build build
#
# It contributes two things the package cannot assume: that third_party lives at
# the root of a checkout, and that `build/rn_linux_host` is where the binary
# should appear. Both are named here rather than inside the package, because
# inside the package they would be wrong for an app.

cmake_minimum_required(VERSION 3.13)
project(react_native_linux_dev NONE)

if(NOT DEFINED RN_LINUX_THIRD_PARTY)
  set(RN_LINUX_THIRD_PARTY ${CMAKE_CURRENT_SOURCE_DIR}/third_party)
endif()

# RN_DIR is documented as relative to this directory, and add_subdirectory would
# otherwise resolve it against the package.
if(DEFINED RN_DIR)
  get_filename_component(RN_DIR "${RN_DIR}" ABSOLUTE BASE_DIR "${CMAKE_CURRENT_SOURCE_DIR}")
endif()

add_subdirectory(packages/react-native-linux/native native)
