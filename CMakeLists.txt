cmake_minimum_required(VERSION 3.20)
project(fugue_qt VERSION 0.5.5 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE RelWithDebInfo)
endif()
if(MSVC)
  add_compile_options(/W4 /wd4100 /utf-8 /bigobj /Zc:__cplusplus)
  add_compile_definitions(_CRT_SECURE_NO_WARNINGS)
  # Narrow paths and argv are UTF-8 throughout the client, including the Qt-free modules.
  add_link_options("/MANIFESTINPUT:${CMAKE_CURRENT_SOURCE_DIR}/packaging/windows/fugue.manifest")
else()
  add_compile_options(-Wall -Wextra -Wno-unused-parameter)
endif()
if(WIN32)
  add_compile_definitions(NOMINMAX WIN32_LEAN_AND_MEAN _WIN32_WINNT=0x0A00)
  link_libraries(ws2_32 advapi32)
endif()
add_compile_definitions(FUGUE_VERSION="${PROJECT_VERSION}")   # fugue_version.h

# The metainfo must list the version being built.
file(GLOB _metainfo ${CMAKE_CURRENT_SOURCE_DIR}/packaging/flatpak/*.metainfo.xml)
if(_metainfo)
  file(READ ${_metainfo} _metainfo_text)
  if(NOT _metainfo_text MATCHES "<release version=\"${PROJECT_VERSION}\"")
    message(FATAL_ERROR "packaging/flatpak metainfo has no <release> for ${PROJECT_VERSION}")
  endif()
endif()

# Release build: the player-facing binary. The pager is the no-argument default. Test hooks (FUGUE_AUTO_*, pager
# --auto-join/--quit-after) are compiled out, and the client disables core dumps (PR_SET_DUMPABLE=0 on Linux).
option(FUGUE_RELEASE "build the shippable fugue-qt (pager default, no dev hooks)" OFF)

# Ubuntu names libjpeg's soname 8 and Debian 62; they are not interchangeable, so the .deb links it in (libjpeg-turbo8-dev).
option(FUGUE_STATIC_JPEG "link libjpeg statically instead of depending on the distribution's soname" OFF)

# Headless-only build: just the Qt-free/Core modules, tools and tests (no window, no pager UI). This is an explicit opt-out;
# without it a missing Qt Widgets/WebEngine/WebChannel is a configure ERROR, so nobody ends up with a build that silently
# contains no application.
option(FUGUE_HEADLESS_ONLY "build only the headless modules, tools and tests (no window binary, no pager UI)" OFF)
if(FUGUE_HEADLESS_ONLY)
  set(FUGUE_BUILD_SHELL OFF CACHE BOOL "build shell/ and the fugue-qt window binary" FORCE)
endif()

# Cap build parallelism: compile jobs for QtTest-heavy translation units are large (the heaviest measured about 860 MiB).
# The default is one job per GiB of memory available at configure time, at most one per core. Override per build with
# -DFUGUE_COMPILE_JOBS=N.
cmake_host_system_information(RESULT _fugue_mem_mib QUERY AVAILABLE_PHYSICAL_MEMORY)
cmake_host_system_information(RESULT _fugue_cores QUERY NUMBER_OF_LOGICAL_CORES)
math(EXPR _fugue_jobs "${_fugue_mem_mib} / 1024")
if(_fugue_jobs GREATER _fugue_cores)
  set(_fugue_jobs ${_fugue_cores})
endif()
if(_fugue_jobs LESS 1)
  set(_fugue_jobs 1)
endif()
set(FUGUE_COMPILE_JOBS ${_fugue_jobs} CACHE STRING "max concurrent compile jobs (Ninja job pool)")
set_property(GLOBAL PROPERTY JOB_POOLS fugue_compile=${FUGUE_COMPILE_JOBS} fugue_link=2)
set(CMAKE_JOB_POOL_COMPILE fugue_compile)
set(CMAKE_JOB_POOL_LINK fugue_link)

# Headless modules (net, proto, tools) need at most Qt6::Core; tests need Qt6::Test.
# Widgets and WebEngine are for shell/ only.
find_package(Qt6 REQUIRED COMPONENTS Core)
find_package(Qt6 QUIET OPTIONAL_COMPONENTS Test)   # only the test targets need it
find_package(ZLIB)   # gzip bundles; optional

enable_testing()
include_directories(${CMAKE_CURRENT_SOURCE_DIR})   # includes are "net/cipher.h", "proto/registry.h", ...
include(cmake/FugueModule.cmake)

# Module rule: a module directory's CMakeLists.txt calls `fugue_module(<target>)`, which globs <dir>/*.cpp into an
# OBJECT library (some modules add options or tools after it). New .cpp files need no edit to any shared list. A new
# module directory adds one name to the list below; model, skin, render, audio, account and pager are included when they have a CMakeLists.txt.
add_subdirectory(net)
add_subdirectory(proto)
add_subdirectory(tools)
foreach(m model skin render audio account pager)
  if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/${m}/CMakeLists.txt)
    add_subdirectory(${m})
  endif()
endforeach()

# Experimental GPU executor and differential tests; never adds GUI dependencies to fugue_render.
option(FUGUE_BUILD_GPU "build the experimental QRhi executor (Qt 6.7+, Gui private headers and ShaderTools)" OFF)
if(FUGUE_BUILD_GPU)
  add_subdirectory(rendergpu)
endif()

# shell/ + app/ (Qt Widgets + WebEngine) are REQUIRED unless you opt out: -DFUGUE_HEADLESS_ONLY=ON (or -DFUGUE_BUILD_SHELL=OFF)
# builds just the headless modules, tools and tests, which need no WebEngine.
option(FUGUE_BUILD_SHELL "build shell/ and the fugue-qt window binary (needs Qt Widgets + WebEngineWidgets + WebChannel)" ON)
if(FUGUE_BUILD_SHELL)
  find_package(Qt6 QUIET OPTIONAL_COMPONENTS Gui Widgets Network WebEngineWidgets WebChannel)
  if(Qt6Widgets_FOUND AND Qt6WebEngineWidgets_FOUND AND Qt6WebChannel_FOUND)
    add_subdirectory(shell)
    add_subdirectory(app)
  else()
    message(FATAL_ERROR "fugue-qt: Qt6 Widgets, WebEngineWidgets and WebChannel are required to build the application "
                        "(install the Qt6 WebEngine development packages), but were not all found. "
                        "To build only the headless modules, tools and tests, configure with -DFUGUE_HEADLESS_ONLY=ON.")
  endif()
endif()
# tests/ is optional: built only when present.
if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/tests/CMakeLists.txt)
  add_subdirectory(tests)
endif()
if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/cmake/devtree.cmake)
  include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/devtree.cmake)
endif()
if(UNIX AND NOT APPLE AND FUGUE_BUILD_SHELL)
  include(packaging/deb/CPackDeb.cmake)   # `cpack -G DEB`: the Ubuntu/Debian package, which uses the distro's Qt
endif()
