#ifndef TEKO_IL_H
#define TEKO_IL_H

#include <stdint.h>
#include "codegen_li.h"

// Magic Number exclusivo: "TEKO" em Hexadecimal ASCII
#define TEKO_MAGIC 0x4F4B4554
#define TEKO_VERSION_MAJOR 1
#define TEKO_VERSION_MINOR 0

// Layout físico do cabeçalho do arquivo .tkb (24 bytes) - CORRIGIDO PARA CLANG/MACOS
typedef struct {
    uint32_t magic;              // Deve ser igual a TEKO_MAGIC
    uint16_t version_major;      // Versão major do compilador
    uint16_t version_minor;      // Versão minor do compilador
    uint32_t constant_pool_count;// Quantidade de itens no Pool de Constantes
    uint32_t definitions_count;  // Quantidade de structs/handlers mapeados
    uint32_t code_section_size;  // Tamanho em bytes da seção de opcodes
} __attribute__((packed)) TekoFileHeader;

// Representação de uma definição estrutural em disco - CORRIGIDO PARA CLANG/MACOS
typedef struct {
    uint32_t name_pool_idx;      // Índice do nome da estrutura no Constant Pool
    uint8_t structure_kind;      // 1 = Struct, 2 = Command, 3 = Query, 4 = Handler
    uint32_t fields_count;       // Quantidade de propriedades internas
} __attribute__((packed)) TekoDefinitionEntry;

// Assinaturas para geração física do arquivo de Linguagem Intermediária
void teko_il_serialize_binary(const BytecodeBuffer* buffer, const char* filename);

#endif // TEKO_IL_H