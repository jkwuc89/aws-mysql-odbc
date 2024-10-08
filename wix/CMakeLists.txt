# Modifications Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
#
# Copyright (c) 2007, 2024, Oracle and/or its affiliates.
#
# This program is free software; you can redistribute it and/or modify
# it under the terms of the GNU General Public License, version 2.0, as
# published by the Free Software Foundation.
#
# This program is designed to work with certain software (including
# but not limited to OpenSSL) that is licensed under separate terms, as
# designated in a particular file or component or in included license
# documentation. The authors of MySQL hereby grant you an additional
# permission to link the program and your derivative works with the
# separately licensed software that they have either included with
# the program or referenced in the documentation.
#
# Without limiting anything contained in the foregoing, this file,
# which is part of Connector/ODBC, is also subject to the
# Universal FOSS Exception, version 1.0, a copy of which can be found at
# https://oss.oracle.com/licenses/universal-foss-exception.#
# This program is distributed in the hope that it will be useful, but
# WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
# See the GNU General Public License, version 2.0, for more details.
#
# You should have received a copy of the GNU General Public License
# along with this program; if not, write to the Free Software Foundation, Inc.,
# 51 Franklin St, Fifth Floor, Boston, MA 02110-1301 USA

##########################################################################

cmake_minimum_required(VERSION 2.4.6)

PROJECT(MYODBC_MSI_INSTALLER)

#-------------- wix installer ------------------------
IF(WIN32)
  INCLUDE(${CMAKE_SOURCE_DIR}/cmake/FindWix.cmake)
  # ----------- generate version information ---------
  MESSAGE(${WIX_DIR})
  IF(WIX_DIR)
    FILE(REMOVE "myodbc_version.xml")
    TRY_RUN(VERSION_OUT VERSION_TEST 
            "${CMAKE_SOURCE_DIR}"
            "${CMAKE_SOURCE_DIR}/cmake/getodbcversion.c" 
            ARGS "myodbc_version.xml")
    IF(NOT VERSION_OUT STREQUAL "0")
        MESSAGE(ERROR "Can't create myodbc_version.xml")
    ENDIF(NOT VERSION_OUT STREQUAL "0")
  ENDIF(WIX_DIR)
  # --------------------------------------------------

  # -------------- find binary resources -------------
  FIND_PATH(WIX_RESOURCE AwsConnector.ico
          ./resources
          $ENV{WIX_RESOURCE}
          ${CMAKE_SOURCE_DIR}/../../../wix-installer/resources)
  IF(NOT WIX_RESOURCE)
          MESSAGE(ERROR "Can't find binary MySQL resource files. Please specify WIX_RESOURCE")
  ENDIF(NOT WIX_RESOURCE)
  MESSAGE(STATUS "Wix resources found in ${WIX_RESOURCE}")
  # --------------------------------------------------

  # -------------- add wix variables --- -------------
  WRITE_FILE("myodbc_version.xml" "<?define odbc_resource=\"${WIX_RESOURCE}\" ?>" APPEND)
  IF(${MSI_64})
     WRITE_FILE("myodbc_version.xml" "<?define odbc_64bit=1 ?>" APPEND)
     SET(MSI_VERSION "winx64")
  ELSE(${MSI_64})
     WRITE_FILE("myodbc_version.xml" "<?define odbc_64bit=0 ?>" APPEND)
     SET(MSI_VERSION "win32")
  ENDIF(${MSI_64})
  IF(${LICENSE})
     WRITE_FILE("myodbc_version.xml" "<?define license=\"commercial\" ?><?define odbc_license_suffix=\" - Commercial\" ?>" APPEND)
  ELSE(${LICENSE})
     WRITE_FILE("myodbc_version.xml" "<?define license=\"community\" ?><?define odbc_license_suffix=\"\" ?>" APPEND)
  ENDIF(${LICENSE})
  WRITE_FILE("myodbc_version.xml" "</Include>" APPEND)
  # --------------------------------------------------
  INCLUDE(${CMAKE_SOURCE_DIR}/myodbc_version.cmake)

  IF(ODBC_VERSION_SUFFIX STREQUAL "a")
	  SET(PACKAGE_SUFFIX "a")
  ENDIF(ODBC_VERSION_SUFFIX STREQUAL "a")

  IF(${LICENSE})
	  SET(MSI_PACKAGE "aws-mysql-odbc-commercial-${ODBC_VERSION}-${MSI_VERSION}${PACKAGE_SUFFIX}.msi")
  ELSE(${LICENSE})
	  SET(MSI_PACKAGE "aws-mysql-odbc-${ODBC_VERSION}-${MSI_VERSION}${PACKAGE_SUFFIX}.msi")
  ENDIF(${LICENSE})

  # Generate GUID
EXECUTE_PROCESS(COMMAND uuidgen
                OUTPUT_VARIABLE CONNECTOR_PKG_ID1)
STRING(STRIP "${CONNECTOR_PKG_ID1}" CONNECTOR_PKG_ID1)
EXECUTE_PROCESS(COMMAND uuidgen
                OUTPUT_VARIABLE CONNECTOR_PKG_ID2)
STRING(STRIP "${CONNECTOR_PKG_ID2}" CONNECTOR_PKG_ID2)

ENDIF(WIN32)
#-----------------------------------------------------

SET(LICENSE_FILE "${CMAKE_CURRENT_SOURCE_DIR}/doc/LICENSE.txt")
SET(LICENSE_RTF  "${CMAKE_CURRENT_BINARY_DIR}/resources/LICENSE.rtf")

FILE(READ ${LICENSE_FILE} CONTENTS)
STRING(REGEX REPLACE "\n" "\\\\par\n" CONTENTS "${CONTENTS}")
STRING(REGEX REPLACE "\t" "\\\\tab" CONTENTS "${CONTENTS}")
FILE(WRITE "${LICENSE_RTF}"
     "{\\rtf1\\ansi\\deff0{\\fonttbl{\\f0\\fnil\\fcharset0 Courier New;}}\\viewkind4\\uc1\\pard\\lang1031\\f0\\fs15")
FILE(APPEND "${LICENSE_RTF}" "${CONTENTS}")
FILE(APPEND "${LICENSE_RTF}" "\n}\n")

# Generating XML list of plugins

SET(XML_COMPONENT_WIN64)
SET(XML_FILE_SOURCE_ARCH x86)
IF(${MSI_64})
  SET(XML_COMPONENT_WIN64 "Win64='yes'")
  SET(XML_FILE_SOURCE_ARCH x64)
ENDIF()

SET(XML_PLUGINS mysql_odbc_plugins.xml)
SET(XML_PLUGINS_GUID "BF9FD280-D962-4B3E-99CD-7FA6662B067C")
IF(${MSI_64})
  SET(XML_PLUGINS_GUID "593E8354-467A-4E02-A6A2-28CEF9ECD0BB")
ENDIF()

FILE(WRITE "${XML_PLUGINS}" "<Include><Component Id='plugins' Guid='${XML_PLUGINS_GUID}' SharedDllRefCount='yes' ${XML_COMPONENT_WIN64}>")
FILE(GLOB PLUGIN_DLL_LIST "${CMAKE_CURRENT_BINARY_DIR}/${XML_FILE_SOURCE_ARCH}/plugin/*.dll")
MESSAGE("LOOKING INSIDE ${CMAKE_CURRENT_BINARY_DIR}/${XML_FILE_SOURCE_ARCH}/plugin/*.dll")
MESSAGE("PLUGIN_DLL_LIST=${PLUGIN_DLL_LIST}")
FOREACH(_PLUGIN_DLL ${PLUGIN_DLL_LIST})
  get_filename_component(_PLUGIN_DLL_FILE ${_PLUGIN_DLL} NAME)
  FILE(APPEND "${XML_PLUGINS}" "<File Id='${_PLUGIN_DLL_FILE}' Name='${_PLUGIN_DLL_FILE}' DiskId='1' Source='${XML_FILE_SOURCE_ARCH}\\plugin\\${_PLUGIN_DLL_FILE}' />")
ENDFOREACH()

FILE(APPEND "${XML_PLUGINS}" "</Component></Include>")

# Generating XML list of dependencies

SET(XML_DEPENDENCIES mysql_odbc_dependencies.xml)
SET(XML_DEPS_GUID "4D2D72D1-A72B-40E5-969D-72F723CEBF46")
IF(${MSI_64})
  SET(XML_DEPS_GUID "BF941AF6-D40E-44AA-81D6-33A75F6FAE8A")
ENDIF()

SET(EXCLUDED_FILES "awsmysqlodbca.dll" "awsmysqlodbcw.dll" "awsmysqlodbcS.dll"
  "libcrypto-1_1-x64.dll" "libcrypto-1_1.dll"
  "libssl-1_1-x64.dll" "libssl-1_1.dll"
  "libcrypto-3-x64.dll" "libcrypto-3.dll"
  "libssl-3-x64.dll" "libssl-3.dll"
  "myodbc-installer.exe")

FILE(WRITE "${XML_DEPENDENCIES}" "<Include><Component Id='dependencies' Guid='${XML_DEPS_GUID}' SharedDllRefCount='yes' ${XML_COMPONENT_WIN64}>")
FOREACH(_MASK "*.dll" "*.exe")
  FILE(GLOB DEP_FILE_LIST "${CMAKE_CURRENT_BINARY_DIR}/${XML_FILE_SOURCE_ARCH}/${_MASK}")
  MESSAGE("LOOKING INSIDE ${CMAKE_CURRENT_BINARY_DIR}/${XML_FILE_SOURCE_ARCH}/${_MASK}")
  MESSAGE("DEP_FILE_LIST=${DEP_FILE_LIST}")
  FOREACH(_DEP_FILE ${DEP_FILE_LIST})
    get_filename_component(_DEP_FILE_RESULT ${_DEP_FILE} NAME)
    LIST(FIND EXCLUDED_FILES ${_DEP_FILE_RESULT} _INDEX_FOUND)
    # Replace file name that has "-" with "_" since File Id does not allow "-"
    STRING(REPLACE "-" "_" _DEP_FILE_RESULT_REPLACE ${_DEP_FILE_RESULT})
    IF(_INDEX_FOUND EQUAL -1)
      # Add files not in the exclude list
      FILE(APPEND "${XML_DEPENDENCIES}" "<File Id='${_DEP_FILE_RESULT_REPLACE}' Name='${_DEP_FILE_RESULT}' DiskId='1' Source='${XML_FILE_SOURCE_ARCH}\\${_DEP_FILE_RESULT}' />")
    ENDIF()
  ENDFOREACH()
ENDFOREACH()
FILE(APPEND "${XML_DEPENDENCIES}" "</Component></Include>")

#-----------------------------------------------------


CONFIGURE_FILE(${CMAKE_CURRENT_SOURCE_DIR}/mysql_odbc.xml.in
               ${CMAKE_CURRENT_SOURCE_DIR}/mysql-odbc-${ODBC_VERSION}-${MSI_VERSION}.xml @ONLY)

ADD_CUSTOM_TARGET(
        MSI_INSTALLER ALL
        DEPENDS ${MSI_PACKAGE})

ADD_CUSTOM_COMMAND(
        OUTPUT ${MSI_PACKAGE}
        DEPENDS mysql_odbc.wixobj
                mysql_odbc_fragment.wixobj
        COMMAND ${WIX_DIR}/light.exe mysql_odbc.wixobj mysql_odbc_fragment.wixobj -o ${MSI_PACKAGE})

ADD_CUSTOM_COMMAND(
        OUTPUT mysql_odbc_fragment.wixobj
        DEPENDS mysql_odbc_fragment.xml
        COMMAND ${WIX_DIR}/candle.exe mysql_odbc_fragment.xml -o mysql_odbc_fragment.wixobj)

ADD_CUSTOM_COMMAND(
        OUTPUT mysql_odbc.wixobj
        DEPENDS mysql_odbc.xml.in
                mysql_common_ui.xml
        COMMAND ${WIX_DIR}/candle.exe mysql-odbc-${ODBC_VERSION}-${MSI_VERSION}.xml -o mysql_odbc.wixobj)
