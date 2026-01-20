add_llvm_unittest(PlangBytecodeTests
  LoaderTest.cpp
)

target_link_libraries(PlangBytecodeTests PRIVATE
  plangBytecode
)

add_dependencies(PlangUnitTests PlangBytecodeTests)
