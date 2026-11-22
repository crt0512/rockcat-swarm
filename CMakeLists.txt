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
find_package(PkgConfig REQUIRED)
pkg_check_modules(HIDAPI REQUIRED IMPORTED_TARGET hidapi-hidraw>=0.14)

# ---- library -------------------------------------------------------------
find_package(Threads REQUIRED)
add_library(rockcat SHARED
  src/lib/rockcat.cpp
  src/lib/names.cpp
  src/lib/engine.cpp
  src/lib/config.cpp)
target_include_directories(rockcat PUBLIC
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
  $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>)
target_compile_definitions(rockcat PRIVATE ROCKCAT_BUILDING_SHARED)
target_compile_options(rockcat PRIVATE -Wall -Wextra)
set_target_properties(rockcat PROPERTIES
  VERSION ${PROJECT_VERSION}
  SOVERSION ${PROJECT_VERSION_MAJOR}
  CXX_VISIBILITY_PRESET hidden
  VISIBILITY_INLINES_HIDDEN ON)
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
  add_executable(rockcat-test tests/test_codec.cpp)
  target_link_libraries(rockcat-test PRIVATE rockcat)
  add_test(NAME codec COMMAND rockcat-test)
  add_executable(rockcat-test-config tests/test_config.cpp)
  target_link_libraries(rockcat-test-config PRIVATE rockcat)
  foreach(case move both fresh)
    add_test(NAME config-${case} COMMAND rockcat-test-config ${case})
  endforeach()
endif()
