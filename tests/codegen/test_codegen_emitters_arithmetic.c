#include "unity.h"
#include "codegen/codegen_metal.h"
#include "teko_target.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// Conservative golden safety-net for the per-target emitters: drives the four
// core arithmetic opcodes (ADD/SUB/MUL/DIV) through every one of the 16 emitters
// and asserts the architecture-specific mnemonic for each. This is the safety net
// for the emitter de-duplication refactor (tech-debt #7): the refactor parameterizes
// exactly these per-ISA mnemonics, so any drift in arithmetic output is caught here.
void test_codegen_emitters_arithmetic_per_target(void) {
    typedef struct {
        TekoArch arch;
        TekoOS os;
        const char* name;
        const char* add;
        const char* sub;
        const char* mul;
        const char* div; // a stable core mnemonic (the div-by-zero guard labels carry a %d)
    } EmitterCase;

    // Expected emitted text (fprintf turns "%%" into a single "%").
    static const EmitterCase cases[] = {
        { ARCH_X86_64,        OS_LINUX,        "linux-x86_64",   "addl %ebx, %eax",   "subl %ebx, %eax",   "imull %ebx, %eax", "idivl %ebx" },
        { ARCH_X86,           OS_LINUX,        "linux-x86",      "addl %ebx, %eax",   "subl %ebx, %eax",   "imull %ebx, %eax", "idivl %ebx" },
        { ARCH_ARM64,         OS_LINUX,        "linux-arm64",    "add w0, w0, w1",    "sub w0, w0, w1",    "mul w0, w0, w1",   "sdiv w0, w0, w1" },
        { ARCH_ARM32,         OS_LINUX,        "linux-arm32",    "add r0, r0, r1",    "sub r0, r0, r1",    "mul r0, r0, r1",   "sdiv r0, r0, r1" },
        { ARCH_RISCV64,       OS_LINUX,        "linux-riscv64",  "add a0, a0, a1",    "sub a0, a0, a1",    "mul a0, a0, a1",   "div a0, a0, a1" },
        { ARCH_RISCV32,       OS_LINUX,        "linux-riscv32",  "add a0, a0, a1",    "sub a0, a0, a1",    "mul a0, a0, a1",   "div a0, a0, a1" },
        { ARCH_MIPS64,        OS_LINUX,        "linux-mips",     "addu $v0, $v0, $v1","subu $v0, $v0, $v1","mul $v0, $v0, $v1","div $v0, $v1" },
        { ARCH_PPC64,         OS_LINUX,        "linux-ppc64",    "add 3, 3, 4",       "subf 3, 4, 3",      "mulld 3, 3, 4",    "divd 3, 3, 4" },
        { ARCH_APPLE_SILICON, OS_MACOS_DARWIN, "darwin-arm64",   "add w0, w0, w1",    "sub w0, w0, w1",    "mul w0, w0, w1",   "sdiv w0, w0, w1" },
        { ARCH_X86_64,        OS_MACOS_DARWIN, "darwin-x86_64",  "addl %ebx, %eax",   "subl %ebx, %eax",   "imull %ebx, %eax", "idivl %ebx" },
        { ARCH_X86_64,        OS_UNKNOWN,      "freebsd-x86_64", "addl %ebx, %eax",   "subl %ebx, %eax",   "imull %ebx, %eax", "idivl %ebx" },
        { ARCH_ARM64,         OS_UNKNOWN,      "freebsd-arm64",  "add w0, w0, w1",    "sub w0, w0, w1",    "mul w0, w0, w1",   "sdiv w0, w0, w1" },
        { ARCH_X86_64,        OS_WINDOWS,      "win-x86_64",     "add eax, ebx",      "sub eax, ebx",      "imul eax, ebx",    "idiv ebx" },
        { ARCH_X86,           OS_WINDOWS,      "win-x86",        "add eax, ebx",      "sub eax, ebx",      "imul eax, ebx",    "idiv ebx" },
        { ARCH_ARM64,         OS_WINDOWS,      "win-arm64",      "add w0, w0, w1",    "sub w0, w0, w1",    "mul w0, w0, w1",   "sdiv w0, w0, w1" },
        { ARCH_WASM32,        OS_WASI,         "wasm",           "i32.add",           "i32.sub",           "i32.mul",          "i32.div_s" },
    };
    const int n = (int)(sizeof(cases) / sizeof(cases[0]));

    // OP_ADD, OP_SUB, OP_MUL, OP_DIV, OP_HALT. No ICONST pair, so constant folding
    // does not collapse the ops; they reach the emitter verbatim. The four ops are
    // distinct, so the orchestrator's CSE does not skip any of them.
    const unsigned char program[] = { 0x05, 0x06, 0x07, 0x08, 0x00 };
    const char* path = "golden_arith_emit.s";

    for (int i = 0; i < n; i++) {
        TekoTarget target = { .arch = cases[i].arch, .os = cases[i].os };
        MetalContext* ctx = teko_metal_create(path, target);
        TEST_ASSERT_NOT_NULL_MESSAGE(ctx, cases[i].name);

        teko_metal_emit_program(ctx, program, sizeof(program));
        teko_metal_close(ctx);

        FILE* f = fopen(path, "r");
        TEST_ASSERT_NOT_NULL_MESSAGE(f, cases[i].name);
        char* buf = (char*)malloc(16384);
        TEST_ASSERT_NOT_NULL_MESSAGE(buf, cases[i].name);
        size_t bytes = fread(buf, 1, 16383, f);
        buf[bytes] = '\0';
        fclose(f);
        remove(path);

        char msg[96];
        snprintf(msg, sizeof(msg), "%s ADD (%s)", cases[i].name, cases[i].add);
        TEST_ASSERT_NOT_NULL_MESSAGE(strstr(buf, cases[i].add), msg);
        snprintf(msg, sizeof(msg), "%s SUB (%s)", cases[i].name, cases[i].sub);
        TEST_ASSERT_NOT_NULL_MESSAGE(strstr(buf, cases[i].sub), msg);
        snprintf(msg, sizeof(msg), "%s MUL (%s)", cases[i].name, cases[i].mul);
        TEST_ASSERT_NOT_NULL_MESSAGE(strstr(buf, cases[i].mul), msg);
        snprintf(msg, sizeof(msg), "%s DIV (%s)", cases[i].name, cases[i].div);
        TEST_ASSERT_NOT_NULL_MESSAGE(strstr(buf, cases[i].div), msg);

        free(buf);
    }
}
