//===-- PlangJIT.cpp - ORC JIT Wrapper Implementation ---------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "plang/JIT/PlangJIT.h"
#include "llvm/ExecutionEngine/Orc/CompileUtils.h"
#include "llvm/ExecutionEngine/Orc/ExecutionUtils.h"
#include "llvm/ExecutionEngine/Orc/TargetProcess/TargetExecutionUtils.h"
#include "llvm/IR/Module.h"
#include "llvm/Support/TargetSelect.h"

using namespace plang;
using namespace llvm;
using namespace llvm::orc;

PlangJIT::PlangJIT(std::unique_ptr<ExecutionSession> ES,
                   JITTargetMachineBuilder JTMB, DataLayout DL)
    : ES(std::move(ES)), DL(std::move(DL)), Mangle(*this->ES, this->DL),
      ObjectLayer(*this->ES,
                  []() { return std::make_unique<SectionMemoryManager>(); }),
      CompileLayer(*this->ES, ObjectLayer,
                   std::make_unique<ConcurrentIRCompiler>(std::move(JTMB))),
      Ctx(std::make_unique<LLVMContext>()) {
  MainJD = &this->ES->createBareJITDylib("<main>");
  MainJD->addGenerator(
      cantFail(DynamicLibrarySearchGenerator::GetForCurrentProcess(
          this->DL.getGlobalPrefix())));
}

PlangJIT::~PlangJIT() {
  if (auto Err = ES->endSession())
    ES->reportError(std::move(Err));
}

Expected<std::unique_ptr<PlangJIT>> PlangJIT::Create() {
  // Initialize LLVM native target
  InitializeNativeTarget();
  InitializeNativeTargetAsmPrinter();
  InitializeNativeTargetAsmParser();

  auto EPC = SelfExecutorProcessControl::Create();
  if (!EPC)
    return EPC.takeError();

  auto ES = std::make_unique<ExecutionSession>(std::move(*EPC));

  JITTargetMachineBuilder JTMB(ES->getExecutorProcessControl().getTargetTriple());

  auto DL = JTMB.getDefaultDataLayoutForTarget();
  if (!DL)
    return DL.takeError();

  return std::unique_ptr<PlangJIT>(
      new PlangJIT(std::move(ES), std::move(JTMB), std::move(*DL)));
}

Error PlangJIT::addModule(ThreadSafeModule TSM, ResourceTrackerSP RT) {
  if (!RT)
    RT = MainJD->getDefaultResourceTracker();
  return CompileLayer.add(RT, std::move(TSM));
}

Expected<ExecutorSymbolDef> PlangJIT::lookup(StringRef Name) {
  return ES->lookup({MainJD}, Mangle(Name.str()));
}
