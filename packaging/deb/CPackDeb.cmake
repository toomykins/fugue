# `cpack -G DEB` from a configured build tree. Targets Ubuntu 24.04+ and Debian 13: Qt, WebEngine and libopenmpt come from the
# distro packages, nothing is bundled; build on 24.04 (its glibc is the floor). Ubuntu's
# qt6-webengine ships the AppArmor profile that lets QtWebEngineProcess keep Chromium's sandbox on.
set(CPACK_PACKAGE_NAME fugue)
set(CPACK_PACKAGE_VERSION ${PROJECT_VERSION})
set(CPACK_PACKAGE_CONTACT "Fugue contributors <29069174+toomykins@users.noreply.github.com>")
set(CPACK_PACKAGE_VENDOR "The Fugue contributors")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "A native client for BYOND games such as Space Station 13")
set(CPACK_PACKAGE_DESCRIPTION
    "Fugue replaces DreamSeeker on servers that run stock BYOND, without a BYOND install and without Wine.\nIt has a launcher with a server list and sign-in, and the game window with its map, HUD, verbs, sound and the HTML\nwindows games use. It is unofficial and not affiliated with BYOND.")
set(CPACK_GENERATOR DEB)
set(CPACK_STRIP_FILES ON)
set(CPACK_PACKAGING_INSTALL_PREFIX /usr)
set(CPACK_INSTALL_DEFAULT_DIRECTORY_PERMISSIONS OWNER_READ OWNER_WRITE OWNER_EXECUTE GROUP_READ GROUP_EXECUTE WORLD_READ WORLD_EXECUTE)
set(CPACK_DEBIAN_PACKAGE_ARCHITECTURE amd64)
set(CPACK_DEBIAN_FILE_NAME "fugue_${PROJECT_VERSION}_amd64.deb")
set(CPACK_DEBIAN_PACKAGE_SECTION games)
set(CPACK_DEBIAN_PACKAGE_PRIORITY optional)
set(CPACK_DEBIAN_PACKAGE_HOMEPAGE https://github.com/toomykins/fugue)
set(CPACK_DEBIAN_PACKAGE_LICENSE Unlicense)
# Depends are written out, not computed by dpkg-shlibdeps: Ubuntu 24.04 renamed libqt6gui6, libqt6widgets6 and libqt6network6 to
# *t64 packages and Debian 13 did not, so the one .deb names both. The versions are the floors dpkg-shlibdeps recorded on 24.04
# (glibc 2.38 and libstdc++ 13.1 are the build image's). libjpeg is linked in (FUGUE_STATIC_JPEG): its soname differs between
# the two. libqt6webenginewidgets6 brings the helper process (libqt6webenginecore6-bin) and its data. A library added to the
# binary has to be added here; the clean-container smoke test in the CI job fails when one is missing. The game's audio players
# are only recommended: any of them will do.
set(CPACK_DEBIAN_PACKAGE_SHLIBDEPS OFF)
set(CPACK_DEBIAN_PACKAGE_DEPENDS
    "libc6 (>= 2.38), libgcc-s1 (>= 3.0), libstdc++6 (>= 13.1), libopenmpt0t64, libqt6core6t64 (>= 6.4.0), libqt6gui6t64 (>= 6.4.0) | libqt6gui6 (>= 6.4.0), libqt6widgets6t64 (>= 6.3.0) | libqt6widgets6 (>= 6.3.0), libqt6network6t64 (>= 6.1.2) | libqt6network6 (>= 6.1.2), libqt6webenginecore6 (>= 6.3.0), libqt6webenginecore6-bin (>= 6.3.0), libqt6webenginewidgets6 (>= 6.2.2)")
set(CPACK_DEBIAN_PACKAGE_RECOMMENDS "pipewire-bin | pulseaudio-utils | alsa-utils, qt6-wayland")
if(FUGUE_STATIC_JPEG)
  add_test(NAME deb_depends COMMAND ${CMAKE_COMMAND} "-DDEPENDS=${CPACK_DEBIAN_PACKAGE_DEPENDS}" -DBINARY=$<TARGET_FILE:fugue-qt>
                                    -P ${CMAKE_CURRENT_LIST_DIR}/check_depends.cmake)
endif()
include(CPack)
