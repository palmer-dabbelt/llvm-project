include(CMakeParseArguments)
include(AddLLVM)

# Add a Plang library
# Usage:
#   add_plang_library(name
#     source1.cpp source2.cpp ...
#     [LINK_LIBS lib1 lib2 ...]
#     [LINK_COMPONENTS comp1 comp2 ...]
#     [DEPENDS target1 target2 ...]
#   )
function(add_plang_library name)
  cmake_parse_arguments(ARG
    "SHARED"
    ""
    "LINK_LIBS;LINK_COMPONENTS;DEPENDS"
    ${ARGN})

  if(ARG_SHARED)
    set(LIBTYPE SHARED)
  else()
    set(LIBTYPE STATIC)
  endif()

  llvm_add_library(${name} ${LIBTYPE}
    ${ARG_UNPARSED_ARGUMENTS}
    LINK_LIBS ${ARG_LINK_LIBS}
    LINK_COMPONENTS ${ARG_LINK_COMPONENTS}
    DEPENDS ${ARG_DEPENDS}
  )

  set_target_properties(${name} PROPERTIES FOLDER "Plang/Libraries")

  if(NOT LLVM_INSTALL_TOOLCHAIN_ONLY)
    if(${name} IN_LIST LLVM_DISTRIBUTION_COMPONENTS OR
       NOT LLVM_DISTRIBUTION_COMPONENTS)
      install(TARGETS ${name}
        EXPORT PlangTargets
        LIBRARY DESTINATION lib${LLVM_LIBDIR_SUFFIX}
        ARCHIVE DESTINATION lib${LLVM_LIBDIR_SUFFIX}
        RUNTIME DESTINATION bin)
    endif()
  endif()
endfunction()

# Add a Plang executable tool
# Usage:
#   add_plang_tool(name
#     source1.cpp source2.cpp ...
#     [LINK_LIBS lib1 lib2 ...]
#     [LINK_COMPONENTS comp1 comp2 ...]
#     [DEPENDS target1 target2 ...]
#   )
function(add_plang_tool name)
  cmake_parse_arguments(ARG
    ""
    ""
    "LINK_LIBS;LINK_COMPONENTS;DEPENDS"
    ${ARGN})

  add_llvm_executable(${name} ${ARG_UNPARSED_ARGUMENTS})

  if(ARG_LINK_LIBS)
    target_link_libraries(${name} PRIVATE ${ARG_LINK_LIBS})
  endif()

  if(ARG_LINK_COMPONENTS)
    llvm_map_components_to_libnames(llvm_libs ${ARG_LINK_COMPONENTS})
    target_link_libraries(${name} PRIVATE ${llvm_libs})
  endif()

  if(ARG_DEPENDS)
    add_dependencies(${name} ${ARG_DEPENDS})
  endif()

  set_target_properties(${name} PROPERTIES FOLDER "Plang/Tools")

  if(PLANG_BUILD_TOOLS)
    install(TARGETS ${name} RUNTIME DESTINATION bin COMPONENT ${name})

    if(NOT LLVM_ENABLE_IDE)
      add_llvm_install_targets(install-${name}
        DEPENDS ${name}
        COMPONENT ${name})
    endif()
  endif()
endfunction()
