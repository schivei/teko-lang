# Arandu Language Technical Specification

## 1. Overview
Arandu is a native programming language (AOT) designed for modular, scalable, and expressive software development. 
It integrates directory-based namespaces, traits, dependency injection, meta-code execution, and built-in testing. 
The language emphasizes clarity, compositional design, and productivity.

---

## 2. File Extensions
- `.aru` → Primary source code files.
- `.arm` → Meta-code files (compile-time bindings, code generation, validations).
- `.art` → Test files (unit, integration, and system tests).

### Example Project Layout
/src
   /logging
       ILogger.aru
       ConsoleLogger.aru
       TimestampLogger.aru
       Bindings.arm
       Logger.test
   /services
       IService.aru
       ServiceImpl.aru
       CachableTrait.aru
       Bindings.arm
       Service.test
   Program.aru

---

## 3. Namespace and Scope
- Each directory defines a **namespace** automatically.
- Imports are directory-based: `import logging`, `import services`.
- `.arm` and `.art` files inherit the namespace of their directory.
- No manual namespace declarations are required.

---

## 4. Language Constructs

### 4.1 Keywords
- `implements` → interface implementation.
- `extends` → class inheritance.
- `decorates` → native decorator pipeline.
- `trait` → horizontal composition of behavior.
- `with` → apply traits to classes/structs.
- `inject` → native dependency injection.

### 4.2 Visibility Modifiers
- `public`, `private`, `protected`, `internal`, `package`, `scoped`, `threaded`.

### 4.3 Dependency Injection
- Built-in DI container.
- Supports lifetimes: `singleton`, `scoped`, `threaded`.
- Example:
  inject singleton ILogger logger = new TimestampLogger(new ConsoleLogger());

### 4.4 Traits
- Traits provide reusable behavior.
- Applied with `with`.
- Example:
  trait CachableTrait { ... }
  class ServiceImpl with CachableTrait { ... }

---

## 5. Build System

### 5.1 Commands
- `arandu build ./src` → full build (code, meta, tests).
- Options:
  - `--no-test` → skip tests.
  - `--meta-only` → execute only meta-code.
  - `--test-only` → run only tests.
  - `--release` → optimized build for production.

### 5.2 Run Mode
- `arandu run ./src` → interpret IR without optimization.
- Options:
  - `--debug` → enable debugger (breakpoints, stepping, variable inspection).
  - `--no-meta` → skip meta-code execution.
  - `--no-test` → skip tests.
  - `--release-map` → generate binary symbol map for release debugging (disabled by default).

---

## 6. Compilation Pipeline
1. **Parsing** → parse `.aru` files into AST.
2. **Meta Execution** → execute `.arm` files at compile-time.
3. **Test Discovery** → compile `.art` files into a test binary.
4. **IR Generation** → produce intermediate representation (IR) with DI resolved.
5. **Backend** → translate IR to assembly/machine code.
6. **Linker** → link binary with external libraries.
7. **Test Runner** → execute test binaries, report PASS/FAIL.

---

## 7. Testing
- `.art` files define tests scoped to their directory.
- Tests are compiled into a separate binary.
- Runner outputs results grouped by namespace.
- Example:
  test ServiceProcess {
      IService s = new ServiceImpl();
      s.Process();
      assert s.LastResult == "OK";
  }

---

## 8. Meta-Code
- `.arm` files executed at compile-time.
- Used for bindings, validations, and code generation.
- Example:
  generateBindings("liblogging.so", symbols: [
      "init_logger()",
      "shutdown_logger()"
  ]);

---

## 9. Debugging
- Integrated with `arandu run --debug`.
- Supports:
  - Breakpoints.
  - Step execution.
  - Variable inspection.
- Optional release symbol map with `--release-map`.

---

## 10. LSP and IDE Integration
- Visual Studio Code → official Arandu LSP extension.
- Visual Studio → plugin with advanced debugger integration.
- JetBrains IDEs → generic LSP support.
- Vim/Neovim/Emacs → LSP client integration.

### LSP Features
- Syntax highlighting for `.aru`, `.arm`, `.art`.
- Autocomplete for imports and symbols.
- Real-time diagnostics.
- Refactoring tools (rename, extract traits).
- Debugging integration with run mode.
- Test runner feedback inside IDE.

---

## 11. Licensing
- Recommended: **Dual License (MIT + Apache 2.0)** for maximum adoption and patent protection.
- Alternative: **GPLv3** if community protection is prioritized.
- Libraries may use **LGPLv3** for compatibility with proprietary software.

---

## 12. Benefits
- Directory-based modularity.
- Clear separation of code, meta, and tests.
- Expressive language constructs.
- Built-in dependency injection.
- Flexible build and run modes.
- Strong IDE integration.
- Open-source licensing aligned with modern ecosystems.

---

## 13. Conclusion
Arandu combines clarity, modularity, and execution power. 
With traits, meta-code, dependency injection, and integrated testing, 
it provides a complete ecosystem for modern software development, 
balancing simplicity with technical depth and scalability.
