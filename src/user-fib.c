#include <stdint.h>

static inline void syscall(uint32_t eax, uint32_t ebx, uint32_t ecx, uint32_t edx) {
    __asm__ volatile("int $0x30" : : "a"(eax), "b"(ebx), "c"(ecx), "d"(edx) : "memory");
}

static void print_str(const char *s, uint8_t color) {
    uint32_t len = 0;
    while (s[len]) len++;
    syscall(6, (uint32_t) s, len, (uint32_t) color);
}

static void print_num(uint32_t val) {
    char buf[12];
    int i = 0;
    if (val == 0) {
        buf[i++] = '0';
    } else {
        char temp[12];
        int t = 0;
        while (val > 0) {
            temp[t++] = (val % 10) + '0';
            val /= 10;
        }
        while (t > 0) {
            buf[i++] = temp[--t];
        }
    }
    buf[i] = '\0';
    print_str(buf, 0x0E);
}

int main(void) {
    print_str("--- Kalkulasi Deret Fibonacci ---\n", 0x0B);
    print_str("Hasil: ", 0x07);
    uint32_t a = 0, b = 1;
    for (int i = 0; i < 10; i++) {
        print_num(a);
        if (i < 9) print_str(", ", 0x07);
        uint32_t next = a + b;
        a = b;
        b = next;
    }
    print_str("\nSelesai!\n", 0x0A);
    return 0;
}