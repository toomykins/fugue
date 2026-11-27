# shell/: Qt Widgets + WebEngine UI layer. A STATIC library, not an
# fugue_module OBJECT lib: it needs AUTOMOC and Qt6::Widgets/WebEngineWidgets. Only configured when those
# components were found (top-level CMakeLists.txt), so the headless modules never depend on WebEngine.
# Older Qt SDKs export GuiPrivate from Gui; newer SDKs provide it as a separate component.
if(NOT TARGET Qt6::GuiPrivate)
  find_package(Qt6 6.7 REQUIRED COMPONENTS GuiPrivate)
endif()
find_package(Qt6 6.7 REQUIRED COMPONENTS ShaderTools)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)   # fugue.qrc: Fugue's icon (shell/fugueicon.h)
file(GLOB shell_srcs CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/*.cpp)
add_library(fugue_shell STATIC ${shell_srcs} ${CMAKE_CURRENT_SOURCE_DIR}/fugue.qrc)
target_include_directories(fugue_shell PUBLIC ${PROJECT_SOURCE_DIR})
# fugue_canon: model/json.cpp calls canon::str.
target_link_libraries(fugue_shell PUBLIC fugue_skin fugue_render fugue_proto fugue_model fugue_net fugue_canon Qt6::Widgets Qt6::WebEngineWidgets Qt6::WebChannel Qt6::Network)

find_package(ZLIB REQUIRED) # bounded DMI PNG metadata decompression
target_link_libraries(fugue_shell PRIVATE Qt6::GuiPrivate ZLIB::ZLIB)
qt_add_shaders(fugue_shell mapshaders
  PREFIX "/map"
  GLSL "100es,120,150,300es,330"
  FILES shaders/grid.vert shaders/grid.frag shaders/sprite.vert shaders/sprite.frag)

# Suites are compiled separately and linked into the shared test runner.
set(CMAKE_AUTOMOC ON)
# fugue_shell_test(<ctest name> <source stem> [<shards>])
# With <shards> the suite runs as that many entries, <ctest name>_1of<shards> and so on (fugue_add_sharded_test).
function(fugue_shell_test name stem)
  if(NOT EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/tests/${stem}.cpp)
    return()
  endif()
  fugue_test_suite(${stem} tests/${stem}.cpp)
  target_link_libraries(${stem} PRIVATE fugue_shell Qt6::Test Qt6::Network)
  string(REGEX REPLACE "_test$" "" suite "${stem}")
  if(ARGC GREATER 2)
    fugue_add_sharded_test(${name} ${ARGV2} fugue_tests ${suite} -o -,txt)
  else()
    fugue_add_test(${name} fugue_tests ${suite} -o -,txt)
  endif()
endfunction()

# Every GPU and native entry runs on one graphics backend, which its environment names (FUGUE_GPU_BACKEND), so a
# developer's own environment cannot change it; the suites check the device they got. The tests themselves do not
# choose. Off-screen devices (tests/gpucomparison.h) are Direct3D 11, OpenGL or Vulkan; a MapWidget can also use
# Metal.
# The machine's own adapter is used; FUGUE_GPU_SOFTWARE=1 in the caller's environment asks for a software one.
#
# Which backends get entries is FUGUE_TEST_GPU_BACKENDS (cmake/FugueTests.cmake). Qt declares its Vulkan types only
# where the Vulkan headers are installed: fugue_shell_vulkan_headers(<target>) tells an off-screen suite.
if(WIN32)
  set(_shell_native_platform windows)
  set(_shell_gpu_all d3d11 opengl)
  set(_shell_native_all d3d11 opengl)
  set(_shell_default d3d11)
  if(TARGET Vulkan::Headers)
    list(APPEND _shell_gpu_all vulkan)
    list(APPEND _shell_native_all vulkan)
    set(_shell_default vulkan)
  else()
    message(STATUS "Vulkan headers not found: GPU tests default to Direct3D 11 (install vcpkg's vulkan-headers)")
  endif()
elseif(APPLE)
  set(_shell_native_platform cocoa)
  set(_shell_gpu_all opengl)
  set(_shell_native_all metal opengl)
  set(_shell_default metal opengl)   # no off-screen Metal device: the off-screen suites need OpenGL
else()
  set(_shell_native_platform xcb)
  set(_shell_gpu_all opengl)
  set(_shell_native_all opengl)
  if(TARGET Vulkan::Headers)
    list(APPEND _shell_gpu_all vulkan)
    list(APPEND _shell_native_all vulkan)
  endif()
  set(_shell_default opengl)
endif()
if(FUGUE_TEST_GPU_BACKENDS STREQUAL "all")
  set(_shell_gpu_backends ${_shell_gpu_all})
  set(_shell_native_backends ${_shell_native_all})
else()
  set(_shell_wanted ${FUGUE_TEST_GPU_BACKENDS})
  if(FUGUE_TEST_GPU_BACKENDS STREQUAL "default")
    set(_shell_wanted ${_shell_default})
  endif()
  set(_shell_gpu_backends)
  set(_shell_native_backends)
  foreach(backend IN LISTS _shell_wanted)
    if(NOT backend IN_LIST _shell_native_all)
      message(FATAL_ERROR "FUGUE_TEST_GPU_BACKENDS names '${backend}', which this build cannot test "
        "(available: ${_shell_native_all}; vulkan needs vcpkg's vulkan-headers)")
    endif()
    list(APPEND _shell_native_backends ${backend})
    if(backend IN_LIST _shell_gpu_all)
      list(APPEND _shell_gpu_backends ${backend})
    endif()
  endforeach()
endif()
message(STATUS "GPU test backends: off-screen ${_shell_gpu_backends}; native ${_shell_native_backends}")
# The headless backend comes first: a GPU or native entry names its own after it, and the later one counts.
set(_shell_profile_env "FUGUE_AUDIO=none;FUGUE_GPU_BACKEND=${FUGUE_TEST_BACKEND};QTWEBENGINE_CHROMIUM_FLAGS=--no-sandbox")
function(fugue_shell_vulkan_headers target)
  if(TARGET Vulkan::Headers)
    target_link_libraries(${target} PRIVATE Vulkan::Headers)
    target_compile_definitions(${target} PRIVATE FUGUE_TEST_VULKAN=1)
  endif()
endfunction()
# fugue_shell_gpu_test(<name> <labels> <command...>): <name>_<backend> for each off-screen backend. Direct3D 11
# draws into textures without a display; OpenGL and Vulkan need the window-system connection even then (label
# display).
function(fugue_shell_gpu_test name labels)
  foreach(backend IN LISTS _shell_gpu_backends)
    fugue_add_test(${name}_${backend} ${ARGN})
    if(backend STREQUAL "d3d11")
      set_tests_properties(${name}_${backend} PROPERTIES LABELS "${labels}"
        ENVIRONMENT "${_shell_profile_env};FUGUE_GPU_BACKEND=${backend};QT_QPA_PLATFORM=offscreen")
    else()
      set_tests_properties(${name}_${backend} PROPERTIES LABELS "${labels};display"
        ENVIRONMENT "${_shell_profile_env};FUGUE_GPU_BACKEND=${backend};QT_QPA_PLATFORM=${_shell_native_platform}")
    endif()
  endforeach()
endfunction()
# fugue_shell_native_test(<name> <labels> <command...>): <name>_<backend> for each backend a real MapWidget has
# here, one at a time on the desktop.
function(fugue_shell_native_test name labels)
  foreach(backend IN LISTS _shell_native_backends)
    fugue_add_test(${name}_${backend} ${ARGN})
    set_tests_properties(${name}_${backend} PROPERTIES LABELS "${labels}" RUN_SERIAL TRUE
      ENVIRONMENT "${_shell_profile_env};FUGUE_GPU_BACKEND=${backend};QT_QPA_PLATFORM=${_shell_native_platform}")
  endforeach()
endfunction()

fugue_shell_test(shell_urlopener urlopener_test)   # macOS QFileOpenEvent URL delivery, without Launch Services
fugue_shell_test(shell_statview statview_test) # worker INFO thumbnails and row lifetime validation
if(TARGET statview_test)
  target_link_libraries(statview_test PRIVATE fugue_proto) # decoder registration objects for real stat frames
endif()
fugue_shell_test(shell_stathover stathover_test) # stat panel hover against stock's recorded reports, and the widget's picks
if(TARGET stathover_test)
  target_link_libraries(stathover_test PRIVATE fugue_proto)
  target_compile_definitions(stathover_test PRIVATE FUGUE_TEST_DATA_DIR="${PROJECT_SOURCE_DIR}/tests/data")
endif()
fugue_shell_test(shell_atomdiagnostic atomdiagnostic_test) # bounded worker-side atom/INFO evidence
fugue_shell_test(shell_iconcache iconcache_test) # generated icon recipes and resource dependencies
fugue_shell_test(shell_assetrecovery assetrecovery_test)
if(TARGET assetrecovery_test)
  target_compile_definitions(assetrecovery_test PRIVATE FUGUE_TEST_DATA_DIR="${PROJECT_SOURCE_DIR}/tests/data")
  target_link_libraries(assetrecovery_test PRIVATE fugue_proto)
endif()
fugue_shell_test(shell_turfmap  turfmap_test)     # DMI decoding and turf-only composition
fugue_shell_test(shell_mapvisibility mapvisibility_test)
if(TARGET mapvisibility_test)
  target_link_libraries(mapvisibility_test PRIVATE fugue_proto)
  target_compile_definitions(mapvisibility_test PRIVATE FUGUE_TEST_DATA_DIR="${PROJECT_SOURCE_DIR}/tests/data")
endif()
foreach(animation clientanim movanim visanim runechat)
  fugue_shell_test(shell_${animation} ${animation}_test)
  if(TARGET ${animation}_test)
    set_property(TEST shell_${animation} APPEND PROPERTY LABELS animation)
  endif()
endforeach()
if(TARGET clientanim_test)
  set_property(TEST shell_clientanim APPEND PROPERTY LABELS client-animation)
endif()
fugue_shell_test(shell_composition composition_test)
if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/tests/masteropacity_test.cpp)
  fugue_test_suite(masteropacity_test tests/masteropacity_test.cpp)
  target_link_libraries(masteropacity_test PRIVATE fugue_shell Qt6::Test Qt6::GuiPrivate)
  fugue_shell_vulkan_headers(masteropacity_test)
  target_compile_definitions(masteropacity_test PRIVATE FUGUE_TEST_DATA_DIR="${PROJECT_SOURCE_DIR}/tests/data")
  fugue_add_test(shell_masteropacity_portable fugue_tests masteropacity portable lifetimes -o -,txt)
  set_tests_properties(shell_masteropacity_portable PROPERTIES LABELS "master-opacity;portable;fast"
    ENVIRONMENT "${_shell_profile_env};QT_QPA_PLATFORM=offscreen")
  fugue_shell_gpu_test(shell_masteropacity_gpu "master-opacity;gpu" fugue_tests masteropacity gpu -o -,txt)
  fugue_shell_native_test(shell_masteropacity_native "master-opacity;native" fugue_tests masteropacity native -o -,txt)
endif()
fugue_shell_test(shell_compositionplan compositionplan_test) # shared model-free graph and admission
if(TARGET compositionplan_test)
  target_link_libraries(compositionplan_test PRIVATE fugue_proto)
  target_compile_definitions(compositionplan_test PRIVATE FUGUE_TEST_DATA_DIR="${PROJECT_SOURCE_DIR}/tests/data")
  set_property(TEST shell_compositionplan APPEND PROPERTY LABELS composition)
endif()
if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/tests/compositiongpu_test.cpp)
  fugue_test_suite(compositiongpu_test tests/compositiongpu_test.cpp)
  target_link_libraries(compositiongpu_test PRIVATE fugue_shell Qt6::Test Qt6::GuiPrivate)
  fugue_shell_vulkan_headers(compositiongpu_test)
  target_compile_definitions(compositiongpu_test PRIVATE FUGUE_TEST_DATA_DIR="${PROJECT_SOURCE_DIR}/tests/data")
  fugue_shell_gpu_test(shell_composition_stock_gpu "composition;gpu" fugue_tests compositiongpu stockParityReplay -o -,txt)
  fugue_shell_gpu_test(shell_composition_transform_gpu "composition;gpu" fugue_tests compositiongpu masterTransformFilters -o -,txt)
  fugue_shell_gpu_test(shell_composition_animated_master_gpu "composition;gpu" fugue_tests compositiongpu animatedMasterReplay -o -,txt)
fugue_shell_gpu_test(shell_composition_plane_relay_gpu "composition;gpu" fugue_tests compositiongpu planeRelayReplay -o -,txt)
  fugue_shell_gpu_test(shell_composition_wide_map_gpu "composition;gpu" fugue_tests compositiongpu masterLocWideReplay -o -,txt)
  fugue_shell_gpu_test(shell_composition_material_gpu "composition;gpu" fugue_tests compositiongpu contributorMaterialGpu -o -,txt)
endif()
foreach(animation_test clientanim_test movanim_test visanim_test runechat_test composition_test)
  if(TARGET ${animation_test})
    target_compile_definitions(${animation_test} PRIVATE FUGUE_TEST_DATA_DIR="${PROJECT_SOURCE_DIR}/tests/data")
    target_link_libraries(${animation_test} PRIVATE fugue_proto)
  endif()
endforeach()
# The longest CPU-bound suite, and independent test functions: four entries for `ctest -j`.
fugue_shell_test(shell_entitymap entitymap_test 4) # immutable world sprites, movement, ordering and picking
if(TARGET entitymap_test)
  target_link_libraries(entitymap_test PRIVATE fugue_proto) # include decoder registrations for ingest-clock tests
  target_compile_definitions(entitymap_test PRIVATE FUGUE_TEST_DATA_DIR="${PROJECT_SOURCE_DIR}/tests/data")
endif()
fugue_shell_test(shell_viscontents viscontents_test)
if(TARGET viscontents_test)
  target_link_libraries(viscontents_test PRIVATE fugue_proto)
  target_compile_definitions(viscontents_test PRIVATE FUGUE_TEST_DATA_DIR="${PROJECT_SOURCE_DIR}/tests/data")
endif()
fugue_shell_test(shell_maptext maptext_test) # bounded worker maptext rasterization and scene integration
target_compile_definitions(maptext_test PRIVATE FUGUE_TEST_DATA_DIR="${PROJECT_SOURCE_DIR}/tests/data")
fugue_shell_test(shell_mapinput mapinput_test) # camera-aware map coordinates and mouse/wheel packets
if(TARGET mapinput_test)
  target_link_libraries(mapinput_test PRIVATE fugue_proto) # independent wire-backed input regressions
  target_compile_definitions(mapinput_test PRIVATE FUGUE_TEST_DATA_DIR="${PROJECT_SOURCE_DIR}/tests/data")
endif()
fugue_shell_test(shell_mapmenu mapmenu_test) # right-click menu entries, verb labels and pictures against stock
if(TARGET mapmenu_test)
  target_link_libraries(mapmenu_test PRIVATE fugue_proto)
  target_compile_definitions(mapmenu_test PRIVATE FUGUE_TEST_DATA_DIR="${PROJECT_SOURCE_DIR}/tests/data")
endif()
fugue_shell_test(shell_mapmailbox mapmailbox_test) # coalesced notifications and newest-frame delivery
fugue_shell_test(shell_map      map_test)         # portable configuration, routing and skin rebuilds
if(TARGET map_test)
  set_property(TEST shell_map APPEND PROPERTY LABELS map)
endif()
# Rendering, presentation and input through a real MapWidget.
if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/tests/mapnative_test.cpp)
  fugue_test_suite(mapnative_test tests/mapnative_test.cpp)
  target_link_libraries(mapnative_test PRIVATE fugue_shell fugue_proto Qt6::Test Qt6::Network Qt6::GuiPrivate)
  target_compile_definitions(mapnative_test PRIVATE FUGUE_TEST_DATA_DIR="${PROJECT_SOURCE_DIR}/tests/data")
  fugue_shell_native_test(shell_map_native "map;native" fugue_tests mapnative -o -,txt)
  foreach(backend IN LISTS _shell_native_backends)
    set_tests_properties(shell_map_native_${backend} PROPERTIES TIMEOUT 300)
  endforeach()
endif()
# Desktop-composited pixels; windows must stay foreground and unobstructed.
if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/tests/mapdesktop_test.cpp)
  fugue_test_suite(mapdesktop_test tests/mapdesktop_test.cpp)
  target_link_libraries(mapdesktop_test PRIVATE fugue_shell Qt6::Test Qt6::Network)
  fugue_shell_native_test(shell_map_desktop "map;native;desktop" fugue_tests mapdesktop -o -,txt)
  # Another program's window over the test's own is a desktop in use, not a failure: reported as not run.
  foreach(backend IN LISTS _shell_native_backends)
    set_tests_properties(shell_map_desktop_${backend} PROPERTIES SKIP_REGULAR_EXPRESSION "Desktop interference:")
  endforeach()
endif()
fugue_shell_test(shell          shell_test)       # shell input routing
fugue_shell_test(shell_skincolor skincolor_test)  # winset colours -> widgets
fugue_shell_test(shell_clientmenu clientmenu_test) # skin menu transitions and client-only hotkey routing
if(TARGET clientmenu_test)
  fugue_test_entry(clientmenu_native fugue_run_clientmenu_native)
  fugue_add_test(shell_clientmenu_native fugue_tests clientmenu_native -o -,txt)
  set_tests_properties(shell_clientmenu_native PROPERTIES LABELS "clientmenu;native" RUN_SERIAL TRUE
    ENVIRONMENT "${_shell_profile_env};QT_QPA_PLATFORM=${_shell_native_platform}")
endif()
fugue_shell_test(shell_input    input_test)       # keys -> their macro bindings only, command routing
fugue_shell_test(shell_screenshot screenshot_test) # PNG saves, naming, default screenshot controls
fugue_shell_test(shell_live     live_test)        # winset -> widgets, menu bar, skin button / menu commands, cross-thread sync
fugue_shell_test(shell_browse   browse_test)      # browse(): popups, loopback origin, page<->client bridge, chat page
if(TARGET browse_test)
  fugue_test_entry(browse_dock fugue_run_browse_dock)
  fugue_test_entry(browse_runtime fugue_run_browse_runtime)
  fugue_add_test(shell_browse_dock fugue_tests browse_dock -o -,txt)
  fugue_add_test(shell_browse_runtime fugue_tests browse_runtime -o -,txt)
  # WebEngine start-up and page loads: portable, but outside the fast tier.
  set_tests_properties(shell_browse shell_browse_dock shell_browse_runtime PROPERTIES LABELS portable)
endif()
fugue_shell_test(shell_recipefiler recipefiler_test)  # browse_rsc runtime icons (DDMI recipes) filed for pages as PNG
fugue_shell_test(shell_prompt   prompt_test)      # alert()/input() PromptHost, the INFO control's StatPanelWidget, paste in every key-taking widget
if(TARGET prompt_test AND NOT WIN32 AND NOT APPLE)
  target_compile_definitions(prompt_test PRIVATE FUGUE_TEST_X11_PRIMARY_SELECTION=1)
  fugue_test_entry(prompt_primary_selection fugue_run_prompt_primary_selection)
  fugue_add_test(shell_prompt_primary_selection fugue_tests prompt_primary_selection -o -,txt)
  set_tests_properties(shell_prompt_primary_selection PROPERTIES LABELS "prompt;native;x11" RUN_SERIAL TRUE
    ENVIRONMENT "${_shell_profile_env};QT_QPA_PLATFORM=xcb")
endif()
fugue_shell_test(shell_skinfont skinfont_test)    # skin control fonts: stock defaults, .dmf and winset font-family/size/style
fugue_shell_test(shell_serverkey serverkey_test)  # the per-server macro key across a link() to the same server

# Source scan: every editable widget constructed in shell/ and app/ must be registered with the paste helper.
if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/tests/paste_scan_test.cpp)
  fugue_test_suite(paste_scan_test tests/paste_scan_test.cpp)
  target_link_libraries(paste_scan_test PRIVATE Qt6::Test Qt6::Core)
  target_compile_definitions(paste_scan_test PRIVATE FUGUE_SOURCE_ROOT="${PROJECT_SOURCE_DIR}")
  fugue_add_test(shell_paste_scan fugue_tests paste_scan -o -,txt)
endif()
fugue_shell_test(shell_chaticons chaticons_test)  # \icon[x] in chat: the IMG tag becomes the state cell inline
