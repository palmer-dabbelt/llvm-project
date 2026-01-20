//===-- plang/Bytecode/CodeObject.h - Python Code Object -------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file defines the CodeObject class which represents a Python code object.
//
//===----------------------------------------------------------------------===//

#ifndef PLANG_BYTECODE_CODEOBJECT_H
#define PLANG_BYTECODE_CODEOBJECT_H

#include "plang/Bytecode/Opcodes.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Support/Error.h"
#include <cstdint>
#include <memory>
#include <string>
#include <variant>
#include <vector>

namespace plang {

/// Represents a Python constant value
class PyObject {
public:
  enum class Type {
    None,
    Bool,
    Int,
    Float,
    String,
    Bytes,
    Tuple,
    Code,
    StopIteration,
  };

  using TupleType = std::vector<std::shared_ptr<PyObject>>;

private:
  Type Ty;
  std::variant<std::monostate, bool, int64_t, double, std::string,
               std::vector<uint8_t>, TupleType, std::shared_ptr<class CodeObject>>
      Value;

public:
  PyObject() : Ty(Type::None) {}
  explicit PyObject(bool V) : Ty(Type::Bool), Value(V) {}
  explicit PyObject(int64_t V) : Ty(Type::Int), Value(V) {}
  explicit PyObject(double V) : Ty(Type::Float), Value(V) {}
  explicit PyObject(std::string V) : Ty(Type::String), Value(std::move(V)) {}
  explicit PyObject(std::vector<uint8_t> V)
      : Ty(Type::Bytes), Value(std::move(V)) {}
  explicit PyObject(TupleType V) : Ty(Type::Tuple), Value(std::move(V)) {}
  explicit PyObject(std::shared_ptr<CodeObject> V);

  static PyObject makeNone() { return PyObject(); }
  static PyObject makeStopIteration() {
    PyObject Obj;
    Obj.Ty = Type::StopIteration;
    return Obj;
  }

  Type getType() const { return Ty; }
  bool isNone() const { return Ty == Type::None; }
  bool isBool() const { return Ty == Type::Bool; }
  bool isInt() const { return Ty == Type::Int; }
  bool isFloat() const { return Ty == Type::Float; }
  bool isString() const { return Ty == Type::String; }
  bool isBytes() const { return Ty == Type::Bytes; }
  bool isTuple() const { return Ty == Type::Tuple; }
  bool isCode() const { return Ty == Type::Code; }

  bool getBool() const { return std::get<bool>(Value); }
  int64_t getInt() const { return std::get<int64_t>(Value); }
  double getFloat() const { return std::get<double>(Value); }
  llvm::StringRef getString() const { return std::get<std::string>(Value); }
  llvm::ArrayRef<uint8_t> getBytes() const {
    return std::get<std::vector<uint8_t>>(Value);
  }
  const TupleType &getTuple() const { return std::get<TupleType>(Value); }
  std::shared_ptr<CodeObject> getCode() const;

  std::string toString() const;
};

/// Represents a single bytecode instruction
struct Instruction {
  Opcode Op;
  uint32_t Arg; // Combined argument (after EXTENDED_ARG processing)
  uint32_t Offset; // Byte offset in the code

  Instruction(Opcode Op, uint32_t Arg, uint32_t Offset)
      : Op(Op), Arg(Arg), Offset(Offset) {}
};

/// Represents a Python code object
class CodeObject {
public:
  // Code object fields (Python 3.12+)
  uint32_t ArgCount = 0;
  uint32_t PosOnlyArgCount = 0;
  uint32_t KwOnlyArgCount = 0;
  uint32_t NumLocals = 0;
  uint32_t StackSize = 0;
  uint32_t Flags = 0;

  std::vector<uint8_t> Bytecode;
  std::vector<std::shared_ptr<PyObject>> Constants;
  std::vector<std::string> Names;
  std::vector<std::string> LocalNames;
  std::vector<std::string> FreeVars;
  std::vector<std::string> CellVars;

  std::string Filename;
  std::string Name;
  std::string QualName;
  uint32_t FirstLineNo = 0;

  std::vector<uint8_t> LineTable;
  std::vector<uint8_t> ExceptionTable;

  /// Parse bytecode into instructions
  llvm::Expected<std::vector<Instruction>> parseInstructions() const;

  /// Get the number of instructions
  size_t getInstructionCount() const;

  /// Disassemble to a human-readable string
  std::string disassemble() const;

  /// Code object flags
  static constexpr uint32_t CO_OPTIMIZED = 0x0001;
  static constexpr uint32_t CO_NEWLOCALS = 0x0002;
  static constexpr uint32_t CO_VARARGS = 0x0004;
  static constexpr uint32_t CO_VARKEYWORDS = 0x0008;
  static constexpr uint32_t CO_NESTED = 0x0010;
  static constexpr uint32_t CO_GENERATOR = 0x0020;
  static constexpr uint32_t CO_COROUTINE = 0x0080;
  static constexpr uint32_t CO_ITERABLE_COROUTINE = 0x0100;
  static constexpr uint32_t CO_ASYNC_GENERATOR = 0x0200;
};

} // namespace plang

#endif // PLANG_BYTECODE_CODEOBJECT_H
