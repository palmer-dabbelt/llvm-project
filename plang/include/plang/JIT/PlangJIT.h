//===-- plang/JIT/PlangJIT.h - ORC JIT Wrapper ----------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file defines the PlangJIT class which wraps LLVM's ORC JIT.
//
//===----------------------------------------------------------------------===//

#ifndef PLANG_JIT_PLANGJIT_H
#define PLANG_JIT_PLANGJIT_H

#include "llvm/ExecutionEngine/Orc/Core.h"
#include "llvm/ExecutionEngine/Orc/ExecutorProcessControl.h"
#include "llvm/ExecutionEngine/Orc/IRCompileLayer.h"
#include "llvm/ExecutionEngine/Orc/JITTargetMachineBuilder.h"
#include "llvm/ExecutionEngine/Orc/RTDyldObjectLinkingLayer.h"
#include "llvm/ExecutionEngine/Orc/Shared/ExecutorSymbolDef.h"
#include "llvm/ExecutionEngine/SectionMemoryManager.h"
#include "llvm/IR/DataLayout.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/Support/Error.h"
#include <memory>

namespace plang {

class CodeObject;

/// PlangJIT - A simple ORC-based JIT for executing compiled Python bytecode
class PlangJIT {
public:
  ~PlangJIT();

  /// Create a new PlangJIT instance
  static llvm::Expected<std::unique_ptr<PlangJIT>> Create();

  /// Get the data layout
  const llvm::DataLayout &getDataLayout() const { return DL; }

  /// Get a reference to the LLVM context
  llvm::LLVMContext &getContext() { return *Ctx.getContext(); }

  /// Add an LLVM module to the JIT
  llvm::Error addModule(llvm::orc::ThreadSafeModule TSM,
                        llvm::orc::ResourceTrackerSP RT = nullptr);

  /// Look up a symbol in the JIT
  llvm::Expected<llvm::orc::ExecutorSymbolDef> lookup(llvm::StringRef Name);

  /// Get the JIT's main JITDylib
  llvm::orc::JITDylib &getMainJITDylib() { return *MainJD; }

  /// Create a resource tracker for the main dylib
  llvm::orc::ResourceTrackerSP createResourceTracker() {
    return MainJD->createResourceTracker();
  }

private:
  PlangJIT(std::unique_ptr<llvm::orc::ExecutionSession> ES,
           llvm::orc::JITTargetMachineBuilder JTMB, llvm::DataLayout DL);

  std::unique_ptr<llvm::orc::ExecutionSession> ES;
  llvm::DataLayout DL;
  llvm::orc::MangleAndInterner Mangle;
  llvm::orc::RTDyldObjectLinkingLayer ObjectLayer;
  llvm::orc::IRCompileLayer CompileLayer;
  llvm::orc::JITDylib *MainJD;
  llvm::orc::ThreadSafeContext Ctx;
};

} // namespace plang

#endif // PLANG_JIT_PLANGJIT_H
