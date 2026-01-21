//===-- Loader.cpp - Python Bytecode Loader Implementation -----*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "plang/Bytecode/Loader.h"
#include "llvm/ADT/Twine.h"
#include "llvm/Support/Endian.h"
#include "llvm/Support/Error.h"
#include "llvm/Support/MemoryBuffer.h"
#include <cstring>

using namespace plang;
using namespace llvm;

namespace {

// Marshal type codes
enum MarshalType : uint8_t {
  TYPE_NULL = '0',
  TYPE_NONE = 'N',
  TYPE_FALSE = 'F',
  TYPE_TRUE = 'T',
  TYPE_STOPITER = 'S',
  TYPE_ELLIPSIS = '.',
  TYPE_INT = 'i',
  TYPE_INT64 = 'I',
  TYPE_FLOAT = 'f',
  TYPE_BINARY_FLOAT = 'g',
  TYPE_COMPLEX = 'x',
  TYPE_BINARY_COMPLEX = 'y',
  TYPE_LONG = 'l',
  TYPE_STRING = 's',
  TYPE_INTERNED = 't',
  TYPE_REF = 'r',
  TYPE_TUPLE = '(',
  TYPE_LIST = '[',
  TYPE_DICT = '{',
  TYPE_CODE = 'c',
  TYPE_UNICODE = 'u',
  TYPE_UNKNOWN = '?',
  TYPE_SET = '<',
  TYPE_FROZENSET = '>',
  TYPE_ASCII = 'a',
  TYPE_ASCII_INTERNED = 'A',
  TYPE_SMALL_TUPLE = ')',
  TYPE_SHORT_ASCII = 'z',
  TYPE_SHORT_ASCII_INTERNED = 'Z',
};

// Flag to indicate object should be added to refs
constexpr uint8_t FLAG_REF = 0x80;

inline uint32_t readU32LE(const uint8_t *Data) {
  return support::endian::read32le(Data);
}

inline int32_t readS32LE(const uint8_t *Data) {
  return static_cast<int32_t>(support::endian::read32le(Data));
}

} // namespace

std::pair<unsigned, unsigned> PycHeader::getPythonVersion() const {
  // Extract version from magic number
  // Magic = (minor << 8) | major, approximately
  uint16_t magicNum = Magic & 0xFFFF;

  if (magicNum >= 3568)
    return {3, 13};
  if (magicNum >= 3531)
    return {3, 12};
  if (magicNum >= 3495)
    return {3, 11};
  if (magicNum >= 3439)
    return {3, 10};
  if (magicNum >= 3425)
    return {3, 9};
  if (magicNum >= 3413)
    return {3, 8};
  if (magicNum >= 3394)
    return {3, 7};
  if (magicNum >= 3379)
    return {3, 6};

  return {3, 0}; // Unknown, assume 3.x
}

std::string PycHeader::getVersionString() const {
  auto [major, minor] = getPythonVersion();
  return "Python " + std::to_string(major) + "." + std::to_string(minor);
}

Expected<std::pair<PycHeader, std::shared_ptr<CodeObject>>>
BytecodeLoader::loadFromFile(StringRef Path) {
  auto BufferOrErr = MemoryBuffer::getFile(Path);
  if (!BufferOrErr)
    return createStringError(BufferOrErr.getError(),
                             Twine("Failed to open file: ") + Path);

  return loadFromBuffer((*BufferOrErr)->getMemBufferRef());
}

Expected<std::pair<PycHeader, std::shared_ptr<CodeObject>>>
BytecodeLoader::loadFromBuffer(MemoryBufferRef Buffer) {
  const uint8_t *Data =
      reinterpret_cast<const uint8_t *>(Buffer.getBufferStart());
  size_t Size = Buffer.getBufferSize();

  if (Size < 16)
    return createStringError("File too small to be a valid .pyc file");

  // Parse header
  auto HeaderOrErr = parseHeader(Data, Size);
  if (!HeaderOrErr)
    return HeaderOrErr.takeError();

  PycHeader Header = *HeaderOrErr;

  // Unmarshal the code object starting after the header
  size_t Offset = 16;
  auto CodeOrErr = unmarshalCode(Data, Size, Offset);
  if (!CodeOrErr)
    return CodeOrErr.takeError();

  return std::make_pair(Header, *CodeOrErr);
}

Expected<PycHeader> BytecodeLoader::parseHeader(const uint8_t *Data,
                                                 size_t Size) {
  if (Size < 16)
    return createStringError("Header too small");

  PycHeader Header;
  Header.Magic = readU32LE(Data);
  Header.BitField = readU32LE(Data + 4);

  // Check if this is a valid Python magic number
  // Python magic is structured as: magic_number | (0x0a0d << 16)
  uint16_t magicCheck = (Header.Magic >> 16) & 0xFFFF;
  if (magicCheck != 0x0a0d)
    return createStringError("Invalid Python magic number");

  // PEP 552: bit 0 of BitField indicates hash-based pyc
  Header.IsHashBased = (Header.BitField & 1) != 0;

  if (Header.IsHashBased) {
    Header.Timestamp = 0;
    Header.SourceSize = 0;
    Header.SourceHash = readU32LE(Data + 8);
  } else {
    Header.Timestamp = readU32LE(Data + 8);
    Header.SourceSize = readU32LE(Data + 12);
    Header.SourceHash = 0;
  }

  return Header;
}

Expected<std::shared_ptr<CodeObject>>
BytecodeLoader::unmarshalCode(const uint8_t *Data, size_t Size,
                               size_t &Offset) {
  std::vector<std::shared_ptr<PyObject>> refs;
  auto ObjOrErr = unmarshalObject(Data, Size, Offset, refs);
  if (!ObjOrErr)
    return ObjOrErr.takeError();

  auto Obj = *ObjOrErr;
  if (!Obj->isCode())
    return createStringError("Expected code object at top level");

  return Obj->getCode();
}

Expected<std::shared_ptr<PyObject>>
BytecodeLoader::unmarshalObject(const uint8_t *Data, size_t Size,
                                 size_t &Offset,
                                 std::vector<std::shared_ptr<PyObject>> &Refs) {
  if (Offset >= Size)
    return createStringError("Unexpected end of marshal data");

  uint8_t TypeByte = Data[Offset++];
  bool AddRef = (TypeByte & FLAG_REF) != 0;
  uint8_t Type = TypeByte & ~FLAG_REF;

  std::shared_ptr<PyObject> Result;

  switch (Type) {
  case TYPE_NONE:
    Result = std::make_shared<PyObject>(PyObject::makeNone());
    break;

  case TYPE_FALSE:
    Result = std::make_shared<PyObject>(false);
    break;

  case TYPE_TRUE:
    Result = std::make_shared<PyObject>(true);
    break;

  case TYPE_STOPITER:
    Result = std::make_shared<PyObject>(PyObject::makeStopIteration());
    break;

  case TYPE_INT: {
    if (Offset + 4 > Size)
      return createStringError("Truncated int");
    int32_t val = readS32LE(Data + Offset);
    Offset += 4;
    Result = std::make_shared<PyObject>(static_cast<int64_t>(val));
    break;
  }

  case TYPE_LONG: {
    if (Offset + 4 > Size)
      return createStringError("Truncated long size");
    int32_t ndigits = readS32LE(Data + Offset);
    Offset += 4;
    bool negative = ndigits < 0;
    size_t absDigits = negative ? -ndigits : ndigits;

    if (Offset + absDigits * 2 > Size)
      return createStringError("Truncated long digits");

    // Simple conversion for small longs (fits in int64)
    int64_t value = 0;
    for (size_t i = 0; i < absDigits && i < 4; ++i) {
      uint16_t digit = support::endian::read16le(Data + Offset + i * 2);
      value |= static_cast<int64_t>(digit) << (15 * i);
    }
    if (negative)
      value = -value;

    Offset += absDigits * 2;
    Result = std::make_shared<PyObject>(value);
    break;
  }

  case TYPE_BINARY_FLOAT: {
    if (Offset + 8 > Size)
      return createStringError("Truncated float");
    double val;
    std::memcpy(&val, Data + Offset, 8);
    Offset += 8;
    Result = std::make_shared<PyObject>(val);
    break;
  }

  case TYPE_SHORT_ASCII:
  case TYPE_SHORT_ASCII_INTERNED: {
    if (Offset >= Size)
      return createStringError("Truncated short ascii length");
    uint8_t len = Data[Offset++];
    if (Offset + len > Size)
      return createStringError("Truncated short ascii string");
    std::string str(reinterpret_cast<const char *>(Data + Offset), len);
    Offset += len;
    Result = std::make_shared<PyObject>(std::move(str));
    break;
  }

  case TYPE_ASCII:
  case TYPE_ASCII_INTERNED:
  case TYPE_UNICODE:
  case TYPE_STRING:
  case TYPE_INTERNED: {
    if (Offset + 4 > Size)
      return createStringError("Truncated string length");
    uint32_t len = readU32LE(Data + Offset);
    Offset += 4;
    if (Offset + len > Size)
      return createStringError("Truncated string data");
    std::string str(reinterpret_cast<const char *>(Data + Offset), len);
    Offset += len;
    Result = std::make_shared<PyObject>(std::move(str));
    break;
  }

  case TYPE_SMALL_TUPLE: {
    if (Offset >= Size)
      return createStringError("Truncated small tuple size");
    uint8_t n = Data[Offset++];

    // Reserve ref slot early if needed
    size_t refIdx = 0;
    if (AddRef) {
      refIdx = Refs.size();
      Refs.push_back(nullptr);
      AddRef = false; // Don't add again at the end
    }

    PyObject::TupleType elements;
    elements.reserve(n);
    for (uint8_t i = 0; i < n; ++i) {
      auto ElemOrErr = unmarshalObject(Data, Size, Offset, Refs);
      if (!ElemOrErr)
        return ElemOrErr.takeError();
      elements.push_back(*ElemOrErr);
    }
    Result = std::make_shared<PyObject>(std::move(elements));
    if (refIdx < Refs.size())
      Refs[refIdx] = Result;
    break;
  }

  case TYPE_TUPLE: {
    if (Offset + 4 > Size)
      return createStringError("Truncated tuple size");
    uint32_t n = readU32LE(Data + Offset);
    Offset += 4;

    size_t refIdx = 0;
    if (AddRef) {
      refIdx = Refs.size();
      Refs.push_back(nullptr);
      AddRef = false;
    }

    PyObject::TupleType elements;
    elements.reserve(n);
    for (uint32_t i = 0; i < n; ++i) {
      auto ElemOrErr = unmarshalObject(Data, Size, Offset, Refs);
      if (!ElemOrErr)
        return ElemOrErr.takeError();
      elements.push_back(*ElemOrErr);
    }
    Result = std::make_shared<PyObject>(std::move(elements));
    if (refIdx < Refs.size())
      Refs[refIdx] = Result;
    break;
  }

  case TYPE_REF: {
    if (Offset + 4 > Size)
      return createStringError("Truncated ref index");
    uint32_t idx = readU32LE(Data + Offset);
    Offset += 4;
    if (idx >= Refs.size() || !Refs[idx])
      return createStringError("Invalid ref index");
    return Refs[idx];
  }

  case TYPE_CODE: {
    // Reserve ref slot early
    size_t refIdx = 0;
    if (AddRef) {
      refIdx = Refs.size();
      Refs.push_back(nullptr);
      AddRef = false;
    }

    auto Code = std::make_shared<CodeObject>();

    // Python 3.11+ code object format (5 int fields, not 6 - no nlocals)
    if (Offset + 4 > Size)
      return createStringError("Truncated code argcount");
    Code->ArgCount = readU32LE(Data + Offset);
    Offset += 4;

    if (Offset + 4 > Size)
      return createStringError("Truncated code posonlyargcount");
    Code->PosOnlyArgCount = readU32LE(Data + Offset);
    Offset += 4;

    if (Offset + 4 > Size)
      return createStringError("Truncated code kwonlyargcount");
    Code->KwOnlyArgCount = readU32LE(Data + Offset);
    Offset += 4;

    // Note: Python 3.11+ removed nlocals from serialized format
    // It's now derived from localspluskinds

    if (Offset + 4 > Size)
      return createStringError("Truncated code stacksize");
    Code->StackSize = readU32LE(Data + Offset);
    Offset += 4;

    if (Offset + 4 > Size)
      return createStringError("Truncated code flags");
    Code->Flags = readU32LE(Data + Offset);
    Offset += 4;

    // Bytecode
    auto BytecodeOrErr = unmarshalObject(Data, Size, Offset, Refs);
    if (!BytecodeOrErr)
      return BytecodeOrErr.takeError();
    if ((*BytecodeOrErr)->isBytes()) {
      auto bytes = (*BytecodeOrErr)->getBytes();
      Code->Bytecode.assign(bytes.begin(), bytes.end());
    } else if ((*BytecodeOrErr)->isString()) {
      auto str = (*BytecodeOrErr)->getString();
      Code->Bytecode.assign(str.begin(), str.end());
    }

    // Constants
    auto ConstantsOrErr = unmarshalObject(Data, Size, Offset, Refs);
    if (!ConstantsOrErr)
      return ConstantsOrErr.takeError();
    if ((*ConstantsOrErr)->isTuple()) {
      for (const auto &elem : (*ConstantsOrErr)->getTuple()) {
        Code->Constants.push_back(elem);
      }
    }

    // Names
    auto NamesOrErr = unmarshalObject(Data, Size, Offset, Refs);
    if (!NamesOrErr)
      return NamesOrErr.takeError();
    if ((*NamesOrErr)->isTuple()) {
      for (const auto &elem : (*NamesOrErr)->getTuple()) {
        if (elem->isString())
          Code->Names.push_back(std::string(elem->getString()));
      }
    }

    // Python 3.11+: localsplusnames (combines varnames, freevars, cellvars)
    auto LocalPlusNamesOrErr = unmarshalObject(Data, Size, Offset, Refs);
    if (!LocalPlusNamesOrErr)
      return LocalPlusNamesOrErr.takeError();
    std::vector<std::string> localPlusNames;
    if ((*LocalPlusNamesOrErr)->isTuple()) {
      for (const auto &elem : (*LocalPlusNamesOrErr)->getTuple()) {
        if (elem->isString())
          localPlusNames.push_back(std::string(elem->getString()));
      }
    }

    // Python 3.11+: localspluskinds (byte array indicating type of each local)
    // CO_FAST_LOCAL = 0x20, CO_FAST_CELL = 0x40, CO_FAST_FREE = 0x80
    auto LocalPlusKindsOrErr = unmarshalObject(Data, Size, Offset, Refs);
    if (!LocalPlusKindsOrErr)
      return LocalPlusKindsOrErr.takeError();
    std::vector<uint8_t> localPlusKinds;
    if ((*LocalPlusKindsOrErr)->isBytes()) {
      auto bytes = (*LocalPlusKindsOrErr)->getBytes();
      localPlusKinds.assign(bytes.begin(), bytes.end());
    } else if ((*LocalPlusKindsOrErr)->isString()) {
      auto str = (*LocalPlusKindsOrErr)->getString();
      localPlusKinds.assign(str.begin(), str.end());
    }

    // Parse localsplusnames/kinds into LocalNames, FreeVars, CellVars
    constexpr uint8_t CO_FAST_LOCAL = 0x20;
    constexpr uint8_t CO_FAST_CELL = 0x40;
    constexpr uint8_t CO_FAST_FREE = 0x80;

    for (size_t i = 0; i < localPlusNames.size() && i < localPlusKinds.size(); ++i) {
      uint8_t kind = localPlusKinds[i];
      if (kind & CO_FAST_FREE) {
        Code->FreeVars.push_back(localPlusNames[i]);
      } else if (kind & CO_FAST_CELL) {
        Code->CellVars.push_back(localPlusNames[i]);
      } else if (kind & CO_FAST_LOCAL) {
        Code->LocalNames.push_back(localPlusNames[i]);
      }
    }

    // NumLocals is the count of locals (not freevars or cellvars)
    Code->NumLocals = Code->LocalNames.size();

    // Filename
    auto FilenameOrErr = unmarshalObject(Data, Size, Offset, Refs);
    if (!FilenameOrErr)
      return FilenameOrErr.takeError();
    if ((*FilenameOrErr)->isString())
      Code->Filename = std::string((*FilenameOrErr)->getString());

    // Name
    auto NameOrErr = unmarshalObject(Data, Size, Offset, Refs);
    if (!NameOrErr)
      return NameOrErr.takeError();
    if ((*NameOrErr)->isString())
      Code->Name = std::string((*NameOrErr)->getString());

    // Qualname (Python 3.11+)
    auto QualnameOrErr = unmarshalObject(Data, Size, Offset, Refs);
    if (!QualnameOrErr)
      return QualnameOrErr.takeError();
    if ((*QualnameOrErr)->isString())
      Code->QualName = std::string((*QualnameOrErr)->getString());

    // First line number
    if (Offset + 4 > Size)
      return createStringError("Truncated code firstlineno");
    Code->FirstLineNo = readU32LE(Data + Offset);
    Offset += 4;

    // Line table
    auto LineTableOrErr = unmarshalObject(Data, Size, Offset, Refs);
    if (!LineTableOrErr)
      return LineTableOrErr.takeError();
    if ((*LineTableOrErr)->isBytes()) {
      auto bytes = (*LineTableOrErr)->getBytes();
      Code->LineTable.assign(bytes.begin(), bytes.end());
    } else if ((*LineTableOrErr)->isString()) {
      auto str = (*LineTableOrErr)->getString();
      Code->LineTable.assign(str.begin(), str.end());
    }

    // Exception table (Python 3.11+)
    auto ExceptionTableOrErr = unmarshalObject(Data, Size, Offset, Refs);
    if (!ExceptionTableOrErr)
      return ExceptionTableOrErr.takeError();
    if ((*ExceptionTableOrErr)->isBytes()) {
      auto bytes = (*ExceptionTableOrErr)->getBytes();
      Code->ExceptionTable.assign(bytes.begin(), bytes.end());
    } else if ((*ExceptionTableOrErr)->isString()) {
      auto str = (*ExceptionTableOrErr)->getString();
      Code->ExceptionTable.assign(str.begin(), str.end());
    }

    Result = std::make_shared<PyObject>(Code);
    if (refIdx < Refs.size())
      Refs[refIdx] = Result;
    break;
  }

  case TYPE_NULL:
    // NULL is used as a placeholder, return None
    Result = std::make_shared<PyObject>(PyObject::makeNone());
    break;

  default:
    return createStringError(
        Twine("Unsupported marshal type: ") + Twine(Type) + " ('" +
            Twine(static_cast<char>(Type)) + "')");
  }

  if (AddRef && Result)
    Refs.push_back(Result);

  return Result;
}

Expected<uint32_t> BytecodeLoader::readVarint(const uint8_t *Data, size_t Size,
                                               size_t &Offset) {
  uint32_t result = 0;
  unsigned shift = 0;

  while (Offset < Size) {
    uint8_t byte = Data[Offset++];
    result |= static_cast<uint32_t>(byte & 0x7F) << shift;
    if ((byte & 0x80) == 0)
      return result;
    shift += 7;
    if (shift > 28)
      return createStringError("Varint too large");
  }

  return createStringError("Truncated varint");
}
