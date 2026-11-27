# app/: the fugue-qt window binary (session -> model -> shell, plus the launcher and pager).
set(CMAKE_AUTOMOC ON)
file(GLOB app_srcs CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/*.cpp)
add_executable(fugue-qt ${app_srcs})
target_link_libraries(fugue-qt PRIVATE fugue_shell fugue_audio fugue_net fugue_proto fugue_model fugue_skin fugue_canon fugue_account fugue_pager fugue_pager_ui
                      Qt6::Widgets Qt6::WebEngineWidgets Qt6::WebChannel)
if(FUGUE_RELEASE)
  target_compile_definitions(fugue-qt PRIVATE FUGUE_RELEASE=1)   # no FUGUE_AUTO_* hooks, no pager --auto-join/--quit-after, disable core dumps
endif()
fugue_add_test(version fugue-qt --version)
set_tests_properties(version PROPERTIES PASS_REGULAR_EXPRESSION "^fugue-qt ${PROJECT_VERSION}(-dev)?\n")
fugue_add_test(webcheck fugue-qt webcheck)
set_tests_properties(webcheck PROPERTIES PASS_REGULAR_EXPRESSION "webcheck ok\n"
  LABELS portable) # starts WebEngine: outside the fast tier
if(WIN32 AND TARGET Qt6::QOffscreenIntegrationPlugin)
  # The deployed app's qt.conf points at its minimal plugin bundle (windows only).
  # This developer-only test needs offscreen from the selected SDK, not extra shipped plugins.
  set_property(TEST webcheck APPEND PROPERTY ENVIRONMENT
    "QT_QPA_PLATFORM_PLUGIN_PATH=$<TARGET_FILE_DIR:Qt6::QOffscreenIntegrationPlugin>")
endif()
fugue_add_test(pager_url_one_argument fugue-qt pager --url "byond://127.0.0.1:1" -keytxt x)
set_tests_properties(pager_url_one_argument PROPERTIES PASS_REGULAR_EXPRESSION "pager: --url takes exactly one link")
if(APPLE)
  # Declare the SVG plugin's framework explicitly so macdeployqt can resolve it even with split Qt installations.
  find_package(Qt6 REQUIRED COMPONENTS Svg)
  # LSMinimumSystemVersion: the deployment target, else this Mac's version (what the compiler then targets at most).
  set(FUGUE_MACOS_MIN "${CMAKE_OSX_DEPLOYMENT_TARGET}")
  if(NOT FUGUE_MACOS_MIN)
    execute_process(COMMAND sw_vers -productVersion OUTPUT_VARIABLE FUGUE_MACOS_MIN OUTPUT_STRIP_TRAILING_WHITESPACE COMMAND_ERROR_IS_FATAL ANY)
  endif()
  target_link_libraries(fugue-qt PRIVATE Qt6::Svg)
  set_target_properties(fugue-qt PROPERTIES
    MACOSX_BUNDLE TRUE
    OUTPUT_NAME Fugue
    MACOSX_BUNDLE_INFO_PLIST "${PROJECT_SOURCE_DIR}/packaging/macos/Info.plist.in")
  set(_icon "${CMAKE_CURRENT_BINARY_DIR}/Fugue.icns")
  file(GLOB _icon_sources "${PROJECT_SOURCE_DIR}/packaging/flatpak/icons/*.png")
  add_custom_command(OUTPUT "${_icon}"
    COMMAND ${CMAKE_COMMAND}
      "-DSOURCE_DIR=${PROJECT_SOURCE_DIR}/packaging/flatpak/icons"
      "-DOUTPUT_DIR=${CMAKE_CURRENT_BINARY_DIR}"
      -P "${PROJECT_SOURCE_DIR}/packaging/macos/Icon.cmake"
    DEPENDS ${_icon_sources} "${PROJECT_SOURCE_DIR}/packaging/macos/Icon.cmake"
    VERBATIM)
  target_sources(fugue-qt PRIVATE "${_icon}")
  set_source_files_properties("${_icon}" PROPERTIES MACOSX_PACKAGE_LOCATION Resources)
endif()
install(TARGETS fugue-qt BUNDLE DESTINATION . RUNTIME DESTINATION bin)
if(UNIX AND NOT APPLE)
  # Menu entry, icons and metadata for the AppImage (the Flatpak manifest installs the same files itself).
  set(_fugue_id io.github.toomykins.Fugue)
  set(_fugue_pkg "${PROJECT_SOURCE_DIR}/packaging/flatpak")
  install(FILES "${_fugue_pkg}/${_fugue_id}.desktop" DESTINATION share/applications)
  install(FILES "${_fugue_pkg}/${_fugue_id}.svg" DESTINATION share/icons/hicolor/scalable/apps)
  foreach(_s 16 24 32 48 64 128 256)
    install(FILES "${_fugue_pkg}/icons/${_fugue_id}-${_s}.png" DESTINATION share/icons/hicolor/${_s}x${_s}/apps
            RENAME ${_fugue_id}.png)
  endforeach()
  install(FILES "${_fugue_pkg}/${_fugue_id}.metainfo.xml" DESTINATION share/metainfo)
  install(FILES "${PROJECT_SOURCE_DIR}/LICENSE" DESTINATION share/doc/fugue RENAME copyright)
endif()
if(APPLE)
  # Use the deployment tool from the selected Qt, not another Qt installation on PATH.
  get_target_property(_qmake Qt6::qmake IMPORTED_LOCATION)
  execute_process(COMMAND "${_qmake}" -query QT_INSTALL_LIBS
    OUTPUT_VARIABLE FUGUE_QT_LIBS OUTPUT_STRIP_TRAILING_WHITESPACE COMMAND_ERROR_IS_FATAL ANY)
  execute_process(COMMAND "${_qmake}" -query QT_INSTALL_BINS
    OUTPUT_VARIABLE _qt_bins OUTPUT_STRIP_TRAILING_WHITESPACE COMMAND_ERROR_IS_FATAL ANY)
  find_program(FUGUE_MACDEPLOYQT macdeployqt HINTS "${_qt_bins}" NO_DEFAULT_PATH REQUIRED)
  configure_file("${PROJECT_SOURCE_DIR}/packaging/macos/Deploy.cmake.in"
    "${CMAKE_CURRENT_BINARY_DIR}/Deploy.cmake" @ONLY)
  install(SCRIPT "${CMAKE_CURRENT_BINARY_DIR}/Deploy.cmake")
endif()
