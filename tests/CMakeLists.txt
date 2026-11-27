set(CMAKE_AUTOMOC ON)
# Each tests/*_test.cpp becomes a suite in fugue_tests and a ctest named <stem>.
# New files are picked up on the next build (CONFIGURE_DEPENDS); no list to edit.
file(GLOB test_srcs CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/*_test.cpp)
foreach(src ${test_srcs})
  get_filename_component(stem ${src} NAME_WE)
  if(WIN32 AND stem STREQUAL "platform_test")
    continue() # POSIX descriptor/pipe behavior; not available on Windows.
  endif()
  string(REGEX REPLACE "_test$" "" name ${stem})
  fugue_test_suite(${stem} ${src})
  target_link_libraries(${stem} PRIVATE fugue_audio fugue_net fugue_proto fugue_model fugue_render fugue_skin fugue_canon Qt6::Core Qt6::Test)
  target_compile_definitions(${stem} PRIVATE
    FUGUE_QT_ROOT="${PROJECT_SOURCE_DIR}"
    FUGUE_TEST_DATA_DIR="${CMAKE_CURRENT_SOURCE_DIR}/data")
  fugue_add_test(${name} fugue_tests ${name} -o -,txt)
endforeach()

if(WIN32 OR APPLE)
  if(APPLE)
    target_compile_definitions(audio_sink_test PRIVATE FUGUE_TEST_APPLE=1)
  endif()
  fugue_test_entry(audio_device fugue_run_audio_device)
  fugue_add_test(audio_device fugue_tests audio_device -o -,txt)
  set_tests_properties(audio_device PROPERTIES LABELS "audio;device" RUN_SERIAL TRUE)
endif()

fugue_test_entry(gdifon_host fugue_run_gdifon_host)
fugue_add_test(gdifon_host fugue_tests gdifon_host -o -,txt)
# The one suite that may skip: it checks a font only some hosts have.
set_tests_properties(gdifon_host PROPERTIES LABELS font
  FAIL_REGULAR_EXPRESSION "" SKIP_REGULAR_EXPRESSION "SKIP[ \t]*:")
