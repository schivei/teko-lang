#ifndef PARSER_DI_H
#define PARSER_DI_H

#include "parser.h"
#include "parser_types.h"

// Nós específicos para a arquitetura de Injeção de Dependência na AST
typedef enum {
    NODE_DI_INTERFACE = 500,
    NODE_DI_SERVICE,
    NODE_DI_DECORATOR,
    NODE_INTERFACE_METHOD,
    NODE_METHOD_DECL
} DIASTNodeType;

// Ciclos de vida das Arenas injetadas nativamente
typedef enum {
    LIFETIME_TRANSIENT, // Nova instância na Frame Arena do destino (Padrão)
    LIFETIME_SCOPED,    // Fixo por Green Thread / Request Arena
    LIFETIME_SINGLETON  // Fixo na Arena do Contexto Principal (main)
} DILifetime;

// Estrutura para os métodos declarados dentro de interfaces ou implementados em serviços
typedef struct DIMethodSignature {
    char* method_name;
    TypeInfo* return_type;
    bool is_public;
    char* parameters_raw;
} DIMethodSignature;

// Estrutura para dependências injetadas em Handlers com amarração de Arena
typedef struct HandlerDependency {
    char* dep_type;
    char* dep_name;
    DILifetime lifetime; // <-- NOVO: Controla a estratégia de alocação da dependência
} HandlerDependency;

// Nó da AST para o ecossistema de Injeção de Dependência
typedef struct DIASTNode {
    int type;
    char* name;
    union {
        struct {
            bool is_exported;
            DIMethodSignature* methods;
            int method_count;
        } di_interface;

        struct {
            char* implements_interface;
            DIMethodSignature* methods;
            int method_count;
        } di_service;

        struct {
            char* next_param_name;
            char* target_interface;
            int precedence_order;
            DIMethodSignature* methods;
            int method_count;
        } di_decorator;

        // Propriedades do Handler com injeção dirigida por Arena
        struct {
            bool is_async_handler;
            char* handle_param_name;
            TypeInfo* handle_return_type;
            HandlerDependency* dependencies;
            int dependency_count;
        } msg_handler;
    } data;
} DIASTNode;

// Assinaturas públicas do Parser de DI
DIASTNode* parse_di_interface(Parser* parser, bool is_exported);
DIASTNode* parse_di_service(Parser* parser);
DIASTNode* parse_di_decorator(Parser* parser);
void free_di_ast_node(DIASTNode* node);

#endif // PARSER_DI_H