# Cross-compile toolchain: clang-cl + lld-link against an xwin sysroot.
# Usage: cmake -G Ninja -B build-x64 -DCMAKE_TOOLCHAIN_FILE=cmake/clang-cl-xwin.cmake -DXWIN_ARCH=x86_64
#        cmake -G Ninja -B build-x86 -DCMAKE_TOOLCHAIN_FILE=cmake/clang-cl-xwin.cmake -DXWIN_ARCH=x86
set(CMAKE_SYSTEM_NAME Windows)
set(CMAKE_SYSTEM_VERSION 10.0)

if(NOT XWIN_ARCH)
  set(XWIN_ARCH x86_64)
endif()
if(NOT XWIN_ROOT)
  set(XWIN_ROOT "$ENV{HOME}/.xwin")
endif()
list(APPEND CMAKE_TRY_COMPILE_PLATFORM_VARIABLES XWIN_ARCH XWIN_ROOT)
# xwin splats only release CRT libs; compiler checks must not link msvcrtd.lib.
set(CMAKE_TRY_COMPILE_CONFIGURATION Release)

if(XWIN_ARCH STREQUAL "x86")
  set(CMAKE_SYSTEM_PROCESSOR x86)
  set(_triple i686-pc-windows-msvc)
else()
  set(CMAKE_SYSTEM_PROCESSOR AMD64)
  set(_triple x86_64-pc-windows-msvc)
endif()

set(CMAKE_C_COMPILER clang-cl)
set(CMAKE_CXX_COMPILER clang-cl)
set(CMAKE_RC_COMPILER llvm-rc)
set(CMAKE_LINKER lld-link)
set(CMAKE_AR llvm-lib)
set(CMAKE_MT llvm-mt)
set(CMAKE_C_COMPILER_TARGET ${_triple})
set(CMAKE_CXX_COMPILER_TARGET ${_triple})

set(_inc "/imsvc ${XWIN_ROOT}/crt/include /imsvc ${XWIN_ROOT}/sdk/include/ucrt /imsvc ${XWIN_ROOT}/sdk/include/um /imsvc ${XWIN_ROOT}/sdk/include/shared /imsvc ${XWIN_ROOT}/sdk/include/winrt")
set(CMAKE_C_FLAGS_INIT "${_inc}")
set(CMAKE_CXX_FLAGS_INIT "${_inc}")
set(CMAKE_RC_FLAGS_INIT "-I ${XWIN_ROOT}/crt/include -I ${XWIN_ROOT}/sdk/include/um -I ${XWIN_ROOT}/sdk/include/shared")

set(_libs "/libpath:${XWIN_ROOT}/crt/lib/${XWIN_ARCH} /libpath:${XWIN_ROOT}/sdk/lib/um/${XWIN_ARCH} /libpath:${XWIN_ROOT}/sdk/lib/ucrt/${XWIN_ARCH}")
set(CMAKE_EXE_LINKER_FLAGS_INIT "${_libs}")
set(CMAKE_SHARED_LINKER_FLAGS_INIT "${_libs}")
set(CMAKE_MODULE_LINKER_FLAGS_INIT "${_libs}")

set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
