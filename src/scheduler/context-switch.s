global process_context_switch

process_context_switch:
    lea  ebp, [esp + 4]

    mov  eax, [ebp + 44]       
    push eax

    mov  eax, [ebp + 12]        
    push eax

    mov  eax, [ebp + 52]        
    or   eax, 0x200             
    push eax

    mov  eax, 0x18 | 0x3
    push eax

    mov  eax, [ebp + 48]        
    push eax

    mov  ax, [ebp + 32]        
    mov  gs, ax
    mov  ax, [ebp + 36]         
    mov  fs, ax
    mov  ax, [ebp + 40]       
    mov  es, ax
    mov  ax, [ebp + 44]
    mov  ds, ax

    mov  edi, [ebp + 0]         ; ctx.cpu.index.edi
    mov  esi, [ebp + 4]         ; ctx.cpu.index.esi
    mov  ebx, [ebp + 16]        ; ctx.cpu.general.ebx
    mov  edx, [ebp + 20]        ; ctx.cpu.general.edx
    mov  ecx, [ebp + 24]        ; ctx.cpu.general.ecx
    mov  eax, [ebp + 28]        ; ctx.cpu.general.eax

    mov  ebp, [ebp + 8]         ; ctx.cpu.stack.ebp

    iret