#include <stdint.h>
#include <stdbool.h>
#include "header/filesystem/ext2.h"

static inline void syscall(uint32_t eax, uint32_t ebx, uint32_t ecx, uint32_t edx) {
    __asm__ volatile("int $0x30" : : "a"(eax), "b"(ebx), "c"(ecx), "d"(edx) : "memory");
}

int main(void) {
    // 1. Uji Syscall 6 (Puts): Tampilkan banner awal
    syscall(6, (uint32_t) "BillyOS User Mode Shell Initialized!\n", 37, 0x0A);

    // 2. Uji Syscall 0 (FS Read): Coba baca binary file shell dari storage EXT2
    uint8_t file_buf[2048] = {0};
    struct EXT2DriverRequest request = {
        .buf          = file_buf,
        .name         = "shell",
        .name_len     = 5,
        .parent_inode = 1,
        .buffer_size  = sizeof(file_buf),
        .is_directory = false
    };
    int8_t retcode = -1;
    syscall(0, (uint32_t) &request, (uint32_t) &retcode, 0);
    if (retcode == 0) {
        syscall(6, (uint32_t) "Syscall 0 (FS Read): Berhasil membaca shell dari disk!\n", 55, 0x0E);
    }

    // 3. Uji Syscall 7 (Aktifkan Keyboard) & Syscall 4 + 5 (Echo Input)
    syscall(7, 0, 0, 0);
    syscall(6, (uint32_t) "Ketikkan sesuatu di keyboard: ", 30, 0x0F);

    char c = 0;
    while (true) {
        syscall(4, (uint32_t) &c, 0, 0);
        if (c != 0) {
            syscall(5, (uint32_t) c, 0x0F, 0); // Echo karakter ke layar
            c = 0;
        }
    }

    return 0;
}