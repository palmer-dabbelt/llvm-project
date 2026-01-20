//===-- Main.cpp - Plang Driver ---------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This is the main driver for the plang Python bytecode JIT.
//
//===----------------------------------------------------------------------===//

#include "plang/Bytecode/Loader.h"
#include "plang/JIT/Compiler.h"
#include "plang/JIT/PlangJIT.h"
#include "plang/Support/Version.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/Error.h"
#include "llvm/Support/InitLLVM.h"
#include "llvm/Support/TargetSelect.h"
#include "llvm/Support/raw_ostream.h"

using namespace llvm;
using namespace plang;

// Command line options
static cl::OptionCategory PlangCategory("Plang Options");

static cl::opt<std::string> InputFile(cl::Positional, cl::desc("<input .pyc file>"),
                                       cl::cat(PlangCategory));

static cl::opt<bool> Disassemble("disassemble",
                                  cl::desc("Disassemble the bytecode"),
                                  cl::cat(PlangCategory));

static cl::alias DisassembleAlias("d", cl::desc("Alias for --disassemble"),
                                   cl::aliasopt(Disassemble),
                                   cl::cat(PlangCategory));

static cl::opt<bool> DumpIR("dump-ir",
                            cl::desc("Dump the generated LLVM IR"),
                            cl::cat(PlangCategory));

static cl::opt<bool> Run("run", cl::desc("JIT compile and run the code"),
                         cl::cat(PlangCategory));

static cl::alias RunAlias("r", cl::desc("Alias for --run"), cl::aliasopt(Run),
                          cl::cat(PlangCategory));

static cl::opt<bool> ShowVersion("version", cl::desc("Show version information"),
                                  cl::cat(PlangCategory));

static cl::alias VersionAlias("v", cl::desc("Alias for --version"),
                               cl::aliasopt(ShowVersion),
                               cl::cat(PlangCategory));

static void printVersion(raw_ostream &OS) {
  OS << getPlangFullVersion() << "\n";
}

int main(int argc, char **argv) {
  InitLLVM X(argc, argv);

  cl::HideUnrelatedOptions(PlangCategory);
  cl::SetVersionPrinter(printVersion);
  cl::ParseCommandLineOptions(argc, argv,
                               "plang - Python bytecode JIT compiler\n\n"
                               "A tool for loading, disassembling, and JIT "
                               "compiling Python bytecode.\n");

  if (ShowVersion) {
    printVersion(outs());
    return 0;
  }

  if (InputFile.empty()) {
    errs() << "Error: No input file specified.\n";
    errs() << "Usage: plang [options] <input.pyc>\n";
    return 1;
  }

  // Load the .pyc file
  auto LoadResult = BytecodeLoader::loadFromFile(InputFile);
  if (!LoadResult) {
    errs() << "Error loading " << InputFile << ": "
           << toString(LoadResult.takeError()) << "\n";
    return 1;
  }

  auto &[Header, Code] = *LoadResult;

  outs() << "Loaded: " << InputFile << "\n";
  outs() << "Python version: " << Header.getVersionString() << "\n";
  outs() << "Code object: " << (Code->Name.empty() ? "<module>" : Code->Name)
         << "\n";
  outs() << "\n";

  // Disassemble mode
  if (Disassemble) {
    outs() << Code->disassemble();
    return 0;
  }

  // Dump IR mode
  if (DumpIR || Run) {
    // Initialize LLVM targets for JIT
    InitializeNativeTarget();
    InitializeNativeTargetAsmPrinter();
    InitializeNativeTargetAsmParser();

    LLVMContext Ctx;
    BytecodeCompiler Compiler(Ctx);

    auto ModuleOrErr = Compiler.compile(*Code);
    if (!ModuleOrErr) {
      errs() << "Error compiling: " << toString(ModuleOrErr.takeError()) << "\n";
      return 1;
    }

    auto &Module = *ModuleOrErr;

    if (DumpIR) {
      outs() << "Generated LLVM IR:\n";
      outs() << "==================\n";
      Module->print(outs(), nullptr);
      outs() << "\n";

      if (!Run)
        return 0;
    }

    // JIT and run
    if (Run) {
      auto JITOrErr = PlangJIT::Create();
      if (!JITOrErr) {
        errs() << "Error creating JIT: " << toString(JITOrErr.takeError())
               << "\n";
        return 1;
      }

      auto &JIT = *JITOrErr;

      // Add the module
      if (auto Err = JIT->addModule(
              orc::ThreadSafeModule(std::move(Module),
                                    std::make_unique<LLVMContext>()))) {
        errs() << "Error adding module: " << toString(std::move(Err)) << "\n";
        return 1;
      }

      // Look up the entry function
      std::string EntryName = BytecodeCompiler::getEntryFunctionName(*Code);
      auto SymOrErr = JIT->lookup(EntryName);
      if (!SymOrErr) {
        errs() << "Error looking up " << EntryName << ": "
               << toString(SymOrErr.takeError()) << "\n";
        return 1;
      }

      // Get the function pointer and call it
      auto *EntryFn = SymOrErr->getAddress().toPtr<int64_t (*)()>();
      outs() << "Executing " << EntryName << "...\n";

      int64_t Result = EntryFn();

      outs() << "Result: " << Result << "\n";
      return 0;
    }
  }

  // Default: just show info
  outs() << "Use --disassemble to view bytecode\n";
  outs() << "Use --dump-ir to see generated LLVM IR\n";
  outs() << "Use --run to JIT compile and execute\n";

  return 0;
}
