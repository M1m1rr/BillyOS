## BillyOS
**Anggota**:
  1. 13525050 - Jason Hartanto
  2. 13525082 - Ausa Haadiyan Mochtar Yusuf
  3. 13525102 - Karmel Tua Haloho
  4. 13525131 - Mirza Aryasatya Akmal

## Daftar Isi
1. Nama Kelompok
2. Daftar Isi
3. Cara Run
4. Fitur
5. Maskot Kelompok Kalian

## Cara Run

### Requirements
- `gcc`
- `nasm`
- `make`
- `qemu-system-i386`
- `genisoimage` / `xorriso` / `grub-pc-bin`

### Langkah-langkah Pembuatan & Menjalankan OS
### Kompilasi Source Code
```bash
make build
```

### Menjalankan OS di Emulator
```bash
make run
```

## Fitur yang Dibuat (so far)

1. Kernel Entrypoint & GDT/IDT
Fondasi utama saat komputer pertama kali menyala dan meload kernel ke memori.

a. Kernel Entrypoint (kernel-entrypoint.s & kernel.c):
- Titik masuk pertama yang dieksekusi oleh bootloader (GRUB).
- Mempersiapkan stack pointer, mengatur register segmen dasar, mengalihkan kontrol dari instruksi bahasa assembly rendah ke fungsi kernel_setup() atau main() di C.

b. Global Descriptor Table (GDT):
- Mengonfigurasi skema segmentasi memori pada arsitektur x86 32-bit.
- Menentukan batas memori dan hak akses untuk Kernel Space dan User Space.

c. Interrupt Descriptor Table (IDT) dan Interupsi:
- Mengendalikan tabel pemetaan instruksi interupsi perangkat keras maupun software interrupts atau syscalls
- Menyediakan fungsi Interrupt Handler untuk menangani CPU Exceptions agar sistem tidak langsung crash.

d. Port I/O:
- Fungsi pembacaan dan penulisan byte/word ke port komunikasi I/O x86 (inb, outb, inw, outw) untuk berinteraksi langsung dengan pengontrol hardware.

2. Driver Framebuffer & Keyboard
Menangani pengontrolan i/o dasar agar pengguna dapat berinteraksi secara visual dengan OS.

a. Driver Framebuffer:
- Mengontrol memori video VGA untuk mendisplay karakter dan warna ke monitor.
- Menyediakan abstraksi screen clear, cetak karakter/string di koordinat baris dan kolom tertentu, manipulasi warna latar dan teks, serta pergeseran kursor.

b. Driver Keyboard:
- Mengelola data masukan langsung dari kontroler keyboard PS/2 via port I/O.
- Menerjemahkan nilai scancode mentah dari tombol keyboard yang ditekan/dilepas menjadi karakter ASCII.
- Menangani penekanan tombol khusus (Backspace, Enter, Shift, dan Caps Lock) untuk dimasukkan ke buffer input.

3. File System (EXT2)
Modul penyimpanan non-volatile berbasis standar EXT2 untuk mengelola struktur berkas pada disk storage
a. Struktur Data EXT2:
- Membaca dan mengurai struktur dasar EXT2 seperti Superblock, Block Group Descriptor, Inode Table, dan Block & Inode Bitmaps.

b. Operasi File & Direktori (CRUD):
- Create/Write: Mengalokasikan blok data dan inode baru pada disk untuk membuat file atau direktori baru serta menuliskan isi konten ke dalamnya.
- Read: Menerjemahkan path file, mencari nomor Inode terkait, dan membaca isi blok memori file dari disk.
- Directory Traversal: Membaca entri direktori untuk menampilkan daftar file dan sub-folder di dalam suatu lokasi.
- Delete: Membebaskan blok data dan inode yang digunakan oleh file di dalam bitmap serta menghapus entri nama file terkait di direktori induknya.

4. Virtual Memory & Paging
Modul isolasi dan pengalokasian memori untuk mengamankan ruang alamat memori sistem.
a. Paging Engine:
- Mengaktifkan fitur paging bawaan prosesor x86 dengan mengonfigurasi Page Directory dan Page Table.
- Menerjemahkan alamat memori virtual (Virtual Address) menjadi alamat memori fisik (Physical Address) dengan ukuran halaman 4 KB.

b. Isolasi Kernel & User Space:
- Memetakan area Kernel Space ke lokasi memori yang dilindungi agar tidak dapat diakses secara ilegal oleh aplikasi pengguna.
- Menangani Page Fault Exception jika ada proses yang mencoba mengakses alamat memori yang tidak valid atau tidak diizinkan.

5. Process & Scheduler
Modul abstraksi process management untuk mendukung eksekusi banyak program secara bersamaan (multi-tasking).
a. Process Control Block (PCB):
- Menyimpan status dan konteks lengkap dari suatu proses yang sedang berjalan, termasuk Register CPU, Stack Pointer, Page Directory, dan Process ID.

b. Preemptive/Cooperative Scheduler:
- Mengatur jalurnya eksekusi proses secara bergantian menggunakan interupsi pewaktu (Programmable Interval Timer - PIT).
- Melakukan Context Switching untuk menyimpan status proses yang dihentikan sementara dan memuat kembali status proses berikutnya yang akan dieksekusi oleh CPU.

## Maskot
![maskot](ucup.jpg)
