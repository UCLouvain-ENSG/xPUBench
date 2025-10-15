cmake_minimum_required(VERSION 3.10)

execute_process(COMMAND cat /opt/mellanox/doca/applications/VERSION OUTPUT_VARIABLE DOCA_VERSION)
string(STRIP ${DOCA_VERSION} DOCA_VERSION)

message(STATUS "DOCA_VERSION: ${DOCA_VERSION}")

project(DOCA_SAMPLE VERSION ${DOCA_VERSION})

# whether on DPU or HOST
# this option is useless now
option(DPU OFF)

if (CMAKE_SYSTEM_PROCESSOR MATCHES "aarch64")
    message(STATUS "Compile on DPU")
    message(STATUS "Output Dir is /bin_dpu")
    set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}/bin_dpu)
    set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}/build_dpu)
    set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}/lib_dpu)

elseif(CMAKE_SYSTEM_PROCESSOR MATCHES "x86_64")
    message(STATUS "Compile on HOST")
    message(STATUS "Output Dir is /bin_host")
    set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}/bin_host)
    set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}/build_host)
    set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}/lib_host)
else()
    # 在其他架构下的逻辑代码
    message("Unknown architecture detected")
    return()
endif ()


set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED True)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)


set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -O3 -DALLOW_EXPERIMENTAL_API -std=c++20 -Wall -Wextra -Werror -pedantic")
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wold-style-cast -Wno-unused-function -Wno-missing-braces")
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wno-address-of-packed-member -Wno-nested-anon-types -Wno-keyword-macro -Wno-deprecated-declarations")

add_definitions(-DDOCA_ALLOW_EXPERIMENTAL_API)

find_package(PkgConfig REQUIRED)
pkg_check_modules(DOCA IMPORTED_TARGET doca)
pkg_check_modules(LIBFLEXIO IMPORTED_TARGET libflexio)


find_package(gflags REQUIRED)

set(Protobuf_USE_STATIC_LIBS ON)
find_package(Protobuf REQUIRED)
if (Protobuf_FOUND)
    message(STATUS "Using Protocol Buffers ${Protobuf_VERSION}")
    #  message(STATUS "${Protobuf_INCLUDE_DIRS}")
    #  message(STATUS "${Protobuf_LITE_LIBRARIES}")
endif ()

if (DOCA_FOUND)
    message(STATUS "DOCA_LIB found")
else ()
    message(FATAL_ERROR "DOCA_LIB not found")
    return()
endif ()

if (LIBFLEXIO_FOUND)
    message(STATUS "LIBFLEXIO found")
else ()
    message(FATAL_ERROR "LIBFLEXIO not found")
    return()
endif ()

#include: utils
include_directories("${CMAKE_SOURCE_DIR}/utils")

#include: wrapper
include_directories("${CMAKE_SOURCE_DIR}/wrapper_flexio")

#include: DOCA
include_directories("${DOCA_INCLUDE_DIRS}")
set(UTILSOURCES
        ${CMAKE_SOURCE_DIR}/utils/numautil.cpp
)


file(GLOB WRAPPERSOURCES ${CMAKE_SOURCE_DIR}/wrapper_flexio/*.cpp)
file(GLOB WRAPPERSOURCES ${CMAKE_SOURCE_DIR}/wrapper/*.cpp)
list(APPEND UTILSOURCES ${WRAPPERSOURCES})

set(LIBRARIES ${LIBRARIES} PkgConfig::DOCA PkgConfig::LIBFLEXIO gflags numa)

add_subdirectory(${CMAKE_SOURCE_DIR}/flexio/dpa_xpu)
