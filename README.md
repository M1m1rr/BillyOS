## BillyOS

Intinya OS 32-bit x86.

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
5. Shell & Program User
6. Daftar System Call
7. Peta Memori & Disk
8. Struktur Repository
9. Batasan
10. Maskot Kelompok Kalian

## Cara Run

### Requirements
- `gcc` (support `-m32` atau `gcc-multilib`)
- `nasm`
- `make`
- `ld` (binutils)
- `qemu-system-i386` (untuk suara PC Speaker, target `run` memakai audio backend PulseAudio, lihat catatan di bawah)
- `genisoimage` atau `xorriso` (bootloader GRUB legacy sudah ada di `other/grub1`)
- `bin/doom1.wad` (WAD shareware DOOM), hanya untuk menjalankan DOOM
- `bin/badapple.dat` (video Bad Apple format BillyOS, lihat bagian *Format File*), hanya untuk menjalankan `badapple`

> Folder `bin/` di-ignore oleh git, jadi `doom1.wad` dan `badapple.dat` harus disiapkan sendiri sebelum `make insert-shell`.

### Langkah-langkah Pembuatan & Menjalankan OS

**1. Kompilasi kernel dan buat ISO**
```bash
make build
```

**2. Buat disk image 32 MB (EXT2 16 MB di awal + area BillyFS) dari `.kit/ch2/sample-image.bin`**
```bash
make disk
```

**3. Masukkan shell dan program user ke disk**
```bash
make insert-shell
```
Target mengcompile `shell`, `hello`, `loop`, `clock`, `fib`, `echo`, `draw`, `doom`, `badapple`, lalu memasukkannya ke root EXT2 dengan `inserter`, bersama `doom1.wad`, `badapple.dat`, `banner`, dan file partitur `*.mus`.

**4. Jalankan di emulator**
```bash
make run
```
Debugging dengan GDB (QEMU waiting di port 1234):
```bash
make debug
```

> Urutan pertama kali: `make disk` -> `make insert-shell` -> `make run`.
> Setelah mengubah kode kernel cukup `make run`. Setelah mengubah program user ulangi `make disk` dan `make insert-shell` agar disk berisi binary terbaru.

> **Catatan audio:** `make run` menjalankan QEMU dengan `-audiodev pa` (PulseAudio) dan `-machine pcspk-audiodev=snd0` agar PC Speaker terdengar. Jika QEMU kamu tidak punya backend PulseAudio (misalnya Windows/macOS), ganti `pa` di target `run` pada `Makefile` dengan backend yang tersedia (`dsound`, `coreaudio`, `sdl`, dst.).

## Fitur yang Dibuat

### 1. Kernel Entrypoint & GDT/IDT
Fondasi utama saat komputer pertama kali menyala dan meload kernel ke memori.

a. Kernel Entrypoint (`kernel-entrypoint.s` & `kernel.c`):
- Titik masuk pertama yang dieksekusi oleh bootloader (GRUB legacy).
- Menyiapkan paging awal agar kernel berjalan di **higher half** (`0xC0000000`), menyiapkan stack, menginisialisasi driver, filesystem, heap, dan swap, lalu membuat proses `shell` dan menyerahkan kendali ke scheduler.

b. Global Descriptor Table (GDT) & TSS:
- Segmen kernel code/data (DPL 0) dan user code/data (DPL 3), ditambah *Task State Segment* untuk perpindahan stack dari ring 3 ke ring 0.

c. Interrupt Descriptor Table (IDT) dan Interupsi:
- 64 ISR stub (exception CPU, IRQ PIC, dan software interrupt `int 0x30` untuk system call).
- PIC di-remap ke vektor `0x20`/`0x28`; saat ini IRQ yang aktif adalah timer (IRQ0) dan keyboard (IRQ1).
- Handler utama `main_interrupt_handler()` meneruskan ke timer/scheduler, keyboard, page fault, dan system call.

d. Port I/O:
- Fungsi `in`, `out`, `in16`, `out16` untuk berkomunikasi dengan hardware.

### 2. Driver
a. Framebuffer & VGA (`framebuffer.c`, `vga.c`):
- Mode teks lewat framebuffer `0xB8000` (clear, tulis karakter, warna, kursor, scroll).
- Mode grafis **VGA 12h (640x480, 16 warna)** dengan render teks sendiri; primitif `put_pixel`, `draw_line`, `draw_rect`, `draw_circle`, `scroll`. Palet 16 warna dipulihkan otomatis saat kembali dari mode 13h.
- **Scrollback 128 baris** pada terminal grafis: baris yang keluar dari layar disimpan dan bisa dilihat kembali (indikator `[UP +n]` di pojok kanan atas). Kembali ke bawah otomatis saat ada teks baru.
- Mode **13h (320x200, 256 warna)** dengan pengaturan palet dan *blit* frame, dipakai oleh DOOM dan Bad Apple.

b. Keyboard PS/2 (`keyboard.c`):
- Menerjemahkan scancode set 1 menjadi ASCII ke dalam ring buffer; mendukung Shift (huruf besar dan simbol), Backspace, Enter, Tab, tombol panah, Ctrl, serta PageUp/PageDown dan Shift+panah atas/bawah (untuk scrollback).
- Mencatat **status fisik tombol** (make code dan break code, termasuk tombol extended `E0 xx`), sehingga program dapat mengetahui tombol sedang ditahan atau sudah dilepas (syscall 23). "Fake shift" yang dikirim keyboard di sekitar tombol panah diabaikan.

c. Disk ATA PIO (`disk.c`):
- Baca/tulis sektor 512 byte (LBA28) pada primary master lewat port I/O.

d. CMOS RTC (`cmos.c`):
- Membaca jam dan tanggal real-time (dipakai program `clock`).

e. PIT Timer:
- Timer 1000 Hz sebagai sumber preemption scheduler dan penghitung milidetik sejak boot (syscall 22).

f. PC Speaker (`speaker.c`):
- Nada kotak lewat PIT channel 2 dan port `0x61`: `speaker_play(freq)`, `speaker_stop()`, dan `speaker_beep(freq, ms)`.
- Dipakai oleh perintah `beep`, `music`, `play`, animasi boot/shutdown, dan soundtrack Bad Apple lewat syscall 24.

### 3. File System EXT2
Penyimpanan non-volatile berbasis struktur EXT2 pada partisi 16 MB di awal disk, blok 512 byte, 8 block group x 4096 blok.

a. Struktur Data EXT2:
- Superblock, Block Group Descriptor Table, Inode Table, serta Block & Inode Bitmap.

b. Operasi File & Direktori:
- **Read**: resolusi path, pembacaan blok direct, *singly indirect*, dan *doubly indirect* (ukuran file maksimum sekitar 8,4 MB).
- **Write/Create**: alokasi inode dan blok, pembuatan file dan direktori (lengkap dengan entri `.` dan `..`).
- **Read Directory**: membaca entri direktori.
- **Delete**: membebaskan blok dan inode, menghapus entri di direktori induk.
- Proteksi: tidak dapat menghapus `.`, `..`, root, maupun direktori kerja yang sedang aktif; folder yang tidak kosong ditolak.

### 4. BillyFS (filesystem kedua, non-EXT2)
Filesystem sederhana pada partisi terpisah (mulai LBA 32768, tepat setelah EXT2) yang diakses lewat syscall 19.
- Superblock, bitmap alokasi blok, tabel 256 inode (record 64 byte), dan 4 direct block per file (maks 2 KB per file).
- Operasi: format otomatis jika belum ada, create, write, read, delete, dan list direktori; mendukung folder bertingkat lewat `parent_id`.
- **Indeks nama**: hash DJB2 disimpan terurut di satu node B+ Tree (kapasitas 8 kunci) dan dicari dengan *binary search* lewat `bfs_find_indexed()`.

### 5. Virtual Memory & Paging
a. Paging Engine:
- Page Directory dan Page Table x86 dengan halaman 4 KB, memakai skema **recursive page directory** (entri 1023).
- Manajemen frame fisik lewat `page_frame_map` untuk 128 MB RAM; 4 MB pertama dicadangkan untuk kernel.
- Page Directory terpisah untuk tiap proses (kernel di-share lewat entri higher half).

b. Isolasi Kernel & User Space:
- Kernel dipetakan di `0xC0000000`; halaman user ditandai user-accessible.

c. Page Fault & Swap:
- Swap space di disk: 64 slot x 4 KB. `swap_out_page()` memindahkan halaman ke disk, dan page fault handler melakukan `swap_in_page()` secara otomatis saat halaman diakses kembali. Swap-out belum dipicu otomatis (lihat Batasan).

d. Heap:
- **Heap user** (`user_malloc` / `user_free` via syscall 20): *first-fit*, *block splitting*, dan *coalescing* dua arah; mulai di `0x500000` dan tumbuh per halaman.
- **Heap kernel** (`kmalloc`): pool statis 32 KB bertipe *bump allocator* (digunakan untuk pelacakan halaman proses).

### 6. Process & Scheduler
a. Process Control Block (PCB):
- Menyimpan PID, nama, state (`INACTIVE`/`READY`/`RUNNING`), konteks CPU (register, `eip`, `eflags`, page directory), dan daftar halaman milik proses. Maksimum 16 proses.
- Pembuatan proses memuat binary dari EXT2 ke ruang alamat baru (kode di `0x0`, stack user di bawah `0x400000`) dan menyusun `argc`/`argv` di stack.

b. Preemptive Round-Robin Scheduler:
- Dipicu timer PIT 1000 Hz; konteks proses disimpan di PCB dan proses berikutnya dijalankan lewat `iret` ke ring 3.
- System call dijalankan dengan interrupt dimatikan agar tidak terjadi preemption di tengah mode kernel.

### 7. DOOM
Port DOOM (PureDOOM) sebagai program user di mode VGA 13h, membaca `doom1.wad` dari EXT2.
- Input memakai status fisik tombol (syscall 23): mendukung beberapa tombol sekaligus (maju + belok + tembak + lari) tanpa delay auto-repeat.
- Kecepatan game dibatasi 35 tic/detik berdasarkan jam sistem (syscall 22).
- Kontrol:

| Aksi | Tombol |
|------|--------|
| Maju / Mundur | `W` / `S` atau panah atas / bawah |
| Belok kiri / kanan | `A` / `D` atau panah kiri / kanan |
| Geser (strafe) | `Z` / `X`, atau `Alt` + panah |
| Tembak | `Ctrl` atau `F` |
| Buka pintu / pakai | `Space` atau `E` |
| Lari | `Shift` atau `R` |
| Senjata | `1` - `7` |
| Menu | `Enter`, `Esc`, `Backspace`, `Y` / `N` |
| Peta (automap) | `Tab` |
| Zoom layar | `=` / `-` |

### 8. Bad Apple!! Video Player
Program user `badapple` memutar video hitam-putih 320x200 di mode VGA 13h dengan suara dari PC Speaker.
- File video (`badapple.dat`) dimuat seluruhnya ke heap user, tiap frame didekode dari RLE ke framebuffer lalu di-blit lewat syscall 21.
- Sinkronisasi video dan nada memakai jam milidetik (syscall 22); frame dilewati jika pemutaran tertinggal.
- Kontrol: `Esc` / `Q` / `Ctrl+C` untuk keluar, `F` untuk menampilkan overlay FPS. Nama file lain bisa diberikan sebagai argumen: `badapple <file>`.

### 9. Musik, Boot & Shutdown
- Animasi boot (logo BillyOS + nada) saat shell pertama kali menyala, dapat dilewati dengan menekan tombol apa saja.
- `shutdown` dan `reboot` memainkan animasi lalu memanggil syscall 25 (power-off via port ACPI QEMU/Bochs/VirtualBox, reboot via reset keyboard controller).
- Pemutar musik: lagu bawaan (`music`) dan pemutar partitur dari file `.mus` (`play`).

## Shell & Program User

### Perintah Shell (`user-shell.c`)
| Perintah | Fungsi |
|----------|--------|
| `help` | Daftar perintah |
| `clear` | Bersihkan layar |
| `ls [path]`, `cd <path>` | Lihat isi dan pindah direktori |
| `cat <file>` | Tampilkan isi file |
| `mkdir <nama>` | Buat direktori |
| `rm <path>` | Hapus file/direktori |
| `cp <src> <dst>`, `mv <src> <dst>` | Salin / pindahkan |
| `find [-name] <nama>` | Cari file |
| `grep <pola> [file]` | Cari teks; dapat menerima input dari pipe |
| `ps` | Daftar proses (PID, nama, state, memori) |
| `exec <program> [args]` | Jalankan program dari disk |
| `kill <pid>` | Hentikan proses |
| `export` | Atur variabel `PATH` (disimpan di `path.env`) |
| `bfs <info\|ls\|mkdir\|write\|cat\|rm\|find>` | Operasi pada BillyFS |
| `beep [freq] [ms]` | Bunyikan PC Speaker (default 750 Hz, 150 ms) |
| `music <doom\|mario\|tetris\|starwars\|zelda\|list>` | Putar lagu bawaan |
| `play <file.mus>` | Putar partitur dari file (`Esc`/`Q` untuk berhenti) |
| `heap` | Uji coba `malloc`/`free` |
| `shutdown`, `reboot` | Matikan / restart mesin |
| `exit` | Keluar dari shell |

Fitur shell lainnya:
- **Pipe** (`cmd1 | cmd2`) dan menjalankan program langsung dengan mengetik namanya (dicari lewat `PATH`).
- **Riwayat perintah** 16 entri: panah atas / bawah.
- **Scrollback**: `PageUp` / `PageDown` atau `Shift` + panah atas / bawah (5 baris per langkah).

### Program User
| Program | Keterangan |
|---------|------------|
| `hello` | Cetak pesan sederhana |
| `loop` | Proses dengan loop tak berujung (untuk uji scheduler dan `kill`) |
| `clock` | Menampilkan jam dan tanggal dari RTC |
| `fib` | Deret Fibonacci |
| `echo` | Mencetak argumen (uji `argc`/`argv`) |
| `draw` | Demo grafis VGA (pixel, garis, persegi) |
| `doom` | DOOM |
| `badapple` | Pemutar video Bad Apple!! |

### Format File
**Partitur `.mus`** (teks): satu not per entri, dipisah spasi/baris baru/koma.
```
# komentar (juga // komentar)
E5 125      # nama not + durasi (ms)
C#4:150     # pemisah ':' juga boleh
659 400     # atau frekuensi langsung dalam Hz
0 35        # 0 / R / P = jeda
```
Nama not: `C D E F G A B`, `#`/`S` = naik setengah nada, `b` = turun setengah nada, diikuti oktaf 1-6 (default 4).

**Video `badapple.dat`** (little-endian, packed):
| Bagian | Isi |
|--------|-----|
| Header (24 B) | `magic` = `0x50504142` ("BAPP"), `width`=320, `height`=200, `fps`, `version`=1, `frame_count`, `audio_offset`, `audio_count` |
| Tabel offset | `frame_count + 1` nilai `uint32` (awal tiap frame; entri terakhir = ukuran file) |
| Event audio | `audio_count` x {`time_ms` uint32, `freq_hz` uint16, `flags` uint16}; `freq_hz = 0` dengan `flags & 1` = hentikan suara |
| Data frame | Frame RLE: bit 7 = warna (1 = putih 255, 0 = hitam 0), bit 6 = panjang 2 byte; panjang run = nilai + 1 |

Ukuran file maksimum sekitar 8,07 MiB (batas file EXT2).

## Daftar System Call
Dipanggil dengan `int 0x30`; nomor di `eax`, argumen di `ebx`, `ecx`, `edx`.

| No | Fungsi |
|----|--------|
| 0 | Baca file EXT2 |
| 1 | Baca direktori EXT2 |
| 2 | Tulis file/direktori EXT2 |
| 3 | Hapus file/direktori EXT2 |
| 4 | Ambil satu karakter dari buffer keyboard |
| 5 | Cetak satu karakter berwarna |
| 6 | Cetak string berwarna |
| 7 | Aktifkan input keyboard |
| 8 | Bersihkan layar |
| 10 | Keluar dari proses (exit) |
| 11 | Buat proses baru (`exec`) dengan argumen |
| 12 | Hentikan proses berdasarkan PID |
| 13 | Ambil daftar metadata proses |
| 14 | Baca jam RTC |
| 15 | Cetak teks pada posisi baris/kolom tertentu |
| 16 / 17 / 18 | Gambar pixel / persegi / garis (mode grafis) |
| 19 | BillyFS: info, create, write, read, delete, read dir, find |
| 20 | Heap user: `malloc` / `free` |
| 21 | VGA mode 13h: set mode (`ebx`=0), set palet (1), blit frame (2), kembali ke mode 12h (3) |
| 22 | Milidetik sejak boot (`ebx` = pointer `uint32_t`) |
| 23 | Status fisik 256 tombol keyboard (`index = extended ? 128 : 0 + scancode`) |
| 24 | PC Speaker: `ebx`=0 putar (`ecx` = Hz), 1 berhenti, 2 beep berdurasi (`ecx` = Hz, `edx` = ms) |
| 25 | Power: `ebx`=1 reboot, selain itu power-off |
| 26 | Scroll viewport terminal: `ebx` = jumlah baris (positif = ke atas, negatif = ke bawah, 0 = kembali ke bawah) |

## Peta Memori & Disk

**Memori virtual (per proses)**
| Alamat | Isi |
|--------|-----|
| `0x00000000` | Kode dan data program user |
| `0x00400000 - 4 KB` | Stack user (tumbuh ke bawah dari `0x400000`) |
| `0x00500000` | Awal heap user |
| `0xC0000000` | Kernel (higher half) |

**Disk (`storage.bin`, 32 MB, sektor 512 B)**
| LBA | Isi |
|-----|-----|
| `0` - `32767` | Filesystem EXT2 (16 MB, 8 grup x 4096 blok) |
| `32768` - `40959` | Partisi BillyFS (4096 blok = 2 MB) |
| `4096` - `4607` | Area swap (64 slot x 8 sektor), **saat ini berada di dalam area EXT2**, lihat Batasan |

**Isi root EXT2 setelah `make insert-shell`:** `shell`, `hello`, `loop`, `clock`, `fib`, `echo`, `draw`, `doom`, `doom1.wad`, `badapple`, `badapple.dat`, `banner`, `mario.mus`, `tetris.mus`, `starwars.mus`, `zelda.mus`, `lagu.mus`.

## Struktur Repository
```
src/
  kernel.c, kernel-entrypoint.s, intsetup.s, linker.ld, menu.lst
  header/cpu/        gdt, idt, interrupt (ISR + syscall), portio
  header/driver/     framebuffer, vga, keyboard, disk, cmos, speaker
  filesystem/        ext2.c, bfs.c
  memory/            paging.c, heap.c, swap.c
  process/           process.c
  scheduler/         scheduler.c, context-switch.s
  stdlib/            string.c
  external/          inserter dan init_storage (tool di sisi host)
  user-*.c, crt0*.s, user-linker.ld   program user (shell, doom, badapple, dst.)
*.mus                partitur lagu untuk perintah `play`
banner               teks banner shell
other/grub1          bootloader GRUB legacy
.kit/                materi kit praktikum
```

## Batasan
- Pointer dari user pada system call belum divalidasi.
- Page fault dan exception CPU lain yang tidak dapat ditangani belum mematikan proses penyebabnya.
- Area swap (LBA 4096 - 4607) berada di awal grup 1 EXT2 (bitmap dan inode table), dan swap-out belum dipicu otomatis (tidak ada page replacement); swap saat ini hanya fondasi.
- Halaman heap user dan frame proses lain belum dibebaskan saat proses berakhir, sehingga memori fisik bocor (mis. `badapple` memuat hingga sekitar 8 MB per eksekusi).
- `kfree` belum membebaskan memori (bump allocator 32 KB).
- Jam milidetik (syscall 22) dapat tertinggal saat syscall berjalan lama karena interrupt dimatikan (mis. membaca file besar dari disk).
- Indeks B+ Tree BillyFS masih satu node (maks 8 kunci) tanpa *split*; ukuran file BillyFS maks 2 KB.
- Caps Lock belum didukung.

## Maskot
![maskot](ucup.jpg)
