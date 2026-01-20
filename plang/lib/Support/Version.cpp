//===-- Version.cpp - Plang Version Information ----------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "plang/Support/Version.h"
#include "llvm/Config/llvm-config.h"

using namespace plang;

llvm::StringRef plang::getPlangVersion() { return "0.1.0"; }

std::string plang::getPlangFullVersion() {
  return "plang version " + std::string(getPlangVersion()) + " (LLVM " +
         LLVM_VERSION_STRING + ")";
}
