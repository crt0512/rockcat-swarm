cmake_minimum_required(VERSION 3.16)
project(rockcat-swarm VERSION 2.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE RelWithDebInfo)
endif()

option(ROCKCAT_BUILD_GUI "Build the GTK3 GUI" ON)
option(ROCKCAT_BUILD_CLI "Build the command line tool" ON)
option(ROCKCAT_BUILD_TESTS "Build offline unit tests" ON)
# the device pictures are ROCCATs and not ours to license, see assets/roccat/ATTRIBUTION.md. OFF swaps in our own placeholder drawings
option(ROCKCAT_ROCCAT_ASSETS "Ship the original ROCCAT device artwork" ON)

include(GNUInstallDirs)
# otherwise the folders in a package inherit whatever umask the person building it happens to have
set(CMAKE_INSTALL_DEFAULT_DIRECTORY_PERMISSIONS
  OWNER_READ OWNER_WRITE OWNER_EXECUTE GROUP_READ GROUP_EXECUTE WORLD_READ WORLD_EXECUTE)
find_package(PkgConfig REQUIRED)
pkg_check_modules(HIDAPI REQUIRED IMPORTED_TARGET hidapi-hidraw>=0.14)

# ---- library -------------------------------------------------------------
find_package(Threads REQUIRED)
# the sources get compiled once into an object library, the shared lib is made from that
# and the tests that need to reach past the public api link the objects directly
# every device family is a file (or a few) in src/lib/families/, a new one gets picked up without touching this
file(GLOB ROCKCAT_FAMILY_SOURCES CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/src/lib/families/*.cpp)
add_library(rockcat-objs OBJECT
  src/lib/rockcat.cpp
  src/lib/names.cpp
  src/lib/engine.cpp
  src/lib/config.cpp
  src/lib/transport_hid.cpp
  src/lib/capture.cpp
  src/lib/sim.cpp
  src/lib/devices.cpp
  src/lib/profile.cpp
  src/lib/reports.cpp
  src/lib/backup.cpp
  src/lib/keyboards.cpp
  src/lib/kb_effects.cpp
  ${ROCKCAT_FAMILY_SOURCES})
target_include_directories(rockcat-objs PUBLIC
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>)
target_compile_definitions(rockcat-objs PRIVATE ROCKCAT_BUILDING_SHARED)
target_compile_options(rockcat-objs PRIVATE -Wall -Wextra)
set_target_properties(rockcat-objs PROPERTIES
  POSITION_INDEPENDENT_CODE ON
  CXX_VISIBILITY_PRESET hidden
  VISIBILITY_INLINES_HIDDEN ON)
target_link_libraries(rockcat-objs PRIVATE PkgConfig::HIDAPI Threads::Threads)

add_library(rockcat SHARED $<TARGET_OBJECTS:rockcat-objs>)
target_include_directories(rockcat PUBLIC
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
  $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>)
set_target_properties(rockcat PROPERTIES
  VERSION ${PROJECT_VERSION}
  SOVERSION ${PROJECT_VERSION_MAJOR})
target_link_libraries(rockcat PRIVATE PkgConfig::HIDAPI Threads::Threads)

install(TARGETS rockcat EXPORT rockcatTargets
  LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR})
install(DIRECTORY include/rockcat DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
install(EXPORT rockcatTargets NAMESPACE rockcat:: DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/rockcat)
file(WRITE ${CMAKE_CURRENT_BINARY_DIR}/rockcatConfig.cmake
  "include(\"\${CMAKE_CURRENT_LIST_DIR}/rockcatTargets.cmake\")\n")
install(FILES ${CMAKE_CURRENT_BINARY_DIR}/rockcatConfig.cmake DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/rockcat)
configure_file(data/rockcat.pc.in rockcat.pc @ONLY)
install(FILES ${CMAKE_CURRENT_BINARY_DIR}/rockcat.pc DESTINATION ${CMAKE_INSTALL_LIBDIR}/pkgconfig)
install(FILES data/70-rockcat.rules DESTINATION ${CMAKE_INSTALL_PREFIX}/lib/udev/rules.d)
configure_file(data/rockcat-engine.service.in rockcat-engine.service @ONLY)
install(FILES ${CMAKE_CURRENT_BINARY_DIR}/rockcat-engine.service DESTINATION ${CMAKE_INSTALL_PREFIX}/lib/systemd/user)

install(FILES README.md docs/CAPTURES.md LICENSE DESTINATION ${CMAKE_INSTALL_DOCDIR})
install(DIRECTORY docs/protocol DESTINATION ${CMAKE_INSTALL_DOCDIR})

# ---- CLI -----------------------------------------------------------------
if(ROCKCAT_BUILD_CLI)
  add_executable(rockcat-cli src/cli/main.cpp)
  target_link_libraries(rockcat-cli PRIVATE rockcat)
  target_compile_options(rockcat-cli PRIVATE -Wall -Wextra)
  install(TARGETS rockcat-cli RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
endif()

# ---- GUI -----------------------------------------------------------------
if(ROCKCAT_BUILD_GUI)
  pkg_check_modules(GTK3 REQUIRED IMPORTED_TARGET gtk+-3.0)
  find_program(GLIB_COMPILE_RESOURCES glib-compile-resources REQUIRED)
  set(GRESOURCE_XML ${CMAKE_CURRENT_SOURCE_DIR}/data/rockcat.gresource.xml)
  set(GRESOURCE_C ${CMAKE_CURRENT_BINARY_DIR}/rockcat-resources.c)
  add_custom_command(
    OUTPUT ${GRESOURCE_C}
    COMMAND ${GLIB_COMPILE_RESOURCES} --target=${GRESOURCE_C} --generate-source
            --sourcedir=${CMAKE_CURRENT_SOURCE_DIR}/data ${GRESOURCE_XML}
    DEPENDS ${GRESOURCE_XML} data/rockcat.css data/rockcat-swarm.svg data/rc-bug.svg
            data/rockcat-swarm-tray-white.svg data/rockcat-swarm-tray-black.svg
    VERBATIM)
  # device pictures come from one of two folders with identical file names, the gui never knows which one it got
  if(ROCKCAT_ROCCAT_ASSETS)
    set(ASSET_DIR ${CMAKE_CURRENT_SOURCE_DIR}/assets/roccat)
  else()
    set(ASSET_DIR ${CMAKE_CURRENT_SOURCE_DIR}/assets/free)
  endif()
  # device icons, per mouse its picture with light masks and marker positions (views/), per keyboard the board, its keycaps and prints
  file(GLOB ASSET_FILES RELATIVE ${ASSET_DIR} CONFIGURE_DEPENDS ${ASSET_DIR}/devices/*.png ${ASSET_DIR}/views/*/*.png ${ASSET_DIR}/views/*/*.ini ${ASSET_DIR}/keyboards/*/*.png
       ${ASSET_DIR}/devices/*.webp ${ASSET_DIR}/views/*/*.webp ${ASSET_DIR}/keyboards/*/*.webp)
  set(ASSET_XML ${CMAKE_CURRENT_BINARY_DIR}/rockcat-assets.gresource.xml)
  set(ASSET_C ${CMAKE_CURRENT_BINARY_DIR}/rockcat-assets.c)
  set(ASSET_ENTRIES "")
  set(ASSET_DEPENDS "")
  foreach(f ${ASSET_FILES})
    string(APPEND ASSET_ENTRIES "    <file>${f}</file>\n")
    list(APPEND ASSET_DEPENDS ${ASSET_DIR}/${f})
  endforeach()
  file(WRITE ${ASSET_XML} "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n<gresources>\n  <gresource prefix=\"/org/rockcat/swarm/assets\">\n${ASSET_ENTRIES}  </gresource>\n</gresources>\n")
  add_custom_command(
    OUTPUT ${ASSET_C}
    COMMAND ${GLIB_COMPILE_RESOURCES} --target=${ASSET_C} --generate-source --c-name rockcat_assets
            --sourcedir=${ASSET_DIR} ${ASSET_XML}
    DEPENDS ${ASSET_XML} ${ASSET_DEPENDS}
    VERBATIM)
  add_executable(rockcat-swarm
    src/gui/main.cpp
    src/gui/shell.cpp
    src/gui/device.cpp
    src/gui/sync.cpp
    src/gui/prefs.cpp
    src/gui/files.cpp
    src/gui/tray.cpp
    src/gui/util.cpp
    src/gui/assets.cpp
    src/gui/device_view.cpp
    src/gui/keyboard_view.cpp
    src/gui/pages/settings.cpp
    src/gui/pages/buttons.cpp
    src/gui/pages/keys.cpp
    src/gui/pages/illumination.cpp
    src/gui/pages/advanced.cpp
    ${GRESOURCE_C}
    ${ASSET_C})
  if(ROCKCAT_ROCCAT_ASSETS)
    target_compile_definitions(rockcat-swarm PRIVATE ROCKCAT_ROCCAT_ASSETS)
  endif()
  # the ROCCAT pictures are webp (tools/assets/to-webp.py), decoded by us so nobody needs the gdk-pixbuf webp loader
  pkg_check_modules(WEBP REQUIRED IMPORTED_TARGET libwebp)
  target_link_libraries(rockcat-swarm PRIVATE rockcat PkgConfig::GTK3 PkgConfig::WEBP)
  # System tray via StatusNotifierItem (works on Wayland bars like waybar)
  pkg_check_modules(APPINDICATOR IMPORTED_TARGET ayatana-appindicator3-0.1)
  if(APPINDICATOR_FOUND)
    target_link_libraries(rockcat-swarm PRIVATE PkgConfig::APPINDICATOR)
    target_compile_definitions(rockcat-swarm PRIVATE ROCKCAT_HAVE_TRAY)
  else()
    message(STATUS "ayatana-appindicator3 not found - building without system tray support")
  endif()
  target_compile_options(rockcat-swarm PRIVATE -Wall -Wextra -Wno-deprecated-declarations)
  install(TARGETS rockcat-swarm RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
  install(FILES data/rockcat-swarm.desktop DESTINATION ${CMAKE_INSTALL_DATADIR}/applications)
  install(FILES data/rockcat-swarm.svg DESTINATION ${CMAKE_INSTALL_DATADIR}/icons/hicolor/scalable/apps)
endif()

# ---- tests (no hardware needed) -------------------------------------------
if(ROCKCAT_BUILD_TESTS)
  enable_testing()
  add_executable(rockcat-test tests/test_codec.cpp $<TARGET_OBJECTS:rockcat-objs>)
  target_include_directories(rockcat-test PRIVATE include src/lib)
  target_link_libraries(rockcat-test PRIVATE PkgConfig::HIDAPI Threads::Threads)
  add_test(NAME codec COMMAND rockcat-test)
  add_executable(rockcat-test-config tests/test_config.cpp)
  target_link_libraries(rockcat-test-config PRIVATE rockcat)
  foreach(case move both fresh)
    add_test(NAME config-${case} COMMAND rockcat-test-config ${case})
  endforeach()
  add_executable(rockcat-test-replay tests/test_replay.cpp $<TARGET_OBJECTS:rockcat-objs>)
  target_include_directories(rockcat-test-replay PRIVATE include src/lib)
  target_link_libraries(rockcat-test-replay PRIVATE PkgConfig::HIDAPI Threads::Threads)
  set(KONE_CAPTURE ${CMAKE_CURRENT_SOURCE_DIR}/tests/captures/kone-aimo-remastered-synthetic.cap)
  add_test(NAME replay-kone COMMAND rockcat-test-replay check ${KONE_CAPTURE})
  add_test(NAME replay-diverge COMMAND rockcat-test-replay diverge ${KONE_CAPTURE})
  add_test(NAME profile-api COMMAND rockcat-test-replay profile)
  add_test(NAME events COMMAND rockcat-test-replay events)
  add_test(NAME keyboard-tables COMMAND rockcat-test-replay keyboard)
  add_test(NAME kova2016 COMMAND rockcat-test-replay kova ${CMAKE_CURRENT_SOURCE_DIR}/tests/captures/kova2016-gray-readonly.cap)
  add_test(NAME keyboard-engine COMMAND rockcat-test-replay kb-engine)
  add_test(NAME ryos-mk-fx COMMAND rockcat-test-replay ryos ${CMAKE_CURRENT_SOURCE_DIR}/tests/captures/ryos-mk-fx-readonly.cap)
  add_test(NAME ryos-mk COMMAND rockcat-test-replay ryos-mk)
  add_test(NAME lua COMMAND rockcat-test-replay lua)
  add_test(NAME classic-mice COMMAND rockcat-test-replay classic)
  add_test(NAME isku COMMAND rockcat-test-replay isku)
  add_test(NAME skeltr COMMAND rockcat-test-replay skeltr)
  add_test(NAME isku-plus COMMAND rockcat-test-replay isku-plus)
  add_test(NAME vulcan COMMAND rockcat-test-replay vulcan)
  add_test(NAME vulcan-pro COMMAND rockcat-test-replay vulcan-pro)
  add_test(NAME suora COMMAND rockcat-test-replay suora)
  add_test(NAME sova COMMAND rockcat-test-replay sova ${CMAKE_CURRENT_SOURCE_DIR}/tests/captures/sova-membrane-readonly.cap)
  add_test(NAME ryos-mk-pro COMMAND rockcat-test-replay ryos-mk-pro ${CMAKE_CURRENT_SOURCE_DIR}/tests/captures/ryos-mk-pro-readonly.cap)
  add_test(NAME render COMMAND rockcat-test-replay render ${CMAKE_CURRENT_SOURCE_DIR}/tests/render_golden.txt)
  if(ROCKCAT_BUILD_CLI)
    add_test(NAME cli-golden COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/tests/cli_golden.sh $<TARGET_FILE:rockcat-cli>)
    # the rule file in data/ is generated from the device table, this goes red when somebody adds a device and forgets it
    add_test(NAME udev-rules COMMAND sh -c "$<TARGET_FILE:rockcat-cli> udev-rules | diff -u ${CMAKE_CURRENT_SOURCE_DIR}/data/70-rockcat.rules -")
  endif()
  file(GLOB CAPTURES ${CMAKE_CURRENT_SOURCE_DIR}/tests/captures/*.cap)
  foreach(cap ${CAPTURES})
    get_filename_component(cap_name ${cap} NAME_WE)
    add_test(NAME capture-readable-${cap_name} COMMAND rockcat-test-replay parse ${cap})
  endforeach()
endif()

# ---- packaging (`make deb`) -------------------------------------------------
# ROCKCAT_PACKAGE_VERSION lets the Makefile glue the git revision on for dev builds
if(NOT ROCKCAT_PACKAGE_VERSION)
  set(ROCKCAT_PACKAGE_VERSION ${PROJECT_VERSION})
endif()
if(ROCKCAT_PACKAGE STREQUAL "deb")
  install(FILES packaging/deb/copyright DESTINATION ${CMAKE_INSTALL_DOCDIR})
endif()
set(CPACK_PACKAGE_NAME rockcat-swarm)
set(CPACK_PACKAGE_VERSION ${ROCKCAT_PACKAGE_VERSION})
set(CPACK_PACKAGE_CONTACT "crt <crt@512mb.net>")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "open replacement for ROCCAT Swarm (GUI, CLI, library)")
set(CPACK_PACKAGE_DESCRIPTION "Profiles, DPI, buttons, macros and lighting for ROCCAT mice and keyboards.")
set(CPACK_PACKAGING_INSTALL_PREFIX /usr)
set(CPACK_STRIP_FILES ON)
set(CPACK_DEBIAN_FILE_NAME DEB-DEFAULT)
set(CPACK_DEBIAN_PACKAGE_RELEASE 1)
set(CPACK_DEBIAN_PACKAGE_SECTION utils)
set(CPACK_DEBIAN_PACKAGE_PRIORITY optional)
set(CPACK_DEBIAN_PACKAGE_HOMEPAGE "https://github.com/crt0512/rockcat-swarm")
# dependencies come from what the binaries actually link against, nobody has to keep a list in sync
set(CPACK_DEBIAN_PACKAGE_SHLIBDEPS ON)
set(CPACK_DEBIAN_PACKAGE_SHLIBDEPS_PRIVATE_DIRS ${CMAKE_BINARY_DIR})
set(CPACK_DEBIAN_PACKAGE_GENERATE_SHLIBS ON)
# control files get 644/755 no matter what umask the checkout was made with
set(CPACK_DEBIAN_PACKAGE_CONTROL_STRICT_PERMISSION TRUE)
# our own postinst stops cpack from writing one that runs ldconfig, the trigger does that job instead
set(CPACK_DEBIAN_PACKAGE_CONTROL_EXTRA
  ${CMAKE_CURRENT_SOURCE_DIR}/packaging/deb/postinst
  ${CMAKE_CURRENT_SOURCE_DIR}/packaging/deb/triggers)
include(CPack)
