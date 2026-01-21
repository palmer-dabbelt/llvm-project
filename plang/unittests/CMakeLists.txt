add_custom_target(PlangUnitTests)
set_target_properties(PlangUnitTests PROPERTIES FOLDER "Plang/Tests")

function(add_plang_unittest test_dirname)
  add_unittest(PlangUnitTests ${test_dirname} ${ARGN})
endfunction()

add_subdirectory(Bytecode)
