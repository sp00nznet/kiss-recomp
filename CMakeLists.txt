# =============================================================================
# KISS Psycho Circus: The Nightmare Child (1.13) - static recompilation host (32-bit, MSVC x86)
#
#   py -3 tools/rebase.py && py -3 tools/catalog.py && py -3 run_lift.py
#   build.cmd                # vcvarsall x86 + CMake + Ninja -> build/kiss.exe
#   build\kiss.exe --run -- +windowed 1
# =============================================================================
cmake_minimum_required(VERSION 3.20)
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
project(kiss C)

if(NOT CMAKE_SIZEOF_VOID_P EQUAL 4)
  message(FATAL_ERROR "the host must be 32-bit: use build.cmd (vcvarsall x86)")
endif()
if(NOT MSVC)
  message(FATAL_ERROR "runtime/native32 is MSVC x86 inline assembly")
endif()
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE RelWithDebInfo)
endif()

set(PCRECOMP "${CMAKE_SOURCE_DIR}/../pcrecomp" CACHE PATH "pcrecomp checkout")
file(GLOB_RECURSE GEN_SOURCES CONFIGURE_DEPENDS "${CMAKE_SOURCE_DIR}/src/recomp/gen/*.c")
if(NOT GEN_SOURCES)
  message(FATAL_ERROR "src/recomp/gen is empty -- run `py -3 run_lift.py` first")
endif()

add_executable(kiss
  src/runtime/host.c
  ${PCRECOMP}/runtime/native32/native32.c
  ${PCRECOMP}/runtime/recomp32/image_loader.c
  ${PCRECOMP}/runtime/recomp32/recomp_trace.c
  ${PCRECOMP}/runtime/replay/replay.c
  ${GEN_SOURCES}
)
target_include_directories(kiss PRIVATE
  src/runtime src/recomp/gen ${PCRECOMP}/runtime ${PCRECOMP}/runtime/native32 ${PCRECOMP}/runtime/recomp32)
target_compile_definitions(kiss PRIVATE RECOMP_RETADDR=0u _CRT_SECURE_NO_WARNINGS
                                     RECOMP_FLAT_MEMORY RECOMP_LOCAL_REGS)
# Linked high and not relocatable, so the guest's 0x00400000 and
# 0x01CA0000, 0x10000000..0x10718000 and 0x20000000..0x201EB000 are free. /NXCOMPAT is load-bearing: a native call
# into guest code faults, and that fault is the callback trampoline.
target_link_options(kiss PRIVATE /BASE:0x60000000 /FIXED:NO /DYNAMICBASE:NO
                                 /NXCOMPAT /LARGEADDRESSAWARE /STACK:0x40000
                                 /MANIFESTUAC:NO /MAP)
# /Od by default: 28,000 lifted functions build in seconds, and the farm
# stalled on Shogo's biggest generated chunks at /O2. -DGEN_OPT=/O2 for speed.
set(GEN_OPT "/Od" CACHE STRING "optimization for the generated C")
set_source_files_properties(${GEN_SOURCES} PROPERTIES COMPILE_OPTIONS "/w;${GEN_OPT}")
set_source_files_properties(src/runtime/host.c PROPERTIES COMPILE_OPTIONS "/W3")
target_link_libraries(kiss PRIVATE user32 gdi32 kernel32 winmm)

option(KISS_TRACE "record a ring buffer of lifted function entries" OFF)
if(KISS_TRACE)
  target_compile_definitions(kiss PRIVATE RECOMP_TRACE=1)
endif()
