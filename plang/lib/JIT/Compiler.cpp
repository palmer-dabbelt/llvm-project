//===-- Compiler.cpp - Bytecode to IR Compiler Implementation --*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "plang/JIT/Compiler.h"
#include "llvm/ADT/Twine.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/DerivedTypes.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/GlobalVariable.h"
#include "llvm/IR/Verifier.h"
#include "llvm/Support/raw_ostream.h"
#include <algorithm>

using namespace plang;
using namespace llvm;

BytecodeCompiler::BytecodeCompiler(LLVMContext &Ctx)
    : Ctx(Ctx), Builder(std::make_unique<IRBuilder<>>(Ctx)) {}

Type *BytecodeCompiler::getInt64Ty() { return Type::getInt64Ty(Ctx); }

Type *BytecodeCompiler::getDoubleTy() { return Type::getDoubleTy(Ctx); }

Type *BytecodeCompiler::getPyValuePtrTy() {
  // Use i64 to represent Python values for now
  // In a full implementation, this would be a pointer to a tagged union
  return Type::getInt64Ty(Ctx);
}

std::string BytecodeCompiler::getEntryFunctionName(const CodeObject &Code) {
  if (Code.Name.empty())
    return "__plang_module__";
  return "__plang_" + Code.Name + "__";
}

// Magic value to represent the built-in print function
static constexpr int64_t BUILTIN_PRINT = -1000000001;

void BytecodeCompiler::declareRuntimeHelpers(Module &M) {
  // Declare runtime helper functions that will be linked at runtime

  // plang_print: prints a value
  auto *PrintTy = FunctionType::get(Type::getVoidTy(Ctx),
                                    {getPyValuePtrTy()}, false);
  M.getOrInsertFunction("plang_print", PrintTy);

  // plang_register_string: registers a string and returns its tagged value
  auto *RegisterStringTy = FunctionType::get(
      getPyValuePtrTy(),
      {PointerType::get(Ctx, 0), Type::getInt64Ty(Ctx)}, false);
  M.getOrInsertFunction("plang_register_string", RegisterStringTy);

  // plang_add: adds two values
  auto *BinaryOpTy = FunctionType::get(getPyValuePtrTy(),
                                       {getPyValuePtrTy(), getPyValuePtrTy()},
                                       false);
  M.getOrInsertFunction("plang_add", BinaryOpTy);
  M.getOrInsertFunction("plang_sub", BinaryOpTy);
  M.getOrInsertFunction("plang_mul", BinaryOpTy);
  M.getOrInsertFunction("plang_div", BinaryOpTy);
}

Expected<std::unique_ptr<Module>>
BytecodeCompiler::compile(const CodeObject &Code, StringRef ModuleName) {
  auto M = std::make_unique<Module>(ModuleName, Ctx);

  // Declare runtime helpers
  declareRuntimeHelpers(*M);

  // Create the entry function
  std::string FuncName = getEntryFunctionName(Code);
  auto *FuncTy = FunctionType::get(getPyValuePtrTy(), {}, false);
  auto *Func =
      Function::Create(FuncTy, Function::ExternalLinkage, FuncName, M.get());

  // Create entry basic block
  auto *EntryBB = BasicBlock::Create(Ctx, "entry", Func);
  Builder->SetInsertPoint(EntryBB);

  // Initialize compiler state
  CompilerState State;
  State.Function = Func;
  State.EntryBB = EntryBB;

  // Pre-load constants as LLVM values
  // For strings, we need to register them with the runtime
  auto *RegisterStringFn = M->getFunction("plang_register_string");
  size_t StringIndex = 0;

  for (const auto &Const : Code.Constants) {
    Value *ConstVal = nullptr;
    switch (Const->getType()) {
    case PyObject::Type::None:
      // Represent None as 0
      ConstVal = ConstantInt::get(getPyValuePtrTy(), 0);
      break;
    case PyObject::Type::Bool:
      ConstVal = ConstantInt::get(getPyValuePtrTy(), Const->getBool() ? 1 : 0);
      break;
    case PyObject::Type::Int:
      ConstVal = ConstantInt::get(getPyValuePtrTy(), Const->getInt());
      break;
    case PyObject::Type::Float: {
      // Encode float as int64 bits
      double d = Const->getFloat();
      uint64_t bits;
      std::memcpy(&bits, &d, sizeof(bits));
      ConstVal = ConstantInt::get(getPyValuePtrTy(), bits);
      break;
    }
    case PyObject::Type::String: {
      // Create a global string constant and register it at runtime
      std::string Str = Const->getString().str();
      // Create a constant string in the module
      auto *StrArray = ConstantDataArray::getString(Ctx, Str, true);
      auto *GV = new GlobalVariable(*M, StrArray->getType(), true,
                                    GlobalValue::PrivateLinkage, StrArray,
                                    "str_" + std::to_string(StringIndex++));
      auto *StrPtr = Builder->CreateBitCast(GV, PointerType::get(Ctx, 0));
      auto *StrLen = ConstantInt::get(Type::getInt64Ty(Ctx), Str.size());
      Value *StrIdx = Builder->CreateCall(RegisterStringFn->getFunctionType(),
                                          RegisterStringFn, {StrPtr, StrLen});
      // Convert to tagged string value: -(index + 1)
      ConstVal = Builder->CreateNeg(Builder->CreateAdd(StrIdx, ConstantInt::get(getPyValuePtrTy(), 1)));
      break;
    }
    default:
      // For complex types, use a placeholder
      ConstVal = ConstantInt::get(getPyValuePtrTy(), 0);
      break;
    }
    State.Constants.push_back(ConstVal);
  }

  // Allocate locals
  for (size_t i = 0; i < Code.NumLocals; ++i) {
    auto *Alloca = Builder->CreateAlloca(getPyValuePtrTy(), nullptr,
                                          "local_" + std::to_string(i));
    Builder->CreateStore(ConstantInt::get(getPyValuePtrTy(), 0), Alloca);
    State.Locals.push_back(Alloca);
  }

  // Parse and compile instructions
  auto InstructionsOrErr = Code.parseInstructions();
  if (!InstructionsOrErr)
    return InstructionsOrErr.takeError();

  for (const auto &Inst : *InstructionsOrErr) {
    if (auto Err = compileInstruction(Inst, State, Code))
      return std::move(Err);
  }

  // If we haven't returned yet, return None (0)
  if (!Builder->GetInsertBlock()->getTerminator()) {
    Builder->CreateRet(ConstantInt::get(getPyValuePtrTy(), 0));
  }

  // Verify the module
  std::string ErrStr;
  raw_string_ostream ErrOS(ErrStr);
  if (verifyModule(*M, &ErrOS)) {
    return createStringError(Twine("Module verification failed: ") + ErrStr);
  }

  return M;
}

Error BytecodeCompiler::compileInstruction(const Instruction &Inst,
                                            CompilerState &State,
                                            const CodeObject &Code) {
  // Skip if we already have a terminator (e.g., after RETURN)
  if (Builder->GetInsertBlock()->getTerminator())
    return Error::success();

  switch (Inst.Op) {
  case Opcode::NOP:
  case Opcode::RESUME:
  case Opcode::CACHE:
    // No-op instructions
    break;

  case Opcode::PUSH_NULL:
    // Push None onto stack
    State.push(ConstantInt::get(getPyValuePtrTy(), 0));
    break;

  case Opcode::POP_TOP:
    if (!State.Stack.empty())
      State.pop();
    break;

  case Opcode::LOAD_CONST: {
    if (Inst.Arg < State.Constants.size()) {
      State.push(State.Constants[Inst.Arg]);
    } else {
      State.push(ConstantInt::get(getPyValuePtrTy(), 0));
    }
    break;
  }

  case Opcode::RETURN_VALUE: {
    Value *RetVal = State.Stack.empty()
                        ? ConstantInt::get(getPyValuePtrTy(), 0)
                        : State.pop();
    Builder->CreateRet(RetVal);
    break;
  }

  case Opcode::RETURN_CONST: {
    Value *RetVal = Inst.Arg < State.Constants.size()
                        ? State.Constants[Inst.Arg]
                        : ConstantInt::get(getPyValuePtrTy(), 0);
    Builder->CreateRet(RetVal);
    break;
  }

  case Opcode::LOAD_FAST: {
    if (Inst.Arg < State.Locals.size()) {
      Value *Val = Builder->CreateLoad(getPyValuePtrTy(), State.Locals[Inst.Arg]);
      State.push(Val);
    } else {
      State.push(ConstantInt::get(getPyValuePtrTy(), 0));
    }
    break;
  }

  case Opcode::STORE_FAST: {
    if (!State.Stack.empty() && Inst.Arg < State.Locals.size()) {
      Value *Val = State.pop();
      Builder->CreateStore(Val, State.Locals[Inst.Arg]);
    }
    break;
  }

  case Opcode::BINARY_OP: {
    // Binary operations - Inst.Arg indicates the operation
    // For now, implement basic integer arithmetic inline
    if (State.Stack.size() >= 2) {
      Value *RHS = State.pop();
      Value *LHS = State.pop();

      Value *Result = nullptr;
      switch (Inst.Arg) {
      case 0: // +
        Result = Builder->CreateAdd(LHS, RHS, "add");
        break;
      case 1: // &
        Result = Builder->CreateAnd(LHS, RHS, "and");
        break;
      case 2: // //
        Result = Builder->CreateSDiv(LHS, RHS, "floordiv");
        break;
      case 3: // <<
        Result = Builder->CreateShl(LHS, RHS, "shl");
        break;
      case 5: // *
        Result = Builder->CreateMul(LHS, RHS, "mul");
        break;
      case 6: // %
        Result = Builder->CreateSRem(LHS, RHS, "mod");
        break;
      case 7: // |
        Result = Builder->CreateOr(LHS, RHS, "or");
        break;
      case 10: // -
        Result = Builder->CreateSub(LHS, RHS, "sub");
        break;
      case 11: // /
        Result = Builder->CreateSDiv(LHS, RHS, "div");
        break;
      case 12: // ^
        Result = Builder->CreateXor(LHS, RHS, "xor");
        break;
      default:
        // Unsupported binary op, just return LHS
        Result = LHS;
        break;
      }
      State.push(Result);
    }
    break;
  }

  case Opcode::COPY: {
    // Copy the Nth item from the stack to the top
    if (Inst.Arg > 0 && Inst.Arg <= State.Stack.size()) {
      size_t idx = State.Stack.size() - Inst.Arg;
      State.push(State.Stack[idx]);
    }
    break;
  }

  case Opcode::SWAP: {
    // Swap top of stack with the Nth item
    if (Inst.Arg > 0 && Inst.Arg <= State.Stack.size()) {
      size_t idx = State.Stack.size() - Inst.Arg;
      std::swap(State.Stack[idx], State.Stack.back());
    }
    break;
  }

  case Opcode::LOAD_NAME: {
    // Load a name from the names table
    // For now, we only recognize built-in functions
    if (Inst.Arg < Code.Names.size()) {
      const std::string &Name = Code.Names[Inst.Arg];
      if (Name == "print") {
        // Push a magic value representing the print built-in
        State.push(ConstantInt::get(getPyValuePtrTy(), BUILTIN_PRINT));
      } else {
        // Unknown name - push 0 for now
        State.push(ConstantInt::get(getPyValuePtrTy(), 0));
      }
    } else {
      State.push(ConstantInt::get(getPyValuePtrTy(), 0));
    }
    break;
  }

  case Opcode::CALL: {
    // CALL argc - call a function with argc arguments
    // Stack before: [callable, arg0, arg1, ..., argN-1] (with NULL below callable)
    // Stack after: [result]
    //
    // Python 3.11+ CALL opcode:
    // - Inst.Arg is the number of positional arguments
    // - Stack has: NULL, callable, arg0, arg1, ..., argN-1 (from bottom to top)
    // We need to pop args, callable, and NULL

    uint32_t ArgCount = Inst.Arg;

    // Pop arguments
    std::vector<Value *> Args;
    for (uint32_t i = 0; i < ArgCount; ++i) {
      if (!State.Stack.empty())
        Args.push_back(State.pop());
    }
    // Reverse to get correct order
    std::reverse(Args.begin(), Args.end());

    // Pop callable
    Value *Callable = State.Stack.empty()
                          ? ConstantInt::get(getPyValuePtrTy(), 0)
                          : State.pop();

    // Pop the NULL that was pushed by PUSH_NULL
    if (!State.Stack.empty())
      State.pop();

    // Check if this is a known built-in
    // For now, we'll generate a runtime check for print
    auto *CallableConst = dyn_cast<ConstantInt>(Callable);
    if (CallableConst && CallableConst->getSExtValue() == BUILTIN_PRINT) {
      // Call plang_print with the first argument
      if (!Args.empty()) {
        auto *PrintFn = Builder->GetInsertBlock()->getModule()->getFunction("plang_print");
        Builder->CreateCall(PrintFn->getFunctionType(), PrintFn, {Args[0]});
      }
      // print() returns None (0)
      State.push(ConstantInt::get(getPyValuePtrTy(), 0));
    } else {
      // Unknown callable - push None
      State.push(ConstantInt::get(getPyValuePtrTy(), 0));
    }
    break;
  }

  default:
    // Unimplemented opcodes - for now, just skip them
    // In a full implementation, we'd need to handle all opcodes
    break;
  }

  return Error::success();
}
