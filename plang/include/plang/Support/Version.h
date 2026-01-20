//===-- plang/Support/Version.h - Plang Version Info ----------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef PLANG_SUPPORT_VERSION_H
#define PLANG_SUPPORT_VERSION_H

#include "llvm/ADT/StringRef.h"

namespace plang {

/// Get the Plang version string
llvm::StringRef getPlangVersion();

/// Get the full version string including LLVM version
std::string getPlangFullVersion();

} // namespace plang

#endif // PLANG_SUPPORT_VERSION_H
