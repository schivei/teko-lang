#include "tld_elf_arch.h"
#include <string.h>

uint32_t tld_arch_encode_instruction(TekoArch arch, OpCode op, int32_t arg, uint8_t* out_buffer) {
    if (!out_buffer) return 0;

    uint32_t bytes_written = 0;

    // ====================================================================
    // 1. INTEL / AMD x86_64
    // ====================================================================
    if (arch == ARCH_X86_64) {
        switch (op) {
            case OP_ICONST:
                out_buffer[0] = 0xB8; // movl $imm, %eax
                out_buffer[1] = (arg >> 0)  & 0xFF;
                out_buffer[2] = (arg >> 8)  & 0xFF;
                out_buffer[3] = (arg >> 16) & 0xFF;
                out_buffer[4] = (arg >> 24) & 0xFF;
                bytes_written = 5;
                break;
            case OP_STORE: out_buffer[0] = 0x89; out_buffer[1] = 0xC3; bytes_written = 2; break; // movl %eax, %ebx
            case OP_LOAD:  out_buffer[0] = 0x89; out_buffer[1] = 0xD8; bytes_written = 2; break; // movl %ebx, %eax
            case OP_ADD:   out_buffer[0] = 0x01; out_buffer[1] = 0xD8; bytes_written = 2; break; // addl %ebx, %eax
            case OP_SUB:   out_buffer[0] = 0x29; out_buffer[1] = 0xD8; bytes_written = 2; break; // subl %ebx, %eax
            case OP_MUL:   out_buffer[0] = 0x0F; out_buffer[1] = 0xAF; out_buffer[2] = 0xC3; bytes_written = 3; break; // imull %ebx, %eax
            case OP_HALT: {
                uint8_t halt_x64[] = { 0x48, 0xC7, 0xC0, 0x3C, 0x00, 0x00, 0x00, 0x48, 0x31, 0xFF, 0x0F, 0x05 }; // movq $60, %rax; xorq %rdi, %rdi; syscall
                memcpy(out_buffer, halt_x64, sizeof(halt_x64)); bytes_written = sizeof(halt_x64); break;
            }
            default: out_buffer[0] = 0x90; bytes_written = 1; break; // NOP
        }
    }
    // ====================================================================
    // 2. INTEL x86_32
    // ====================================================================
    else if (arch == ARCH_X86) {
        switch (op) {
            case OP_ICONST:
                out_buffer[0] = 0xB8; // movl $imm, %eax
                out_buffer[1] = (arg >> 0)  & 0xFF;
                out_buffer[2] = (arg >> 8)  & 0xFF;
                out_buffer[3] = (arg >> 16) & 0xFF;
                out_buffer[4] = (arg >> 24) & 0xFF;
                bytes_written = 5;
                break;
            case OP_STORE: out_buffer[0] = 0x89; out_buffer[1] = 0xC3; bytes_written = 2; break; // movl %eax, %ebx
            case OP_LOAD:  out_buffer[0] = 0x89; out_buffer[1] = 0xD8; bytes_written = 2; break; // movl %ebx, %eax
            case OP_ADD:   out_buffer[0] = 0x01; out_buffer[1] = 0xD8; bytes_written = 2; break; // addl %ebx, %eax
            case OP_HALT: {
                uint8_t halt_x86[] = { 0xB8, 0x01, 0x00, 0x00, 0x00, 0xBB, 0x00, 0x00, 0x00, 0x00, 0xCD, 0x80 }; // movl $1, %eax; movl $0, %ebx; int $0x80
                memcpy(out_buffer, halt_x86, sizeof(halt_x86)); bytes_written = sizeof(halt_x86); break;
            }
            default: out_buffer[0] = 0x90; bytes_written = 1; break;
        }
    }
    // ====================================================================
    // 3. ARM64 / AArch64
    // ====================================================================
    else if (arch == ARCH_ARM64 || arch == ARCH_APPLE_SILICON) {
        switch (op) {
            case OP_ICONST: { uint32_t instr = 0x52800000 | ((arg & 0xFFFF) << 5); memcpy(out_buffer, &instr, 4); bytes_written = 4; break; } // mov w0, #imm
            case OP_STORE:  { uint32_t instr = 0x2A0003E1; memcpy(out_buffer, &instr, 4); bytes_written = 4; break; } // orr w1, wzr, w0
            case OP_LOAD:   { uint32_t instr = 0x2A0103E0; memcpy(out_buffer, &instr, 4); bytes_written = 4; break; } // orr w0, wzr, w1
            case OP_ADD:    { uint32_t instr = 0x0B010000; memcpy(out_buffer, &instr, 4); bytes_written = 4; break; } // add w0, w0, w1
            case OP_HALT: {
                uint8_t halt_arm64[] = { 0x00, 0x00, 0x80, 0x52, 0xA8, 0x0B, 0x80, 0xD2, 0x01, 0x00, 0x00, 0xD4 }; // mov w0, #0; mov x8, #93; svc #0
                memcpy(out_buffer, halt_arm64, sizeof(halt_arm64)); bytes_written = sizeof(halt_arm64); break;
            }
            default: { uint32_t instr = 0xD503201F; memcpy(out_buffer, &instr, 4); bytes_written = 4; break; } // NOP
        }
    }
    // ====================================================================
    // 4. ARM32 (ARMv7 / armhf)
    // ====================================================================
    else if (arch == ARCH_ARM32) {
        switch (op) {
            case OP_ICONST: { uint32_t instr = 0xE3A00000 | (arg & 0xFF); memcpy(out_buffer, &instr, 4); bytes_written = 4; break; } // mov r0, #imm
            case OP_STORE:  { uint32_t instr = 0xE1A01000; memcpy(out_buffer, &instr, 4); bytes_written = 4; break; } // mov r1, r0
            case OP_LOAD:   { uint32_t instr = 0xE1A00001; memcpy(out_buffer, &instr, 4); bytes_written = 4; break; } // mov r0, r1
            case OP_ADD:    { uint32_t instr = 0xE0800001; memcpy(out_buffer, &instr, 4); bytes_written = 4; break; } // add r0, r0, r1
            case OP_HALT: {
                uint8_t halt_arm32[] = { 0x00, 0x00, 0xA0, 0xE3, 0x01, 0x70, 0xA0, 0xE3, 0x00, 0x00, 0x00, 0xEF }; // mov r0, #0; mov r7, #1; svc 0
                memcpy(out_buffer, halt_arm32, sizeof(halt_arm32)); bytes_written = sizeof(halt_arm32); break;
            }
            default: { uint32_t instr = 0xE1A00000; memcpy(out_buffer, &instr, 4); bytes_written = 4; break; } // NOP (mov r0, r0)
        }
    }
    // ====================================================================
    // 5. RISC-V 64-bit
    // ====================================================================
    else if (arch == ARCH_RISCV64) {
        switch (op) {
            case OP_ICONST: { uint32_t instr = 0x00000513 | ((arg & 0xFFF) << 20); memcpy(out_buffer, &instr, 4); bytes_written = 4; break; } // li a0, imm
            case OP_STORE:  { uint32_t instr = 0x00050593; memcpy(out_buffer, &instr, 4); bytes_written = 4; break; } // mv a1, a0 (addi a1, a0, 0)
            case OP_LOAD:   { uint32_t instr = 0x00058513; memcpy(out_buffer, &instr, 4); bytes_written = 4; break; } // mv a0, a1 (addi a0, a1, 0)
            case OP_ADD:    { uint32_t instr = 0x00B50533; memcpy(out_buffer, &instr, 4); bytes_written = 4; break; } // add a0, a0, a1
            case OP_HALT: {
                uint8_t halt_rv64[] = { 0x13, 0x05, 0x00, 0x00, 0x93, 0x08, 0xD0, 0x05, 0x73, 0x00, 0x00, 0x00 }; // li a0, 0; li a7, 93; ecall
                memcpy(out_buffer, halt_rv64, sizeof(halt_rv64)); bytes_written = sizeof(halt_rv64); break;
            }
            default: { uint32_t instr = 0x00000013; memcpy(out_buffer, &instr, 4); bytes_written = 4; break; } // NOP
        }
    }
    // ====================================================================
    // 6. RISC-V 32-bit
    // ====================================================================
    else if (arch == ARCH_RISCV32) {
        switch (op) {
            case OP_ICONST: { uint32_t instr = 0x00000513 | ((arg & 0xFFF) << 20); memcpy(out_buffer, &instr, 4); bytes_written = 4; break; }
            case OP_STORE:  { uint32_t instr = 0x00050593; memcpy(out_buffer, &instr, 4); bytes_written = 4; break; }
            case OP_ADD:    { uint32_t instr = 0x00B50533; memcpy(out_buffer, &instr, 4); bytes_written = 4; break; }
            case OP_HALT: {
                uint8_t halt_rv32[] = { 0x13, 0x05, 0x00, 0x00, 0x93, 0x08, 0xD0, 0x05, 0x73, 0x00, 0x00, 0x00 }; // li a0, 0; li a7, 93; ecall
                memcpy(out_buffer, halt_rv32, sizeof(halt_rv32)); bytes_written = sizeof(halt_rv32); break;
            }
            default: { uint32_t instr = 0x00000013; memcpy(out_buffer, &instr, 4); bytes_written = 4; break; }
        }
    }
    // ====================================================================
    // 7. MIPS (32/64-bit Big/Little Endian O32/N64)
    // ====================================================================
    else if (arch == ARCH_MIPS32 || arch == ARCH_MIPS64) {
        switch (op) {
            case OP_ICONST: { uint32_t instr = 0x24020000 | (arg & 0xFFFF); memcpy(out_buffer, &instr, 4); bytes_written = 4; break; } // li $v0, imm (addiu $v0, $zero, imm) [INDEX]
            case OP_STORE:  { uint32_t instr = 0x00401825; memcpy(out_buffer, &instr, 4); bytes_written = 4; break; } // move $v1, $v0 (or $v1, $v0, $zero)
            case OP_ADD:    { uint32_t instr = 0x00431021; memcpy(out_buffer, &instr, 4); bytes_written = 4; break; } // addu $v0, $v0, $v1
            case OP_HALT: {
                uint8_t halt_mips[] = { 0x00, 0x00, 0x04, 0x24, 0x01, 0x0F, 0x02, 0x24, 0x0C, 0x00, 0x00, 0x00 }; // li $a0, 0; li $v0, 4001; syscall
                memcpy(out_buffer, halt_mips, sizeof(halt_mips)); bytes_written = sizeof(halt_mips); break;
            }
            default: { uint32_t instr = 0x00000000; memcpy(out_buffer, &instr, 4); bytes_written = 4; break; } // NOP
        }
    }
    // ====================================================================
    // 8. PowerPC 64-bit (PPC64 / PPC64LE)
    // ====================================================================
    else if (arch == ARCH_PPC64) {
        switch (op) {
            case OP_ICONST: { uint32_t instr = 0x38600000 | (arg & 0xFFFF); memcpy(out_buffer, &instr, 4); bytes_written = 4; break; } // li r3, imm (addi r3, 0, imm)
            case OP_STORE:  { uint32_t instr = 0x7C641B78; memcpy(out_buffer, &instr, 4); bytes_written = 4; break; } // mr r4, r3 (or r4, r3, r3)
            case OP_ADD:    { uint32_t instr = 0x7C632214; memcpy(out_buffer, &instr, 4); bytes_written = 4; break; } // add r3, r3, r4
            case OP_HALT: {
                uint8_t halt_ppc[] = { 0x01, 0x00, 0x00, 0x38, 0x00, 0x00, 0x60, 0x38, 0x02, 0x00, 0x00, 0x44 }; // li r0, 1; li r3, 0; sc
                memcpy(out_buffer, halt_ppc, sizeof(halt_ppc));
                bytes_written = sizeof(halt_ppc);
                break;
            }
            default: {
                uint32_t instr = 0x60000000;
                memcpy(out_buffer, &instr, 4);
                bytes_written = 4;
                break;
            } // NOP (ori r0, r0, 0)
        }
    }
    return bytes_written;
}