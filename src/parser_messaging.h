#ifndef PARSER_MESSAGING_H
#define PARSER_MESSAGING_H

#include "parser.h"
#include "parser_types.h"
#include "parser_di.h" // Inclusão obrigatória para herdar DILifetime e HandlerDependency

// Nós específicos para Mensageria e CQRS na AST
typedef enum {
    NODE_MSG_COMMAND = 400,
    NODE_MSG_QUERY,
    NODE_MSG_NOTIFICATION,
    NODE_MSG_HANDLER,
    NODE_STRUCT_PROP
} MessagingASTNodeType;

// Estrutura para propriedades de comandos, queries e notificações
typedef struct MessageProperty {
    char* prop_name;
    TypeInfo* prop_type;
    bool is_required;
    bool is_mutable;
} MessageProperty;

// Nó principal da AST para Mensageria e CQRS integrado com o gerenciamento de DI por Arena
typedef struct MessagingASTNode {
    int type;
    char* name;
    union {
        // Dados para command e notification
        struct {
            MessageProperty* properties;
            int property_count;
        } msg_struct;

        // Dados para query (possui tipo de retorno associado)
        struct {
            MessageProperty* properties;
            int property_count;
            TypeInfo* return_intent_type;
        } query_struct;

        // Dados para: handler for Name { ... } com escopos de alocação de dependências
        struct {
            bool is_async_handler;
            char* handle_param_name;
            TypeInfo* handle_return_type;
            HandlerDependency* dependencies; // Herdado do parser_di.h para linkar as Arenas
            int dependency_count;
        } msg_handler;
    } data;
} MessagingASTNode;

// Assinaturas públicas do Parser de Mensageria
MessagingASTNode* parse_messaging_structure(Parser* parser);
MessagingASTNode* parse_messaging_handler(Parser* parser);
void free_messaging_ast_node(MessagingASTNode* node);

#endif // PARSER_MESSAGING_H