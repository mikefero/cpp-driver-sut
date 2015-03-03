##
# Copyright 2015 DataStax
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
# http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.
##
CMAKE_MINIMUM_REQUIRED(VERSION 2.8.3)
#SET(EXECUTE_PROCESS_SUCCESS 0)
#SET(CMAKE_VERBOSE_MAKEFILE ON) # Enable verbose makefile for Eclipse projects

# TODO: Add 32bit library support for building in linux (e.g. update CXX flags)

# Load modules not in standard package
set(CMAKE_MODULE_PATH ${CMAKE_MODULE_PATH} "${CMAKE_CURRENT_SOURCE_DIR}/cmake/modules/")

#
# Include CMake modules
#
INCLUDE(ExternalProject)
INCLUDE(TargetArch)

#
# Get the target architecture and perform platform specific operations
#
TARGET_ARCHITECTURE(TARGET_ARCH)
IF("${TARGET_ARCH}" STREQUAL "i386")
  MESSAGE(FATAL_ERROR "Currently only 64-bit architectures are supported")
ENDIF()
IF(WIN32)
  IF("${TARGET_ARCH}" STREQUAL "i386")
    ADD_DEFINITIONS(-D_WIN32)
  ELSEIF("${TARGET_ARCH}" STREQUAL "x86_64")
    ADD_DEFINITIONS(-D_WIN64)
  ENDIF()
ENDIF()

#
# Ensure build dependencies are installed
#
IF(NOT WIN32)
  # Find Autotools and libtool (Required for libunwind)
  FIND_PACKAGE(Autotools REQUIRED)
  FIND_PACKAGE(Libtool REQUIRED)
  IF(NOT AUTOTOOLS_FOUND)
    MESSAGE(FATAL_ERROR "Could not find AUTOTOOLS!")
  ENDIF()
  IF(NOT LIBTOOL_FOUND)
    MESSAGE(FATAL_ERROR "Could not find LIBTOOL!")
  ENDIF()
ENDIF()

##
# Project related CMake variables
##
PROJECT(cpp-driver-benchmarks)
SET(PROJECT_DEPENDENCIES_DIR ${PROJECT_BINARY_DIR}/dependencies)
SET(PROJECT_LIBRARIES_DIR ${PROJECT_BINARY_DIR}/libs)
SET(CPP_DRIVER_REPOSITORY git://github.com/datastax/cpp-driver.git)
SET(LIBUV_REPOSITORY git://github.com/libuv/libuv.git)
SET(LIBUV_VERSION v1.4.2)
SET(LIBUNWIND_REPOSITORY git://git.sv.gnu.org/libunwind.git)
SET(LIBUNWIND_VERSION v1.1)
SET(STACKWALKER_REPOSITORY git://git01.codeplex.com/stackwalker)

##
# Project related CMake options
##
OPTION(CPP_DRIVER_VERSION "cpp-driver version")
IF(NOT CPP_DRIVER_VERSION)
  SET(CPP_DRIVER_VERSION HEAD)
ENDIF()
IF(NOT CMAKE_BUILD_TYPE)
  SET(CMAKE_BUILD_TYPE "Release")
ENDIF()

# Set up include directories
INCLUDE_DIRECTORIES(${PROJECT_SOURCE_DIR}/include ${PROJECT_SOURCE_DIR}/src)

# Gather the http server header and source files
FILE(GLOB PROJECT_INCLUDE_FILES ${CMAKE_CURRENT_LIST_DIR}/src/*.h ${CMAKE_CURRENT_LIST_DIR}/src/*.hpp)
FILE(GLOB HTTP_SERVER_SOURCE_FILES ${CMAKE_CURRENT_LIST_DIR}/src/*.cpp)

##
# Ensure build requirements
##
#IF(NOT UNIX AND NOT WIN32)
IF(NOT UNIX)
  #MESSAGE(FATAL_ERROR "Currently only *nix and Windows systems are supported")
  MESSAGE(FATAL_ERROR "Currently only *nix systems are supported")
ENDIF()

# Add libunwind as an external project (iff Unix Platform)
IF(UNIX)
  # Create the configure script for libunwind
  SET(LIBUNWIND_CONFIGURE_SCRIPT ${PROJECT_DEPENDENCIES_DIR}/libunwind/libunwind_configure.sh)
  FILE(REMOVE ${LIBUNWIND_CONFIGURE_SCRIPT})
  FILE(WRITE ${LIBUNWIND_CONFIGURE_SCRIPT} "#!/bin/bash\n")
  FILE(APPEND ${LIBUNWIND_CONFIGURE_SCRIPT} "autoreconf -i\n")
  FILE(APPEND ${LIBUNWIND_CONFIGURE_SCRIPT} "./configure --silent --disable-shared --prefix=${PROJECT_LIBRARIES_DIR}/libunwind\n")
  SET(LIBUNWIND_CONFIGURE_COMMAND sh ${LIBUNWIND_CONFIGURE_SCRIPT})

  # Create an external project for libunwind
  EXTERNALPROJECT_ADD(libunwind-library
    PREFIX ${PROJECT_DEPENDENCIES_DIR}/libunwind
    GIT_REPOSITORY ${LIBUNWIND_REPOSITORY}
    GIT_TAG ${LIBUNWIND_VERSION}
    UPDATE_COMMAND ""
    CONFIGURE_COMMAND ${LIBUNWIND_CONFIGURE_COMMAND}
    BUILD_COMMAND ${MAKE}
    BUILD_IN_SOURCE 1
    LOG_DOWNLOAD 1
    LOG_CONFIGURE 1
    LOG_BUILD 1
    LOG_INSTALL 1)

  # Set up include and library directories for libunwind
  INCLUDE_DIRECTORIES(${PROJECT_LIBRARIES_DIR}/libunwind/include)
  LINK_DIRECTORIES(${PROJECT_LIBRARIES_DIR}/libunwind/lib)

  # Gather the libunwind library header files
  FILE(GLOB_RECURSE LIBUNWIND_LIBRARY_INCLUDE_FILES ${PROJECT_LIBRARIES_DIR}/libunwind/include/*.h ${PROJECT_LIBRARIES_DIR}/libunwind/include/*.hpp)
  SOURCE_GROUP("Header Files\\libunwind" FILES ${LIBUNWIND_LIBRARY_INCLUDE_FILES})

  # Create the libraries variable for libunwind
  SET(LIBUNWIND_LIBRARIES unwind)
ELSEIF(WIN32)
  # Add StackWalker as a dependency
  EXTERNALPROJECT_ADD(stackwalker-source
    PREFIX ${PROJECT_DEPENDENCIES_DIR}/stackwalker
    GIT_REPOSITORY ${STACKWALKER_REPOSITORY}
    UPDATE_COMMAND ""
    CONFIGURE_COMMAND ""
    BUILD_COMMAND ""
    INSTALL_COMMAND ""
    LOG_DOWNLOAD 1)

  # Set up include files for StackWalker
  SET(STACKWALKER_SOURCE_DIR ${PROJECT_DEPENDENCIES_DIR}/stackwalker/src/stackwalker-source)
  INCLUDE_DIRECTORIES(${STACKWALKER_SOURCE_DIR}/include)
  SET(STACKWALKER_INCLUDE_FILES ${STACKWALKER_SOURCE_DIR}/include/StackWalker.h)
  SET(STACKWALKER_SOURCE_FILES ${STACKWALKER_SOURCE_DIR}/src/StackWalker.cpp)
  SET_SOURCE_FILES_PROPERTIES(${STACKWALKER_INCLUDE_FILES} ${STACKWALKER_SOURCE_DIR} PROPERTIES GENERATED TRUE)

  # Gather the StackWalker library header and source files
  SOURCE_GROUP("Header Files\\stackwalker" FILES ${STACKWALKER_INCLUDE_FILES})
  SOURCE_GROUP("Source Files\\stackwalker" FILES ${STACKWALKER_SOURCE_FILES})

  # Ignore compiler warning for deprecated use of GetVersionExA
  ADD_DEFINITIONS("/wd4996")
ENDIF()

# Create the external project commands required for building libuv
IF(UNIX)
  # Create the configuration script for libuv
  SET(LIBUV_CONFIGURE_SCRIPT ${PROJECT_DEPENDENCIES_DIR}/libuv/libuv_configure.sh)
  FILE(REMOVE ${LIBUV_CONFIGURE_SCRIPT})
  FILE(WRITE ${LIBUV_CONFIGURE_SCRIPT} "#!/bin/bash\n")
  FILE(APPEND ${LIBUV_CONFIGURE_SCRIPT} "sh autogen.sh \n")
  FILE(APPEND ${LIBUV_CONFIGURE_SCRIPT} "./configure --silent --prefix=${PROJECT_LIBRARIES_DIR}/libuv\n")
  SET(LIBUV_CONFIGURE_COMMAND sh ${LIBUV_CONFIGURE_SCRIPT})
  SET(LIBUV_LIBRARIES uv)
ELSEIF(WIN32)
  # Determine which architecture to build libuv
  IF("${TARGET_ARCH}" STREQUAL "i386")
    SET(LIBUV_TARGET_ARCH "x86")
  ELSE()
    SET(LIBUV_TARGET_ARCH "x64")
  ENDIF()

  # Disable the configuration command for libuv in Windows
  SET(LIBUV_CONFIGURE_COMMAND "")

  # Create the make command for libuv
  SET(LIBUV_MAKE_SCRIPT ${PROJECT_DEPENDENCIES_DIR}/libuv/libuv_make.bat)
  FILE(REMOVE ${LIBUV_MAKE_SCRIPT})
  FILE(WRITE ${LIBUV_MAKE_SCRIPT} "@ECHO OFF\n")
  FILE(APPEND ${LIBUV_MAKE_SCRIPT} "PUSHD src\\libuv-library > NUL\n")
  FILE(APPEND ${LIBUV_MAKE_SCRIPT} "XCOPY /E /Y /Q \"${PROJECT_DEPENDENCIES_DIR}/gyp/src/gyp-binary\" \".\"\n")
  FILE(APPEND ${LIBUV_MAKE_SCRIPT} "TYPE vcbuild.bat | FINDSTR /V /C:\"if defined WindowsSDKDir goto select-target\" | FINDSTR /V /C:\"if defined VCINSTALLDIR goto select-target\" > vcbuild-modified.bat\n")
  FILE(APPEND ${LIBUV_MAKE_SCRIPT} "vcbuild-modified.bat release ${LIBUV_TARGET_ARCH} static\n")
  FILE(APPEND ${LIBUV_MAKE_SCRIPT} "POPD > NUL\n")
  SET(LIBUV_BUILD_COMMAND ${LIBUV_MAKE_SCRIPT})

  # Create the installation script for libuv
  SET(LIBUV_INSTALL_SCRIPT ${PROJECT_DEPENDENCIES_DIR}/libuv/libuv_install.bat)
  FILE(REMOVE ${LIBUV_INSTALL_SCRIPT})
  FILE(WRITE ${LIBUV_INSTALL_SCRIPT} "@ECHO OFF\n")
  FILE(APPEND ${LIBUV_INSTALL_SCRIPT} "MKDIR \"${PROJECT_LIBRARIES_DIR}/libuv\"\n")
  FILE(APPEND ${LIBUV_INSTALL_SCRIPT} "MKDIR \"${PROJECT_LIBRARIES_DIR}/libuv/include\"\n")
  FILE(APPEND ${LIBUV_INSTALL_SCRIPT} "MKDIR \"${PROJECT_LIBRARIES_DIR}/libuv/lib\"\n")
  FILE(APPEND ${LIBUV_INSTALL_SCRIPT} "XCOPY /E /Y /Q \"${PROJECT_DEPENDENCIES_DIR}/libuv/src/libuv-library/include\" \"${PROJECT_LIBRARIES_DIR}/libuv/include\"\n")
  FILE(APPEND ${LIBUV_INSTALL_SCRIPT} "XCOPY /E /Y /Q \"${PROJECT_DEPENDENCIES_DIR}/libuv/src/libuv-library/Release/lib\" \"${PROJECT_LIBRARIES_DIR}/libuv/lib\"\n")
  SET(LIBUV_INSTALL_COMMAND ${LIBUV_INSTALL_SCRIPT})
  SET(LIBUV_LIBRARIES libuv ws2_32 psapi Iphlpapi)
ENDIF()

# Add libuv as a dependency
EXTERNALPROJECT_ADD(libuv-library
  PREFIX ${PROJECT_DEPENDENCIES_DIR}/libuv
  GIT_REPOSITORY ${LIBUV_REPOSITORY}
  GIT_TAG ${LIBUV_VERSION}
  UPDATE_COMMAND ""
  CONFIGURE_COMMAND ${LIBUV_CONFIGURE_COMMAND}
  BUILD_COMMAND ${LIBUV_BUILD_COMMAND}
  INSTALL_COMMAND ${LIBUV_INSTALL_COMMAND}
  BUILD_IN_SOURCE 1
  LOG_DOWNLOAD 1
  LOG_CONFIGURE 1
  LOG_BUILD 1
  LOG_INSTALL 1)

# Set up include and library directories for libuv
INCLUDE_DIRECTORIES(${PROJECT_LIBRARIES_DIR}/libuv/include)
LINK_DIRECTORIES(${PROJECT_LIBRARIES_DIR}/libuv/lib)

# Gather the libuv library header files
FILE(GLOB_RECURSE LIBUV_LIBRARY_INCLUDE_FILES ${PROJECT_LIBRARIES_DIR}/libuv/include/*.h ${PROJECT_LIBRARIES_DIR}/libuv/include/*.hpp)

# Add cpp-driver as a dependency (libuv is a dependency of cpp-driver)
EXTERNALPROJECT_ADD(cpp-driver-library
  DEPENDS libuv-library
  PREFIX ${PROJECT_DEPENDENCIES_DIR}/cpp-driver
  GIT_REPOSITORY ${CPP_DRIVER_REPOSITORY}
  GIT_TAG ${CPP_DRIVER_VERSION}
  UPDATE_COMMAND ""
  INSTALL_DIR ${PROJECT_LIBRARIES_DIR}/cpp-driver
  CMAKE_ARGS -DCMAKE_BUILD_TYPE=${CMAKE_BUILD_TYPE}
    -DCASS_INSTALL_HEADER=ON
    -DCASS_USE_EXTERNAL_BOOST=OFF
    -DCASS_BUILD_SHARED=OFF
    -DCASS_BUILD_STATIC=ON
    -DCASS_BUILD_EXAMPLES=OFF
    -DCASS_BUILD_TESTS=OFF
    -DCASS_USE_OPENSSL=OFF #TODO: Enable when SSL is added to benchmark tests
    -DCASS_USE_TCMALLOC=OFF
    -DLIBUV_ROOT_DIR=${PROJECT_LIBRARIES_DIR}/libuv
    -DCMAKE_INSTALL_PREFIX=${PROJECT_LIBRARIES_DIR}/cpp-driver
  LOG_DOWNLOAD 1
  LOG_CONFIGURE 1
  LOG_BUILD 1
  LOG_INSTALL 1)

# Set up include and library directories for cpp-driver
INCLUDE_DIRECTORIES(${PROJECT_LIBRARIES_DIR}/cpp-driver/include)
LINK_DIRECTORIES(${PROJECT_LIBRARIES_DIR}/cpp-driver/lib)

# Gather the cpp-driver library header files
FILE(GLOB CPP_DRIVER_LIBRARY_INCLUDE_FILES ${PROJECT_LIBRARIES_DIR}/cpp-driver/include/*.h ${PROJECT_LIBRARIES_DIR}/cpp-driver/include/*.hpp)

# Gather the root header and source files
FILE(GLOB PROJECT_INCLUDE_FILES ${PROJECT_SOURCE_DIR}/include/*.h ${PROJECT_SOURCE_DIR}/include/*.hpp)
FILE(GLOB PROJECT_SOURCE_FILES ${PROJECT_SOURCE_DIR}/src/*.c ${PROJECT_SOURCE_DIR}/src/*.cpp)

# Create the libraries variable for cpp-driver
SET(CPP_DRIVER_LIBRARIES cassandra_static)

##
# Global compiler flags
##
IF("${CMAKE_CXX_COMPILER_ID}" STREQUAL "MSVC")
  SET(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} /NODEFAULTLIB:LIBCMT")
  SET(CMAKE_EXE_LINKER_FLAGS_DEBUG "${CMAKE_EXE_LINKER_FLAGS_DEBUG} /LTCG /INCREMENTAL:NO")
  SET(CMAKE_EXE_LINKER_FLAGS_RELEASE "${CMAKE_EXE_LINKER_FLAGS_RELEASE}")
ENDIF()

##
# Subprojects for build
##
ADD_SUBDIRECTORY(httpserver)
