# Cross-compiles for 64-bit Windows from Linux with clang and lld, against the
# MSVC C runtime and Windows SDK as laid out by `xwin splat`.
#
#   XWIN_DIR=~/.cache/graw-windows/sysroot cmake -DCMAKE_TOOLCHAIN_FILE=cmake/windows-cross.cmake ...

set(CMAKE_SYSTEM_NAME Windows)
set(CMAKE_SYSTEM_VERSION 10.0)
set(CMAKE_SYSTEM_PROCESSOR AMD64)

if(NOT XWIN_DIR)
  set(XWIN_DIR "$ENV{XWIN_DIR}")
endif()
if(NOT IS_DIRECTORY "${XWIN_DIR}/crt/include")
  message(FATAL_ERROR "Set XWIN_DIR to the output of `xwin splat` (missing ${XWIN_DIR}/crt/include)")
endif()
set(XWIN_DIR "${XWIN_DIR}" CACHE PATH "xwin splat output" FORCE)
# try_compile projects get a fresh cache; this carries the path into them.
list(APPEND CMAKE_TRY_COMPILE_PLATFORM_VARIABLES XWIN_DIR)

set(_triple x86_64-pc-windows-msvc)
set(CMAKE_C_COMPILER clang)
set(CMAKE_CXX_COMPILER clang++)
set(CMAKE_C_COMPILER_TARGET ${_triple})
set(CMAKE_CXX_COMPILER_TARGET ${_triple})
set(CMAKE_ASM_COMPILER clang)
set(CMAKE_ASM_COMPILER_TARGET ${_triple})
set(CMAKE_RC_COMPILER llvm-rc)
set(CMAKE_MT llvm-mt)
set(CMAKE_AR llvm-ar)
set(CMAKE_RANLIB llvm-ranlib)
set(CMAKE_LINKER lld-link)

set(_includes
  "${XWIN_DIR}/crt/include"
  "${XWIN_DIR}/sdk/include/ucrt"
  "${XWIN_DIR}/sdk/include/um"
  "${XWIN_DIR}/sdk/include/shared"
  "${XWIN_DIR}/sdk/include/winrt")
# Same baseline as the SDK's own Windows presets. Passing CMAKE_<LANG>_FLAGS on
# the command line replaces these, so add extra flags with CMAKE_<LANG>_FLAGS_RELEASE.
set(_compile_flags "-march=x86-64-v2")
# After clang's own headers, so its intrinsics (emmintrin.h and friends) win
# over the MSVC copies in crt/include.
foreach(_dir IN LISTS _includes)
  string(APPEND _compile_flags " -idirafter \"${_dir}\"")
endforeach()
set(CMAKE_C_FLAGS_INIT "${_compile_flags}")
set(CMAKE_CXX_FLAGS_INIT "${_compile_flags}")
set(CMAKE_RC_FLAGS_INIT "-I \"${XWIN_DIR}/sdk/include/um\" -I \"${XWIN_DIR}/sdk/include/shared\"")

set(_link_flags "-fuse-ld=lld")
foreach(_dir crt/lib/x86_64 sdk/lib/um/x86_64 sdk/lib/ucrt/x86_64)
  string(APPEND _link_flags " -Xlinker \"/libpath:${XWIN_DIR}/${_dir}\"")
endforeach()
set(CMAKE_EXE_LINKER_FLAGS_INIT "${_link_flags}")
set(CMAKE_SHARED_LINKER_FLAGS_INIT "${_link_flags}")
set(CMAKE_MODULE_LINKER_FLAGS_INIT "${_link_flags}")

# xwin fetches only the release CRT. The SDK's DLLs share allocations across
# module boundaries, so all of them use the one DLL runtime.
set(CMAKE_MSVC_RUNTIME_LIBRARY MultiThreadedDLL)
set(CMAKE_TRY_COMPILE_CONFIGURATION Release)

# Never pick up the build machine's Linux headers or libraries.
set(CMAKE_FIND_ROOT_PATH "${XWIN_DIR}")
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE BOTH)
