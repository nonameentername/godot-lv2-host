cmake_minimum_required(VERSION 3.15)

set(CMAKE_OSX_DEPLOYMENT_TARGET "11.0" CACHE STRING "" FORCE)

set(VCPKG_FEATURE_FLAGS versions)
set(X_VCPKG_APPLOCAL_DEPS_INSTALL ON)
set(VCPKG_OVERLAY_TRIPLETS ${CMAKE_CURRENT_SOURCE_DIR}/platform/vcpkg/triplets)
set(VCPKG_OVERLAY_PORTS ${CMAKE_CURRENT_SOURCE_DIR}/platform/vcpkg/ports)
set(CMAKE_TOOLCHAIN_FILE ${CMAKE_CURRENT_SOURCE_DIR}/modules/vcpkg/scripts/buildsystems/vcpkg.cmake CACHE STRING "Vcpkg toolchain file")

if(OSXCROSS_TARGET)
    if(CMAKE_OSX_ARCHITECTURES STREQUAL "x86_64")
        set(VCPKG_TARGET_TRIPLET "x64-osxcross")
        set(VCPKG_CHAINLOAD_TOOLCHAIN_FILE ${CMAKE_CURRENT_SOURCE_DIR}/platform/osxcross/osx-x64.cmake)
    elseif(CMAKE_OSX_ARCHITECTURES STREQUAL "arm64")
        set(VCPKG_TARGET_TRIPLET "arm64-osxcross")
        set(VCPKG_CHAINLOAD_TOOLCHAIN_FILE ${CMAKE_CURRENT_SOURCE_DIR}/platform/osxcross/osx-arm64.cmake)
    endif()

    if(CMAKE_BUILD_TYPE STREQUAL "Debug")
        set(GODOT_LIBRARIES_SUFFIX ".macos.editor.dev.${CMAKE_OSX_ARCHITECTURES}")
        set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -DDEBUG")
    else()
        set(GODOT_LIBRARIES_SUFFIX ".macos.editor.${CMAKE_OSX_ARCHITECTURES}")
    endif()
elseif (CMAKE_SYSTEM_NAME STREQUAL "MinGW" AND UNIX)
    set(VCPKG_TARGET_TRIPLET "x64-mingw-static")
    set(VCPKG_INSTALL_OPTIONS "--allow-unsupported")
    set(VCPKG_CHAINLOAD_TOOLCHAIN_FILE ${CMAKE_CURRENT_SOURCE_DIR}/modules/vcpkg/scripts/toolchains/mingw.cmake)
    set(VCPKG_CMAKE_SYSTEM_NAME MinGW)
    set(VCPKG_TARGET_ARCHITECTURE x64)
elseif(UNIX)
    set(VCPKG_TARGET_TRIPLET "x64-linux")

    if(CMAKE_BUILD_TYPE STREQUAL "Debug")
        set(GODOT_LIBRARIES_SUFFIX ".linuxbsd.editor.dev.x86_64")
        set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -DDEBUG")
    else()
        set(GODOT_LIBRARIES_SUFFIX ".linuxbsd.editor.x86_64")
    endif()
endif()

project(godot-lv2-host VERSION 0.1.0 LANGUAGES C CXX)

message(STATUS "VCPKG_TARGET_TRIPLET: ${VCPKG_TARGET_TRIPLET}")
message(STATUS "CMAKE_TOOLCHAIN_FILE: ${CMAKE_TOOLCHAIN_FILE}")

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Debug CACHE STRING "Build type" FORCE)
endif()

option(ENABLE_ASAN "Enable AddressSanitizer (address,undefined)" OFF)

set(SOURCE
    ${CMAKE_CURRENT_SOURCE_DIR}/src/host/main.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/lv2_host.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/lv2_circular_buffer.cpp
)
set(HEADER
    ${CMAKE_CURRENT_SOURCE_DIR}/src/lv2_host.h
)

add_executable(lv2-host ${SOURCE} ${HEADER})

set_target_properties(lv2-host PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

target_include_directories(lv2-host PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/src/host
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)

find_package(Lilv CONFIG QUIET)

if(Lilv_FOUND)
    target_link_libraries(lilv-host PRIVATE Lilv::Lilv)
else()
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(LILV REQUIRED IMPORTED_TARGET lilv-0)
    target_link_libraries(lv2-host PRIVATE PkgConfig::LILV)
endif()

find_package(SndFile CONFIG REQUIRED)
target_link_libraries(lv2-host PRIVATE SndFile::sndfile)

if(ENABLE_ASAN AND UNIX)
    message(STATUS "Building with AddressSanitizer instrumentation")
    target_compile_options(lv2-host PRIVATE -fsanitize=address -fno-omit-frame-pointer -g)
    target_link_options(lv2-host PRIVATE -fsanitize=address)
endif()
