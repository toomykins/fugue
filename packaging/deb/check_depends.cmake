# cmake -DDEPENDS=<CPACK_DEBIAN_PACKAGE_DEPENDS> -DBINARY=<fugue-qt> (a FUGUE_STATIC_JPEG build) -P check_depends.cmake
# The one .deb must install on Ubuntu 24.04 and Debian 13: the Qt libraries that Ubuntu renamed to *t64 and Debian did not
# are alternatives, and libjpeg (soname 8 on Ubuntu, 62 on Debian) is linked in, so it is neither needed nor depended on.
foreach(lib gui widgets network)
  if(NOT DEPENDS MATCHES "libqt6${lib}6t64 \\(>= [0-9.]+\\) \\| libqt6${lib}6 \\(>= [0-9.]+\\)")
    message(FATAL_ERROR "Depends lacks the libqt6${lib}6t64 | libqt6${lib}6 alternative: ${DEPENDS}")
  endif()
endforeach()
if(DEPENDS MATCHES "libjpeg")
  message(FATAL_ERROR "Depends names libjpeg, whose soname differs between Ubuntu and Debian: ${DEPENDS}")
endif()
execute_process(COMMAND readelf -d "${BINARY}" OUTPUT_VARIABLE needed RESULT_VARIABLE rc)
if(NOT rc EQUAL 0 OR needed MATCHES "libjpeg")
  message(FATAL_ERROR "${BINARY} is missing or links libjpeg dynamically (configure with -DFUGUE_STATIC_JPEG=ON)")
endif()
