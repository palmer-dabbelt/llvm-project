# Plang - Python Bytecode JIT for LLVM

Plang is an LLVM sub-project that implements a JIT compiler for Python bytecode.
It can load, disassemble, and execute Python `.pyc` files using LLVM's ORC JIT
infrastructure.

## Building

### In-tree build (recommended)

```bash
cd llvm-project
mkdir build && cd build
cmake -G Ninja \
  -DLLVM_ENABLE_PROJECTS="plang" \
  -DLLVM_TARGETS_TO_BUILD="Native" \
  -DCMAKE_BUILD_TYPE=Release \
  ../llvm
ninja plang
```

### Standalone build

```bash
cd llvm-project/plang
mkdir build && cd build
cmake -G Ninja \
  -DLLVM_CMAKE_DIR=/path/to/llvm-build/lib/cmake/llvm \
  -DCMAKE_BUILD_TYPE=Release \
  ..
ninja
```

## Usage

```bash
# Show help
plang --help

# Disassemble a .pyc file
plang --disassemble example.pyc

# Dump generated LLVM IR
plang --dump-ir example.pyc

# JIT compile and run
plang --run example.pyc
```

## Creating a test .pyc file

```bash
# Create a simple Python file
echo "x = 1 + 2" > test.py

# Compile to bytecode
python3 -c "import py_compile; py_compile.compile('test.py')"

# The .pyc file will be in __pycache__/
plang --disassemble __pycache__/test.cpython-*.pyc
```

## Features

- **Bytecode Loader**: Parses Python 3.11+ `.pyc` files including:
  - Magic number and version detection
  - Marshal format deserialization
  - Code object extraction

- **Disassembler**: Human-readable bytecode output with:
  - Opcode names and arguments
  - Constant and name annotations
  - Local variable references

- **JIT Compiler**: Compiles bytecode to native code via LLVM:
  - ORC JIT-based execution
  - Basic arithmetic operations
  - Local variable handling
  - Constant loading

## Supported Python Versions

Plang targets Python 3.11+ bytecode format, which uses:
- 2-byte word-aligned instructions
- New `CACHE` pseudo-instructions
- Updated code object layout

## Project Structure

```
plang/
├── include/plang/
│   ├── Bytecode/      # Bytecode parsing
│   ├── JIT/           # LLVM JIT integration
│   └── Support/       # Utilities
├── lib/               # Library implementations
├── tools/plang/       # CLI driver
├── test/              # LIT regression tests
└── unittests/         # GTest unit tests
```

## License

Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
See https://llvm.org/LICENSE.txt for license information.
