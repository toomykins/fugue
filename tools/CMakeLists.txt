# canon_json is a library (OBJECT, globbed).
fugue_module(fugue_canon DIR ${CMAKE_CURRENT_SOURCE_DIR}/canon_json)
add_executable(relay_wire_probe EXCLUDE_FROM_ALL relay_wire_probe.cpp)
target_link_libraries(relay_wire_probe PRIVATE fugue_net fugue_model fugue_proto fugue_canon)
if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/private.cmake)
  include(${CMAKE_CURRENT_SOURCE_DIR}/private.cmake)
endif()
