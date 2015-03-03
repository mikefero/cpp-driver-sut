##
# Copyright 2014 DataStax
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

#
# Check for the presence of LIBTOOL
#
# LIBTOOL_FOUND will be true if all components have been found.
include(FindPackageHandleStandardArgs)

IF (NOT LIBTOOL_FOUND)
	IF (NOT LIBTOOL_ROOT_DIR)
		SET(LIBTOOL_ROOT_DIR ${CMAKE_INSTALL_PREFIX})
	ENDIF (NOT LIBTOOL_ROOT_DIR)

	# Determine if the libtool executables exist 
	FOREACH (APPLICATION libtool libtoolize)
		# Find the executables
		STRING(TOUPPER ${APPLICATION} APPLICATION_NAME)
		FIND_PROGRAM(${APPLICATION_NAME}_EXECUTABLE ${APPLICATION} HINTS ${LIBTOOL_ROOT_DIR} ${CMAKE_INSTALL_PREFIX} PATH_SUFFIXES bin)
	ENDFOREACH(APPLICATION)

	# Finalize results
	FIND_PACKAGE_HANDLE_STANDARD_ARGS(LIBTOOL DEFAULT_MSG LIBTOOL_EXECUTABLE)
	IF (LIBTOOL_FOUND)
		IF (NOT LIBTOOL_FIND_QUIETLY)
			MESSAGE(STATUS "Found components for LIBTOOL")
			MESSAGE(STATUS "LIBTOOL_ROOT_DIR  = ${LIBTOOL_ROOT_DIR}")
			MESSAGE(STATUS "LIBTOOL_EXECUTABLE = ${LIBTOOL_EXECUTABLE}")
			MESSAGE(STATUS "LIBTOOLIZE_EXECUTABLE = ${LIBTOOLIZE_EXECUTABLE}")
		ENDIF (NOT LIBTOOL_FIND_QUIETLY)
	ELSE (LIBTOOL_FOUND)
		IF (LIBTOOL_FIND_REQUIRED)
			MESSAGE(FATAL_ERROR "Could not find LIBTOOL!")
		ENDIF (LIBTOOL_FIND_REQUIRED)
	ENDIF (LIBTOOL_FOUND)
	MARK_AS_ADVANCED(LIBTOOL_ROOT_DIR LIBTOOL_EXECUTABLE LIBTOOLIZE_EXECUTABLE)
endif (NOT LIBTOOL_FOUND)
