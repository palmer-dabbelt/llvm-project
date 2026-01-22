# Plang Development Plan

Plang is a Python bytecode JIT compiler targeting LLVM. This document tracks the development roadmap and current progress.

---

## Phase 1: Systematic Opcode Testing

Create comprehensive test coverage for all Python 3.12+ bytecode opcodes. Each opcode should have at least one test case that verifies it works correctly.

### Tested Opcodes

| Opcode | Test File |
|--------|-----------|
| LOAD_CONST | Various tests |
| LOAD_FAST / STORE_FAST | delete-fast.test |
| LOAD_NAME / STORE_NAME | store-name.test, global-vars.test |
| DELETE_FAST | delete-fast.test |
| BINARY_OP (+, -, *, //, %) | arithmetic.test, binary-*.test |
| BINARY_OP (&, \|, ^, <<, >>) | binary-bitwise.test |
| UNARY_NEGATIVE / UNARY_NOT / UNARY_INVERT | unary-ops.test |
| COMPARE_OP (<) | compare-ops.test |
| IS_OP | is-op.test |
| POP_JUMP_IF_FALSE | if-else.test |
| JUMP_BACKWARD | while-loop.test |
| RETURN_VALUE | All tests |
| CALL | delete-fast.test (user functions), hello-world.test (print) |
| MAKE_FUNCTION | delete-fast.test (basic no-arg) |
| PUSH_NULL | hello-world.test |

### Untested Opcodes

#### Variables
| Opcode | Notes |
|--------|-------|
| LOAD_FAST_CHECK | Used for possibly unbound locals |
| LOAD_FAST_AND_CLEAR | Used in comprehensions |
| DELETE_NAME | Module-level deletion |
| STORE_GLOBAL | Requires MAKE_FUNCTION |
| DELETE_GLOBAL | Requires MAKE_FUNCTION |

#### Arithmetic
| Opcode | Notes |
|--------|-------|
| BINARY_OP (/) | True division - currently stubbed to use // |

#### Comparison & Jumps
| Opcode | Notes |
|--------|-------|
| COMPARE_OP (<=) | Arg encoding needs investigation |
| COMPARE_OP (>) | Arg encoding needs investigation |
| COMPARE_OP (>=) | Arg encoding needs investigation |
| COMPARE_OP (==) | Arg encoding needs investigation |
| COMPARE_OP (!=) | Arg encoding needs investigation |
| CONTAINS_OP | `x in y`, `x not in y` |
| POP_JUMP_IF_TRUE | |
| POP_JUMP_IF_NONE | |
| POP_JUMP_IF_NOT_NONE | |
| JUMP_BACKWARD_NO_INTERRUPT | |

#### Stack Manipulation
| Opcode | Notes |
|--------|-------|
| COPY | Duplicate stack item |
| SWAP | Swap stack items |

#### Collections (All Stubs)
| Opcode | Notes |
|--------|-------|
| BUILD_TUPLE | Returns 0 placeholder |
| BUILD_LIST | Returns 0 placeholder |
| BUILD_SET | Returns 0 placeholder |
| BUILD_MAP | Returns 0 placeholder |
| BUILD_CONST_KEY_MAP | Returns 0 placeholder |
| BUILD_STRING | Returns 0 placeholder |
| BUILD_SLICE | Returns 0 placeholder |
| UNPACK_SEQUENCE | Returns 0 placeholder |
| UNPACK_EX | Returns 0 placeholder |
| BINARY_SUBSCR | `x[y]` - returns 0 |
| STORE_SUBSCR | `x[y] = z` |
| DELETE_SUBSCR | `del x[y]` |
| LIST_APPEND | List comprehensions |
| SET_ADD | Set comprehensions |
| MAP_ADD | Dict comprehensions |
| LIST_EXTEND | `[*x, ...]` |
| SET_UPDATE | `{*x, ...}` |
| DICT_MERGE | `{**x, ...}` |
| DICT_UPDATE | Dict update |
| GET_LEN | `len()` - returns 0 |

#### Iterators (Stubs)
| Opcode | Notes |
|--------|-------|
| GET_ITER | `iter(x)` - returns x |
| FOR_ITER | Always exhausted immediately |
| END_FOR | Cleanup |

#### Attributes (Stubs)
| Opcode | Notes |
|--------|-------|
| LOAD_ATTR | `x.y` - returns 0 |
| STORE_ATTR | `x.y = z` |
| DELETE_ATTR | `del x.y` |
| LOAD_SUPER_ATTR | `super().x` |

#### Functions & Closures (Stubs)
| Opcode | Notes |
|--------|-------|
| MAKE_FUNCTION | Basic no-arg functions work; defaults/annotations/closures not supported |
| CALL_FUNCTION_EX | `f(*args, **kwargs)` |
| KW_NAMES | Keyword arguments |
| MAKE_CELL | Closures |
| LOAD_CLOSURE | Load cell |
| LOAD_DEREF | Load from cell |
| STORE_DEREF | Store to cell |
| DELETE_DEREF | Delete cell |
| COPY_FREE_VARS | Copy free vars |

#### Exceptions (Stubs)
| Opcode | Notes |
|--------|-------|
| PUSH_EXC_INFO | Push exception |
| CHECK_EXC_MATCH | `except E:` |
| CHECK_EG_MATCH | Exception groups |
| POP_EXCEPT | End except block |
| RAISE_VARARGS | `raise` |
| RERAISE | Re-raise |
| LOAD_ASSERTION_ERROR | `assert` |

#### Classes (Stubs)
| Opcode | Notes |
|--------|-------|
| LOAD_BUILD_CLASS | `class C:` |
| MATCH_CLASS | Pattern matching |

#### Imports (Stubs)
| Opcode | Notes |
|--------|-------|
| IMPORT_NAME | `import x` |
| IMPORT_FROM | `from x import y` |

#### Generators/Async (Stubs)
| Opcode | Notes |
|--------|-------|
| RETURN_GENERATOR | Generator function |
| YIELD_VALUE | `yield x` |
| SEND | `.send()` |
| END_SEND | End send |
| GET_YIELD_FROM_ITER | `yield from` |
| GET_AWAITABLE | `await` |
| GET_AITER | `async for` |
| GET_ANEXT | Async iterator |
| BEFORE_ASYNC_WITH | `async with` |
| END_ASYNC_FOR | End async for |

#### Context Managers (Stubs)
| Opcode | Notes |
|--------|-------|
| BEFORE_WITH | `with x:` |
| WITH_EXCEPT_START | With exception |

#### Pattern Matching (Stubs)
| Opcode | Notes |
|--------|-------|
| MATCH_MAPPING | `case {}:` |
| MATCH_SEQUENCE | `case []:` |
| MATCH_KEYS | Dict pattern keys |

#### Other (Stubs)
| Opcode | Notes |
|--------|-------|
| INTERPRETER_EXIT | Exit interpreter |
| SETUP_ANNOTATIONS | Annotations |
| LOAD_LOCALS | `locals()` |
| FORMAT_VALUE | f-strings |
| CALL_INTRINSIC_1 | Internal |
| CALL_INTRINSIC_2 | Internal |
| LOAD_FROM_DICT_OR_GLOBALS | Class body |
| LOAD_FROM_DICT_OR_DEREF | Class body |

### Known Issues

1. **COMPARE_OP arg encoding**: Python 3.12 bytecode uses different arg values than documented. Only `<` (arg=2) is currently working.

2. **MAKE_FUNCTION**: Basic no-arg function calls now work. Functions with default arguments, annotations, keyword-only defaults, and closures are not yet supported.

---

## Future Phases

*(To be defined as development progresses)*

### Phase 2: Core Language Features
- User-defined functions (MAKE_FUNCTION, CALL)
- Classes and objects
- Exception handling

### Phase 3: Collections
- Lists, tuples, dicts, sets
- Iterators and for loops
- Comprehensions

### Phase 4: Advanced Features
- Closures and nested functions
- Generators and coroutines
- Context managers

### Phase 5: Standard Library Compatibility
- Import system
- Built-in functions

### Phase 6: Performance Optimization
- JIT optimizations
- Type specialization
