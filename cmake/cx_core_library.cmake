## ---------------------------------------------------------------------------
## PROPRIETARY CODE – Arthur de Araújo Farias 2025
## All rights reserved.  No part of this file may be reproduced, stored in a
## retrieval system, or transmitted in any form or by any means—electronic,
## mechanical, photocopying, recording, or otherwise—without the prior written
## permission of the copyright holder.
## ---------------------------------------------------------------------------

# cx_core_library([DEPENDS <cx-core-x> ...] [THREADS])
#
# Declares the header-only INTERFACE target named after the enclosing
# project() (cx-core-<namespace>), its cx-core-<namespace>::cx-core-<namespace>
# alias, and its own install/export + package config, so a consumer can
# find_package() and link one functionality without the rest of cx-core.
#
# DEPENDS names sibling cx-core-<namespace> libraries. In-tree they are
# targets the umbrella CMakeLists.txt has already added (it adds them in
# dependency order); standalone they are found as installed packages.
#
# Headers under any testing/ directory are each library's colocated unit
# tests and are never installed.
include_guard(GLOBAL)

include(CMakePackageConfigHelpers)
include(GNUInstallDirs)

function(cx_core_library)
  cmake_parse_arguments(ARG "THREADS" "" "DEPENDS" ${ARGN})

  set(name ${PROJECT_NAME})

  add_library(${name} INTERFACE)
  add_library(${name}::${name} ALIAS ${name})

  target_compile_features(${name} INTERFACE cxx_std_23)
  target_include_directories(${name} INTERFACE
    $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
  )

  set(CX_CORE_LIBRARY_FIND_DEPENDENCIES "")
  if(ARG_THREADS)
    find_package(Threads REQUIRED)
    target_link_libraries(${name} INTERFACE Threads::Threads)
    string(APPEND CX_CORE_LIBRARY_FIND_DEPENDENCIES "find_dependency(Threads)\n")
  endif()

  foreach(dependency IN LISTS ARG_DEPENDS)
    if(NOT TARGET ${dependency}::${dependency})
      find_package(${dependency} CONFIG REQUIRED)
    endif()
    target_link_libraries(${name} INTERFACE ${dependency}::${dependency})
    string(APPEND CX_CORE_LIBRARY_FIND_DEPENDENCIES "find_dependency(${dependency})\n")
  endforeach()

  install(TARGETS ${name} EXPORT ${name}Targets)
  install(DIRECTORY ${PROJECT_SOURCE_DIR}/include/
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
    PATTERN "testing" EXCLUDE
  )
  install(EXPORT ${name}Targets
    FILE ${name}Targets.cmake
    NAMESPACE ${name}::
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/${name}
  )

  set(CX_CORE_LIBRARY_NAME ${name})
  configure_package_config_file(
    ${CMAKE_CURRENT_FUNCTION_LIST_DIR}/cx-core-library-config.cmake.in
    ${PROJECT_BINARY_DIR}/${name}Config.cmake
    INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/${name}
  )
  write_basic_package_version_file(
    ${PROJECT_BINARY_DIR}/${name}ConfigVersion.cmake
    VERSION ${PROJECT_VERSION}
    COMPATIBILITY AnyNewerVersion
  )
  install(FILES
    ${PROJECT_BINARY_DIR}/${name}Config.cmake
    ${PROJECT_BINARY_DIR}/${name}ConfigVersion.cmake
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/${name}
  )
endfunction()
