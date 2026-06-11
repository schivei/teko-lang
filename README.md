# 🌀 Teko Programming Language

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
