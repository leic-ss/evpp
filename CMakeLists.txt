cmake_minimum_required (VERSION 2.8)

project (safe-evpp C CXX)

SET(CMAKE_VERBOSE_MAKEFILE on)

if (UNIX)
    if(NOT CMAKE_BUILD_TYPE)
        set(CMAKE_BUILD_TYPE "Release")
    endif()

    set (CXX_FLAGS
         -g
         -fPIC
         -Wall
         -D__STDC_FORMAT_MACROS
         -Wno-unused-parameter
         -Wno-unused-function
         -Wunused-variable
         -Wunused-value
         -Wshadow
         -Wcast-qual
         -Wcast-align
         -Wwrite-strings
         -Wsign-compare
         -Winvalid-pch
         -fms-extensions
         -Wfloat-equal
         -Wextra
         -std=c++11
    )
    include_directories (${PROJECT_SOURCE_DIR}
                         ${PROJECT_SOURCE_DIR}/3rdparty)
else (UNIX)
set (CXX_FLAGS -DWIN32 -D_WIN32 -EHsc)
    set(VcpkgRoot ${_VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET})
    include_directories(${VcpkgRoot}/include
                        ${PROJECT_SOURCE_DIR}
                        ${PROJECT_SOURCE_DIR}/3rdparty/wininclude)

    # On windows, Debug mode
    link_directories(${VcpkgRoot}/debug/lib ${VcpkgRoot}/debug/lib/manual-link)

    # On Windows, Release mode
    # link_directories(${VcpkgRoot}/lib ${VcpkgRoot}/lib/manual-link)
endif (UNIX)

string (REPLACE ";" " " CMAKE_CXX_FLAGS "${CXX_FLAGS}")

include_directories(${PROJECT_SOURCE_DIR}/examples)

option(CMAKE_USE_OLD_GDB "cmake to support old gdb" OFF)
if(CMAKE_USE_OLD_GDB)
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -gdwarf-3")
    message(STATUS "CMAKE_USE_OLD_GDB : Enabled")
elseif(NOT CMAKE_USE_OLD_GDB)
    message(STATUS "CMAKE_USE_OLD_GDB : Disabled")
endif()

if(CMAKE_USE_BOOST)
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -gdwarf-3")
    message(STATUS "CMAKE_USE_BOOST : Enabled")
    SET(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -DH_HAVE_BOOST=1")
elseif(NOT CMAKE_USE_BOOST)
    message(STATUS "CMAKE_USE_BOOST : Disabled")
endif()

if (UNIX)
    SET(CMAKE_CXX_FLAGS_DEBUG   "-O0 -g -ggdb -D_DEBUG -DGOOGLE_STRIP_LOG=0")
    SET(CMAKE_CXX_FLAGS_RELEASE "-O3 -g -ggdb -DNDEBUG -DGOOGLE_STRIP_LOG=1")
    # SET(DEPENDENT_LIBRARIES event glog pthread)
else (UNIX)
    # SET(DEPENDENT_LIBRARIES event glog)
endif (UNIX)

# Set to true if https support is needed.
# Note that this needs openssl
# SET(HTTPS True)
if (HTTPS)
    list(APPEND DEPENDENT_LIBRARIES ssl crypto event_openssl)
    SET(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -DEVPP_HTTP_CLIENT_SUPPORTS_SSL")
    SET(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -DEVPP_HTTP_SERVER_SUPPORTS_SSL")
endif (HTTPS)

if (DEPS_PREFIX_PATH)
    message(STATUS "DEPS_PREFIX_PATH: " ${DEPS_PREFIX_PATH})
else()
    set(DEPS_PREFIX_PATH ${PROJECT_SOURCE_DIR}/deps_prefix)
    message(STATUS "DEPS_PREFIX_PATH is not given, Default: " ${DEPS_PREFIX_PATH})
endif()

include_directories(${DEPS_PREFIX_PATH}/include)

list(APPEND DEPENDENT_LIBRARIES pthread event)
link_directories(${DEPS_PREFIX_PATH}/lib)

if (CMAKE_BENCHMARK_TESTING)
    SET(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -DH_BENCHMARK_TESTING=1")
endif (CMAKE_BENCHMARK_TESTING)

set (EXECUTABLE_OUTPUT_PATH ${PROJECT_BINARY_DIR}/bin)
set (LIBRARY_OUTPUT_PATH ${PROJECT_BINARY_DIR}/lib)

#set (MACOSX_RPATH ${LIBRARY_OUTPUT_PATH})
set (CMAKE_MACOSX_RPATH 0)

find_program(THRIFT_COMPILER thrift)
find_path(THRIFT_INCLUDE_DIR thrift)
find_library(THRIFT_LIBRARY NAMES thrift)

message (STATUS "This is BINARY dir " ${PROJECT_BINARY_DIR})
message (STATUS "This is SOURCE dir " ${PROJECT_SOURCE_DIR})
message (STATUS "CMAKE_INSTALL_PREFIX : " ${CMAKE_INSTALL_PREFIX})
message (STATUS "CMAKE_CXX_FLAGS " ${CMAKE_CXX_FLAGS})
message (STATUS "EXECUTABLE_OUTPUT_PATH " ${EXECUTABLE_OUTPUT_PATH})
message (STATUS "LIBRARY_OUTPUT_PATH " ${LIBRARY_OUTPUT_PATH})
message (STATUS "THRIFT_COMPILER " ${THRIFT_COMPILER})
message (STATUS "THRIFT_INCLUDE_DIR " ${THRIFT_INCLUDE_DIR})
message (STATUS "THRIFT_LIBRARY " ${THRIFT_LIBRARY})

add_subdirectory (evpp)
add_subdirectory (apps)
#add_subdirectory (labs)

if (NOT EVPP_VCPKG_BUILD)
    add_subdirectory (test)
    add_subdirectory (examples)
    add_subdirectory (benchmark)
endif ()

set (CMAKE_MODULE_PATH "${PROJECT_SOURCE_DIR}/cmake")
include (utils)
include (packages)

include (CPack)
include (CTest)
