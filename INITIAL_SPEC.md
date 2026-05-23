# Arandu Language Specification Document

## Introduction
Arandu is a native programming language (AOT) inspired by modularity, dependency injection, traits, and meta-code. 
The name comes from Guarani and means "wisdom," reflecting the philosophy of the language: simplicity, clarity, and power.

---

## Philosophy
- Native: ahead-of-time compilation, no extra runtime.
- Modular: scope defined by directories, similar to Go.
- Expressive: explicit keywords for inheritance, implementation, and decorator.
- Compositional: native support for traits.
- Segregated: meta-code and tests separated into their own extensions.
- Productive: built-in dependency injection, no external frameworks required.

---

## File Extensions
- `.aru` → main source code.
- `.arm` → meta-code (bindings, validations, code generation).
- `.art` → tests (unit and integration).

### Example Project Structure
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

## Directory Scope
- Each folder automatically defines a namespace.
- `.arm` and `.art` files inherit the scope of their directory.
- Imports are simple: `import logging`, `import services`.

---

## Core Keywords
- `implements` → interface implementation.
- `extends` → class inheritance.
- `decorates` → native pipeline for the Decorator pattern.
- `trait` → horizontal composition of behavior.
- `with` → apply traits to classes/structs.
- `inject` → native dependency injection.
- Visibility modifiers: `public`, `private`, `protected`, `internal`, `package`, `scoped`, `threaded`.

---

## Build and Execution

### Main Build Command
arandu build ./src

### Options
- `--no-test` → build without running tests.
- `--meta-only` → execute only meta-code.
- `--test-only` → execute only tests.
- `--release` → optimized build for production.

### Run Mode
arandu run ./src
- Executes without optimization, like an interpreter.
- Ideal for debugging and hot reload.
- `--debug` enables breakpoints and variable inspection.
- `--release-map` generates a binary symbol map for release debugging (disabled by default).

---

## Compilation Pipeline
1. Parsing → reads `.aru` files.
2. Meta Execution → applies `.arm` files.
3. Test Discovery → compiles `.art` files.
4. IR Generation → produces IR with DI resolved.
5. Backend → translates IR to assembly/machine code.
6. Linker → generates final binary.
7. Test Runner → executes tests.

---

## LSP and IDE Support
- Visual Studio Code → official Arandu LSP extension.
- Visual Studio → plugin with advanced debugger support.
- JetBrains IDEs → generic LSP integration.
- Neovim/Vim/Emacs → LSP client integration.

### LSP Features
- Syntax highlighting for `.aru`, `.arm`, `.art`.
- Autocomplete for imports and symbols.
- Real-time diagnostics.
- Refactoring (rename, extract traits).
- Debugging integrated with `run --debug`.
- Test runner with visual feedback.

---

## Benefits
- Natural organization → directory-based scope.
- Clear separation → meta and tests are segregated.
- Expressive → explicit keywords for inheritance, traits, and decorator.
- Productive → built-in dependency injection.
- Flexible → optimized build or interpreted run mode.
- Integrated → modern IDE support via LSP.

---

## Conclusion
Arandu is a language that unites clarity, modularity, and power. 
With native support for dependency injection, traits, meta-code, and integrated testing, 
it offers a complete ecosystem for modern development, combining ease of use with execution robustness.
