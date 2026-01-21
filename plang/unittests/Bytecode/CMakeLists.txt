add_plang_unittest(PlangBytecodeTests
  LoaderTest.cpp
)

target_link_libraries(PlangBytecodeTests PRIVATE
  plangBytecode
)
