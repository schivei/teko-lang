#ifndef TLD_WASM_H
#define TLD_WASM_H

#include <stdint.h>
#include <stdbool.h>

// Assinatura mágica de 4 bytes e versão do cabeçalho binário WASM
#define WASM_MAGIC_0        0x00
#define WASM_MAGIC_1        0x61
#define WASM_MAGIC_2        0x73
#define WASM_MAGIC_3        0x6D
#define WASM_VERSION_0      0x01
#define WASM_VERSION_1      0x00
#define WASM_VERSION_2      0x00
#define WASM_VERSION_3      0x00

// Identificadores de Seções Oficiais do Módulo WASM
typedef enum {
    WASM_SEC_CUSTOM   = 0,
    WASM_SEC_TYPE     = 1,  // Assinaturas de funções
    WASM_SEC_IMPORT   = 2,
    WASM_SEC_FUNCTION = 3,  // Índices de funções
    WASM_SEC_TABLE    = 4,
    WASM_SEC_MEMORY   = 5,  // Declaração de memória linear
    WASM_SEC_GLOBAL   = 6,
    WASM_SEC_EXPORT   = 7,  // Exportações de main/funções
    WASM_SEC_START    = 8,
    WASM_SEC_ELEMENT  = 9,
    WASM_SEC_CODE     = 10, // Corpo binário das instruções (Opcodes)
    WASM_SEC_DATA     = 11  // Strings estáticas e pooling (.rodata)
} TekoWasmSectionId;

// Tipos de Dados Primitivos do WebAssembly
#define WASM_TYPE_I32      0x7F
#define WASM_TYPE_FUNC     0x60

// Assinatura pública do motor do Linker WASM Binário
uint32_t tld_wasm_encode_leb128(uint32_t value, uint8_t* out_buffer);
bool tld_wasm_write_module(const char* filename, const uint8_t* code_payload, uint32_t payload_size);

#endif // TLD_WASM_H
