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

  // Value stack operations during compilation
  struct CompilerState {
    llvm::Function *Function = nullptr;
    llvm::BasicBlock *EntryBB = nullptr;
    std::vector<llvm::Value *> Stack;
    std::vector<llvm::Value *> Locals;
    std::vector<llvm::Value *> Constants;

    void push(llvm::Value *V) { Stack.push_back(V); }
    llvm::Value *pop() {
      llvm::Value *V = Stack.back();
      Stack.pop_back();
      return V;
    }
    llvm::Value *top() const { return Stack.back(); }
  };

  /// Compile a single instruction
  llvm::Error compileInstruction(const Instruction &Inst, CompilerState &State,
                                  const CodeObject &Code);

  /// Create runtime helper declarations
  void declareRuntimeHelpers(llvm::Module &M);
};

} // namespace plang

#endif // PLANG_JIT_COMPILER_H
