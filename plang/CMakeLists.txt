cmake_minimum_required(VERSION 3.20.0)

set(LLVM_SUBPROJECT_TITLE "Plang")

if(NOT DEFINED LLVM_COMMON_CMAKE_UTILS)
  set(LLVM_COMMON_CMAKE_UTILS ${CMAKE_CURRENT_SOURCE_DIR}/../cmake)
endif()
include(${LLVM_COMMON_CMAKE_UTILS}/Modules/CMakePolicy.cmake
  NO_POLICY_SCOPE)

# Support standalone builds
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
  project(Plang)
  set(PLANG_BUILT_STANDALONE TRUE)
endif()

# CMake module path
list(INSERT CMAKE_MODULE_PATH 0
  "${CMAKE_CURRENT_SOURCE_DIR}/cmake/modules"
  "${LLVM_COMMON_CMAKE_UTILS}/Modules"
)

include(GNUInstallDirs)

# Standalone build configuration
if(PLANG_BUILT_STANDALONE)
  set(CMAKE_CXX_STANDARD 17 CACHE STRING "C++ standard to conform to")
  set(CMAKE_CXX_STANDARD_REQUIRED YES)
  set(CMAKE_CXX_EXTENSIONS NO)

  find_package(LLVM REQUIRED HINTS "${LLVM_CMAKE_DIR}")
  list(APPEND CMAKE_MODULE_PATH "${LLVM_DIR}")

  set(LLVM_INCLUDE_DIRS ${LLVM_INCLUDE_DIRS} CACHE PATH "Path to llvm/include")
  set(LLVM_BINARY_DIR "${LLVM_BINARY_DIR}" CACHE PATH "Path to LLVM build tree")
  set(LLVM_MAIN_SRC_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../llvm" CACHE PATH "Path to LLVM source tree")

  find_program(LLVM_TABLEGEN_EXE "llvm-tblgen" ${LLVM_TOOLS_BINARY_DIR} NO_DEFAULT_PATH)

  set(LLVM_RUNTIME_OUTPUT_INTDIR ${CMAKE_BINARY_DIR}/${CMAKE_CFG_INTDIR}/bin)
  set(LLVM_LIBRARY_OUTPUT_INTDIR ${CMAKE_BINARY_DIR}/${CMAKE_CFG_INTDIR}/lib${LLVM_LIBDIR_SUFFIX})

  separate_arguments(LLVM_DEFINITIONS_LIST NATIVE_COMMAND ${LLVM_DEFINITIONS})
  add_definitions(${LLVM_DEFINITIONS_LIST})

  include(AddLLVM)
  include(TableGen)
  include(HandleLLVMOptions)

  set(PACKAGE_VERSION "${LLVM_PACKAGE_VERSION}")
endif()

# Plang version
set(PLANG_VERSION_MAJOR 0)
set(PLANG_VERSION_MINOR 1)
set(PLANG_VERSION_PATCH 0)
set(PLANG_VERSION "${PLANG_VERSION_MAJOR}.${PLANG_VERSION_MINOR}.${PLANG_VERSION_PATCH}")

# Project paths
set(PLANG_SOURCE_DIR ${CMAKE_CURRENT_SOURCE_DIR})
set(PLANG_INCLUDE_DIR ${PLANG_SOURCE_DIR}/include)
set(PLANG_BINARY_DIR ${CMAKE_CURRENT_BINARY_DIR})

# Options
option(PLANG_BUILD_TOOLS "Build Plang tools" ON)
option(PLANG_INCLUDE_TESTS "Include Plang tests" ${LLVM_INCLUDE_TESTS})

# Include paths
include_directories(BEFORE
  ${CMAKE_CURRENT_BINARY_DIR}/include
  ${CMAKE_CURRENT_SOURCE_DIR}/include
)

# Include our CMake modules
include(AddPlang)

# Add subdirectories
add_subdirectory(lib)
add_subdirectory(tools)

if(PLANG_INCLUDE_TESTS)
  add_subdirectory(test)
  add_subdirectory(unittests)
endif()

# Install headers
add_custom_target(plang-headers)
set_target_properties(plang-headers PROPERTIES FOLDER "Plang/Resources")

if(NOT LLVM_INSTALL_TOOLCHAIN_ONLY)
  install(DIRECTORY include/plang
    DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}"
    COMPONENT plang-headers
    FILES_MATCHING
    PATTERN "*.h"
    PATTERN "*.def"
    PATTERN "*.inc"
  )

  if(NOT LLVM_ENABLE_IDE)
    add_llvm_install_targets(install-plang-headers
      DEPENDS plang-headers
      COMPONENT plang-headers)
  endif()
endif()
