cmake_minimum_required(VERSION 3.16)

# anticheat — MetaMod:Source 2 plugin (CS2)
# Output: build/addons/anticheat/bin/linuxsteamrt64/anticheat.so

project(anticheat LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(AC_OPTIMIZE "Enable -O3 in Release" ON)

function(ac_try_path OUT_VAR)
  set(${OUT_VAR} "" PARENT_SCOPE)
  foreach(candidate IN LISTS ARGN)
    if(candidate AND EXISTS "${candidate}")
      set(${OUT_VAR} "${candidate}" PARENT_SCOPE)
      return()
    endif()
  endforeach()
endfunction()

if(NOT HL2SDK_PATH AND DEFINED ENV{HL2SDKCS2})
  set(HL2SDK_PATH "$ENV{HL2SDKCS2}")
endif()
if(NOT MMS_PATH AND DEFINED ENV{MMSOURCE112})
  set(MMS_PATH "$ENV{MMSOURCE112}")
elseif(NOT MMS_PATH AND DEFINED ENV{MMSOURCE_DEV})
  set(MMS_PATH "$ENV{MMSOURCE_DEV}")
endif()

set(_ac_roots
  "${CMAKE_SOURCE_DIR}"
  "${CMAKE_SOURCE_DIR}/.."
  "${CMAKE_SOURCE_DIR}/../.."
)
foreach(_root IN LISTS _ac_roots)
  list(APPEND _ac_hl2sdk_candidates "${_root}/deps/hl2sdk-cs2")
  list(APPEND _ac_mms_candidates "${_root}/deps/metamod-source")
endforeach()

if(NOT HL2SDK_PATH OR NOT EXISTS "${HL2SDK_PATH}/public/tier0/dbg.h")
  ac_try_path(_found_hl2sdk ${_ac_hl2sdk_candidates})
  if(_found_hl2sdk)
    set(HL2SDK_PATH "${_found_hl2sdk}")
  endif()
endif()
if(NOT MMS_PATH OR NOT EXISTS "${MMS_PATH}/core/ISmmPlugin.h")
  ac_try_path(_found_mms ${_ac_mms_candidates})
  if(_found_mms)
    set(MMS_PATH "${_found_mms}")
  endif()
endif()

set(HL2SDK_PATH "${HL2SDK_PATH}" CACHE PATH "Path to hl2sdk-cs2")
set(MMS_PATH "${MMS_PATH}" CACHE PATH "Path to metamod-source")

if(NOT HL2SDK_PATH OR NOT EXISTS "${HL2SDK_PATH}/public/tier0/dbg.h")
  message(FATAL_ERROR "HL2SDK not found. Run: bash build_cmake.sh")
endif()
if(NOT MMS_PATH OR NOT EXISTS "${MMS_PATH}/core/ISmmPlugin.h")
  message(FATAL_ERROR "Metamod:Source not found. Run: bash build_cmake.sh")
endif()

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

set(MMS_CORE "${MMS_PATH}/core")

add_library(anticheat_flags INTERFACE)

target_compile_definitions(anticheat_flags INTERFACE
  META_IS_SOURCE2
  SOURCE_ENGINE=25
  SE_CS2=25
  GAME_DLL
  RAD_TELEMETRY_DISABLED
  stricmp=strcasecmp
  _stricmp=strcasecmp
  _snprintf=snprintf
  _vsnprintf=vsnprintf
  HAVE_STDINT_H
  GNUC
)

if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
  target_compile_options(anticheat_flags INTERFACE
    -pipe
    -fno-strict-aliasing
    -Wall
    -Wno-uninitialized
    -Wno-parentheses
    -Wno-unused
    -Wno-switch
    -Wno-sign-compare
    -Wno-attributes
    -Wno-invalid-offsetof
    -Wno-non-virtual-dtor
    -Wno-overloaded-virtual
    -fno-threadsafe-statics
    -fvisibility=hidden
    -fvisibility-inlines-hidden
    -fno-omit-frame-pointer
    $<$<COMPILE_LANGUAGE:CXX>:-std=c++17>
  )
endif()

if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
  target_compile_definitions(anticheat_flags INTERFACE
    LINUX
    _LINUX
    POSIX
    _FILE_OFFSET_BITS=64
    X64BITS
    PLATFORM_64BITS
    COMPILER_GCC
    _GLIBCXX_USE_CXX11_ABI=0
  )
  target_compile_options(anticheat_flags INTERFACE -fPIC -msse)
  if(CMAKE_BUILD_TYPE STREQUAL "Release" AND AC_OPTIMIZE)
    target_compile_options(anticheat_flags INTERFACE -O3)
    target_compile_definitions(anticheat_flags INTERFACE NDEBUG)
  endif()
endif()

target_include_directories(anticheat_flags INTERFACE
  "${CMAKE_CURRENT_SOURCE_DIR}/src"
  "${MMS_CORE}"
  "${MMS_CORE}/sourcehook"
  "${HL2SDK_PATH}/thirdparty/protobuf-3.21.8/src"
  "${HL2SDK_PATH}/public"
  "${HL2SDK_PATH}/public/engine"
  "${HL2SDK_PATH}/public/mathlib"
  "${HL2SDK_PATH}/public/tier0"
  "${HL2SDK_PATH}/public/tier1"
  "${HL2SDK_PATH}/public/entity2"
  "${HL2SDK_PATH}/public/game/server"
  "${HL2SDK_PATH}/public/schemasystem"
  "${HL2SDK_PATH}/game/shared"
  "${HL2SDK_PATH}/game/server"
  "${HL2SDK_PATH}/common"
)

set(AC_PROTO_FILES
  "${HL2SDK_PATH}/common/valveextensions.proto"
  "${HL2SDK_PATH}/common/network_connection.proto"
  "${HL2SDK_PATH}/common/networkbasetypes.proto"
)

if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
  set(AC_PROTOC "${HL2SDK_PATH}/devtools/bin/linux/protoc")
else()
  set(AC_PROTOC "${HL2SDK_PATH}/devtools/bin/protoc.exe")
endif()

if(NOT EXISTS "${AC_PROTOC}")
  message(FATAL_ERROR "protoc not found: ${AC_PROTOC}")
endif()

set(AC_PROTO_GEN_DIR "${CMAKE_CURRENT_BINARY_DIR}/generated")
file(MAKE_DIRECTORY "${AC_PROTO_GEN_DIR}")

set(AC_PROTO_SRCS "")
set(AC_PROTO_HDRS "")

foreach(proto ${AC_PROTO_FILES})
  get_filename_component(proto_name "${proto}" NAME_WE)
  set(out_cc "${AC_PROTO_GEN_DIR}/${proto_name}.pb.cc")
  set(out_h "${AC_PROTO_GEN_DIR}/${proto_name}.pb.h")
  list(APPEND AC_PROTO_SRCS "${out_cc}")
  list(APPEND AC_PROTO_HDRS "${out_h}")

  add_custom_command(
    OUTPUT "${out_cc}" "${out_h}"
    COMMAND "${AC_PROTOC}"
      --cpp_out=${AC_PROTO_GEN_DIR}
      "-I${HL2SDK_PATH}/common"
      "-I${HL2SDK_PATH}/thirdparty/protobuf-3.21.8/src"
      "${proto}"
    DEPENDS "${proto}"
    COMMENT "Generating ${proto_name}.pb.cc"
    VERBATIM
  )
endforeach()

add_custom_target(anticheat_protoc DEPENDS ${AC_PROTO_SRCS} ${AC_PROTO_HDRS})

set(AC_PLUGIN_SRCS
  src/plugin.cpp
  src/schema.cpp
  src/vtable_finder.cpp
  src/players.cpp
  src/events.cpp
  src/config.cpp
  src/staff_exempt.cpp
  src/anticheat_core.cpp
  src/integration/admin_bridge.cpp
  src/integration/backend_client.cpp
  src/modules/aim_analyzer.cpp
  src/modules/wallhack_detector.cpp
  src/modules/movement_analyzer.cpp
  src/modules/statistics_tracker.cpp
  src/modules/integrity_checker.cpp
  src/modules/suspicion_scorer.cpp
  src/modules/fps_drop_detector.cpp
  src/modules/game_file_scanner.cpp
  src/modules/shot_tracker.cpp
)

set(AC_SDK_SRCS
  "${HL2SDK_PATH}/public/tier0/memoverride.cpp"
  "${HL2SDK_PATH}/entity2/entitysystem.cpp"
  "${HL2SDK_PATH}/entity2/entityidentity.cpp"
  "${HL2SDK_PATH}/entity2/entitykeyvalues.cpp"
  "${HL2SDK_PATH}/tier1/convar.cpp"
  "${HL2SDK_PATH}/tier1/keyvalues3.cpp"
)

add_library(anticheat SHARED
  ${AC_PLUGIN_SRCS}
  ${AC_SDK_SRCS}
  ${AC_PROTO_SRCS}
)

add_dependencies(anticheat anticheat_protoc)

target_link_libraries(anticheat PRIVATE anticheat_flags)
target_include_directories(anticheat PRIVATE "${AC_PROTO_GEN_DIR}")

set_target_properties(anticheat PROPERTIES
  PREFIX ""
  OUTPUT_NAME "anticheat"
)

if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
  set(HL2SDK_LIB "${HL2SDK_PATH}/lib/linux64")
  set(AC_SDK_LIBS
    "${HL2SDK_LIB}/mathlib.a"
    "${HL2SDK_LIB}/interfaces.a"
    "${HL2SDK_LIB}/release/libprotobuf.a"
  )
  foreach(lib ${AC_SDK_LIBS})
    if(NOT EXISTS "${lib}")
      message(FATAL_ERROR "Missing HL2SDK library: ${lib}")
    endif()
  endforeach()

  target_link_libraries(anticheat PRIVATE
    ${AC_SDK_LIBS}
    dl
    pthread
    m
  )

  target_link_options(anticheat PRIVATE
    -static-libgcc
    -static-libstdc++
  )

  set(AC_OUT_DIR "${CMAKE_BINARY_DIR}/addons/anticheat/bin/linuxsteamrt64")
  set_target_properties(anticheat PROPERTIES
    LIBRARY_OUTPUT_DIRECTORY "${AC_OUT_DIR}"
  )

  set(AC_PKG_METAMOD "${CMAKE_BINARY_DIR}/addons/metamod")
  set(AC_PKG_CFG "${CMAKE_BINARY_DIR}/addons/anticheat/configs")
  add_custom_target(anticheat_package ALL
    DEPENDS anticheat
    COMMAND ${CMAKE_COMMAND} -E make_directory "${AC_PKG_METAMOD}"
    COMMAND ${CMAKE_COMMAND} -E make_directory "${AC_PKG_CFG}"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
      "${CMAKE_SOURCE_DIR}/anticheat.vdf" "${AC_PKG_METAMOD}/"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
      "${CMAKE_SOURCE_DIR}/config/anticheat_config.json" "${AC_PKG_CFG}/"
    COMMENT "Packaging addons/anticheat layout"
  )
else()
  message(FATAL_ERROR "Unsupported platform: ${CMAKE_SYSTEM_NAME} (use WSL/Linux build_cmake.sh)")
endif()

message(STATUS "anticheat: HL2SDK=${HL2SDK_PATH}")
message(STATUS "anticheat: MMS=${MMS_PATH}")
