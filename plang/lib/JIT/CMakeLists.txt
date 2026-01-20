add_plang_library(plangJIT
  Compiler.cpp
  PlangJIT.cpp

  LINK_LIBS
  plangBytecode

  LINK_COMPONENTS
  Core
  ExecutionEngine
  OrcJIT
  Support
  native
)
