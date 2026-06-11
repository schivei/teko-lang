#ifndef PARSER_GENERICS_H
#define PARSER_GENERICS_H

#include "parser.h"
#include "parser_types.h"

// Nós específicos para restrições e parâmetros de tipos genéricos
typedef enum {
    NODE_GENERIC_CONSTRAINT = 800,
    NODE_GENERIC_PARAM_DECL
} GenericASTNodeType;

// Estrutura que representa uma restrição (Constraint) estilo C#
typedef struct GenericConstraint {
    char* type_parameter_name; // O parâmetro restrito (ex: "T")
    char* constraint_bound;    // A regra/tipo limite (ex: "struct")
} GenericConstraint;

// Estrutura unificada de assinatura de funções com suporte completo a Generics
typedef struct GenericFunctionSignature {
    char* fn_name;
    TypeInfo* return_type;
    bool is_async;

    // Parâmetros de tipo da função (ex: <T> ou <T, U>)
    char** type_parameters;
    int type_parameter_count;

    // Cláusulas 'where' associadas
    GenericConstraint* constraints;
    int constraint_count;
} GenericFunctionSignature;

// Assinaturas públicas do Parser de Generics
void parse_generic_parameters_decl(Parser* parser, GenericFunctionSignature* sig);
void parse_generic_constraints_where(Parser* parser, GenericFunctionSignature* sig);
void free_generic_function_signature_members(GenericFunctionSignature* sig);

#endif // PARSER_GENERICS_H