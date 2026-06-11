cmake_minimum_required(VERSION 3.25)
project(teko C)

set(CMAKE_C_STANDARD 23)
set(CMAKE_C_STANDARD_REQUIRED ON)

# --- Gerenciador de Dependências Nativo (Unity) ---
include(FetchContent)
FetchContent_Declare(
        unity
        GIT_REPOSITORY https://github.com/ThrowTheSwitch/Unity
        GIT_TAG        v2.6.1
)
FetchContent_MakeAvailable(unity)

# 1. Coleta TODOS os arquivos .c e .h de src e de QUALQUER subpasta (como src/codegen)
file(GLOB_RECURSE CORE_SOURCES "src/*.c" "src/*.h")

# Remove rigidamente o ponto de entrada principal para virar biblioteca estática core
list(REMOVE_ITEM CORE_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/src/main.c")

# Cria a biblioteca estática interna com todo o motor do seu compilador
add_library(teko_core STATIC ${CORE_SOURCES})

# Garante a visibilidade de includes em subdiretórios relativos
target_include_directories(teko_core PRIVATE src)

# 2. ALVO 1: O Compilador Oficial (Ponto de Entrada de Produção)
add_executable(teko src/main.c)
target_link_libraries(teko PRIVATE teko_core)

# 3. ALVO 2: A Suite de Testes Isolada (Ponto de Entrada de Testes)
file(GLOB_RECURSE TEST_SOURCES "tests/*.c" "tests/*.h")
add_executable(teko_tests ${TEST_SOURCES})
target_link_libraries(teko_tests PRIVATE teko_core unity)
target_include_directories(teko_tests PRIVATE src)
