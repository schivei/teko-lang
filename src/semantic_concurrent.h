#ifndef SEMANTIC_CONCURRENT_H
#define SEMANTIC_CONCURRENT_H

#include "parser.h"
#include "parser_concurrent.h"
#include "parser_statements.h"

// Tipos de erros semânticos mapeados para a infraestrutura de concorrência
typedef enum {
    CONC_ERR_NONE = 0,
    CONC_ERR_INVALID_DECLARATION, // Tentativa de usar let/mut para canais/waiters/mutex
    CONC_ERR_ILLEGAL_METHOD       // Método desconhecido ou chamada inválida em canais
} ConcurrentSemanticError;

// Estrutura que representa o resultado de uma validação de concorrência
typedef struct ConcurrentValidationResult {
    ConcurrentSemanticError error_type;
    char* error_message;
} ConcurrentValidationResult;

// Assinaturas públicas do Verificador Semântico de Concorrência
ConcurrentValidationResult validate_concurrency_variable_creation(const StatementASTNode* var_node);
ConcurrentValidationResult validate_channel_method_access(const char* method_name);
void free_concurrent_validation_result(ConcurrentValidationResult result);

#endif // SEMANTIC_CONCURRENT_H