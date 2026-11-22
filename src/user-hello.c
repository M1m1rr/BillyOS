#include <stdint.h>

static inline void syscall(uint32_t eax, uint32_t ebx, uint32_t ecx, uint32_t edx) {
    __asm__ volatile("int $0x30" : : "a"(eax), "b"(ebx), "c"(ecx), "d"(edx) : "memory");
}

static void print_str(const char *s, uint8_t color) {
    uint32_t len = 0;
    while (s[len]) len++;
    syscall(6, (uint32_t) s, len, (uint32_t) color);
}

int main(void) {
    print_str("Hello World - BillyOS \n", 0x0E);
    return 0;
}