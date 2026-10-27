# Tests need clang with the PowerPC backend and ld.lld to build the homebrew
# programs from source.
find_program(FSR_CLANG NAMES clang clang-18 clang-17 clang-19 REQUIRED)
find_program(FSR_LLD NAMES ld.lld ld.lld-18 ld.lld-17 ld.lld-19 REQUIRED)

set(HB_DIR ${CMAKE_CURRENT_BINARY_DIR}/hb)
set(HB_SRC ${CMAKE_CURRENT_SOURCE_DIR}/homebrew)
set(HB_OUT ${HB_DIR}/hb64.xex ${HB_DIR}/hb32.xex ${HB_DIR}/hb64_enc.xex ${HB_DIR}/hb64.syms ${HB_DIR}/hb32.syms)
add_custom_command(
  OUTPUT ${HB_OUT}
  COMMAND ${CMAKE_COMMAND} -E env CLANG=${FSR_CLANG} LLD=${FSR_LLD}
          bash ${HB_SRC}/build_homebrew.sh $<TARGET_FILE_DIR:mkxex> ${HB_DIR}
  DEPENDS mkxex ${HB_SRC}/selftest.c ${HB_SRC}/vectest.c ${HB_SRC}/demo.c ${HB_SRC}/crt.c ${HB_SRC}/thunks.S ${HB_SRC}/vmx128.S ${HB_SRC}/link.ld
          ${HB_SRC}/build_homebrew.sh
  COMMENT "Building homebrew PowerPC test XEX files")

foreach(m hb64 hb32)
  set(GEN ${CMAKE_CURRENT_BINARY_DIR}/gen_${m})
  set(${m}_GEN ${GEN}/${m}_decls.h ${GEN}/${m}_funcs_000.cpp ${GEN}/${m}_table.cpp)
  add_custom_command(
    OUTPUT ${${m}_GEN}
    COMMAND $<TARGET_FILE:fsr-recomp> ${HB_DIR}/${m}.xex --out ${GEN} --name ${m} --syms ${HB_DIR}/${m}.syms
            --export fsr_selftest --export fsr_frame --export fsr_vectest --export fsr_vectest_dump --export fsr_vmx128_test
    DEPENDS fsr-recomp ${HB_DIR}/${m}.xex ${HB_DIR}/${m}.syms
    COMMENT "Recompiling ${m}.xex to C++")
endforeach()

add_executable(unit_tests unit_tests.cc ${HB_OUT})
target_link_libraries(unit_tests fsr_core)
add_test(NAME unit_tests COMMAND unit_tests ${HB_DIR} $<TARGET_FILE_DIR:mkiso>)

# Native build of the very same homebrew sources, symbols renamed.
add_library(hb_native STATIC ${HB_SRC}/selftest.c ${HB_SRC}/demo.c ${HB_SRC}/vectest.c)
target_compile_definitions(hb_native PRIVATE fsr_selftest=native_selftest fsr_frame=native_frame fsr_demo_reset=native_demo_reset fsr_vectest=native_vectest fsr_vectest_dump=native_vectest_dump
  XamInputGetState=native_XamInputGetState DbgPrint=native_DbgPrint)
target_compile_options(hb_native PRIVATE -O2 -ffp-contract=off -fno-math-errno -fno-strict-aliasing)

add_library(hb_native32 STATIC ${HB_SRC}/selftest.c)
target_compile_definitions(hb_native32 PRIVATE FSR_NO_WIDE fsr_selftest=native32_selftest)
target_compile_options(hb_native32 PRIVATE -O2 -ffp-contract=off -fno-math-errno -fno-strict-aliasing)

add_executable(homebrew_test homebrew_test.cc ${hb64_GEN} ${hb32_GEN})
target_include_directories(homebrew_test PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/gen_hb64 ${CMAKE_CURRENT_BINARY_DIR}/gen_hb32)
target_compile_options(homebrew_test PRIVATE -ffp-contract=off)
target_link_libraries(homebrew_test fsr_core hb_native hb_native32)
add_test(NAME homebrew_test COMMAND homebrew_test ${HB_DIR})
