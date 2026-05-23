# Teko Language Technical Specification

## 1. Overview
Teko is a native programming language (AOT) designed for modular, scalable, and expressive software development. It integrates directory-based namespaces, traits, dependency injection, meta-code execution, built-in testing, and benchmarking. The language emphasizes clarity, compositional design, and productivity.

## 2. File Extensions
- .teko → Primary source code files.
- .meta.teko → Meta-code files (compile-time bindings, code generation, validations).
- .test.teko → Test files (unit and integration tests).
- .spec.teko → Specification tests (behavior-driven development).
- .bench.teko → Benchmark files (performance and stress testing).

### Example Project Layout
/src
   /logging
       ILogger.teko
       ConsoleLogger.teko
       TimestampLogger.teko
       Bindings.meta.teko
       Logger.test.teko
   /services
       IService.teko
       ServiceImpl.teko
       CachableTrait.teko
       Bindings.meta.teko
       Service.spec.teko
   /performance
       CacheBench.bench.teko
       ServiceBench.bench.teko
   Program.teko

## 3. Namespace and Scope
Each directory defines a namespace automatically. Imports are directory-based: import logging, import services. .meta.teko, .test.teko, .spec.teko, and .bench.teko files inherit the namespace of their directory. No manual namespace declarations are required.

## 4. Language Constructs
Keywords: implements (interface implementation), extends (class inheritance), decorates (native decorator pipeline), trait (horizontal composition of behavior), with (apply traits to classes/structs), inject (native dependency injection). Visibility modifiers: public, private, protected, internal, package, scoped, threaded.

### Dependency Injection
Built-in DI container with lifetimes: singleton, scoped, threaded.
Example:
inject singleton ILogger logger = new TimestampLogger(new ConsoleLogger());

### Traits
Traits provide reusable behavior, applied with with.
Example:
trait CachableTrait {
    void Cache(string key, object value) { ... }
}
class ServiceImpl with CachableTrait {
    void Process() { ... }
}

## 5. Build System
teko build ./src → full build (code, meta, tests, benchmarks).
Options: --no-test (skip tests), --meta-only (execute only meta-code), --test-only (run only tests), --bench-only (run only benchmarks), --release (optimized build for production).

teko run ./src → interpret IR without optimization.
Options: --debug (enable debugger), --no-meta (skip meta-code), --no-test (skip tests), --no-bench (skip benchmarks), --release-map (generate binary symbol map for release debugging).

## 6. Compilation Pipeline
1. Parsing → parse .teko files into AST.
2. Meta Execution → execute .meta.teko files at compile-time.
3. Test Discovery → compile .test.teko and .spec.teko files into test binaries.
4. Benchmark Discovery → compile .bench.teko files into benchmark binaries.
5. IR Generation → produce intermediate representation with DI resolved.
6. Backend → translate IR to assembly/machine code.
7. Linker → link binary with external libraries.
8. Test Runner → execute test binaries, report PASS/FAIL.
9. Benchmark Runner → execute benchmarks, report performance metrics.

## 7. Testing
.test.teko → unit and integration tests.
.spec.teko → specification tests for behavior-driven development.
Example:
test ServiceProcess {
    IService s = new ServiceImpl();
    s.Process();
    assert s.LastResult == "OK";
}

## 8. Benchmarking
.bench.teko → performance and stress tests.
Benchmarks measure execution time, memory usage, and throughput.
Example:
bench CachePerformance {
    Cache c = new Cache();
    measure {
        for (int i = 0; i < 1000000; i++) {
            c.Store("key" + i, i);
        }
    }
}

## 9. Meta-Code
.meta.teko files executed at compile-time, used for bindings, validations, and code generation.
Example:
generateBindings("liblogging.so", symbols: [
    "init_logger()",
    "shutdown_logger()"
]);

## 10. Debugging
Integrated with teko run --debug. Supports breakpoints, step execution, variable inspection. Optional release symbol map with --release-map.

## 11. LSP and IDE Integration
Visual Studio Code → official Teko LSP extension.
Visual Studio → plugin with advanced debugger integration.
JetBrains IDEs → generic LSP support.
Vim/Neovim/Emacs → LSP client integration.

Features: syntax highlighting for all extensions, autocomplete, real-time diagnostics, refactoring tools, debugging integration, test and benchmark runner feedback inside IDE.

## 12. Licensing
Recommended: Dual License (MIT + Apache 2.0) for maximum adoption and patent protection. Alternative: GPLv3 if community protection is prioritized. Libraries may use LGPLv3 for compatibility with proprietary software.

## 13. Benefits
Directory-based modularity. Clear separation of code, meta, tests, and benchmarks. Expressive language constructs. Built-in dependency injection. Flexible build and run modes. Strong IDE integration. Open-source licensing aligned with modern ecosystems.

## 14. Conclusion
Teko combines clarity, modularity, and execution power. With traits, meta-code, dependency injection, integrated testing, and benchmarking, it provides a complete ecosystem for modern software development, balancing simplicity with technical depth and scalability.
