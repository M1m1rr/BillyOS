#include <stdint.h>

static inline void syscall(uint32_t eax, uint32_t ebx, uint32_t ecx, uint32_t edx) {
    __asm__ volatile("int $0x30" : : "a"(eax), "b"(ebx), "c"(ecx), "d"(edx) : "memory");
}

int main(void) {
    const char msg[] = "Hello World from External User Program!\n";
    uint32_t len = 0;
    while (msg[len]) len++;
    
    // Syscall 6 (console_puts)
    syscall(6, (uint32_t) msg, len, 0x0A);
    
    // Keluar, crt0.s otomatis memanggil Syscall 10 (exit)
    return 0;
}