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

##
# Project related CMake variables
##
PROJECT(cpp-driver-http-server)
SET(MONGOOSE_REPOSITORY git://github.com/cesanta/mongoose.git)

# Add Mongoose as a dependency
EXTERNALPROJECT_ADD(mongoose-source
  PREFIX ${PROJECT_DEPENDENCIES_DIR}/mongoose
  GIT_REPOSITORY ${MONGOOSE_REPOSITORY}
  UPDATE_COMMAND ""
  CONFIGURE_COMMAND ""
  BUILD_COMMAND ""
  INSTALL_COMMAND ""
  LOG_DOWNLOAD 1)

# Set up include and source files for Mongoose
SET(MONGOOSE_SOURCE_DIR ${PROJECT_DEPENDENCIES_DIR}/mongoose/src/mongoose-source)
INCLUDE_DIRECTORIES(${MONGOOSE_SOURCE_DIR})
SET(MONGOOSE_INCLUDE_FILES ${MONGOOSE_SOURCE_DIR}/mongoose.h)
SET(MONGOOSE_SOURCE_FILES ${MONGOOSE_SOURCE_DIR}/mongoose.c)
SET_SOURCE_FILES_PROPERTIES(${MONGOOSE_INCLUDE_FILES} ${MONGOOSE_SOURCE_FILES} PROPERTIES GENERATED TRUE)

# Gather the http server header and source files
FILE(GLOB HTTP_SERVER_INCLUDE_FILES ${CMAKE_CURRENT_LIST_DIR}/src/*.h ${CMAKE_CURRENT_LIST_DIR}/src/*.hpp)
FILE(GLOB HTTP_SERVER_SOURCE_FILES ${CMAKE_CURRENT_LIST_DIR}/src/*.cpp)

# Update the cpp-driver http server project include and library directories
INCLUDE_DIRECTORIES(${CMAKE_CURRENT_LIST_DIR}/src)

# Create source groups (folder grouping)
SOURCE_GROUP("Header Files" FILES ${HTTP_SERVER_INCLUDE_FILES})
SOURCE_GROUP("Header Files\\cpp-driver" FILES ${CPP_DRIVER_LIBRARY_INCLUDE_FILES})
SOURCE_GROUP("Header Files\\mongoose" FILES ${MONGOOSE_INCLUDE_FILES})
SOURCE_GROUP("Header Files\\libuv" FILES ${LIBUV_LIBRARY_INCLUDE_FILES})
SOURCE_GROUP("Source Files" FILES ${HTTP_SERVER_SOURCE_FILES} ${PROJECT_SOURCE_FILES})

# Disable C++0X/C++11 features from EasyLogging++
ADD_DEFINITIONS(-D_DISABLE_CXX0X -D_DISABLE_CXX11)

##
# *nix dependency for stacktrace (libunwind)
# Windows dependency for stacktrace (StackWalker)
##
IF(NOT WIN32)
  SET(ADDITIONAL_PROJECT_DEPENDENCIES libunwind-library)
  SET(ADDITIONAL_PROJECT_LINK_LIBRARIES ${LIBUNWIND_LIBRARIES} pthread)
ELSE()
  SET(ADDITIONAL_PROJECT_DEPENDENCIES stackwalker-source)
  SET(ADDITIONAL_PROJECT_SOURCE_FILES ${STACKWALKER_SOURCE_FILES})
ENDIF()

# Create the executable target for the cpp-driver http server benchmark
ADD_EXECUTABLE(${PROJECT_NAME} ${HTTP_SERVER_SOURCE_FILES}
  ${MONGOOSE_SOURCE_FILES}
  ${ADDITIONAL_PROJECT_SOURCE_FILES}
  ${PROJECT_SOURCE_FILES}
  ${HTTP_SERVER_INCLUDE_FILES}
  ${CPP_DRIVER_LIBRARY_INCLUDE_FILES}
  ${MONGOOSE_INCLUDE_FILES}
  ${LIBUV_LIBRARY_INCLUDE_FILES}
  ${PROJECT_INCLUDE_FILES})
ADD_DEPENDENCIES(${PROJECT_NAME} cpp-driver-library libuv-library mongoose-source ${ADDITIONAL_PROJECT_DEPENDENCIES})
TARGET_LINK_LIBRARIES(${PROJECT_NAME} ${CPP_DRIVER_LIBRARIES} ${LIBUV_LIBRARIES} ${ADDITIONAL_PROJECT_LINK_LIBRARIES})
