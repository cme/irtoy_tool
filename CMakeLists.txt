cmake_minimum_required(VERSION 3.6)

project(irtoy)
add_executable(irtoy_tool
               irtoy_tool.c dict.c irtoy.c error.c
               keywords.c mac_actions.c)

add_custom_command(
  OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/keywords.inc
  DEPENDS update_keywords.pl irtoy_tool.c
  mac_actions.c
  COMMAND ${PROJECT_SOURCE_DIR}/update_keywords.pl
)

include_directories( ${CMAKE_CURRENT_BINARY_DIR} )
