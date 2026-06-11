#ifndef PROJECT_MANAGER_H
#define PROJECT_MANAGER_H

#include <stdbool.h>

// Tipo de artefato que o projeto vai gerar
typedef enum {
    TARGET_EXECUTABLE,
    TARGET_STATIC_LIB,
    TARGET_DYNAMIC_LIB,
    TARGET_UNKNOWN
} TekoTargetType;

// Estrutura atualizada de metadados do manifesto .tkp
typedef struct {
    char* project_name;
    char* version;
    char* author;
    char* root_dir;
    TekoTargetType target_type;
} TekoProjectConfig;

// Assinaturas públicas do Gerenciador de Projetos
TekoProjectConfig* teko_project_load(const char* tkp_filepath);
bool teko_project_validate_structure(const TekoProjectConfig* config);
void teko_project_free(TekoProjectConfig* config);

#endif // PROJECT_MANAGER_H