#include "semantic_struct.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>

StructValidationResult validate_required_properties(const MessageProperty* defined_props, int defined_count,
                                                   const char** initialized_names, int initialized_count) {
    StructValidationResult result = {STRUCT_ERR_NONE, NULL};

    if (!defined_props) return result;

    for (int i = 0; i < defined_count; i++) {
        if (defined_props[i].is_required) {
            bool found = false;
            for (int j = 0; j < initialized_count; j++) {
                if (initialized_names[j] && strcmp(defined_props[i].prop_name, initialized_names[j]) == 0) {
                    found = true;
                    break;
                }
            }

            // Se um campo obrigatório foi esquecido na inicialização, dispara erro semântico
            if (!found) {
                result.error_type = STRUCT_ERR_MISSING_REQUIRED;
                int msg_len = 128 + (int)strlen(defined_props[i].prop_name);
                result.error_message = (char*)malloc(msg_len);
                if (result.error_message) {
                    snprintf(result.error_message, msg_len,
                             "[Erro Semântico]: Inicialização inválida. A propriedade obrigatória (required) '%s' não foi definida.",
                             defined_props[i].prop_name);
                }
                fprintf(stderr, "%s\n", result.error_message);
                return result;
            }
        }
    }

    return result;
}

void free_struct_validation_result(StructValidationResult result) {
    if (result.error_message) {
        free(result.error_message);
    }
}