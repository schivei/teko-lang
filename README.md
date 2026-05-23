# Teko Programming Language

Teko is a native programming language (AOT) focused on clarity, modularity, and productivity. It integrates directory-based namespaces, traits, dependency injection, meta-code execution, built-in testing, and benchmarking.

## Features
- Directory-based namespaces for natural modularity
- Traits and decorators for expressive composition
- Native dependency injection with lifetimes (singleton, scoped, threaded)
- Meta-code execution via .meta.teko
- Integrated testing with .test.teko and .spec.teko
- Benchmarking support with .bench.teko
- Flexible build and run modes (teko build, teko run)
- LSP support for modern IDEs

## File Extensions
- .teko → source code
- .meta.teko → meta-code
- .test.teko → unit/integration tests
- .spec.teko → specification tests
- .bench.teko → benchmarks

## Example
trait CachableTrait {
    void Cache(string key, object value) { ... }
}

class ServiceImpl with CachableTrait {
    void Process() { ... }
}

test ServiceProcess {
    ServiceImpl s = new ServiceImpl();
    s.Process();
    assert s.LastResult == "OK";
}

bench CachePerformance {
    Cache c = new Cache();
    measure {
        for (int i = 0; i < 1000000; i++) {
            c.Store("key" + i, i);
        }
    }
}

## Build & Run
- teko build ./src → compile project
- teko run ./src → run in interpreted mode
- Options: --debug, --release, --test-only, --bench-only

## License
Dual License: MIT + Apache 2.0
