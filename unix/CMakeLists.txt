# Secondary build for compile-checking on Linux (clang-cl + xwin).
# The primary build is foo_winamp_skins.sln (Visual Studio 2026).
# Source lists are read from the .vcxproj files so both builds stay in sync.
cmake_minimum_required(VERSION 3.25)
set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type")
project(foo_winamp_skins C CXX RC)

get_filename_component(REPO ${CMAKE_SOURCE_DIR}/.. ABSOLUTE)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL")

set(SDK ${REPO}/3rdParty/foobar2000_sdk)
set(WTL_INCLUDE ${REPO}/3rdParty/WTL/Include)

add_compile_definitions(UNICODE _UNICODE WIN32 _WINDOWS)
add_compile_options(/EHsc /Zc:__cplusplus /utf-8 /GR
)
# SDK and WTL are not clang-clean; silence them, our target re-enables warnings.
set_property(DIRECTORY APPEND PROPERTY COMPILE_OPTIONS $<$<NOT:$<STREQUAL:$<TARGET_PROPERTY:NAME>,foo_winamp_skins>>:-Wno-everything>)
if(CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
  include(cmake/case_shim.cmake)
  make_case_shim(${CMAKE_BINARY_DIR}/case-shim)
  include_directories(SYSTEM ${CMAKE_BINARY_DIR}/case-shim)
endif()
include_directories(SYSTEM ${SDK} ${SDK}/foobar2000)
include_directories(SYSTEM ${WTL_INCLUDE})

# Read <ClCompile Include="..."> entries from a vcxproj.
function(vcxproj_sources out vcxproj)
  file(READ ${vcxproj} _xml)
  set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${vcxproj})
  string(REGEX MATCHALL "<ClCompile Include=\"[^\"]+\"" _items "${_xml}")
  get_filename_component(_dir ${vcxproj} DIRECTORY)
  set(_srcs)
  foreach(_i ${_items})
    string(REGEX REPLACE "<ClCompile Include=\"([^\"]+)\"" "\\1" _f "${_i}")
    string(REPLACE "\\" "/" _f "${_f}")
    list(APPEND _srcs ${_dir}/${_f})
  endforeach()
  set(${out} ${_srcs} PARENT_SCOPE)
endfunction()

function(sdk_lib name vcxproj)
  vcxproj_sources(_srcs ${vcxproj})
  add_library(${name} STATIC ${_srcs})
endfunction()

sdk_lib(pfc                      ${SDK}/pfc/pfc.vcxproj)
sdk_lib(foobar2000_SDK           ${SDK}/foobar2000/SDK/foobar2000_SDK.vcxproj)
sdk_lib(foobar2000_component_client ${SDK}/foobar2000/foobar2000_component_client/foobar2000_component_client.vcxproj)

if(CMAKE_SYSTEM_PROCESSOR STREQUAL "x86")
  set(FB2K_PLATFORM Win32)
else()
  set(FB2K_PLATFORM x64)
endif()

# --- the component ---
vcxproj_sources(COMPONENT_SRCS ${REPO}/foo_winamp_skins.vcxproj)
add_library(foo_winamp_skins SHARED ${COMPONENT_SRCS} ${REPO}/src/foo_winamp_skins.rc)
target_include_directories(foo_winamp_skins PRIVATE ${REPO}/src ${REPO}/3rdParty/miniz)
target_compile_definitions(foo_winamp_skins PRIVATE MINIZ_NO_STDIO MINIZ_NO_ARCHIVE_WRITING_APIS MINIZ_NO_ZLIB_COMPATIBLE_NAMES)
set_source_files_properties(${REPO}/3rdParty/miniz/miniz.c PROPERTIES COMPILE_OPTIONS "-Wno-everything")
target_compile_options(foo_winamp_skins PRIVATE -W4 -Wno-unused-parameter
  -Wno-microsoft-include -Wno-pragma-pack -Wno-unknown-pragmas -Wno-nonportable-include-path -Wno-unused-command-line-argument)
target_link_libraries(foo_winamp_skins PRIVATE
  foobar2000_component_client foobar2000_SDK pfc
  ${SDK}/foobar2000/shared/shared-${FB2K_PLATFORM}.lib
  user32 gdi32 shell32 comctl32 comdlg32 ole32 uuid shlwapi winmm msimg32 windowscodecs)
