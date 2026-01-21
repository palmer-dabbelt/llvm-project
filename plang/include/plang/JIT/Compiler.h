//===-- plang/JIT/Compiler.h - Bytecode to IR Compiler --------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file defines the BytecodeCompiler class which compiles Python bytecode
// to LLVM IR.
//
//===----------------------------------------------------------------------===//

#ifndef PLANG_JIT_COMPILER_H
#define PLANG_JIT_COMPILER_H

#include "plang/Bytecode/CodeObject.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/Module.h"
#include "llvm/Support/Error.h"
#include <map>
#include <memory>

namespace plang {

/// Result type for compiled functions
using PyValuePtr = void *;
using CompiledFunctionTy = PyValuePtr (*)();

/// BytecodeCompiler - Compiles Python bytecode to LLVM IR
class BytecodeCompiler {
public:
  explicit BytecodeCompiler(llvm::LLVMContext &Ctx);

  /// Compile a code object to an LLVM module
  /// Returns a module containing a function named after the code object
  llvm::Expected<std::unique_ptr<llvm::Module>>
  compile(const CodeObject &Code, llvm::StringRef ModuleName = "plang_module");

  /// Get the name of the entry function for a compiled code object
  static std::string getEntryFunctionName(const CodeObject &Code);

private:
  llvm::LLVMContext &Ctx;
  std::unique_ptr<llvm::IRBuilder<>> Builder;

  // Type helpers
  llvm::Type *getInt64Ty();
  llvm::Type *getDoubleTy();
  llvm::Type *getPyValuePtrTy();

  // Compiler state during bytecode compilation
  struct CompilerState {
    llvm::Function *Function = nullptr;
    llvm::BasicBlock *EntryBB = nullptr;

    // Runtime stack (alloca'd at function entry)
    llvm::Value *StackBase = nullptr;   // Pointer to [N x i64] array
    llvm::Value *StackPtr = nullptr;    // Pointer to i64 stack index
    llvm::ArrayType *StackArrayTy = nullptr; // Type for GEP operations

    // Compile-time arrays (values known at compile time)
    std::vector<llvm::Value *> Locals;    // Pointers to local variable slots
    std::vector<llvm::Value *> Constants; // Pre-computed constant values
    std::vector<llvm::Value *> Names;     // Pointers to module-level name slots

    // Jump target basic blocks (offset -> BasicBlock)
    std::map<uint32_t, llvm::BasicBlock *> JumpTargets;
  };

  /// Compile a single instruction
  llvm::Error compileInstruction(const Instruction &Inst, CompilerState &State,
                                  const CodeObject &Code);

  /// Create runtime helper declarations
  void declareRuntimeHelpers(llvm::Module &M);

  // Runtime stack operations - emit IR for stack manipulation
  void emitPush(CompilerState &State, llvm::Value *V);
  llvm::Value *emitPop(CompilerState &State);
  llvm::Value *emitPeek(CompilerState &State, unsigned Offset);
  llvm::Value *emitStackAddr(CompilerState &State, unsigned Offset);
};

} // namespace plang

#endif // PLANG_JIT_COMPILER_H
