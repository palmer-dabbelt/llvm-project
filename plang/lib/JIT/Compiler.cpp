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
#include <set>

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

  // Allocate runtime stack
  size_t StackSize = std::max(static_cast<size_t>(Code.StackSize), size_t{16});
  State.StackArrayTy = ArrayType::get(getPyValuePtrTy(), StackSize);
  State.StackBase = Builder->CreateAlloca(State.StackArrayTy, nullptr, "stack");
  State.StackPtr = Builder->CreateAlloca(Type::getInt64Ty(Ctx), nullptr, "sp");
  Builder->CreateStore(ConstantInt::get(Type::getInt64Ty(Ctx), 0), State.StackPtr);

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

  // Allocate storage for module-level names
  for (size_t i = 0; i < Code.Names.size(); ++i) {
    auto *Alloca = Builder->CreateAlloca(getPyValuePtrTy(), nullptr,
                                          "name_" + Code.Names[i]);
    Builder->CreateStore(ConstantInt::get(getPyValuePtrTy(), 0), Alloca);
    State.Names.push_back(Alloca);
  }

  // Parse and compile instructions
  auto InstructionsOrErr = Code.parseInstructions();
  if (!InstructionsOrErr)
    return InstructionsOrErr.takeError();

  auto &Instructions = *InstructionsOrErr;

  // Pre-scan for jump targets and create basic blocks
  std::set<uint32_t> JumpTargetOffsets;
  for (const auto &Inst : Instructions) {
    if (opcodeIsJump(Inst.Op)) {
      uint32_t TargetOffset = 0;
      if (Inst.Op == Opcode::JUMP_BACKWARD ||
          Inst.Op == Opcode::JUMP_BACKWARD_NO_INTERRUPT) {
        // Backward jumps: target = current offset + 2 - arg * 2
        // (relative to end of instruction)
        TargetOffset = Inst.Offset + 2 - Inst.Arg * 2;
      } else {
        // Forward jumps: target = current offset + arg * 2 + 2 (after this instruction)
        TargetOffset = Inst.Offset + Inst.Arg * 2 + 2;
      }
      JumpTargetOffsets.insert(TargetOffset);
    }
  }

  // Create basic blocks for jump targets
  for (uint32_t Offset : JumpTargetOffsets) {
    State.JumpTargets[Offset] =
        BasicBlock::Create(Ctx, "bb_" + std::to_string(Offset), Func);
  }

  // Compile instructions
  for (const auto &Inst : Instructions) {
    // Check if this instruction is a jump target - if so, switch to its block
    if (State.JumpTargets.count(Inst.Offset)) {
      BasicBlock *TargetBB = State.JumpTargets[Inst.Offset];
      // If current block doesn't have a terminator, add branch to target
      if (!Builder->GetInsertBlock()->getTerminator()) {
        Builder->CreateBr(TargetBB);
      }
      Builder->SetInsertPoint(TargetBB);
    }

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

void BytecodeCompiler::emitPush(CompilerState &State, Value *V) {
  // Load current SP
  Value *SP = Builder->CreateLoad(Type::getInt64Ty(Ctx), State.StackPtr, "sp_val");
  // GEP into stack array at SP
  Value *Addr = Builder->CreateGEP(
      State.StackArrayTy, State.StackBase,
      {ConstantInt::get(Type::getInt64Ty(Ctx), 0), SP}, "stack_addr");
  // Store value
  Builder->CreateStore(V, Addr);
  // Increment SP
  Value *NewSP = Builder->CreateAdd(SP, ConstantInt::get(Type::getInt64Ty(Ctx), 1), "sp_inc");
  Builder->CreateStore(NewSP, State.StackPtr);
}

Value *BytecodeCompiler::emitPop(CompilerState &State) {
  // Load current SP
  Value *SP = Builder->CreateLoad(Type::getInt64Ty(Ctx), State.StackPtr, "sp_val");
  // Decrement SP
  Value *NewSP = Builder->CreateSub(SP, ConstantInt::get(Type::getInt64Ty(Ctx), 1), "sp_dec");
  Builder->CreateStore(NewSP, State.StackPtr);
  // GEP into stack array at new SP and load
  Value *Addr = Builder->CreateGEP(
      State.StackArrayTy, State.StackBase,
      {ConstantInt::get(Type::getInt64Ty(Ctx), 0), NewSP}, "stack_addr");
  return Builder->CreateLoad(getPyValuePtrTy(), Addr, "popped");
}

Value *BytecodeCompiler::emitPeek(CompilerState &State, unsigned Offset) {
  // Load SP and compute index: SP - Offset
  Value *SP = Builder->CreateLoad(Type::getInt64Ty(Ctx), State.StackPtr, "sp_val");
  Value *Idx = Builder->CreateSub(SP, ConstantInt::get(Type::getInt64Ty(Ctx), Offset), "peek_idx");
  Value *Addr = Builder->CreateGEP(
      State.StackArrayTy, State.StackBase,
      {ConstantInt::get(Type::getInt64Ty(Ctx), 0), Idx}, "peek_addr");
  return Builder->CreateLoad(getPyValuePtrTy(), Addr, "peeked");
}

Value *BytecodeCompiler::emitStackAddr(CompilerState &State, unsigned Offset) {
  // Load SP and compute index: SP - Offset
  Value *SP = Builder->CreateLoad(Type::getInt64Ty(Ctx), State.StackPtr, "sp_val");
  Value *Idx = Builder->CreateSub(SP, ConstantInt::get(Type::getInt64Ty(Ctx), Offset), "stack_idx");
  return Builder->CreateGEP(
      State.StackArrayTy, State.StackBase,
      {ConstantInt::get(Type::getInt64Ty(Ctx), 0), Idx}, "stack_slot_addr");
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
    emitPush(State, ConstantInt::get(getPyValuePtrTy(), 0));
    break;

  case Opcode::POP_TOP:
    // Pop and discard top of stack
    emitPop(State);
    break;

  case Opcode::LOAD_CONST: {
    if (Inst.Arg < State.Constants.size()) {
      emitPush(State, State.Constants[Inst.Arg]);
    } else {
      emitPush(State, ConstantInt::get(getPyValuePtrTy(), 0));
    }
    break;
  }

  case Opcode::RETURN_VALUE: {
    Value *RetVal = emitPop(State);
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
      emitPush(State, Val);
    } else {
      emitPush(State, ConstantInt::get(getPyValuePtrTy(), 0));
    }
    break;
  }

  case Opcode::STORE_FAST: {
    if (Inst.Arg < State.Locals.size()) {
      Value *Val = emitPop(State);
      Builder->CreateStore(Val, State.Locals[Inst.Arg]);
    }
    break;
  }

  case Opcode::BINARY_OP: {
    // Binary operations - Inst.Arg indicates the operation
    Value *RHS = emitPop(State);
    Value *LHS = emitPop(State);

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
    emitPush(State, Result);
    break;
  }

  case Opcode::COPY: {
    // Copy the Nth item from the stack to the top
    // COPY 1 copies TOS, COPY 2 copies second from top, etc.
    Value *Val = emitPeek(State, Inst.Arg);
    emitPush(State, Val);
    break;
  }

  case Opcode::SWAP: {
    // Swap top of stack with the Nth item
    // SWAP 2 swaps TOS with second from top
    Value *TopAddr = emitStackAddr(State, 1);
    Value *OtherAddr = emitStackAddr(State, Inst.Arg);
    Value *TopVal = Builder->CreateLoad(getPyValuePtrTy(), TopAddr, "swap_top");
    Value *OtherVal = Builder->CreateLoad(getPyValuePtrTy(), OtherAddr, "swap_other");
    Builder->CreateStore(OtherVal, TopAddr);
    Builder->CreateStore(TopVal, OtherAddr);
    break;
  }

  case Opcode::LOAD_NAME: {
    // Load a name from the names table
    if (Inst.Arg < Code.Names.size()) {
      const std::string &Name = Code.Names[Inst.Arg];
      if (Name == "print") {
        // Push a magic value representing the print built-in
        emitPush(State, ConstantInt::get(getPyValuePtrTy(), BUILTIN_PRINT));
      } else if (Inst.Arg < State.Names.size()) {
        // Load from the name slot
        Value *Val = Builder->CreateLoad(getPyValuePtrTy(), State.Names[Inst.Arg], "name_val");
        emitPush(State, Val);
      } else {
        emitPush(State, ConstantInt::get(getPyValuePtrTy(), 0));
      }
    } else {
      emitPush(State, ConstantInt::get(getPyValuePtrTy(), 0));
    }
    break;
  }

  case Opcode::STORE_NAME: {
    // Store TOS to a name slot
    if (Inst.Arg < State.Names.size()) {
      Value *Val = emitPop(State);
      Builder->CreateStore(Val, State.Names[Inst.Arg]);
    } else {
      emitPop(State); // Pop and discard if invalid
    }
    break;
  }

  case Opcode::COMPARE_OP: {
    // Compare two values
    // Arg: 0=<, 1=<=, 2=>, 3=>=, 4=!=, 5===
    // Note: Python 3.12 seems to use different encoding than 3.11
    Value *RHS = emitPop(State);
    Value *LHS = emitPop(State);
    Value *Result = nullptr;

    switch (Inst.Arg) {
    case 2: // < (mapped from Python's encoding)
      Result = Builder->CreateICmpSLT(LHS, RHS, "cmp_lt");
      break;
    case 26: // <=
      Result = Builder->CreateICmpSLE(LHS, RHS, "cmp_le");
      break;
    case 68: // ==
      Result = Builder->CreateICmpEQ(LHS, RHS, "cmp_eq");
      break;
    case 72: // !=
      Result = Builder->CreateICmpNE(LHS, RHS, "cmp_ne");
      break;
    case 4: // >
      Result = Builder->CreateICmpSGT(LHS, RHS, "cmp_gt");
      break;
    case 94: // >=
      Result = Builder->CreateICmpSGE(LHS, RHS, "cmp_ge");
      break;
    default:
      // Fallback: treat as less-than
      Result = Builder->CreateICmpSLT(LHS, RHS, "cmp_default");
      break;
    }
    // Convert i1 to i64 (0 or 1)
    Value *ResultInt = Builder->CreateZExt(Result, getPyValuePtrTy(), "cmp_result");
    emitPush(State, ResultInt);
    break;
  }

  case Opcode::POP_JUMP_IF_FALSE: {
    // Pop TOS, if false jump to target
    Value *Cond = emitPop(State);
    Value *IsTrue = Builder->CreateICmpNE(Cond, ConstantInt::get(getPyValuePtrTy(), 0), "is_true");

    // Calculate jump target: offset + arg * 2 + 2
    uint32_t TargetOffset = Inst.Offset + Inst.Arg * 2 + 2;

    BasicBlock *TargetBB = State.JumpTargets.count(TargetOffset)
                               ? State.JumpTargets[TargetOffset]
                               : nullptr;
    if (!TargetBB) {
      // If target block doesn't exist, create it
      TargetBB = BasicBlock::Create(Ctx, "jump_target", State.Function);
      State.JumpTargets[TargetOffset] = TargetBB;
    }

    // Create fall-through block for true case
    BasicBlock *FallThrough = BasicBlock::Create(Ctx, "fall_through", State.Function);

    Builder->CreateCondBr(IsTrue, FallThrough, TargetBB);
    Builder->SetInsertPoint(FallThrough);
    break;
  }

  case Opcode::POP_JUMP_IF_TRUE: {
    // Pop TOS, if true jump to target
    Value *Cond = emitPop(State);
    Value *IsTrue = Builder->CreateICmpNE(Cond, ConstantInt::get(getPyValuePtrTy(), 0), "is_true");

    uint32_t TargetOffset = Inst.Offset + Inst.Arg * 2 + 2;

    BasicBlock *TargetBB = State.JumpTargets.count(TargetOffset)
                               ? State.JumpTargets[TargetOffset]
                               : nullptr;
    if (!TargetBB) {
      TargetBB = BasicBlock::Create(Ctx, "jump_target", State.Function);
      State.JumpTargets[TargetOffset] = TargetBB;
    }

    BasicBlock *FallThrough = BasicBlock::Create(Ctx, "fall_through", State.Function);

    Builder->CreateCondBr(IsTrue, TargetBB, FallThrough);
    Builder->SetInsertPoint(FallThrough);
    break;
  }

  case Opcode::JUMP_BACKWARD:
  case Opcode::JUMP_BACKWARD_NO_INTERRUPT: {
    // Unconditional backward jump
    // Target = offset + 2 - arg * 2 (relative to end of instruction)
    uint32_t TargetOffset = Inst.Offset + 2 - Inst.Arg * 2;

    BasicBlock *TargetBB = State.JumpTargets.count(TargetOffset)
                               ? State.JumpTargets[TargetOffset]
                               : nullptr;
    if (!TargetBB) {
      TargetBB = BasicBlock::Create(Ctx, "back_target", State.Function);
      State.JumpTargets[TargetOffset] = TargetBB;
    }

    Builder->CreateBr(TargetBB);
    // Create a dead block for any following instructions
    BasicBlock *DeadBB = BasicBlock::Create(Ctx, "dead", State.Function);
    Builder->SetInsertPoint(DeadBB);
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

    // Pop arguments (in reverse order, so we reverse after)
    std::vector<Value *> Args;
    for (uint32_t i = 0; i < ArgCount; ++i) {
      Args.push_back(emitPop(State));
    }
    // Reverse to get correct order
    std::reverse(Args.begin(), Args.end());

    // Pop callable
    Value *Callable = emitPop(State);

    // Pop the NULL that was pushed by PUSH_NULL
    emitPop(State);

    // Generate runtime check for print built-in
    // Compare callable to BUILTIN_PRINT constant
    Value *IsPrint = Builder->CreateICmpEQ(
        Callable, ConstantInt::get(getPyValuePtrTy(), BUILTIN_PRINT), "is_print");

    // Create basic blocks for conditional execution
    Function *F = Builder->GetInsertBlock()->getParent();
    BasicBlock *PrintBB = BasicBlock::Create(Ctx, "call_print", F);
    BasicBlock *UnknownBB = BasicBlock::Create(Ctx, "call_unknown", F);
    BasicBlock *ContBB = BasicBlock::Create(Ctx, "call_cont", F);

    Builder->CreateCondBr(IsPrint, PrintBB, UnknownBB);

    // Print branch: call plang_print
    Builder->SetInsertPoint(PrintBB);
    if (!Args.empty()) {
      auto *PrintFn = Builder->GetInsertBlock()->getModule()->getFunction("plang_print");
      Builder->CreateCall(PrintFn->getFunctionType(), PrintFn, {Args[0]});
    }
    Builder->CreateBr(ContBB);

    // Unknown callable branch: do nothing
    Builder->SetInsertPoint(UnknownBB);
    Builder->CreateBr(ContBB);

    // Continuation: push result (None/0) for both paths
    Builder->SetInsertPoint(ContBB);
    emitPush(State, ConstantInt::get(getPyValuePtrTy(), 0));
    break;
  }

  default:
    return createStringError("unhandled opcode: " + getOpcodeName(Inst.Op).str() +
                             " (" + std::to_string(static_cast<int>(Inst.Op)) +
                             ") at offset " + std::to_string(Inst.Offset));
  }

  return Error::success();
}
