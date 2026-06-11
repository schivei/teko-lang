# 🗺️ Plano de Engenharia: Compilador Leko AOT Bare-Metal (Fase 5)

Este documento especifica a arquitetura definitiva do backend **Ahead-of-Time (AOT)** da linguagem **Leko**. O objetivo é eliminar completamente a dependência de uma Máquina Virtual (VM) no modo de produção (*Release*), transpilando a Árvore Sintática Abstrata (AST) e a Linguagem Intermediária (LI) diretamente em código de máquina nativo (*bare-metal*) específico para cada combinação de **Sistema Operacional e Arquitetura de Processador**.

---

## 1. Matriz de Destinos Nativos (Target Triples) e ABIs

Para suportar a universalidade da linguagem sem depender de ecossistemas pesados de terceiros, o backend em C puro segmenta o gerenciamento de chamadas e convenções de registradores (*Calling Conventions*) conforme a tabela abaixo:

| Sistema Operacional (OS) | Família de Executável | Convenção de Chamada / ABI | Saída de Sistema (Syscall) |
| :--- | :--- | :--- | :--- |
| **macOS (Darwin)** | Mach-O (`.s` -> `.o`) | ARM64 Standard Apple ABI | `svc #0x80` / `sys_exit` (1) |
| **Linux (64-bit)** | ELF (`.s` -> `.o`) | System V AMD64 ABI | `syscall` / `sys_exit` (60) |
| **Linux (32-bit)** | ELF (`.s` -> `.o`) | System V i386 ABI | `int $0x80` / `sys_exit` (1) |
| **Windows (64-bit)** | PE/COFF (`.s` -> `.obj`) | Microsoft x64 Calling Convention | `ExitProcess` via `KERNEL32` |
| **FreeBSD (Unix)** | ELF (`.s` -> `.o`) | System V AMD64 (FreeBSD syscall) | `int $0x80` / `sys_exit` (1) |
| **Bare-Metal (WASM)** | WebAssembly Web Text | Expressões S-Expression Monádicas | Instrução `unreachable` |
| **Bare-Metal (AVR)** | Flat Binary (Arduino) | Atmel AVR 8-bit ABI | Loop infinito `rjmp .` |

---

## 2. Requisitos Físicos por Opcode da ISA Leko

Cada arquivo de arquitetura segregado nas subpastas (`apple/`, `linux/`, `windows/`, `bsd_unix/`, `bare_metal/`) deve implementar um `switch` contíguo traduzindo os opcodes abstratos da LI para instruções reais de registradores de hardware:

### A) Carga de Texto e Alocação de Strings (`OP_SCONST`)
Como strings dinâmicas ou literais não cabem diretamente dentro de um único registrador de CPU, o compilador adota estratégias de ponteiros de memória relativos:
*   **macOS (Darwin ARM64):** Injeta o operador de página `adrp x0, .L_str_idx@PAGE` combinado com a carga de deslocamento `add x0, x0, .L_str_idx@PAGEOFF`. Armazena os bytes na seção `.section __TEXT,__cstring,cstring_literals` via diretiva `.asciz`.
*   **Linux / FreeBSD (x86_64):** Utiliza endereçamento relativo ao ponteiro de instrução (*RIP-relative*) através de `leaq .L_str_idx(%rip), %rax`. Armazena os bytes na seção `.section .rodata` via diretiva `.asciz`.
*   **Windows (x86_64):** Adota a sintaxe Intel com `lea rax, [rip + .L_str_idx]`. Armazena os bytes na seção `.section .rdata,"dr"` utilizando a diretiva de definição de bytes `db "texto", 0`.
*   **WebAssembly (WASM):** Mapeia todos os literais contiguamente no segmento de dados do módulo virtual: `(data (i32.const offset) "texto\00")`.
*   **AVR (Microcontroladores 8-bit):** Injeta o modificador `.section .progmem.data` para salvar o texto na memória Flash do chip, impedindo que strings estáticas devorem a escassa memória RAM (SRAM) do hardware. A carga quebra o ponteiro de 16-bits em dois registradores: `ldi r24, lo8(.L_str_idx)` e `ldi r25, hi8(.L_str_idx)`.

### B) Proteções Aritméticas no Silício (`OP_ADD`, `OP_DIV`)
*   **Processadores RISC (ARM64, RISC-V, MIPS):** Executam aritmética de três operandos (`add w0, w0, w1`). O `OP_DIV` exige uma barreira de hardware prévia através de saltos se o divisor for zero (`cbz` no ARM, `beqz` no RISC-V) para evitar o travamento (*crash*) do processador por exceção de ponto flutuante.
*   **Processadores CISC (x86_64, x86_32):** Executam aritmética de dois operandos com acúmulo no primeiro registrador (`addl %ebx, %eax`). O `OP_DIV` exige estender o sinal do acumulador antes da instrução física (`cltd` em 64-bit, `cdq` em 32-bit), e o desvio de segurança contra divisão por zero utiliza `cmpl $0, %ebx` combinado com o salto condicional `je`.

### C) O Alocador de Arenas Físico Nativo (`OP_ARENA_PUSH`, `OP_ARENA_POP`)
Para dar suporte ao gerenciamento de memória automático de custo zero (*Region-Based Memory Management*) sem o peso de um Garbage Collector ou checagens dinâmicas de Heap em produção:
*   O compilador reserva um registrador imutável e preservável entre funções (*callee-saved register*) para atuar como o **Cursor da Arena Ativa** (`x19` no ARM64, `%r12` no x86_64, `x9` no RISC-V, `r28/r29` no AVR).
*   **`OP_ARENA_PUSH` (Início de Bloco `using`):** Aloca um frame contíguo fixo de memória (ex: 1024 bytes) decrementando diretamente o ponteiro de pilha física da CPU (`sp` / `%rsp`) e travando o registrador da Arena neste endereço.
*   **`OP_ARENA_POP` (Descarte de Escopo):** Executa a liberação de toda a memória da região em tempo constante **\(O(1)\)**, recuando o ponteiro de pilha em lote através de uma única instrução de adição aritmética (`add sp, sp, #1024` ou `addq $1024, %rsp`). Gigabytes de dados locais são limpos em um único ciclo de clock.

---

## 3. Fluxo de Orquestração do Emissor Central (`codegen_metal.c`)

Para coordenar a geração multimotores sem gerar conflitos de tipos ou alertas lógicos nas IDEs, o arquivo principal de roteamento utiliza marcas sentinelas (`0xFE` para inicializar o Prólogo físico do OS de destino e `0xFF` para cuspir o Epílogo de fechamento), varrendo a LI de forma contígua:

```c
void teko_metal_emit_program(MetalContext* ctx, const unsigned char* bytecode, uint32_t size) {
    if (!ctx || !bytecode || size == 0) return;

    // 1. Despacha o sinal de início (Injeta diretivas .global e alinhamentos de pilha do OS)
    teko_metal_route_instruction(ctx, 0xFE, 0);

    // 2. Varredura linear e triagem rigorosa de Opcodes e argumentos Little Endian
    uint32_t i = 0;
    while (i < size) {
        OpCode op = (OpCode)bytecode[i++];
        int32_t arg = 0;
        if (op == OP_ICONST || op == OP_SCONST || op == OP_JMP || op == OP_JMP_IF_FALSE) {
            arg = (bytecode[i+0] << 0) | (bytecode[i+1] << 8) | (bytecode[i+2] << 16) | (bytecode[i+3] << 24);
            i += 4;
        }
        teko_metal_route_instruction(ctx, op, arg);
    }

    // 3. Despacha o sinal de término (Restaura registradores e injeta a instrução 'ret' ou 'syscall')
    teko_metal_route_instruction(ctx, 0xFF, 0);
}
```

---

## 4. Próximos Passos de Codificação Dinâmica

Com a árvore física de pastas montada via terminal (`mkdir -p`) e o planejamento de hardware documentado, os submódulos serão codificados na seguinte sequência estável de arquivos isolados:

1.  `src/codegen/apple/emit_darwin_arm64.c`
2.  `src/codegen/bsd_unix/emit_freebsd_x86_64.c`
3.  `src/codegen/linux/emit_linux_x86_64.c`
4.  `src/codegen/windows/emit_win_x86_64.c`
5.  `src/codegen/bare_metal/emit_wasm.c`
6.  `src/codegen/bare_metal/emit_avr.c`

Cada incremento será suportado por asserções rígidas do Unity dentro da pasta `tests/codegen/` para garantir 100% de estabilidade de baixo nível.
