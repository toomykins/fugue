# account/: the pager's pure-function core: on-disk formats, key.txt discovery, and sign-in helpers.
# NO SOCKETS in this module. Qt6::Core only (QJson for pager.json so unknown keys survive a save).
# Self-contained: the library, tool and tests are all built from this directory.
file(GLOB acct_srcs CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/*.cpp)
add_library(fugue_account OBJECT ${acct_srcs})
target_include_directories(fugue_account PUBLIC ${PROJECT_SOURCE_DIR})
target_link_libraries(fugue_account PUBLIC Qt6::Core)

# fugue-account: command-line helper (reads and writes the on-disk formats, prints canonical JSON) used by ctest.
file(GLOB acct_tool_srcs CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/tool/*.cpp)
add_executable(fugue-account ${acct_tool_srcs})
target_link_libraries(fugue-account PRIVATE fugue_account fugue_net fugue_proto fugue_canon Qt6::Core)

# account/tests/*_test.cpp -> one suite in fugue_tests + one ctest each (names: account_<stem>).
set(CMAKE_AUTOMOC ON)
file(GLOB acct_test_srcs CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/tests/*_test.cpp)
foreach(src ${acct_test_srcs})
  get_filename_component(stem ${src} NAME_WE)
  fugue_test_suite(${stem} ${src})
  target_link_libraries(${stem} PRIVATE fugue_account fugue_net fugue_proto fugue_canon Qt6::Core Qt6::Test)
  target_compile_definitions(${stem} PRIVATE FUGUE_ACCOUNT_BIN="$<TARGET_FILE:fugue-account>")
  add_dependencies(${stem} fugue-account)
  string(REGEX REPLACE "_test$" "" name ${stem})
  fugue_add_test(${name} fugue_tests ${name} -o -,txt)
endforeach()
