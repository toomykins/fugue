# pager/: the pager's logic and window. Self-contained like account/: the library, helper and tests live here.
#   fugue_pager     OBJECT  hub listing parser, parseTarget, pager model, `-list` text, Clear cache (links net::RscStore,
#                           so consumers link fugue_net). Qt6::Core only (sockets only in hublink).
#   fugue-pager     tool    command-line helper for the pager's logic and sign-in
#   fugue_pager_ui  STATIC  Qt Widgets window. Only when Widgets exists and FUGUE_BUILD_SHELL is not OFF (undefined counts as ON).
file(GLOB pager_srcs CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/*.cpp)
add_library(fugue_pager OBJECT ${pager_srcs})
target_include_directories(fugue_pager PUBLIC ${PROJECT_SOURCE_DIR})
target_link_libraries(fugue_pager PUBLIC fugue_account Qt6::Core)

add_executable(fugue-pager ${CMAKE_CURRENT_SOURCE_DIR}/tool/main.cpp)
target_link_libraries(fugue-pager PRIVATE fugue_pager fugue_account fugue_net fugue_proto fugue_canon Qt6::Core)

set(CMAKE_AUTOMOC ON)
set(pager_test_libs fugue_pager fugue_account fugue_net fugue_proto fugue_canon Qt6::Core Qt6::Test)
file(GLOB pager_test_srcs CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/tests/*_test.cpp)
foreach(src ${pager_test_srcs})
  get_filename_component(stem ${src} NAME_WE)
  if(stem STREQUAL "pager_ui_test")
    continue()   # needs Widgets; declared below
  endif()
  fugue_test_suite(${stem} ${src})
  target_link_libraries(${stem} PRIVATE ${pager_test_libs})
  target_compile_definitions(${stem} PRIVATE FUGUE_PAGER_BIN="$<TARGET_FILE:fugue-pager>")
  add_dependencies(${stem} fugue-pager)
  string(REGEX REPLACE "_test$" "" name ${stem})
  fugue_add_test(${name} fugue_tests ${name} -o -,txt)
endforeach()

if((NOT DEFINED FUGUE_BUILD_SHELL) OR FUGUE_BUILD_SHELL)
  find_package(Qt6 QUIET OPTIONAL_COMPONENTS Widgets Network)
  if(Qt6Widgets_FOUND AND Qt6Network_FOUND)
    file(GLOB pager_ui_srcs CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/ui/*.cpp)
    add_library(fugue_pager_ui STATIC ${pager_ui_srcs})
    target_include_directories(fugue_pager_ui PUBLIC ${PROJECT_SOURCE_DIR})
    target_link_libraries(fugue_pager_ui PUBLIC fugue_pager fugue_account Qt6::Widgets Qt6::Network)

    # fugue-pager-shot: offscreen screenshot of each tab.
    add_executable(fugue-pager-shot ${CMAKE_CURRENT_SOURCE_DIR}/tool/shot.cpp)
    target_link_libraries(fugue-pager-shot PRIVATE fugue_pager_ui fugue_net fugue_proto fugue_canon Qt6::Widgets)

    if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/tests/pager_ui_test.cpp)
      fugue_test_suite(pager_ui_test ${CMAKE_CURRENT_SOURCE_DIR}/tests/pager_ui_test.cpp)
      target_link_libraries(pager_ui_test PRIVATE fugue_pager_ui fugue_net fugue_proto fugue_canon Qt6::Widgets Qt6::Test)
      fugue_add_test(pager_ui fugue_tests pager_ui -o -,txt)
    endif()
  else()
    message(STATUS "pager: Qt Widgets or Network not found - skipping the pager window")
  endif()
endif()
