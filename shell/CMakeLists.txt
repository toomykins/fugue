# shell/: Qt Widgets + WebEngine UI layer. A STATIC library, not an
# fugue_module OBJECT lib: it needs AUTOMOC and Qt6::Widgets/WebEngineWidgets. Only configured when those
# components were found (top-level CMakeLists.txt), so the headless modules never depend on WebEngine.
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)   # fugue.qrc: Fugue's icon (shell/fugueicon.h)
file(GLOB shell_srcs CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/*.cpp)
add_library(fugue_shell STATIC ${shell_srcs} ${CMAKE_CURRENT_SOURCE_DIR}/fugue.qrc)
target_include_directories(fugue_shell PUBLIC ${PROJECT_SOURCE_DIR})
# fugue_canon: model/json.cpp (pulled in by render/scene.cpp and model::plainAtomName) calls canon::str.
target_link_libraries(fugue_shell PUBLIC fugue_skin fugue_render fugue_proto fugue_model fugue_net fugue_canon Qt6::Widgets Qt6::WebEngineWidgets Qt6::WebChannel Qt6::Network)

# Test targets: each is added only when its source file exists, so a tree that ships a subset of tests still configures.
set(CMAKE_AUTOMOC ON)
set(_shell_test_env "QT_QPA_PLATFORM=offscreen;QTWEBENGINE_CHROMIUM_FLAGS=--no-sandbox --disable-gpu")
# fugue_shell_test(<ctest name> <source stem> <description>)
function(fugue_shell_test name stem)
  if(NOT EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/tests/${stem}.cpp)
    return()
  endif()
  add_executable(${stem} tests/${stem}.cpp)
  target_link_libraries(${stem} PRIVATE fugue_shell Qt6::Test Qt6::Network)
  add_test(NAME ${name} COMMAND ${stem} -o -,txt)
  set_tests_properties(${name} PROPERTIES ENVIRONMENT "${_shell_test_env}")
endfunction()

fugue_shell_test(shell_urlopener urlopener_test)   # macOS QFileOpenEvent URL delivery, without Launch Services
fugue_shell_test(shell          shell_test)       # one QtTest executable
fugue_shell_test(shell_skincolor skincolor_test)  # winset colours -> widgets
fugue_shell_test(shell_input    input_test)       # pointer events -> MapPointer, keys -> their macro bindings only, command routing
fugue_shell_test(shell_live     live_test)        # winset -> widgets, menu bar, skin button / menu commands, cross-thread sync
fugue_shell_test(shell_browse   browse_test)      # browse(): popups, loopback origin, page<->client bridge, chat page
fugue_shell_test(shell_recipefiler recipefiler_test)  # browse_rsc runtime icons (DDMI recipes) filed for pages as PNG
fugue_shell_test(shell_prompt   prompt_test)      # alert()/input() PromptHost, the INFO control's StatPanelWidget, paste in every key-taking widget
fugue_shell_test(shell_ttmetrics ttmetrics_test)  # MeasureText's TrueType metrics
fugue_shell_test(shell_skinfont skinfont_test)    # skin control fonts: stock defaults, .dmf and winset font-family/size/style
fugue_shell_test(shell_serverkey serverkey_test)  # the per-server macro key across a link() to the same server
if(TARGET browse_test)
  target_sources(browse_test PRIVATE ${PROJECT_SOURCE_DIR}/app/renderthread.cpp)   # the map-remount test drives the real render thread
endif()

# Source scan: every editable widget constructed in shell/ and app/ must be registered with the paste helper.
if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/tests/paste_scan_test.cpp)
  add_executable(paste_scan_test tests/paste_scan_test.cpp)
  target_link_libraries(paste_scan_test PRIVATE Qt6::Test Qt6::Core)
  target_compile_definitions(paste_scan_test PRIVATE FUGUE_SOURCE_ROOT="${PROJECT_SOURCE_DIR}")
  add_test(NAME shell_paste_scan COMMAND paste_scan_test -o -,txt)
endif()
