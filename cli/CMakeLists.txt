# ************************************************************************
#  Copyright (C) 2023 Alec Leamas                                        *
#  Copyright (C) 2010 David S Register                                   *
#                                                                        *
#  This program is free software; you can redistribute it and/or modify  *
#  it under the terms of the GNU General Public License as published by  *
#  the Free Software Foundation; either version 2 of the License, or     *
#  (at your option) any later version.                                   *
#                                                                        .
#  This program is distributed in the hope that it will be useful,       *
#  but WITHOUT ANY WARRANTY; without even the implied warranty of        *
#  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the         *
#  GNU General Public License for more details.                          *
#                                                                        *
#  You should have received a copy of the GNU General Public License     *
#  along with this program; if not, write to the                         *
#  Free Software Foundation, Inc.,                                       *
#  51 Franklin Street, Fifth Floor, Boston, MA 02110-1301,  USA.         *
# ************************************************************************


set(SRC_CONSOLE console.cpp api_shim.cpp)
set(CLI_TARGET ${OCPN_APP_PACKAGE_NAME}-cmd)
add_executable(${CLI_TARGET} ${SRC_CONSOLE})
set_property(TARGET ${CLI_TARGET} PROPERTY ENABLE_EXPORTS ON)
if (NOT "${ENABLE_SANITIZER}" MATCHES "none")
  target_link_libraries(${CLI_TARGET} PRIVATE -fsanitize=${ENABLE_SANITIZER})
endif ()

target_include_directories(
  ${CLI_TARGET}
    PRIVATE
      ${PROJECT_SOURCE_DIR}/include
      # ${PROJECT_SOURCE_DIR}/test/include
      #      ${PROJECT_SOURCE_DIR}/model/include
      ${PROJECT_SOURCE_DIR}/gui/include
)

target_compile_definitions(${CLI_TARGET} PUBLIC USE_MOCK_DEFS)
if (MSVC)
  target_link_libraries(${CLI_TARGET} PRIVATE setupapi.lib psapi.lib)
endif ()

if (TARGET ocpn::wxcurl)
  target_link_libraries(${CLI_TARGET} PRIVATE ocpn::wxcurl)
else ()
  target_link_libraries(${CLI_TARGET} PRIVATE ${SYS_WXCURL_LIBRARIES})
endif ()

target_link_libraries(${CLI_TARGET} PRIVATE ${wxWidgets_LIBRARIES})
if (TARGET ocpn::wxsvg)
  target_link_libraries(${CLI_TARGET} PRIVATE ocpn::wxsvg)
endif ()


target_link_libraries(${CLI_TARGET} PRIVATE ocpn::serial)
target_link_libraries(${CLI_TARGET} PRIVATE ocpn::tinyxml)
target_link_libraries(${CLI_TARGET} PRIVATE ocpn::filesystem)
target_link_libraries(${CLI_TARGET} PRIVATE ocpn::gl-headers)

if (HAVE_LIBUDEV)
  target_link_libraries(${CLI_TARGET} PRIVATE ocpn::libudev)
endif ()

if(APPLE AND OCPN_USE_DEPS_BUNDLE)
  target_include_directories(
    ${CLI_TARGET} PUBLIC ${OCPN_DEPS_BUNDLE_PATH}/include
  )
endif()

target_link_libraries(${CLI_TARGET} PRIVATE ocpn::model ocpn::model-src)
if (MSVC)
  target_link_libraries(${CLI_TARGET} PRIVATE  iphlpapi)
endif ()


if (WIN32)
  install(TARGETS ${CLI_TARGET} RUNTIME DESTINATION ".")
else()
  install(TARGETS ${CLI_TARGET} RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
endif ()

if (APPLE)
  install(
    FILES ${CMAKE_CURRENT_BINARY_DIR}/${CLI_TARGET}
    DESTINATION "bin/OpenCPN.app/Contents/MacOS"
    PERMISSIONS OWNER_READ OWNER_WRITE GROUP_READ WORLD_READ
      WORLD_EXECUTE GROUP_EXECUTE OWNER_EXECUTE
  )
  set_target_properties(${CLI_TARGET} PROPERTIES
                               XCODE_ATTRIBUTE_ENABLE_HARDENED_RUNTIME YES
  )
endif ()
