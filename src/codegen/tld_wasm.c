#include "tld_wasm.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

// Algoritmo de compressão LEB128 para inteiros de 32 bits sem sinal
uint32_t tld_wasm_encode_leb128(uint32_t value, uint8_t* out_buffer) {
    uint32_t bytes_written = 0;
    do {
        uint8_t byte = value & 0x7F;
        value >>= 7;
        if (value != 0) {
            byte |= 0x80; // Ativa o bit mais significativo (MSB) se houver mais bytes vindo
        }
        if (out_buffer) {
            out_buffer[bytes_written] = byte;
        }
        bytes_written++;
    } while (value != 0);
    return bytes_written;
}

// Auxiliar para injetar seções estruturadas no arquivo binário .wasm
static void tld_wasm_write_section(FILE* out, TekoWasmSectionId sec_id, const uint8_t* sec_data, uint32_t sec_size) {
    uint8_t leb_buf[5];

    // Escreve o ID da Seção (1 byte)
    uint8_t id_byte = (uint8_t)sec_id;
    fwrite(&id_byte, 1, 1, out);

    // Escreve o tamanho da seção comprimido em LEB128
    uint32_t leb_bytes = tld_wasm_encode_leb128(sec_size, leb_buf);
    fwrite(leb_buf, 1, leb_bytes, out);

    // Escreve os dados físicos da seção
    if (sec_size > 0 && sec_data) {
        fwrite(sec_data, 1, sec_size, out);
    }
}

bool tld_wasm_write_module(const char* filename, const uint8_t* code_payload, uint32_t payload_size) {
    if (!filename || !code_payload || payload_size == 0) return false;

    FILE* out = fopen(filename, "wb");
    if (!out) return false;

    // 1. ESCREVE O CABEÇALHO INICIAL OBRIGATÓRIO (\x00asm\x01\x00\x00\x00)
    uint8_t header[] = {
        WASM_MAGIC_0, WASM_MAGIC_1, WASM_MAGIC_2, WASM_MAGIC_3,
        WASM_VERSION_0, WASM_VERSION_1, WASM_VERSION_2, WASM_VERSION_3
    };
    fwrite(header, 1, sizeof(header), out);

    // 2. SEÇÃO 1: TYPE SECTION (Declara a assinatura da função: () -> i32)
    uint8_t type_sec[] = {
        0x01,              // Número de tipos/assinaturas no vetor (1)
        WASM_TYPE_FUNC,    // Tipo de assinatura de função (0x60)
        0x00,              // Contagem de parâmetros de entrada (0)
        0x01,              // Contagem de retornos de saída (1)
        WASM_TYPE_I32      // Tipo do retorno: i32 (0x7F)
    };
    tld_wasm_write_section(out, WASM_SEC_TYPE, type_sec, sizeof(type_sec));

    // 3. SEÇÃO 3: FUNCTION SECTION (Mapeia a função 0 à assinatura do índice 0)
    uint8_t func_sec[] = {
        0x01,              // Contagem de funções (1)
        0x00               // Índice do tipo correspondente (0)
    };
    tld_wasm_write_section(out, WASM_SEC_FUNCTION, func_sec, sizeof(func_sec));

    // 4. SEÇÃO 5: MEMORY SECTION (Aloca a memória linear estável O(1) de 1 página de 64KB)
    uint8_t mem_sec[] = {
        0x01,              // Contagem de memórias (1)
        0x00,              // Flags: apenas limite mínimo (0)
        0x01               // Tamanho inicial: 1 página (64KB) para Arenas
    };
    tld_wasm_write_section(out, WASM_SEC_MEMORY, mem_sec, sizeof(mem_sec));

    // 5. SEÇÃO 7: EXPORT SECTION (Exporta a função $main sob o rótulo público "main")
    uint8_t export_sec[] = {
        0x01,              // Número de exportações (1)
        0x04, 'm', 'a', 'i', 'n', // Nome textual exportado: "main" (4 bytes + string)
        0x00,              // Tipo de exportação: Função (0)
        0x00               // Índice da função exportada (0)
    };
    tld_wasm_write_section(out, WASM_SEC_EXPORT, export_sec, sizeof(export_sec));

    // 6. SEÇÃO 10: CODE SECTION (Injeta as instruções brutas do compilador metal)
    // O layout do Code exige: [Tamanho_Total] [Contagem_Funções] [Tamanho_Corpo] [Vetor_Locais] [Payload_Instructions] [0x0B_End]
    uint32_t body_size = 1 + payload_size + 1; // 1 byte vetor locais + instruções + 1 byte do op_end (0x0B)
    uint8_t* code_sec_buf = (uint8_t*)malloc(body_size + 5);

    if (code_sec_buf) {
        uint32_t cursor = 0;
        cursor += tld_wasm_encode_leb128(1, &code_sec_buf[cursor]); // Contagem de funções (1)
        cursor += tld_wasm_encode_leb128(body_size, &code_sec_buf[cursor]); // Tamanho do corpo

        code_sec_buf[cursor++] = 0x00; // Vetor de variáveis locais adicionais (0 entries)
        memcpy(&code_sec_buf[cursor], code_payload, payload_size);
        cursor += payload_size;
        code_sec_buf[cursor++] = 0x0B; // Marcador formal obrigatório de encerramento de escopo: end [INDEX]

        tld_wasm_write_section(out, WASM_SEC_CODE, code_sec_buf, cursor);
        free(code_sec_buf);
    }

    fclose(out);
    return true;
}
