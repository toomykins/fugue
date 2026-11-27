# Compile suites in their original dependency contexts, then link one runner.
# CTest still starts a fresh process for each registered command.
add_executable(fugue_tests ${PROJECT_SOURCE_DIR}/tests/runner.cpp)
target_include_directories(fugue_tests PRIVATE ${PROJECT_BINARY_DIR} ${PROJECT_SOURCE_DIR})
target_link_libraries(fugue_tests PRIVATE Qt6::Core Qt6::Test)

# Which graphics backends the GPU and native entries run on (shell/CMakeLists.txt registers them): "default" is the
# one the client itself uses on this platform (Vulkan on Windows), "all" is every backend the platform has, and a
# list names them (d3d11;opengl). The off-screen suites can use Vulkan only where its headers are installed
# (vcpkg's vulkan-headers, not a dependency of the build): without them the default on Windows is Direct3D 11.
set(FUGUE_TEST_GPU_BACKENDS "default" CACHE STRING
  "graphics backends the GPU and native test entries run on: default, all, or a list of d3d11;opengl;vulkan;metal")
find_package(VulkanHeaders CONFIG QUIET)
# Every entry names a backend in its environment, so that no test process has to ask the machine which one it has
# (shell/gpubackend.h: about a tenth of a second each). Headless entries draw nothing with it.
if(NOT FUGUE_TEST_GPU_BACKENDS MATCHES "^(default|all)$")
  list(GET FUGUE_TEST_GPU_BACKENDS 0 FUGUE_TEST_BACKEND)
elseif(WIN32 AND TARGET Vulkan::Headers)
  set(FUGUE_TEST_BACKEND vulkan)
elseif(WIN32)
  set(FUGUE_TEST_BACKEND d3d11)
elseif(APPLE)
  set(FUGUE_TEST_BACKEND metal)
else()
  set(FUGUE_TEST_BACKEND opengl)
endif()

# Where test processes run is the platform's choice, not each entry's: every entry starts through the same
# launcher. On Linux that is a private Xvfb display per test process (tools/xvfb-test.sh), so no test sees or draws
# on the developer's desktop or takes its clipboard, the same way locally as in CI. -DFUGUE_TEST_XVFB=OFF runs the
# tests on the current display instead, e.g. to exercise the machine's own GPU driver.
set(_fugue_test_launcher)
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
  option(FUGUE_TEST_XVFB "Run each test process on a private Xvfb display" ON)
  if(FUGUE_TEST_XVFB)
    find_program(FUGUE_SH sh REQUIRED)
    set(_fugue_test_launcher ${FUGUE_SH} ${PROJECT_SOURCE_DIR}/tools/xvfb-test.sh)
  endif()
endif()

# Entries start as portable and fast: headless checks that take a few seconds each, the inner-loop selection
# (`ctest -L fast`). An entry that needs a GPU device, a window, a device or WebEngine replaces these labels.
function(fugue_add_test name)
  set(command ${ARGN})
  list(GET command 0 program)
  if(_fugue_test_launcher AND TARGET ${program}) # behind a launcher CMake no longer resolves the target itself
    list(REMOVE_AT command 0)
    list(PREPEND command "$<TARGET_FILE:${program}>")
  endif()
  add_test(NAME ${name} COMMAND ${_fugue_test_launcher} ${command})
  set_tests_properties(${name} PROPERTIES LABELS "portable;fast" TIMEOUT 120
    FAIL_REGULAR_EXPRESSION "SKIP[ \t]*:"
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen;FUGUE_AUDIO=none;FUGUE_GPU_BACKEND=${FUGUE_TEST_BACKEND};QTWEBENGINE_CHROMIUM_FLAGS=--no-sandbox --disable-gpu")
endfunction()

# fugue_add_sharded_test(<name> <count> <command...>): <count> entries, <name>_1of<count> and so on, that
# together run the suite once, so `ctest -j` spreads a long CPU-bound suite over cores. The runner chooses each
# entry's test functions (--shard, tests/runner.cpp); only suites using a FUGUE_TEST_*MAIN entry accept it.
# Their names are returned in <name>_SHARDS.
function(fugue_add_sharded_test name count)
  set(shards)
  foreach(part RANGE 1 ${count})
    fugue_add_test(${name}_${part}of${count} ${ARGN} --shard=${part}/${count})
    list(APPEND shards ${name}_${part}of${count})
  endforeach()
  set(${name}_SHARDS ${shards} PARENT_SCOPE)
endfunction()

function(fugue_test_entry name entry)
  get_property(names GLOBAL PROPERTY FUGUE_TEST_NAMES)
  if(name IN_LIST names)
    message(FATAL_ERROR "Duplicate test suite: ${name}")
  endif()
  set_property(GLOBAL APPEND PROPERTY FUGUE_TEST_NAMES "${name}")
  set_property(GLOBAL APPEND_STRING PROPERTY FUGUE_TEST_ENTRIES
    "FUGUE_TEST_SUITE(\"${name}\", ${entry})\n")
endfunction()

function(fugue_test_suite target source)
  add_library(${target} OBJECT ${source})
  target_compile_definitions(${target} PRIVATE FUGUE_TEST_ENTRY=fugue_run_${target})
  string(REGEX REPLACE "_test$" "" name "${target}")
  fugue_test_entry("${name}" fugue_run_${target})
  set_property(GLOBAL APPEND PROPERTY FUGUE_TEST_TARGETS "${target}")
endfunction()

function(fugue_finish_tests)
  get_property(targets GLOBAL PROPERTY FUGUE_TEST_TARGETS)
  foreach(target IN LISTS targets)
    # OBJECT dependencies must also reach the executable directly: their
    # registration objects are not propagated through another OBJECT library.
    get_target_property(libraries ${target} LINK_LIBRARIES)
    target_link_libraries(fugue_tests PRIVATE ${target})
    foreach(library IN LISTS libraries)
      if(TARGET ${library})
        get_target_property(type ${library} TYPE)
        if(type STREQUAL "OBJECT_LIBRARY")
          target_link_libraries(fugue_tests PRIVATE ${library})
        endif()
      endif()
    endforeach()
  endforeach()
  get_property(entries GLOBAL PROPERTY FUGUE_TEST_ENTRIES)
  file(CONFIGURE OUTPUT ${PROJECT_BINARY_DIR}/fugue_test_suites.inc
    CONTENT "${entries}" @ONLY)
endfunction()
