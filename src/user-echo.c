#include <stdint.h>

static inline void syscall(uint32_t eax, uint32_t ebx, uint32_t ecx, uint32_t edx) {
    __asm__ volatile("int $0x30" : : "a"(eax), "b"(ebx), "c"(ecx), "d"(edx) : "memory");
}

static uint32_t str_len(const char *s) {
    uint32_t l = 0;
    while (s[l]) l++;
    return l;
}

static void sys_puts(const char *s, uint8_t color) {
    syscall(6, (uint32_t) s, str_len(s), (uint32_t) color);
}

int main(int argc, char *argv[]) {
    for (int i = 1; i < argc; i++) {
        sys_puts(argv[i], 0x0F);
        if (i < argc - 1) {
            sys_puts(" ", 0x0F);
        }
    }
    sys_puts("\n", 0x0F);

    return 0;
}