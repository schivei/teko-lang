#include "codegen_li.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// Inicializa o ambiente de emissão do bytecode e o pool de constantes
BytecodeBuffer* codegen_li_create_context(void) {
    BytecodeBuffer* buffer = (BytecodeBuffer*)malloc(sizeof(BytecodeBuffer));
    if (!buffer) return NULL;

    buffer->capacity = 64;
    buffer->size = 0;
    buffer->code = (unsigned char*)malloc(buffer->capacity);

    buffer->pool.capacity = 16;
    buffer->pool.count = 0;
    buffer->pool.strings = (char**)malloc(sizeof(char*) * buffer->pool.capacity);

    return buffer;
}

// Auxiliar para injetar um byte de opcode bruto no fluxo binário
static void emit_byte(BytecodeBuffer* buffer, unsigned char byte) {
    if (buffer->size >= buffer->capacity) {
        buffer->capacity *= 2;
        buffer->code = (unsigned char*)realloc(buffer->code, buffer->capacity);
    }
    buffer->code[buffer->size++] = byte;
}

// Auxiliar para injetar um valor inteiro de 32 bits (Little Endian por padrão do host arm64/x86)
static void emit_int(BytecodeBuffer* buffer, int val) {
    emit_byte(buffer, (val >> 0) & 0xFF);
    emit_byte(buffer, (val >> 8) & 0xFF);
    emit_byte(buffer, (val >> 16) & 0xFF);
    emit_byte(buffer, (val >> 24) & 0xFF);
}

// Adiciona uma string literal ao pool de constantes sem duplicar e retorna seu índice
int codegen_li_add_string_constant(BytecodeBuffer* buffer, const char* str) {
    if (!buffer || !str) return -1;

    // Busca se a string já existe no pool de constantes (deduplicidade)
    for (int i = 0; i < buffer->pool.count; i++) {
        if (strcmp(buffer->pool.strings[i], str) == 0) {
            return i;
        }
    }

    if (buffer->pool.count >= buffer->pool.capacity) {
        buffer->pool.capacity *= 2;
        buffer->pool.strings = (char**)realloc(buffer->pool.strings, sizeof(char*) * buffer->pool.capacity);
    }

    buffer->pool.strings[buffer->pool.count] = strdup(str);
    return buffer->pool.count++;
}

// Percorre recursivamente a AST e emite as instruções da ISA baseadas em registradores virtuais
void codegen_li_emit_statement(BytecodeBuffer* buffer, const StatementASTNode* stmt) {
    if (!buffer || !stmt) return;

    switch (stmt->type) {
        case NODE_VAR_DECL: {
            // Se houver uma expressão/literal de inicialização
            if (stmt->data.var_decl.initializer_raw) {
                const char* init_val = stmt->data.var_decl.initializer_raw;

                // Se o inicializador for uma string, emite o índice correspondente no pool
                if (init_val[0] == '"' || init_val[0] == '`') {
                    int pool_index = codegen_li_add_string_constant(buffer, init_val);
                    emit_byte(buffer, OP_SCONST);
                    emit_int(buffer, pool_index);
                } else {
                    // Trata como número inteiro básico para a LI por padrão neste estágio
                    int num = atoi(init_val);
                    emit_byte(buffer, OP_ICONST);
                    emit_int(buffer, num);
                }

                // Emite a instrução de armazenamento na variável local
                emit_byte(buffer, OP_STORE);
                int var_id = codegen_li_add_string_constant(buffer, stmt->data.var_decl.var_name);
                emit_int(buffer, var_id);
            }
            break;
        }

        case NODE_FOR_LOOP: {
            // 1. Executa a instrução de inicialização do laço (ex: mut i: i32)
            if (stmt->data.for_loop.init_stmt) {
                codegen_li_emit_statement(buffer, stmt->data.for_loop.init_stmt);
            }

            int loop_condition_address = buffer->size;

            // 2. Emite uma simulação da condição lida em texto bruto (resolvida no bytecode por saltos)
            emit_byte(buffer, OP_LOAD);
            emit_int(buffer, 0); // Assume registrador temporário do loop
            emit_byte(buffer, OP_JMP_IF_FALSE);
            int jump_patch_address = buffer->size;
            emit_int(buffer, 0); // Espaço reservado (Patch) para saltar para fora do loop

            // 3. Emite recursivamente os comandos contidos no corpo do bloco do for
            if (stmt->data.for_loop.body_statements) {
                for (int i = 0; i < stmt->data.for_loop.body_count; i++) {
                    codegen_li_emit_statement(buffer, stmt->data.for_loop.body_statements[i]);
                }
            }

            // 4. Salta de volta para reavaliar a condição lenda
            emit_byte(buffer, OP_JMP);
            emit_int(buffer, loop_condition_address);

            // 5. Corrige o endereço de salto de saída do loop (Backpatching)
            int loop_exit_address = buffer->size;
            buffer->code[jump_patch_address] = (loop_exit_address >> 0) & 0xFF;
            buffer->code[jump_patch_address + 1] = (loop_exit_address >> 8) & 0xFF;
            buffer->code[jump_patch_address + 2] = (loop_exit_address >> 16) & 0xFF;
            buffer->code[jump_patch_address + 3] = (loop_exit_address >> 24) & 0xFF;
            break;
        }

        default:
            // Intercepta expressões residuais e emite um retorno implícito de segurança
            emit_byte(buffer, OP_RETURN);
            break;
    }
}

// Serializa o arquivo binário compilado portátil (.tkb) estruturando o cabeçalho
void codegen_li_write_to_file(const BytecodeBuffer* buffer, const char* filename) {
    if (!buffer || !filename) return;

    FILE* file = fopen(filename, "wb");
    if (!file) return;

    // 1. Escreve o Cabeçalho Mágico de identificação (Teko Magic Number: TEKO = 0x54454B4F)
    unsigned char magic[4] = {0x54, 0x45, 0x4B, 0x4F};
    fwrite(magic, 1, 4, file);

    // 2. Serializa a quantidade de registros e o corpo do Pool de Strings
    fwrite(&buffer->pool.count, sizeof(int), 1, file);
    for (int i = 0; i < buffer->pool.count; i++) {
        int str_len = (int)strlen(buffer->pool.strings[i]);
        fwrite(&str_len, sizeof(int), 1, file);
        fwrite(buffer->pool.strings[i], 1, str_len, file);
    }

    // 3. Serializa o bloco linear contíguo de opcodes da LI
    fwrite(&buffer->size, sizeof(int), 1, file);
    fwrite(buffer->code, 1, buffer->size, file);

    fclose(file);
}

// Limpeza minuciosa de memória heap do subsistema emissor
void codegen_li_free_context(BytecodeBuffer* buffer) {
    if (!buffer) return;

    if (buffer->code) free(buffer->code);
    for (int i = 0; i < buffer->pool.count; i++) {
        if (buffer->pool.strings[i]) free(buffer->pool.strings[i]);
    }
    free(buffer->pool.strings);
    free(buffer);
}