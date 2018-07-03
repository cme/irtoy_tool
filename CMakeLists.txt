cmake_minimum_required(VERSION 3.0)

project(irtoy)
add_executable(irtoy_tool
               irtoy_tool.c toolbag/dict/dict.c irtoy.c error.c server.c
               keywords.c mac_actions.c)

add_custom_command(
  OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/keywords.inc
  DEPENDS update_keywords.pl irtoy_tool.c
  mac_actions.c
  COMMAND ${PROJECT_SOURCE_DIR}/update_keywords.pl ${PROJECT_SOURCE_DIR}/irtoy_tool.c ${PROJECT_SOURCE_DIR}/mac_actions.c > ${CMAKE_CURRENT_BINARY_DIR}/keywords.inc
)

add_dependencies(irtoy_tool Keywords)
add_custom_target(Keywords DEPENDS  ${CMAKE_CURRENT_BINARY_DIR}/keywords.inc)

include_directories( ${CMAKE_CURRENT_BINARY_DIR} )
include_directories( ${CMAKE_CURRENT_SOURCE_DIR}/toolbag/dict )
