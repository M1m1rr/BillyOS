#include <stdint.h>

// Pintu masuk user program ke kernel: int 0x30.
// eax = nomor layanan, ebx/ecx/edx = parameter (lihat tabel syscall di buku).
static inline void syscall(uint32_t eax, uint32_t ebx, uint32_t ecx, uint32_t edx) {
    __asm__ volatile("int $0x30" : : "a"(eax), "b"(ebx), "c"(ecx), "d"(edx) : "memory");
}

int main(void) {
    const char *msg = "Halo dari ring 3!\n";
    // eax=6 -> puts(), ebx=pointer string, ecx=jumlah char (18), edx=warna (0xF = putih)
    syscall(6, (uint32_t) msg, 18, 0xF);
    return 0;
}
