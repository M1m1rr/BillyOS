global _start
extern main

section .text
_start:
    ; Base pointer
    mov ebp, esp

    ; Clear registers
    xor eax, eax
    xor ebx, ebx
    xor ecx, ecx
    xor edx, edx
    xor esi, esi
    xor edi, edi

    ; Konfigurasi pemanggilan C cdecl: main(argc, argv)
    ; [ebp]     = argc
    ; [ebp + 4] = argv (pointer ke argv[0])
    lea eax, [ebp + 4]    ; eax = argv
    push eax              ; Argumen ke-2: char **argv
    push dword [ebp]      ; Argumen ke-1: int argc

    ; Eksekusi main function
    call main

    ; Exit program secara bersih (Syscall 10 di BillyOS)
    mov ebx, eax          ; Exit code dari return main()
    mov eax, 10           ; Syscall 10 (exit)
    int 0x30

    ; Fallback hang jika syscall gagal
    cli
    hlt