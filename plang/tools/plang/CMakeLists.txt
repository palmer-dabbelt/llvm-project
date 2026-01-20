add_plang_tool(plang
  Main.cpp

  LINK_LIBS
  plangBytecode
  plangJIT
  plangSupport

  LINK_COMPONENTS
  Core
  Support
)
