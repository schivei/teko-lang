cmake_minimum_required(VERSION 3.20)

project(teko_lang_bootstrap C)

set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

set(TEKO_BOOTSTRAP_DIR "${CMAKE_CURRENT_SOURCE_DIR}/bootstrap/c")
set(TEKO_CORE0_GENERATED_C "${CMAKE_CURRENT_BINARY_DIR}/generated/core0.c")

add_library(teko_bootstrap_core STATIC
    bootstrap/c/core/backend_c.c
    bootstrap/c/core/compile.c
    bootstrap/c/core/diagnostics.c
    bootstrap/c/core/lexer.c
    bootstrap/c/core/parser.c
    bootstrap/c/core/strings.c
    bootstrap/c/core/teko_core.h
    bootstrap/c/core/teko_internal.h
)

target_include_directories(teko_bootstrap_core
    PUBLIC
        "${TEKO_BOOTSTRAP_DIR}/core"
)

target_compile_options(teko_bootstrap_core PRIVATE
    -Wall
    -Wextra
    -pedantic
)

add_library(teko_bootstrap_platform STATIC
    bootstrap/c/platform/filesystem.c
    bootstrap/c/platform/filesystem.h
)

target_include_directories(teko_bootstrap_platform
    PUBLIC
        "${TEKO_BOOTSTRAP_DIR}/platform"
)

target_link_libraries(teko_bootstrap_platform
    PUBLIC
        teko_bootstrap_core
)

target_compile_options(teko_bootstrap_platform PRIVATE
    -Wall
    -Wextra
    -pedantic
)

add_executable(teko-bootstrap
    bootstrap/c/cli/main.c
)

target_link_libraries(teko-bootstrap
    PRIVATE
        teko_bootstrap_core
        teko_bootstrap_platform
)

target_compile_options(teko-bootstrap PRIVATE
    -Wall
    -Wextra
    -pedantic
)

set(TEKO_CORE0_SOURCES
    "${CMAKE_CURRENT_SOURCE_DIR}/examples/core0/Point.struct.teko"
    "${CMAKE_CURRENT_SOURCE_DIR}/examples/core0/TokenKind.enum.teko"
    "${CMAKE_CURRENT_SOURCE_DIR}/examples/core0/main.teko"
)

add_custom_command(
    OUTPUT "${TEKO_CORE0_GENERATED_C}"
    COMMAND "${CMAKE_COMMAND}" -E make_directory "${CMAKE_CURRENT_BINARY_DIR}/generated"
    COMMAND teko-bootstrap -o "${TEKO_CORE0_GENERATED_C}" ${TEKO_CORE0_SOURCES}
    DEPENDS teko-bootstrap ${TEKO_CORE0_SOURCES}
    COMMENT "Generating C from Core 0 Teko examples"
    VERBATIM
)

add_executable(teko_core0_smoke
    "${TEKO_CORE0_GENERATED_C}"
)

target_compile_options(teko_core0_smoke PRIVATE
    -Wall
    -Wextra
    -pedantic
)

enable_testing()
add_test(NAME core0_smoke COMMAND teko_core0_smoke)
