#include "unity.h"
#include "teko_target.h"
#include <string.h>

void test_teko_bare_metal_target_detection_and_parsing(void) {
    // 1. Validates auto-detection on the local hardware. Platform-aware so the
    //    same test passes on every CI runner (Linux, macOS, Windows; x86_64 / arm64).
    TekoTarget host = teko_target_detect_host();
#if defined(__APPLE__) && defined(__aarch64__)
    TEST_ASSERT_EQUAL_INT(ARCH_APPLE_SILICON, host.arch);
    TEST_ASSERT_EQUAL_INT(OS_MACOS_DARWIN, host.os);
    TEST_ASSERT_NOT_NULL(strstr(host.target_string, "aarch64-apple-darwin"));
#elif defined(__APPLE__) && defined(__x86_64__)
    TEST_ASSERT_EQUAL_INT(ARCH_X86_64, host.arch);
    TEST_ASSERT_EQUAL_INT(OS_MACOS_DARWIN, host.os);
    TEST_ASSERT_NOT_NULL(strstr(host.target_string, "x86_64-apple-darwin"));
#elif defined(__linux__) && defined(__x86_64__)
    TEST_ASSERT_EQUAL_INT(ARCH_X86_64, host.arch);
    TEST_ASSERT_EQUAL_INT(OS_LINUX, host.os);
    TEST_ASSERT_NOT_NULL(strstr(host.target_string, "x86_64-unknown-linux-gnu"));
#elif defined(__linux__) && defined(__aarch64__)
    TEST_ASSERT_EQUAL_INT(ARCH_ARM64, host.arch);
    TEST_ASSERT_EQUAL_INT(OS_LINUX, host.os);
    TEST_ASSERT_NOT_NULL(strstr(host.target_string, "aarch64-unknown-linux-gnu"));
#elif defined(_WIN32) && (defined(_M_X64) || defined(__x86_64__))
    TEST_ASSERT_EQUAL_INT(ARCH_X86_64, host.arch);
    TEST_ASSERT_EQUAL_INT(OS_WINDOWS, host.os);
    TEST_ASSERT_NOT_NULL(strstr(host.target_string, "x86_64-pc-windows-msvc"));
#elif defined(_WIN32) && (defined(_M_ARM64) || defined(__aarch64__))
    TEST_ASSERT_EQUAL_INT(ARCH_ARM64, host.arch);
    TEST_ASSERT_EQUAL_INT(OS_WINDOWS, host.os);
    TEST_ASSERT_NOT_NULL(strstr(host.target_string, "aarch64-pc-windows-msvc"));
#else
    // Unknown host combo: at minimum, detection must yield a resolved (non-unknown) target.
    TEST_ASSERT_NOT_EQUAL_INT(ARCH_UNKNOWN, host.arch);
    TEST_ASSERT_NOT_EQUAL_INT(OS_UNKNOWN, host.os);
#endif

    // 2. Cross-compilation test for the open RISC-V 64-bit architecture
    TekoTarget riscv_target = teko_target_parse("riscv64-unknown-linux-gnu");
    TEST_ASSERT_EQUAL_INT(ARCH_RISCV64, riscv_target.arch);
    TEST_ASSERT_EQUAL_INT(OS_LINUX, riscv_target.os);

    // 3. Cross-compilation test for WebAssembly targeting Web/Cloud runtimes (WASI)
    TekoTarget wasm_target = teko_target_parse("wasm32-unknown-wasi");
    TEST_ASSERT_EQUAL_INT(ARCH_WASM32, wasm_target.arch);
    TEST_ASSERT_EQUAL_INT(OS_WASI, wasm_target.os);
}