//===-- LoaderTest.cpp - Unit tests for BytecodeLoader ---------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "plang/Bytecode/Loader.h"
#include "plang/Bytecode/CodeObject.h"
#include "plang/Bytecode/Opcodes.h"
#include "gtest/gtest.h"

using namespace plang;

namespace {

TEST(OpcodesTest, GetOpcodeName) {
  EXPECT_EQ(getOpcodeName(Opcode::LOAD_CONST), "LOAD_CONST");
  EXPECT_EQ(getOpcodeName(Opcode::RETURN_VALUE), "RETURN_VALUE");
  EXPECT_EQ(getOpcodeName(Opcode::BINARY_OP), "BINARY_OP");
  EXPECT_EQ(getOpcodeName(Opcode::NOP), "NOP");
}

TEST(OpcodesTest, OpcodeHasArg) {
  EXPECT_FALSE(opcodeHasArg(Opcode::NOP));
  EXPECT_FALSE(opcodeHasArg(Opcode::POP_TOP));
  EXPECT_TRUE(opcodeHasArg(Opcode::LOAD_CONST));
  EXPECT_TRUE(opcodeHasArg(Opcode::LOAD_FAST));
  EXPECT_TRUE(opcodeHasArg(Opcode::STORE_NAME));
}

TEST(OpcodesTest, OpcodeIsJump) {
  EXPECT_TRUE(opcodeIsJump(Opcode::JUMP_FORWARD));
  EXPECT_TRUE(opcodeIsJump(Opcode::JUMP_BACKWARD));
  EXPECT_TRUE(opcodeIsJump(Opcode::POP_JUMP_IF_FALSE));
  EXPECT_FALSE(opcodeIsJump(Opcode::LOAD_CONST));
  EXPECT_FALSE(opcodeIsJump(Opcode::RETURN_VALUE));
}

TEST(PyObjectTest, BasicTypes) {
  PyObject none = PyObject::makeNone();
  EXPECT_TRUE(none.isNone());
  EXPECT_EQ(none.toString(), "None");

  PyObject boolTrue(true);
  EXPECT_TRUE(boolTrue.isBool());
  EXPECT_TRUE(boolTrue.getBool());
  EXPECT_EQ(boolTrue.toString(), "True");

  PyObject boolFalse(false);
  EXPECT_TRUE(boolFalse.isBool());
  EXPECT_FALSE(boolFalse.getBool());
  EXPECT_EQ(boolFalse.toString(), "False");

  PyObject intVal(static_cast<int64_t>(42));
  EXPECT_TRUE(intVal.isInt());
  EXPECT_EQ(intVal.getInt(), 42);
  EXPECT_EQ(intVal.toString(), "42");

  PyObject floatVal(3.14);
  EXPECT_TRUE(floatVal.isFloat());
  EXPECT_DOUBLE_EQ(floatVal.getFloat(), 3.14);

  PyObject strVal(std::string("hello"));
  EXPECT_TRUE(strVal.isString());
  EXPECT_EQ(strVal.getString(), "hello");
  EXPECT_EQ(strVal.toString(), "'hello'");
}

TEST(PyObjectTest, Tuple) {
  PyObject::TupleType elements;
  elements.push_back(std::make_shared<PyObject>(static_cast<int64_t>(1)));
  elements.push_back(std::make_shared<PyObject>(static_cast<int64_t>(2)));
  elements.push_back(std::make_shared<PyObject>(static_cast<int64_t>(3)));

  PyObject tuple(std::move(elements));
  EXPECT_TRUE(tuple.isTuple());
  EXPECT_EQ(tuple.getTuple().size(), 3u);
  EXPECT_EQ(tuple.toString(), "(1, 2, 3)");
}

TEST(CodeObjectTest, Disassemble) {
  CodeObject code;
  code.Name = "test_func";
  code.ArgCount = 2;
  code.NumLocals = 3;
  code.StackSize = 4;
  code.Flags = CodeObject::CO_OPTIMIZED;

  // Simple bytecode: LOAD_CONST 0, RETURN_VALUE
  code.Bytecode = {
      static_cast<uint8_t>(Opcode::LOAD_CONST), 0,
      static_cast<uint8_t>(Opcode::RETURN_VALUE), 0
  };

  code.Constants.push_back(
      std::make_shared<PyObject>(static_cast<int64_t>(42)));

  std::string disasm = code.disassemble();
  EXPECT_NE(disasm.find("test_func"), std::string::npos);
  EXPECT_NE(disasm.find("LOAD_CONST"), std::string::npos);
  EXPECT_NE(disasm.find("RETURN_VALUE"), std::string::npos);
}

TEST(CodeObjectTest, ParseInstructions) {
  CodeObject code;
  code.Bytecode = {
      static_cast<uint8_t>(Opcode::LOAD_CONST), 0,
      static_cast<uint8_t>(Opcode::LOAD_CONST), 1,
      static_cast<uint8_t>(Opcode::BINARY_OP), 0,
      static_cast<uint8_t>(Opcode::RETURN_VALUE), 0
  };

  auto result = code.parseInstructions();
  ASSERT_TRUE(static_cast<bool>(result));

  auto &instructions = *result;
  ASSERT_EQ(instructions.size(), 4u);

  EXPECT_EQ(instructions[0].Op, Opcode::LOAD_CONST);
  EXPECT_EQ(instructions[0].Arg, 0u);
  EXPECT_EQ(instructions[1].Op, Opcode::LOAD_CONST);
  EXPECT_EQ(instructions[1].Arg, 1u);
  EXPECT_EQ(instructions[2].Op, Opcode::BINARY_OP);
  EXPECT_EQ(instructions[2].Arg, 0u);
  EXPECT_EQ(instructions[3].Op, Opcode::RETURN_VALUE);
}

TEST(PycHeaderTest, GetPythonVersion) {
  PycHeader header;

  // Python 3.12 magic
  header.Magic = (0x0a0d << 16) | 3531;
  auto [major, minor] = header.getPythonVersion();
  EXPECT_EQ(major, 3u);
  EXPECT_EQ(minor, 12u);

  // Python 3.11 magic
  header.Magic = (0x0a0d << 16) | 3495;
  auto [major2, minor2] = header.getPythonVersion();
  EXPECT_EQ(major2, 3u);
  EXPECT_EQ(minor2, 11u);
}

} // namespace
