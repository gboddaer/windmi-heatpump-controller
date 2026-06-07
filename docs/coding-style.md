# Coding Style Guide

This project follows a simple, practical style inspired by the
[Google C++ Style Guide](https://google.github.io/styleguide/cppguide.html).
The goal is readable, maintainable, portable code rather than clever code.

## Core Principles

- Optimize for the reader, not the writer.
- Prefer clear, explicit code over clever or compact code.
- Be consistent with nearby existing code.
- Be consistent with the broader C++ community when appropriate.
- Keep functions and files focused.
- Avoid surprising or dangerous constructs.
- Rules should pull their weight — don't add conventions that solve problems
  that rarely occur in this project.
- Favor portability across Linux, Windows, and embedded-style environments.
- Concede to optimization when necessary, but document the reason.

## Files and Headers

- Use lowercase file names with underscores where helpful.
  - Good: `modbus_client.c`, `serial_port.cpp`, `selftest.hpp`
- Header files must be self-contained: they should compile when included alone.
- Headers must include everything they directly use.
- Do not rely on transitive includes from other headers.
- Use include guards named `WINDMI_<PATH>_<FILE>_H_` (for `.h`) or `WINDMI_<PATH>_<FILE>_HPP_`
  (for `.hpp`). Use single underscores between segments, matching the existing
  project convention. For example, `include/utils/Config.hpp` uses
  `WINDMI_UTILS_CONFIG_HPP_`, and `include/modbus_client.h` uses
  `WINDMI_MODBUS_CLIENT_H_`.
- Prefer `#include` over forward declarations. Forward declarations introduce
  subtle risks (hidden dependencies, ABI changes, ODR violations) and should
  only be used when the full header is prohibitively expensive to include.
- Keep public headers small and avoid exposing implementation details.
- Source files should include their matching header first when one exists.

Recommended include order:

1. Matching project header
2. C system headers
3. C++ standard library headers
4. Third-party headers
5. Other project headers

Separate groups with a blank line.

## Naming

Use names that explain intent. Avoid abbreviations unless they are widely known
or already common in the project.

Recommended conventions:

- Files: `snake_case.c`, `snake_case.cpp`, `snake_case.h`, `snake_case.hpp`
- Variables and parameters: `snake_case`
- Functions: `PascalCase` in C++ (e.g., `AddTableEntry`), `snake_case` in C
  files. Accessors and mutators may use `snake_case` matching the variable
  (e.g., `count()`, `set_count(int)`).
- Types/classes/structs: `PascalCase` in C++ code
- Enumerators: `kPascalCase` (e.g., `kOutOfMemory`)
- Constants: `kPascalCase` in C++ or existing uppercase C-style constants in C
- Macros: `PROJECT_SPECIFIC_UPPER_CASE`
- Namespace names: `snake_case`
- Private C++ class members: trailing underscore (e.g., `port_name_`)
- Struct data members (public by definition): no trailing underscore

Capitalize abbreviations as single words: `StartRpc()` not `StartRPC()`.

Avoid names like `tmp`, `data`, `val`, or `foo` unless the scope is tiny and the
meaning is obvious.

## Formatting

- Use spaces, not tabs.
- Indent by 2 spaces.
- Aim for 80 columns maximum. 100 columns is acceptable when breaking would
  hurt readability (long URLs, string literals, include guards).
- Put a space before `{` in functions, loops, and conditionals.
- Use braces for `if`, `else`, `for`, and `while` bodies, especially when the body
  is more than one short statement.
- Do not leave trailing whitespace.
- Use blank lines to separate logical sections, not after every statement.
- Do not indent the contents of a namespace.
- Place the `public:`, `protected:`, `private:` labels indented 1 space within a
  class. `public` comes first, then `protected`, then `private`.
- In boolean expressions that wrap across lines, put the operator at the end of
  the line, not the beginning of the next line.
- Align pointer/reference with the type, not the name: `char* c`, `const string& s`,
  not `char *c` or `const string &s`.
- Do not surround the `return` expression with unnecessary parentheses.
- Prefer preincrement (`++i`) unless postfix semantics are needed.
- Prefer `sizeof(varname)` over `sizeof(type)`.
- Preprocessor directives always start at column 1, even inside indented code.

```cpp
if (is_connected) {
  SendRequest(request);
} else {
  LogConnectionError();
}
```

Constructor initializer lists — wrap before the colon, indent 4 spaces,
align subsequent initializers:

```cpp
MyClass::MyClass(int var)
    : some_var_(var),
      other_var_(var + 1) {
  DoSomething();
}
```

Switch statements — 2-space indent for `case`, braces optional, `default` required
unless switching on an enum where all values are handled:

```cpp
switch (status) {
  case kOk: {
    HandleOk();
    break;
  }
  case kError: {
    HandleError();
    break;
  }
  default: {
    LOG(FATAL) << "Unexpected status: " << status;
  }
}
```

Annotate intentional fall-through with `[[fallthrough]];`.

## Functions

- Prefer small, focused functions.
- If a function grows beyond roughly 40 lines, consider splitting it.
- Put input parameters before output parameters.
- Prefer return values over output parameters when practical.
- Use `const` references or pointers for inputs that are not modified.
- Avoid boolean parameters that make call sites unclear; use an enum or options
  struct when readability benefits.
- Avoid hidden ownership transfer. Make ownership obvious in the type or comment.
- Mark single-argument constructors and conversion operators `explicit` to prevent
  implicit conversions. Copy and move constructors are the exception.
- Default arguments are allowed on non-virtual functions when the default value
  is guaranteed to always be the same. Never use default arguments on virtual
  functions.
- Function overloading is acceptable only when a reader can tell which overload
  is called without needing to resolve the types first. Overloads that differ
  only in argument type with identical semantics are fine.
- Use trailing return types (`auto foo() -> int`) only where the ordinary syntax
  is impractical (e.g., lambdas, complex templates).

## Classes and Structs

- Use `struct` for passive data with public fields and no invariants.
- Use `class` when behavior, invariants, or encapsulation matter.
- Struct data members use `snake_case` without a trailing underscore. Class data
  members use `snake_case` with a trailing underscore.
- Keep data members private in classes unless the type is a simple data struct.
- Prefer composition over inheritance.
- Mark overridden virtual functions with `override`.
- Do not call virtual functions from constructors.
- Make copy/move behavior explicit: declare (or delete) copy operations,
  move operations, or both in the `public` section so the API is clear.
- Avoid doing complex work in constructors if failure cannot be reported clearly.
- Declaration order within a class: types/aliases, static constants, factory
  functions, constructors, destructor, other methods, data members.
- Prefer a `struct` with named fields over a `pair` or `tuple` when the elements
  can have meaningful names.

## C++ Language Features

- Prefer standard, portable C++ features supported by the project toolchain.
- Avoid non-standard compiler extensions unless hidden behind portability wrappers.
- Prefer `nullptr` over `NULL` or `0` for pointers in C++.
- Prefer `constexpr` for true compile-time constants. Use `constinit` for
  non-constant static variables that need constant initialization.
- Use `auto` only when it improves readability or avoids an obviously redundant
  type. Do not use it merely to save typing.
- Prefer C++ casts over C-style casts. Use the first that correctly applies:
  brace initialization for arithmetic conversions (`int64_t{x}`), `static_cast`
  for value conversions and safe downcasts, `const_cast` to remove `const`,
  `reinterpret_cast` for unsafe pointer conversions, `std::bit_cast` for type
  punning same-sized values. Never use C-style casts.
- Avoid exceptions unless the project explicitly enables and supports them.
- Specify `noexcept` on move constructors and on functions where it is both
  correct and beneficial for performance. Prefer unconditional `noexcept` when
  exceptions are disabled in the build.
- Avoid RTTI (`dynamic_cast`, `typeid`) outside of test code. Prefer virtual
  methods or the Visitor pattern instead.
- Avoid complex template metaprogramming in ordinary application code.
- Avoid macros when an inline function, enum, or constant will work. If macros
  are necessary, define them right before use, `#undef` them right after, and
  give them a project-specific prefix.
- Do not use `using namespace foo;` (using-directives). Using-declarations
  (`using ::foo::Bar;`) are acceptable in `.cc` files but not in headers.
- Do not use inline namespaces.
- Do not define user-defined literals (`operator""`).
- Use operator overloading only when the meaning is obvious and consistent with
  the corresponding built-in operator. Prefer `==`, `=`, and `<<` over
  `Equals()`, `CopyFrom()`, and `PrintTo()`.
- Lambda captures: prefer explicit captures. Use `[&]` only when the lambda's
  lifetime is obviously shorter than all captures. Use `[=]` only for short
  lambdas where the captured set is obvious at a glance.
- Static and global variables must be trivially destructible. Use `constexpr` or
  `constinit` to ensure constant initialization.

## C Code

This project also contains C code. For C files:

- Keep interfaces simple and explicit.
- Prefer fixed-width integer types from `<stdint.h>` when size matters. Use plain
  `int` for values that fit in 32 bits. Use `int64_t` when values may exceed 2^31.
- Avoid unsigned integer types to assert non-negativity; use assertions instead.
  Unsigned types are appropriate for bitfields and modular arithmetic.
- Do not use `long double`.
- Avoid global mutable state unless necessary.
- Document ownership and lifetime for pointers.
- Check return values from system calls, allocation functions, and I/O functions.
- Keep platform-specific code isolated behind small functions or modules.

## Error Handling

- Check and handle errors close to where they occur.
- Return clear status values from lower-level code.
- Log enough context to diagnose failures, but avoid noisy logs in tight loops.
- Do not silently ignore failed I/O, parsing, allocation, or protocol operations.
- Use assertions only for programmer errors or impossible states, not normal input
  validation.

## Memory and Ownership

- Prefer automatic storage and value types when practical.
- In C++, prefer RAII for resources.
- Prefer `std::unique_ptr` for single ownership when dynamic allocation is needed.
  Factory functions can return `std::unique_ptr` to transfer ownership explicitly.
- Use `std::shared_ptr` only when shared ownership is genuinely needed (e.g.,
  immutable data where copying is expensive). Never use `std::auto_ptr`.
- Avoid shared ownership unless there is a clear design reason.
- In C, clearly document who owns allocated memory and who frees it.
- Avoid returning pointers to local variables or temporary storage.
- `thread_local` variables at class or namespace scope must be initialized with a
  constant expression; annotate with `constinit`.

## Comments

Good comments explain why code exists or clarify non-obvious behavior. They should
not repeat what the code already says.

- Every file should start with a license boilerplate appropriate to the project.
- Comment class and struct declarations with a brief description of what the type
  is for and how to use it. A short example is often helpful.
- Comment function declarations (not definitions) describing what the function
  does, its inputs and outputs, and any ownership or lifetime requirements on
  pointer/reference arguments. Omit only if the function is trivially obvious.
- When overriding a virtual function, comment only if the override adds behavior
  not already described by the base documentation.
- Comment tricky implementation details before the code they explain.
- Use complete sentences for longer comments.
- Keep comments up to date when changing code.
- Use inclusive language in code, comments, and documentation.
- Use `//` comments consistently. `/* */` is also acceptable but pick one style
  and stick with it in a given file.

TODO comments should include context — a bug number, design doc, or owner:

```cpp
// TODO: Replace this fallback when Windows serial detection is implemented.
// TODO(bug 12345): Update this list after the serial service is turned down.
```

## Tests

- Add or update tests when changing behavior.
- Prefer focused tests that exercise one behavior clearly.
- Include edge cases for parsing, protocol handling, and error paths.
- Run the relevant test target before submitting changes.

## Local Consistency

Existing files may not fully match this guide. When editing existing code:

- Prefer consistency with the surrounding file.
- Improve style opportunistically only where it supports the current change.
- Avoid unrelated large formatting rewrites.
- If a cleanup is needed, make it a separate change.
