#include "parser_generics.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void parse_generic_parameters_decl(Parser* parser, GenericFunctionSignature* sig) {
    sig->type_parameters = NULL;
    sig->type_parameter_count = 0;

    if (parser->current_token.type != TOKEN_LT) {
        return;
    }

    parser->current_token = parser->peek_token;
    parser->peek_token = lexer_next_token(parser->lexer);

    int cap = 2;
    sig->type_parameters = (char**)malloc(sizeof(char*) * cap);

    while (true) {
        if (parser->current_token.type == TOKEN_EOF) {
            break;
        }

        if (parser->current_token.type == TOKEN_IDENTIFIER) {
            if (sig->type_parameter_count >= cap) {
                cap *= 2;
                sig->type_parameters = (char**)realloc(sig->type_parameters, sizeof(char*) * cap);
            }

            sig->type_parameters[sig->type_parameter_count++] = strdup(parser->current_token.lexeme);
        }

        parser->current_token = parser->peek_token;
        parser->peek_token = lexer_next_token(parser->lexer);

        if (parser->current_token.type == TOKEN_GT) {
            break;
        }
    }
}

void parse_generic_constraints_where(Parser* parser, GenericFunctionSignature* sig) {
    sig->constraints = NULL;
    sig->constraint_count = 0;

    if (parser->current_token.type != TOKEN_WHERE) {
        return;
    }

    parser->current_token = parser->peek_token;
    parser->peek_token = lexer_next_token(parser->lexer);

    int cap = 2;
    sig->constraints = (GenericConstraint*)malloc(sizeof(GenericConstraint) * cap);

    while (true) {
        if (parser->current_token.type == TOKEN_LBRACE ||
            parser->current_token.type == TOKEN_SEMICOLON ||
            parser->current_token.type == TOKEN_EOF) {
            break;
        }

        if (sig->constraint_count >= cap) {
            cap *= 2;
            sig->constraints = (GenericConstraint*)realloc(sig->constraints, sizeof(GenericConstraint) * cap);
        }

        GenericConstraint* constraint = &sig->constraints[sig->constraint_count];
        constraint->type_parameter_name = NULL;
        constraint->constraint_bound = NULL;

        if (parser->current_token.type == TOKEN_IDENTIFIER) {
            constraint->type_parameter_name = strdup(parser->current_token.lexeme);
            parser->current_token = parser->peek_token;
            parser->peek_token = lexer_next_token(parser->lexer);
        }

        if (parser->current_token.type == TOKEN_STRUCT || parser->current_token.type == TOKEN_IDENTIFIER) {
            constraint->constraint_bound = strdup(parser->current_token.lexeme);
            parser->current_token = parser->peek_token;
            parser->peek_token = lexer_next_token(parser->lexer);
            sig->constraint_count++;
        }

        if ((parser->current_token.type == TOKEN_COLON) ||
            (parser->current_token.type == TOKEN_COMMA || parser->current_token.type == TOKEN_WHERE) ||
            (parser->current_token.type != TOKEN_LBRACE && parser->current_token.type != TOKEN_SEMICOLON)) {
            parser->current_token = parser->peek_token;
            parser->peek_token = lexer_next_token(parser->lexer);
        }
    }
}