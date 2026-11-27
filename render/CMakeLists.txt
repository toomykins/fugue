# Icon decoding (dmi, ddmi, gif, jpeg), DreamSeeker's text layout (dsmaptext),
# its bitmap fonts (gdifon) and the maptext painter. Pure C++; glyphs and TrueType metrics come in through shell/ttmetrics.h.
fugue_module(fugue_render)
# Reference pixel arithmetic must stay bit-exact, so FMA contraction is off.
if(MSVC)
  set(_fugue_fp /fp:strict)
else()
  set(_fugue_fp -ffp-contract=off)
endif()
target_compile_options(fugue_render PRIVATE ${_fugue_fp})
# JPEG plain-image icons (render/jpeg.h): the system libjpeg(-turbo), whose default decode DreamDaemon's matches exactly.
find_package(JPEG REQUIRED)
target_link_libraries(fugue_render PUBLIC JPEG::JPEG)
