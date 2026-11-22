cmake_minimum_required(VERSION 3.16)
project(rockcat-swarm VERSION 2.0.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE RelWithDebInfo)
endif()

option(ROCKCAT_BUILD_GUI "Build the GTK3 GUI" ON)
option(ROCKCAT_BUILD_CLI "Build the command line tool" ON)
option(ROCKCAT_BUILD_TESTS "Build offline unit tests" ON)

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
  src/lib/families/kone_aimo.cpp)
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

install(FILES README.md docs/PROTOCOL.md LICENSE DESTINATION ${CMAKE_INSTALL_DOCDIR})

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
    DEPENDS ${GRESOURCE_XML} data/rockcat.css data/rockcat-swarm.svg
    VERBATIM)
  add_executable(rockcat-swarm
    src/gui/main.cpp
    src/gui/window.cpp
    ${GRESOURCE_C})
  target_link_libraries(rockcat-swarm PRIVATE rockcat PkgConfig::GTK3)
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
  add_test(NAME render COMMAND rockcat-test-replay render ${CMAKE_CURRENT_SOURCE_DIR}/tests/render_golden.txt)
  if(ROCKCAT_BUILD_CLI)
    add_test(NAME cli-golden COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/tests/cli_golden.sh $<TARGET_FILE:rockcat-cli>)
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
