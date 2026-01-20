//===-- plang/Bytecode/Loader.h - Python Bytecode Loader -------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file defines the BytecodeLoader class which parses Python .pyc files.
//
//===----------------------------------------------------------------------===//

#ifndef PLANG_BYTECODE_LOADER_H
#define PLANG_BYTECODE_LOADER_H

#include "plang/Bytecode/CodeObject.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Support/Error.h"
#include "llvm/Support/MemoryBuffer.h"
#include <memory>

namespace plang {

/// Information about a parsed .pyc file
struct PycHeader {
  uint32_t Magic;           // Python version magic number
  uint32_t BitField;        // PEP 552 hash-based pyc
  uint32_t Timestamp;       // Source modification time (or 0 for hash-based)
  uint32_t SourceSize;      // Source file size (or source hash)
  uint32_t SourceHash;      // Source hash (hash-based only)
  bool IsHashBased;         // True if using hash-based invalidation

  /// Get the Python version from the magic number
  std::pair<unsigned, unsigned> getPythonVersion() const;

  /// Get a human-readable description of the Python version
  std::string getVersionString() const;
};

/// Loads and parses Python .pyc files
class BytecodeLoader {
public:
  /// Load a .pyc file from a path
  static llvm::Expected<std::pair<PycHeader, std::shared_ptr<CodeObject>>>
  loadFromFile(llvm::StringRef Path);

  /// Load from a memory buffer
  static llvm::Expected<std::pair<PycHeader, std::shared_ptr<CodeObject>>>
  loadFromBuffer(llvm::MemoryBufferRef Buffer);

private:
  /// Parse the .pyc header (16 bytes for Python 3.7+)
  static llvm::Expected<PycHeader> parseHeader(const uint8_t *Data,
                                                size_t Size);

  /// Unmarshal a code object from the data stream
  static llvm::Expected<std::shared_ptr<CodeObject>>
  unmarshalCode(const uint8_t *Data, size_t Size, size_t &Offset);

  /// Unmarshal a PyObject from the data stream
  static llvm::Expected<std::shared_ptr<PyObject>>
  unmarshalObject(const uint8_t *Data, size_t Size, size_t &Offset,
                  std::vector<std::shared_ptr<PyObject>> &Refs);

  /// Read a varint from the data stream (for line table etc.)
  static llvm::Expected<uint32_t> readVarint(const uint8_t *Data, size_t Size,
                                              size_t &Offset);
};

/// Python magic numbers for version detection
namespace magic {
constexpr uint32_t PYTHON_3_8 = 3413;
constexpr uint32_t PYTHON_3_9 = 3425;
constexpr uint32_t PYTHON_3_10 = 3439;
constexpr uint32_t PYTHON_3_11 = 3495;
constexpr uint32_t PYTHON_3_12 = 3531;
constexpr uint32_t PYTHON_3_13 = 3568;
} // namespace magic

} // namespace plang

#endif // PLANG_BYTECODE_LOADER_H
