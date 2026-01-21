# Plang: Systematic Opcode Testing Plan

## Overview
Create comprehensive test coverage for all Python 3.12+ bytecode opcodes. Each opcode should have at least one test case that verifies it works correctly.

## Current State

**Total Tests: 19 (all passing)**

**Original Tests (7):**
- `pass.py.test` - Empty module execution
- `fail.py.test` - Failure case
- `compileall-simple.test` - Empty module via compileall
- `compileall-expr.test` - Expression evaluation
- `arithmetic.test` - Constant folding (1+2+3=6)
- `hello-world.test` - print() function call
- `while-loop.test` - While loop with COMPARE_OP and jumps

**New Tests (12):**
- `binary-sub.test` - Subtraction (10 - 3 = 7)
- `binary-mul.test` - Multiplication (6 * 7 = 42)
- `binary-div.test` - Division (15 // 3 = 5)
- `binary-floordiv.test` - Floor division (10 // 3 = 3)
- `binary-mod.test` - Modulo (10 % 3 = 1)
- `binary-bitwise.test` - Bitwise &, |, ^, << operations
- `unary-ops.test` - Negation, not, bitwise invert (~)
- `compare-ops.test` - Less-than comparison (<)
- `if-else.test` - If-else control flow with JUMP_FORWARD
- `is-op.test` - Identity operators (is, is not)
- `store-name.test` - Module-level name storage
- `global-vars.test` - Module-level variable operations

## Opcode Test Status

### Core Opcodes (Well Tested)
| Opcode | Status | Test File | Notes |
|--------|--------|-----------|-------|
| CACHE | ✅ Tested | hello-world.test | Implicitly tested (no-op) |
| NOP | ✅ Tested | * | Implicitly tested (no-op) |
| RESUME | ✅ Tested | hello-world.test | Entry point opcode |
| LOAD_CONST | ✅ Tested | arithmetic.test | Load constants |
| RETURN_VALUE | ✅ Tested | arithmetic.test | Return from function |
| RETURN_CONST | ✅ Tested | compileall-simple.test | Return constant directly |
| POP_TOP | ✅ Tested | compileall-expr.test | Discard value |
| PUSH_NULL | ✅ Tested | hello-world.test | Used before CALL |

### Variables (Mostly Tested)
| Opcode | Status | Notes |
|--------|--------|-------|
| LOAD_FAST | ✅ Tested | while-loop.test (x variable) |
| STORE_FAST | ✅ Tested | while-loop.test (x = ...) |
| DELETE_FAST | ❌ Untested | Needs test: `del x` in function |
| LOAD_FAST_CHECK | ❌ Untested | Used for possibly unbound locals |
| LOAD_FAST_AND_CLEAR | ❌ Untested | Used in comprehensions |
| LOAD_NAME | ✅ Tested | hello-world.test (print), store-name.test |
| STORE_NAME | ✅ Tested | store-name.test, global-vars.test |
| DELETE_NAME | ❌ Untested | Module-level deletion |
| LOAD_GLOBAL | ✅ Tested | Implicitly via LOAD_NAME for builtins |
| STORE_GLOBAL | ❌ Stub | Functions not yet supported |
| DELETE_GLOBAL | ❌ Stub | Functions not yet supported |

### Arithmetic & Unary Ops (Well Tested)
| Opcode | Status | Notes |
|--------|--------|-------|
| BINARY_OP (+) | ✅ Tested | arithmetic.test, while-loop.test |
| BINARY_OP (-) | ✅ Tested | binary-sub.test |
| BINARY_OP (*) | ✅ Tested | binary-mul.test |
| BINARY_OP (/) | ⚠️ Partial | binary-div.test uses // (true division stub) |
| BINARY_OP (//) | ✅ Tested | binary-floordiv.test |
| BINARY_OP (%) | ✅ Tested | binary-mod.test |
| BINARY_OP (&) | ✅ Tested | binary-bitwise.test |
| BINARY_OP (\|) | ✅ Tested | binary-bitwise.test |
| BINARY_OP (^) | ✅ Tested | binary-bitwise.test |
| BINARY_OP (<<) | ✅ Tested | binary-bitwise.test |
| UNARY_NEGATIVE | ✅ Tested | unary-ops.test (-x) |
| UNARY_NOT | ✅ Tested | unary-ops.test (not 0, not 1) |
| UNARY_INVERT | ✅ Tested | unary-ops.test (~5 = -6) |

### Comparison & Jumps (Partially Tested)
| Opcode | Status | Notes |
|--------|--------|-------|
| COMPARE_OP (<) | ✅ Tested | while-loop.test, compare-ops.test |
| COMPARE_OP (<=) | ⚠️ Untested | Arg encoding may differ |
| COMPARE_OP (>) | ⚠️ Untested | Arg encoding may differ |
| COMPARE_OP (>=) | ⚠️ Untested | Arg encoding may differ |
| COMPARE_OP (==) | ⚠️ Untested | Arg encoding may differ |
| COMPARE_OP (!=) | ⚠️ Untested | Arg encoding may differ |
| IS_OP | ✅ Tested | is-op.test |
| CONTAINS_OP | ❌ Untested | `x in y`, `x not in y` |
| POP_JUMP_IF_FALSE | ✅ Tested | while-loop.test, if-else.test |
| POP_JUMP_IF_TRUE | ❌ Untested | |
| POP_JUMP_IF_NONE | ❌ Untested | |
| POP_JUMP_IF_NOT_NONE | ❌ Untested | |
| JUMP_FORWARD | ✅ Tested | if-else.test |
| JUMP_BACKWARD | ✅ Tested | while-loop.test (loop back) |
| JUMP_BACKWARD_NO_INTERRUPT | ❌ Untested | |

### Stack Manipulation (Untested)
| Opcode | Status | Notes |
|--------|--------|-------|
| COPY | ❌ Untested | Duplicate stack item |
| SWAP | ❌ Untested | Swap stack items |

### Collections (All Stubs - Untested)
| Opcode | Status | Notes |
|--------|--------|-------|
| BUILD_TUPLE | ❌ Stub | Returns 0 placeholder |
| BUILD_LIST | ❌ Stub | Returns 0 placeholder |
| BUILD_SET | ❌ Stub | Returns 0 placeholder |
| BUILD_MAP | ❌ Stub | Returns 0 placeholder |
| BUILD_CONST_KEY_MAP | ❌ Stub | Returns 0 placeholder |
| BUILD_STRING | ❌ Stub | Returns 0 placeholder |
| BUILD_SLICE | ❌ Stub | Returns 0 placeholder |
| UNPACK_SEQUENCE | ❌ Stub | Returns 0 placeholder |
| UNPACK_EX | ❌ Stub | Returns 0 placeholder |
| BINARY_SUBSCR | ❌ Stub | `x[y]` - returns 0 |
| STORE_SUBSCR | ❌ Stub | `x[y] = z` |
| DELETE_SUBSCR | ❌ Stub | `del x[y]` |
| LIST_APPEND | ❌ Stub | List comprehensions |
| SET_ADD | ❌ Stub | Set comprehensions |
| MAP_ADD | ❌ Stub | Dict comprehensions |
| LIST_EXTEND | ❌ Stub | `[*x, ...]` |
| SET_UPDATE | ❌ Stub | `{*x, ...}` |
| DICT_MERGE | ❌ Stub | `{**x, ...}` |
| DICT_UPDATE | ❌ Stub | Dict update |
| GET_LEN | ❌ Stub | `len()` - returns 0 |

### Iterators (Stubs)
| Opcode | Status | Notes |
|--------|--------|-------|
| GET_ITER | ❌ Stub | `iter(x)` - returns x |
| FOR_ITER | ❌ Stub | Always exhausted immediately |
| END_FOR | ❌ Stub | Cleanup |

### Attributes (Stubs)
| Opcode | Status | Notes |
|--------|--------|-------|
| LOAD_ATTR | ❌ Stub | `x.y` - returns 0 |
| STORE_ATTR | ❌ Stub | `x.y = z` |
| DELETE_ATTR | ❌ Stub | `del x.y` |
| LOAD_SUPER_ATTR | ❌ Stub | `super().x` |

### Functions & Closures (Stubs)
| Opcode | Status | Notes |
|--------|--------|-------|
| CALL | ✅ Tested | hello-world.test (print) |
| MAKE_FUNCTION | ❌ Stub | `def f(): ...` |
| CALL_FUNCTION_EX | ❌ Stub | `f(*args, **kwargs)` |
| KW_NAMES | ❌ Stub | Keyword arguments |
| MAKE_CELL | ❌ Stub | Closures |
| LOAD_CLOSURE | ❌ Stub | Load cell |
| LOAD_DEREF | ❌ Stub | Load from cell |
| STORE_DEREF | ❌ Stub | Store to cell |
| DELETE_DEREF | ❌ Stub | Delete cell |
| COPY_FREE_VARS | ❌ Stub | Copy free vars |

### Exceptions (Stubs)
| Opcode | Status | Notes |
|--------|--------|-------|
| PUSH_EXC_INFO | ❌ Stub | Push exception |
| CHECK_EXC_MATCH | ❌ Stub | `except E:` |
| CHECK_EG_MATCH | ❌ Stub | Exception groups |
| POP_EXCEPT | ❌ Stub | End except block |
| RAISE_VARARGS | ❌ Stub | `raise` |
| RERAISE | ❌ Stub | Re-raise |
| LOAD_ASSERTION_ERROR | ❌ Stub | `assert` |

### Classes (Stubs)
| Opcode | Status | Notes |
|--------|--------|-------|
| LOAD_BUILD_CLASS | ❌ Stub | `class C:` |
| MATCH_CLASS | ❌ Stub | Pattern matching |

### Imports (Stubs)
| Opcode | Status | Notes |
|--------|--------|-------|
| IMPORT_NAME | ❌ Stub | `import x` |
| IMPORT_FROM | ❌ Stub | `from x import y` |

### Generators/Async (Stubs)
| Opcode | Status | Notes |
|--------|--------|-------|
| RETURN_GENERATOR | ❌ Stub | Generator function |
| YIELD_VALUE | ❌ Stub | `yield x` |
| SEND | ❌ Stub | `.send()` |
| END_SEND | ❌ Stub | End send |
| GET_YIELD_FROM_ITER | ❌ Stub | `yield from` |
| GET_AWAITABLE | ❌ Stub | `await` |
| GET_AITER | ❌ Stub | `async for` |
| GET_ANEXT | ❌ Stub | Async iterator |
| BEFORE_ASYNC_WITH | ❌ Stub | `async with` |
| END_ASYNC_FOR | ❌ Stub | End async for |

### Context Managers (Stubs)
| Opcode | Status | Notes |
|--------|--------|-------|
| BEFORE_WITH | ❌ Stub | `with x:` |
| WITH_EXCEPT_START | ❌ Stub | With exception |

### Pattern Matching (Stubs)
| Opcode | Status | Notes |
|--------|--------|-------|
| MATCH_MAPPING | ❌ Stub | `case {}:` |
| MATCH_SEQUENCE | ❌ Stub | `case []:` |
| MATCH_KEYS | ❌ Stub | Dict pattern keys |

### Other (Stubs/No-ops)
| Opcode | Status | Notes |
|--------|--------|-------|
| INTERPRETER_EXIT | ❌ Stub | Exit interpreter |
| SETUP_ANNOTATIONS | ❌ Stub | Annotations |
| LOAD_LOCALS | ❌ Stub | `locals()` |
| EXTENDED_ARG | ✅ N/A | Handled in parser |
| FORMAT_VALUE | ❌ Stub | f-strings |
| CALL_INTRINSIC_1 | ❌ Stub | Internal |
| CALL_INTRINSIC_2 | ❌ Stub | Internal |
| LOAD_FROM_DICT_OR_GLOBALS | ❌ Stub | Class body |
| LOAD_FROM_DICT_OR_DEREF | ❌ Stub | Class body |

## Known Issues

1. **COMPARE_OP arg encoding**: The Python 3.12 bytecode uses different arg values than documented. Only `<` (arg=2) is currently working. Other comparison operators need investigation.

2. **MAKE_FUNCTION**: Function definitions don't work yet, preventing tests for STORE_GLOBAL and other function-related features.

## Next Steps

### Immediate (Phase 1 complete)
- [x] Arithmetic operators (-, *, //, %, &, |, ^, <<)
- [x] Unary operators (-, not, ~)
- [x] COMPARE_OP (<)
- [x] IS_OP
- [x] JUMP_FORWARD
- [x] STORE_NAME/LOAD_NAME

### Future Work
1. Fix COMPARE_OP arg encoding for <=, >, >=, ==, !=
2. Implement MAKE_FUNCTION for user-defined functions
3. Implement collections (lists, tuples, dicts)
4. Implement iterators and for loops
5. Implement exception handling
6. Implement classes
