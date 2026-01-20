//===-- CodeObject.cpp - Python Code Object Implementation -----*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "plang/Bytecode/CodeObject.h"
#include "llvm/Support/FormatVariadic.h"
#include "llvm/Support/raw_ostream.h"
#include <sstream>

using namespace plang;

PyObject::PyObject(std::shared_ptr<CodeObject> V)
    : Ty(Type::Code), Value(std::move(V)) {}

std::shared_ptr<CodeObject> PyObject::getCode() const {
  return std::get<std::shared_ptr<CodeObject>>(Value);
}

std::string PyObject::toString() const {
  switch (Ty) {
  case Type::None:
    return "None";
  case Type::StopIteration:
    return "StopIteration";
  case Type::Bool:
    return getBool() ? "True" : "False";
  case Type::Int:
    return std::to_string(getInt());
  case Type::Float: {
    std::ostringstream oss;
    oss << getFloat();
    return oss.str();
  }
  case Type::String:
    return "'" + std::string(getString()) + "'";
  case Type::Bytes: {
    std::string result = "b'";
    for (uint8_t b : getBytes()) {
      if (b >= 32 && b < 127 && b != '\'' && b != '\\') {
        result += static_cast<char>(b);
      } else {
        result += llvm::formatv("\\x{0:2}", b).str();
      }
    }
    result += "'";
    return result;
  }
  case Type::Tuple: {
    std::string result = "(";
    const auto &tuple = getTuple();
    for (size_t i = 0; i < tuple.size(); ++i) {
      if (i > 0)
        result += ", ";
      result += tuple[i]->toString();
    }
    if (tuple.size() == 1)
      result += ",";
    result += ")";
    return result;
  }
  case Type::Code: {
    auto code = getCode();
    return llvm::formatv("<code object {0} at {1}>", code->Name,
                         static_cast<void *>(code.get()))
        .str();
  }
  }
  return "<unknown>";
}

llvm::Expected<std::vector<Instruction>> CodeObject::parseInstructions() const {
  std::vector<Instruction> instructions;

  // Python 3.12+ uses 2-byte word-aligned instructions
  uint32_t extendedArg = 0;

  for (size_t i = 0; i + 1 < Bytecode.size(); i += 2) {
    uint8_t opByte = Bytecode[i];
    uint8_t argByte = Bytecode[i + 1];

    Opcode op = static_cast<Opcode>(opByte);

    // Handle CACHE instructions (skip them)
    if (op == Opcode::CACHE)
      continue;

    uint32_t arg = argByte;

    // Handle EXTENDED_ARG
    if (op == Opcode::EXTENDED_ARG) {
      extendedArg = (extendedArg | arg) << 8;
      continue;
    }

    // Combine with extended arg if present
    if (extendedArg != 0) {
      arg = extendedArg | arg;
      extendedArg = 0;
    }

    instructions.emplace_back(op, arg, static_cast<uint32_t>(i));
  }

  return instructions;
}

size_t CodeObject::getInstructionCount() const {
  // Rough estimate: bytecode size / 2 (each instruction is 2 bytes in 3.12+)
  return Bytecode.size() / 2;
}

std::string CodeObject::disassemble() const {
  std::string result;
  llvm::raw_string_ostream os(result);

  os << "Disassembly of " << (Name.empty() ? "<module>" : Name) << ":\n";
  os << "  Arguments: " << ArgCount << "\n";
  os << "  Locals: " << NumLocals << "\n";
  os << "  Stack size: " << StackSize << "\n";
  os << "  Flags: 0x" << llvm::format_hex_no_prefix(Flags, 4) << "\n";
  os << "\n";

  // Print constants
  if (!Constants.empty()) {
    os << "Constants:\n";
    for (size_t i = 0; i < Constants.size(); ++i) {
      os << "  " << i << ": " << Constants[i]->toString() << "\n";
    }
    os << "\n";
  }

  // Print names
  if (!Names.empty()) {
    os << "Names:\n";
    for (size_t i = 0; i < Names.size(); ++i) {
      os << "  " << i << ": " << Names[i] << "\n";
    }
    os << "\n";
  }

  // Print local names
  if (!LocalNames.empty()) {
    os << "Local names:\n";
    for (size_t i = 0; i < LocalNames.size(); ++i) {
      os << "  " << i << ": " << LocalNames[i] << "\n";
    }
    os << "\n";
  }

  // Parse and print instructions
  auto instructionsOrErr = parseInstructions();
  if (!instructionsOrErr) {
    os << "Error parsing instructions: "
       << llvm::toString(instructionsOrErr.takeError()) << "\n";
    return result;
  }

  os << "Bytecode:\n";
  for (const auto &inst : *instructionsOrErr) {
    os << llvm::format("  %4d ", inst.Offset);
    os << getOpcodeName(inst.Op);

    if (opcodeHasArg(inst.Op)) {
      os << " " << inst.Arg;

      // Add helpful annotations
      switch (inst.Op) {
      case Opcode::LOAD_CONST:
      case Opcode::RETURN_CONST:
        if (inst.Arg < Constants.size()) {
          os << " (" << Constants[inst.Arg]->toString() << ")";
        }
        break;
      case Opcode::LOAD_NAME:
      case Opcode::STORE_NAME:
      case Opcode::LOAD_GLOBAL:
      case Opcode::STORE_GLOBAL:
      case Opcode::LOAD_ATTR:
      case Opcode::STORE_ATTR:
        // LOAD_GLOBAL arg in 3.12 encodes push_null in low bit
        if (inst.Op == Opcode::LOAD_GLOBAL) {
          size_t nameIdx = inst.Arg >> 1;
          if (nameIdx < Names.size()) {
            os << " (" << Names[nameIdx] << ")";
          }
        } else if (inst.Arg < Names.size()) {
          os << " (" << Names[inst.Arg] << ")";
        }
        break;
      case Opcode::LOAD_FAST:
      case Opcode::STORE_FAST:
      case Opcode::DELETE_FAST:
      case Opcode::LOAD_FAST_CHECK:
        if (inst.Arg < LocalNames.size()) {
          os << " (" << LocalNames[inst.Arg] << ")";
        }
        break;
      default:
        break;
      }
    }

    os << "\n";
  }

  return result;
}
