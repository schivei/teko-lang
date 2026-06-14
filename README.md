# 🌀 Teko Programming Language

[![Teko Compiler Multi-Platform CI](https://github.com/schivei/teko-lang/actions/workflows/ci.yml/badge.svg)](https://github.com/schivei/teko-lang/actions/workflows/ci.yml)
[![Teko Compiler Multi-Platform CI (main)](https://github.com/schivei/teko-lang/actions/workflows/ci.yml/badge.svg?branch=main)](https://github.com/schivei/teko-lang/actions/workflows/ci.yml?query=branch%3Amain)

![teko.jpeg](docs/teko.jpeg)

Teko is a high-performance, ahead-of-time (AOT) compiled, general-purpose programming language tailored for modern systems programming. It combines the raw execution speed of C, the type safety and clean architecture of modern compilers, and a zero-overhead **Region-Based Memory Management (O(1) Arenas)** to eliminate both manual `free()` errors and garbage collection pauses.

---

## ✨ Key Features

*   **⚡ Bare-Metal Execution:** Fully independent compiler infrastructure that skips heavy third-party backends (like LLVM) to emit optimized machine code directly to target architectures.
*   **🧠 Zero-Cost Region Memory:** Automatic compile-time Escape Analysis triggers stack allocation or promotes data to deterministic O(1) stack-allocated sub-arenas. Megabytes of local memory are recycled instantly in a single clock cycle.
*   **🧵 Native M:N Concurrency:** Lightweight cooperative green threads (`using` blocks and channels) baked directly into the instruction set and routed dynamically to native OS architectures.
*   **🗺️ Clean Platform Isolation:** Highly decoupled backend structure strictly segregating architecture and system conventions (Apple Silicon, macOS Intel x86_64, Linux ELF, Windows PE/COFF, FreeBSD, and WebAssembly).
*   **🛠️ Multi-Mode Tooling:** Robust CLI compiler driver providing precise compilation interrupts via standard industry flags (`-S` for pure text assembly, `-c` for unlinked binary object files).

---

## 🎯 Supported Targets

Teko's backend is a polymorphic router (`src/codegen/codegen_metal.c`) that dispatches
each `(OS, architecture)` pair to a dedicated emitter. There are **16 emitters** — 15
native families plus WebAssembly. Targets are selected by a triple passed to
`--target` (matched by substring in `src/teko_target.c`).

**Honest status legend**

- **Emission (CI)** — the emitter produces target assembly/IR text and its output is
  pinned by golden tests in the suite. That suite runs on every CI host, so the
  *emission* of every target below is exercised in CI. The native assembly is **not**
  assembled, linked, or executed as a process — only its text/bytes are asserted (the
  in-memory ELF/Mach-O/PE writers are likewise checked by assertions, not run).
- **Executed (CI)** — the emitted module is actually built and **run** in CI, with its
  result asserted. Today this applies only to WebAssembly.

| OS | Architecture | Emitter (`src/codegen/…`) | Object / Backend | Example triple | Status |
|----|--------------|---------------------------|------------------|----------------|--------|
| macOS (Darwin) | arm64 / Apple Silicon | `apple/emit_darwin_arm64.c` | Mach-O | `aarch64-apple-darwin` | Emission (CI) — not executed |
| macOS (Darwin) | x86_64 (Intel) | `apple/emit_darwin_x86_64.c` | Mach-O | `x86_64-apple-darwin` | Emission (CI) — not executed |
| Linux | x86_64 | `linux/emit_linux_x86_64.c` | ELF | `x86_64-unknown-linux-gnu` | Emission (CI) — not executed |
| Linux | x86 / i386 | `linux/emit_linux_x86.c` | ELF | `i386-linux` | Emission (CI) — not executed |
| Linux | arm64 / aarch64 | `linux/emit_linux_arm64.c` | ELF | `aarch64-linux-gnu` | Emission (CI) — not executed |
| Linux | arm32 / armv7 | `linux/emit_linux_arm32.c` | ELF | `arm-linux-gnueabihf` | Emission (CI) — not executed |
| Linux | riscv64 | `linux/emit_linux_riscv64.c` | ELF | `riscv64-unknown-linux-gnu` | Emission (CI) — not executed |
| Linux | riscv32 | `linux/emit_linux_riscv32.c` | ELF | `riscv32-linux` | Emission (CI) — not executed |
| Linux | mips | `linux/emit_linux_mips.c` | ELF | `mips-linux` | Emission (CI) — not executed |
| Linux | ppc64 / powerpc64 | `linux/emit_linux_ppc64.c` | ELF | `powerpc64-linux` | Emission (CI) — not executed |
| Windows | x86_64 | `windows/emit_win_x86_64.c` | PE/COFF | `x86_64-pc-windows-msvc` | Emission (CI) — not executed |
| Windows | x86 / i386 | `windows/emit_win_x86.c` | PE/COFF | `i386-windows` | Emission (CI) — not executed |
| Windows | arm64 | `windows/emit_win_arm64.c` | PE/COFF | `aarch64-pc-windows-msvc` | Emission (CI) — not executed |
| FreeBSD | x86_64 | `bsd_unix/emit_freebsd_x86_64.c` | ELF | `x86_64-unknown-freebsd` | Emission (CI) — not executed |
| FreeBSD | arm64 | `bsd_unix/emit_freebsd_arm64.c` | ELF | `aarch64-freebsd` | Emission (CI) — not executed |
| WebAssembly | wasm32 / wasm64 | `bare_metal/emit_wasm.c` | WASM (WAT) | `wasm32-wasi` | **Executed (CI)** — see below |

### WebAssembly targets (the only family executed end-to-end in CI)

- **Layer A — cooperative (default `wasm`/`wasm32-wasi`).** Single WebAssembly thread
  reproducing the M:N green-thread model entirely *inside* the module: an in-memory run
  queue + function table, `call_indirect` dispatch, linear-memory channels, and
  mid-function suspension via a state machine (`OP_FUNC_BEGIN`/`END`, `OP_CHAN_GET`).
  No host runtime required. Executed in CI under **Node + wasmtime + headless Chromium**
  (`channels=42`, `scheduler=15`, real backend output `emitted=7`, suspension `=30`,
  multi-spawn contention `=15`).
- **Layer B — real multicore (`--target=…-wasm-threads`).** Opt-in parallelism: imports
  a `shared` memory, lowers channels to the **atomics** proposal, and delegates `spawn`
  to a host `teko_rt.spawn` that starts a real OS thread (Web Worker / `worker_threads`)
  which re-instantiates the module against the shared memory. Executed in CI under
  **node `worker_threads`** and **headless Chromium Web Workers** with COOP/COEP
  (`threads=777`, real backend output `emitted_threads=99`). Layered on top of A, not a
  replacement (Workers are 1:1 OS threads, not M:N).

### What CI actually exercises

- **Compiler built & run (host) — gating:** Linux x86_64, Linux arm64, macOS arm64,
  Windows x86_64, Windows arm64. Plus **Linux riscv64 under QEMU** (non-blocking). Every
  emitter's emission golden test runs on these hosts; the bare suite is also run **20×**
  on Windows x86_64 as a stress gate.
- **WASM executed — gating:** `wasm-wasmtime` (Node + wasmtime), `wasm-browser`
  (Chromium, COOP/COEP), `wasm-threads-node` (`worker_threads`), `wasm-threads-browser`
  (Web Workers).
- **Sanitizers — gating:** Release + ASan/UBSan on both VM dispatch paths
  (computed-goto and portable `switch`), plus a **ThreadSanitizer** regression guard.
- **Not in CI today:** macOS x86_64 (Intel runners retired), 32-bit (x86 / arm32 /
  riscv32), mips, ppc64, and FreeBSD have **no host runner**, so their emitters are
  validated by emission goldens only — not by building/executing the compiler on that
  platform, nor by running the emitted native code anywhere.

## 📂 Project Directory Structure

```text
teko/
├── src/
│   ├── main.c                          # CLI compiler driver & option parsing
│   ├── codegen/                        # Native Bare-Metal Backend Core
│   │   ├── codegen_metal.h             # Orchestrator central definitions
│   │   ├── codegen_metal.c             # Polymorphic OS/CPU instruction router
│   │   ├── codegen_opt.h / .c          # Escape Analysis & optimization pipeline
│   │   ├── apple/                      # macOS Darwin Ecosystem (Mach-O)
│   │   │   ├── emit_darwin_arm64.c     # Apple Silicon M-Series
│   │   │   └── emit_darwin_x86_64.c    # Intel Mac legacy support
│   │   ├── linux/                      # Linux Ecosystem (ELF ABI)
│   │   │   ├── emit_linux_x86_64.c     # 64-bit Intel/AMD Linux
│   │   │   └── (other Linux architectures...)
│   │   ├── windows/                    # Windows Ecosystem (PE/COFF)
│   │   └── bare_metal/                 # Virtual and Embedded Targets (WASM WAT)
│   └── (frontend/lexer/parser...)
└── tests/
    ├── test_main.c                     # Unity framework automated executor
    └── codegen/                        # Isolated backend sanity smoke tests
        ├── test_codegen_apple.c        # Apple Silicon and Intel validations
        ├── test_codegen_linux.c        # Multi-architecture ELF validations
        ├── test_codegen_windows.c      # Win32 & Win64 structural assertions
        └── test_codegen_unix.c         # FreeBSD system trap validations
```

---

## 🛠️ Building and Testing

### Prerequisites
Make sure you have a standard C compiler (GCC, Clang, or MSVC) and CMake installed on your machine.

### Compilation
To build the CLI driver compiler and the test engine, use the following native instructions:

```bash
mkdir build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Debug
make -j\$(nproc)
```

### Running Automated Test Suite
To run all cross-platform compiler smoke tests via the Unity framework:

```bash
./teko_tests
```

### Compiling a Teko Project
You can build your native entry-point manifests using the CLI driver options:

```bash
# Generate pure text assembly for audit (.s / .asm)
./teko build path/to/project.tkp -S --target=aarch64-apple-darwin

# Generate object binary files without final linking (.o / .obj)
./teko build path/to/project.tkp -c --target=x86_64-unknown-linux-gnu

# Generate a clean production bare-metal native executable
./teko build path/to/project.tkp -o my_app
```

---

## 📜 License
Teko is released under the MIT License. Feel free to use, modify, and distribute.
