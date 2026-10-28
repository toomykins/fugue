fugue_module(fugue_render)
# The executor's float64 blend arithmetic must stay bit-exact, so FMA contraction is off (see render/executor.cpp header).
if(MSVC)
  set(_fugue_fp /fp:strict)
else()
  set(_fugue_fp -ffp-contract=off)
endif()
target_compile_options(fugue_render PRIVATE ${_fugue_fp})

# maptext + font tables: the MeasureText twin (render/maptext.h) and DreamSeeker's own measure
# (render/dsmaptext.h, what net/session.cpp answers 0x00fc with). A STATIC library so that fugue_net can use it
# without every net consumer adding a link line; excluded from the fugue_render object set so nothing is compiled or
# defined twice. Pure C++: TrueType metrics come in through
# render::setTrueTypeResolver (shell/ttmetrics.h), so no Qt Gui dependency reaches the net layer.
add_library(fugue_text STATIC ${CMAKE_CURRENT_SOURCE_DIR}/maptext.cpp ${CMAKE_CURRENT_SOURCE_DIR}/font.cpp
            ${CMAKE_CURRENT_SOURCE_DIR}/dsmaptext.cpp)
target_include_directories(fugue_text PUBLIC ${PROJECT_SOURCE_DIR})
target_compile_options(fugue_text PRIVATE ${_fugue_fp})   # Float64 CSS size maths; no fused multiply-add
get_target_property(_fr_srcs fugue_render SOURCES)
list(FILTER _fr_srcs EXCLUDE REGEX "/(maptext|font|dsmaptext)\\.cpp$")
set_property(TARGET fugue_render PROPERTY SOURCES ${_fr_srcs})
target_link_libraries(fugue_render PUBLIC fugue_text)
# JPEG plain-image icons (render/jpeg.h): the system libjpeg(-turbo), whose default decode DreamDaemon's matches exactly.
if(FUGUE_STATIC_JPEG)
  set(_fugue_sfx ${CMAKE_FIND_LIBRARY_SUFFIXES})
  set(CMAKE_FIND_LIBRARY_SUFFIXES .a)
endif()
find_package(JPEG REQUIRED)
if(FUGUE_STATIC_JPEG)
  set(CMAKE_FIND_LIBRARY_SUFFIXES ${_fugue_sfx})
endif()
target_link_libraries(fugue_render PUBLIC JPEG::JPEG)
