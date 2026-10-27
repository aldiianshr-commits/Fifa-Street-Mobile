cmake_minimum_required(VERSION 3.20)
project(fsr_core C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

option(FSR_BUILD_TOOLS "Build host tools (fsr-analyze, fsr-recomp, mkxex)" ON)
option(FSR_BUILD_TESTS "Build unit + homebrew differential tests" ON)

# libmspack LZX decoder (LGPL-2.1), as modified by the Xenia project.
add_library(fsr_mspack STATIC third_party/mspack/lzxd.c third_party/mspack/system.c)
target_include_directories(fsr_mspack PUBLIC third_party/mspack)
target_compile_options(fsr_mspack PRIVATE -w)

add_library(fsr_core STATIC
  src/util.cc
  src/crypto.cc
  src/xex.cc
  src/exports.cc
  src/lzx_glue.cc
  src/ppc_decode.cc
  src/recomp.cc
  src/report.cc
  src/runtime.cc
  src/kernel.cc
  src/iso.cc
)
target_include_directories(fsr_core PUBLIC include PRIVATE third_party/xenia)
target_link_libraries(fsr_core PUBLIC fsr_mspack)
target_compile_options(fsr_core PRIVATE -Wall -Wextra -Wno-unused-parameter)

if(FSR_BUILD_TOOLS)
  add_executable(fsr-analyze tools/fsr_tools.cc)
  target_link_libraries(fsr-analyze fsr_core)
  add_executable(fsr-recomp tools/fsr_tools.cc)
  target_link_libraries(fsr-recomp fsr_core)
  add_executable(mkxex tools/mkxex.cc)
  target_link_libraries(mkxex fsr_core)
  add_executable(mkiso tools/mkiso.cc)
  target_link_libraries(mkiso fsr_core)
endif()

if(FSR_BUILD_TESTS)
  enable_testing()
  add_subdirectory(tests)
endif()
