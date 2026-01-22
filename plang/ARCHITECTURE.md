# Plang Architecture

Plang is a Python bytecode JIT compiler targeting LLVM. It reads compiled Python `.pyc` files, translates the bytecode to LLVM IR, and executes it using LLVM's ORC JIT.

---

## Directory Structure

```
plang/
├── include/plang/
│   ├── Bytecode/           # Bytecode parsing and representation
│   │   ├── CodeObject.h    # Python code object representation
│   │   ├── Loader.h        # .pyc file parser
│   │   └── Opcodes.h       # Python opcode definitions
│   ├── JIT/                # JIT compilation
│   │   ├── Compiler.h      # Bytecode to LLVM IR compiler
│   │   └── PlangJIT.h      # ORC JIT wrapper
│   └── Support/
│       └── Version.h       # Version information
├── lib/
│   ├── Bytecode/           # Bytecode layer implementation
│   ├── JIT/                # JIT layer implementation
│   └── Support/            # Support utilities
├── tools/plang/
│   └── Main.cpp            # CLI driver and runtime functions
├── test/Bytecode/          # Lit tests for bytecode support
└── unittests/              # Unit tests
```

---

## Architecture Layers

### 1. Bytecode Layer

The bytecode layer handles parsing Python `.pyc` files and representing their contents in C++ data structures.

#### Components

**`BytecodeLoader`** (`lib/Bytecode/Loader.cpp`)
- Parses Python `.pyc` file format (Python 3.7+ 16-byte headers)
- Unmarshals Python objects from the serialized format
- Handles both timestamp-based and hash-based `.pyc` invalidation modes
- Supports Python 3.8 through 3.13 via magic number detection

**`CodeObject`** (`lib/Bytecode/CodeObject.cpp`)
- Represents a Python code object (function, module, class body)
- Contains:
  - Bytecode instructions
  - Constants (integers, strings, nested CodeObjects)
  - Local variable names
  - Module-level names
  - Free/cell variables (for closures)
  - Metadata (filename, line numbers, flags)
- Provides `parseInstructions()` to decode raw bytecode into `Instruction` structs
- Provides `disassemble()` for human-readable output

**`Opcodes`** (`lib/Bytecode/Opcodes.cpp`)
- Defines all Python 3.12+ opcodes as an enum
- Provides helper functions: `getOpcodeName()`, `opcodeIsJump()`, etc.
- Note: Python 3.12 changed to word-aligned (2-byte) instructions

**`PyObject`** (in `CodeObject.h`)
- Represents Python constant values
- Supports: None, Bool, Int, Float, String, Bytes, Tuple, Code, StopIteration
- Uses `std::variant` for type-safe storage

### 2. JIT Layer

The JIT layer compiles Python bytecode to LLVM IR and executes it.

#### Components

**`BytecodeCompiler`** (`lib/JIT/Compiler.cpp`)
- Translates Python bytecode to LLVM IR
- Uses a stack-based model matching Python's virtual machine
- Key responsibilities:
  1. Create LLVM function for the code object
  2. Allocate runtime stack as an LLVM array
  3. Pre-compute constant values (register strings with runtime)
  4. Allocate local variable slots
  5. First pass: Scan for nested code objects and create function declarations
  6. Second pass: Compile bytecode instructions to IR
  7. Compile nested function bodies

**Compilation Model:**
```
Python Bytecode → BytecodeCompiler → LLVM IR Module → PlangJIT → Native Code
```

**Value Representation:**
Values are represented as `i64` with a tagged scheme:
- Positive values: integers (stored directly)
- Negative values: special meanings
  - `-(index + 1)`: string reference (index into runtime string table)
  - `BUILTIN_PRINT (-1000000001)`: built-in print function
  - `USER_FUNC_BASE - n`: user-defined function at index n

**Stack Operations:**
The compiler maintains a runtime stack using LLVM allocas:
- `StackBase`: Pointer to `[N x i64]` array
- `StackPtr`: Pointer to `i64` tracking current stack position
- `emitPush()`, `emitPop()`, `emitPeek()`: Stack manipulation

**Jump Handling:**
- First pass identifies all jump targets and creates basic blocks
- Jumps are compiled as LLVM branches to the target blocks
- Backward jumps (loops) and forward jumps (conditionals) both supported

**`PlangJIT`** (`lib/JIT/PlangJIT.cpp`)
- Wraps LLVM's ORC (On-Request Compilation) JIT
- Components:
  - `ExecutionSession`: Manages JIT state
  - `RTDyldObjectLinkingLayer`: Links object files
  - `IRCompileLayer`: Compiles LLVM IR to machine code
- Provides:
  - `addModule()`: Add LLVM modules for compilation
  - `lookup()`: Find compiled symbols
  - `defineAbsoluteSymbol()`: Register runtime functions

### 3. Runtime

The runtime provides functions callable from JIT-compiled code, defined in `tools/plang/Main.cpp`.

**`plang_print(int64_t value)`**
- Prints a Python value
- Checks if value is a string reference (negative) or integer (positive)
- Looks up strings in `RuntimeStrings` table

**`plang_register_string(const char* str, size_t len)`**
- Registers a string in the runtime table
- Returns the index (used to create tagged string reference)
- Called during module initialization for string constants

**`RuntimeStrings`**
- Global string table indexed by integer
- Strings are registered at compile time, printed at runtime

### 4. Driver

**`tools/plang/Main.cpp`**
- CLI interface with three modes:
  - `--disassemble` / `-d`: Show human-readable bytecode
  - `--dump-ir`: Show generated LLVM IR
  - `--run` / `-r`: JIT compile and execute

**Execution Flow:**
1. Parse command line options
2. Load `.pyc` file via `BytecodeLoader`
3. If `--run`:
   - Initialize LLVM native targets
   - Compile with `BytecodeCompiler`
   - Create `PlangJIT` instance
   - Register runtime functions
   - Add compiled module to JIT
   - Look up entry function (`__plang_<name>__`)
   - Execute and print result

---

## Opcode Implementation Status

Opcodes fall into categories by implementation status:

### Fully Implemented
- **Variables**: LOAD_CONST, LOAD_FAST, STORE_FAST, DELETE_FAST, LOAD_NAME, STORE_NAME
- **Arithmetic**: BINARY_OP (+, -, *, //, %, &, |, ^, <<, >>)
- **Unary**: UNARY_NEGATIVE, UNARY_NOT, UNARY_INVERT
- **Comparison**: COMPARE_OP (<), IS_OP
- **Control Flow**: POP_JUMP_IF_FALSE, JUMP_BACKWARD, RETURN_VALUE, RETURN_CONST
- **Functions**: CALL (print, user functions), MAKE_FUNCTION (basic no-arg), PUSH_NULL
- **Stack**: POP_TOP

### Stubbed (Return 0 or No-op)
- Collections: BUILD_TUPLE, BUILD_LIST, BUILD_MAP, BUILD_SET
- Iterators: GET_ITER, FOR_ITER
- Attributes: LOAD_ATTR, STORE_ATTR
- Closures: MAKE_CELL, LOAD_DEREF, STORE_DEREF
- Exceptions: PUSH_EXC_INFO, POP_EXCEPT, RAISE_VARARGS
- Classes: LOAD_BUILD_CLASS
- Imports: IMPORT_NAME, IMPORT_FROM
- Generators: YIELD_VALUE, RETURN_GENERATOR

See `PLAN.md` for the full opcode tracking table.

---

## Data Flow

```
                    ┌─────────────────┐
                    │   .pyc File     │
                    └────────┬────────┘
                             │
                             ▼
                    ┌─────────────────┐
                    │ BytecodeLoader  │  Parse header + unmarshal
                    └────────┬────────┘
                             │
                             ▼
                    ┌─────────────────┐
                    │   CodeObject    │  In-memory representation
                    └────────┬────────┘
                             │
                             ▼
                    ┌─────────────────┐
                    │BytecodeCompiler │  Generate LLVM IR
                    └────────┬────────┘
                             │
                             ▼
                    ┌─────────────────┐
                    │  LLVM Module    │  IR representation
                    └────────┬────────┘
                             │
                             ▼
                    ┌─────────────────┐
                    │    PlangJIT     │  ORC JIT compilation
                    └────────┬────────┘
                             │
                             ▼
                    ┌─────────────────┐
                    │  Native Code    │  Executable machine code
                    └────────┬────────┘
                             │
                             ▼
                    ┌─────────────────┐
                    │    Runtime      │  plang_print, etc.
                    └─────────────────┘
```

---

## Testing

### Lit Tests (`test/Bytecode/`)
- Integration tests using LLVM's lit framework
- Each test:
  1. Copies Python source to temp directory
  2. Compiles with `python -m compileall`
  3. Runs with `plang --run`
  4. Verifies output with FileCheck

### Unit Tests (`unittests/`)
- C++ unit tests for individual components
- Currently focused on bytecode loader

### Running Tests
```bash
ninja check-plang       # Run all plang tests
ninja check-plang-unit  # Run unit tests only
```

---

## Future Directions

See `PLAN.md` for the development roadmap. Key areas:

1. **Complete opcode support**: Remaining comparison operators, iteration, collections
2. **Function features**: Default arguments, closures, keyword arguments
3. **Object system**: Classes, instances, attribute access
4. **Exception handling**: Try/except/finally
5. **Performance**: Type specialization, inline caching
