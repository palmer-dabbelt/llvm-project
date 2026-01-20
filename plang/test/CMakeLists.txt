set(PLANG_SOURCE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/..)
set(PLANG_BINARY_DIR ${CMAKE_CURRENT_BINARY_DIR}/..)
set(PLANG_TOOLS_DIR ${LLVM_RUNTIME_OUTPUT_INTDIR})

configure_lit_site_cfg(
  ${CMAKE_CURRENT_SOURCE_DIR}/lit.site.cfg.py.in
  ${CMAKE_CURRENT_BINARY_DIR}/lit.site.cfg.py
  MAIN_CONFIG
  ${CMAKE_CURRENT_SOURCE_DIR}/lit.cfg.py
)

set(PLANG_TEST_DEPENDS
  plang
  FileCheck
  count
  not
)

add_lit_testsuite(check-plang "Running Plang regression tests"
  ${CMAKE_CURRENT_BINARY_DIR}
  DEPENDS ${PLANG_TEST_DEPENDS}
)

set_target_properties(check-plang PROPERTIES FOLDER "Plang/Tests")
