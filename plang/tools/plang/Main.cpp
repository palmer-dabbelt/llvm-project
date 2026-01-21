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
#include <cstdio>

using namespace llvm;
using namespace plang;

//===----------------------------------------------------------------------===//
// Plang Runtime Functions
// These functions are called by JIT-compiled code and resolved at runtime
// through the DynamicLibrarySearchGenerator.
//===----------------------------------------------------------------------===//

/// Runtime string table - stores strings that can be printed
/// Strings are identified by their index (encoded in the low bits of the value)
static std::vector<std::string> RuntimeStrings;

/// Register a string in the runtime table and return its index
extern "C" int64_t plang_register_string(const char *str, size_t len) {
  int64_t index = static_cast<int64_t>(RuntimeStrings.size());
  RuntimeStrings.emplace_back(str, len);
  return index;
}

/// Print a Python value
/// For now, we use a tagged representation:
/// - Positive values: integers
/// - Special negative values: string indices (index = -(value + 1))
extern "C" void plang_print(int64_t value) {
  // Check if this is a string reference (negative value indicates string)
  if (value < 0) {
    int64_t index = -(value + 1);
    if (static_cast<size_t>(index) < RuntimeStrings.size()) {
      printf("%s\n", RuntimeStrings[index].c_str());
      fflush(stdout);
      return;
    }
  }
  // Otherwise print as integer
  printf("%ld\n", value);
  fflush(stdout);
}

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

      // Register runtime functions with the JIT
      if (auto Err = JIT->defineAbsoluteSymbol("plang_print",
                                                reinterpret_cast<void *>(&plang_print))) {
        errs() << "Error registering plang_print: " << toString(std::move(Err)) << "\n";
        return 1;
      }
      if (auto Err = JIT->defineAbsoluteSymbol("plang_register_string",
                                                reinterpret_cast<void *>(&plang_register_string))) {
        errs() << "Error registering plang_register_string: " << toString(std::move(Err)) << "\n";
        return 1;
      }

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
      outs().flush();  // Flush before JIT code runs (it uses printf)

      int64_t Result = EntryFn();

      outs() << "Result: " << Result << "\n";
      // Use the module's return value as exit code (clamped to valid range)
      return static_cast<int>(Result & 0xFF);
    }
  }

  // Default: just show info
  outs() << "Use --disassemble to view bytecode\n";
  outs() << "Use --dump-ir to see generated LLVM IR\n";
  outs() << "Use --run to JIT compile and execute\n";

  return 0;
}
