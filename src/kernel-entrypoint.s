global loader                        ; the entry symbol for ELF
global load_gdt                      ; load GDT table
global set_tss_register              ; set tss register to GDT entry
global kernel_execute_user_program
extern kernel_setup                  ; kernel C entrypoint
extern _paging_kernel_page_directory ; kernel page directory
extern _paging_kernel_page_table     ; kernel page table

KERNEL_VIRTUAL_BASE equ 0xC0000000    ; kernel virtual memory
KERNEL_STACK_SIZE   equ 2097152       ; size of stack in bytes (2 MiB)
MAGIC_NUMBER        equ 0x1BADB002    ; define the magic number constant
FLAGS               equ 0x0           ; multiboot flags
CHECKSUM            equ -MAGIC_NUMBER ; calculate the checksum (magic number + checksum + flags == 0)


section .bss
align 4                    ; align at 4 bytes
kernel_stack:              ; label points to beginning of memory
    resb KERNEL_STACK_SIZE ; reserve stack for the kernel


section .multiboot  ; GRUB multiboot header
align 4             ; the code must be 4 byte aligned
    dd MAGIC_NUMBER ; write the magic number to the machine code,
    dd FLAGS        ; the flags,
    dd CHECKSUM     ; and the checksum


; start of the text (code) section
section .setup.text 
loader equ (loader_entrypoint - KERNEL_VIRTUAL_BASE)
loader_entrypoint:         ; the loader label (defined as entry point in linker script)
; Build the initial 4 KiB identity/high-half mappings before paging is on.
    mov edi, _paging_kernel_page_table - KERNEL_VIRTUAL_BASE
    xor eax, eax
    mov ecx, 1024
.fill_kernel_page_table:
    mov edx, eax
    or  edx, 0x3
    mov [edi], edx
    add edi, 4
    add eax, 0x1000
    loop .fill_kernel_page_table

    mov eax, _paging_kernel_page_table - KERNEL_VIRTUAL_BASE
    or  eax, 0x3
    mov [_paging_kernel_page_directory - KERNEL_VIRTUAL_BASE], eax
    mov [_paging_kernel_page_directory - KERNEL_VIRTUAL_BASE + 0xC00], eax

    ; Recursive page directory entry pada index 1023 (1023 * 4 = 0xFFC)
    mov eax, _paging_kernel_page_directory - KERNEL_VIRTUAL_BASE
    or  eax, 0x3
    mov [_paging_kernel_page_directory - KERNEL_VIRTUAL_BASE + 0xFFC], eax

    ; Set CR3 (CPU page register)
    mov eax, _paging_kernel_page_directory - KERNEL_VIRTUAL_BASE
    mov cr3, eax

    ; Disable 4 MiB pages; semua mapping menggunakan 4 KiB page tables
    mov eax, cr4
    and eax, 0xFFFFFFEF
    mov cr4, eax

    ; Enable paging pada register CR0 (bit 31 PG)
    mov eax, cr0
    or  eax, 0x80000000    ; PG flag
    mov cr0, eax

    ; Melompat ke higher half virtual address
    lea eax, [loader_virtual]
    jmp eax

loader_virtual:
    ; Hapus identity mapping frame 0 dan invalidate TLB
    mov dword [_paging_kernel_page_directory], 0
    invlpg [0]
    mov esp, kernel_stack + KERNEL_STACK_SIZE ; Setup stack register di higher half
    call kernel_setup
.loop:
    jmp .loop                                 ; loop forever


section .text
; More details: https://en.wikibooks.org/wiki/X86_Assembly/Protected_Mode
load_gdt:
    cli
    mov  eax, [esp+4]
    lgdt [eax] ; Load GDT from GDTDescriptor
    
    mov  eax, cr0
    or   eax, 1
    mov  cr0, eax

    jmp 0x8:flush_cs
flush_cs:
    mov ax, 10h
    mov ss, ax
    mov ds, ax
    mov es, ax
    ret


set_tss_register:
    mov ax, 0x28 | 0 ; GDT TSS Selector, ring 0
    ltr ax
    ret

kernel_execute_user_program:
    mov  eax, 0x20 | 0x3     ; GDT_USER_DATA_SEGMENT_SELECTOR dengan RPL 3
    mov  ds, ax
    mov  es, ax
    mov  fs, ax
    mov  gs, ax
    
    ; Menyusun stack frame untuk perpindahan privilege via iret:
    ; Urutan stack: [esp] -> eip, [esp+4] -> cs, [esp+8] -> eflags, [esp+12] -> esp, [esp+16] -> ss
    mov  ecx, [esp+4]        ; Simpan virtual_addr target program
    push eax                 ; User SS: 0x23 (Data selector RPL 3)
    mov  eax, ecx
    add  eax, 0x400000 - 4   ; User ESP: posisikan di puncak 4 MiB
    push eax                 ; User ESP
    pushf                    ; EFLAGS saat ini
    pop  edx
    or   edx, 0x200          ; Pastikan Interrupt Flag (IF bit 9) aktif di User Mode!
    push edx
    mov  eax, 0x18 | 0x3     ; User CS: 0x1B (Code selector RPL 3)
    push eax                 ; User CS
    mov  eax, ecx
    push eax                 ; User EIP (alamat awal user program)

    iret                     ; Eksekusi iret untuk melompat ke Ring 3!