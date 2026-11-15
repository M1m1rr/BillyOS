# Compiler & linker
ASM           = nasm
LIN           = ld
CC            = gcc

# Directory
SOURCE_FOLDER = src
OUTPUT_FOLDER = bin
ISO_NAME      = OS2025
DISK_NAME     = storage

# Flags
WARNING_CFLAG = -Wall -Wextra -Werror
DEBUG_CFLAG   = -fshort-wchar -g
STRIP_CFLAG   = -nostdlib -fno-stack-protector -nostartfiles -nodefaultlibs -ffreestanding
CFLAGS        = $(DEBUG_CFLAG) $(WARNING_CFLAG) $(STRIP_CFLAG) -m32 -c -I$(SOURCE_FOLDER)
AFLAGS        = -f elf32 -g -F dwarf
LFLAGS        = -T $(SOURCE_FOLDER)/linker.ld -melf_i386


run: all
	@qemu-system-i386 \
	-drive file=$(OUTPUT_FOLDER)/$(DISK_NAME).bin,format=raw,if=ide,index=0,media=disk \
	-cdrom $(OUTPUT_FOLDER)/$(ISO_NAME).iso
debug: all
	@qemu-system-i386 -s -S \
	-drive file=$(OUTPUT_FOLDER)/$(DISK_NAME).bin,format=raw,if=ide,index=0,media=disk \
	-cdrom $(OUTPUT_FOLDER)/$(ISO_NAME).iso

all: build
build: iso
clean:
	rm -rf *.o *.iso $(OUTPUT_FOLDER)/kernel

kernel:
	@$(ASM) $(AFLAGS) $(SOURCE_FOLDER)/kernel-entrypoint.s -o $(OUTPUT_FOLDER)/kernel-entrypoint.o
	@$(ASM) $(AFLAGS) $(SOURCE_FOLDER)/intsetup.s -o $(OUTPUT_FOLDER)/intsetup.o
	@$(ASM) $(AFLAGS) $(SOURCE_FOLDER)/scheduler/context-switch.s -o $(OUTPUT_FOLDER)/context-switch.o
	@$(CC) $(CFLAGS) $(SOURCE_FOLDER)/kernel.c -o $(OUTPUT_FOLDER)/kernel.o
	@$(CC) $(CFLAGS) $(SOURCE_FOLDER)/header/cpu/gdt.c -o $(OUTPUT_FOLDER)/gdt.o
	@$(CC) $(CFLAGS) $(SOURCE_FOLDER)/header/cpu/portio.c -o $(OUTPUT_FOLDER)/portio.o
	@$(CC) $(CFLAGS) $(SOURCE_FOLDER)/header/cpu/idt.c -o $(OUTPUT_FOLDER)/idt.o
	@$(CC) $(CFLAGS) $(SOURCE_FOLDER)/header/cpu/interrupt.c -o $(OUTPUT_FOLDER)/interrupt.o
	@$(CC) $(CFLAGS) $(SOURCE_FOLDER)/header/driver/framebuffer.c -o $(OUTPUT_FOLDER)/framebuffer.o
	@$(CC) $(CFLAGS) $(SOURCE_FOLDER)/header/driver/keyboard.c -o $(OUTPUT_FOLDER)/keyboard.o
	@$(CC) $(CFLAGS) $(SOURCE_FOLDER)/header/driver/disk.c -o $(OUTPUT_FOLDER)/disk.o
	@$(CC) $(CFLAGS) $(SOURCE_FOLDER)/header/driver/cmos.c -o $(OUTPUT_FOLDER)/cmos.o
	@$(CC) $(CFLAGS) $(SOURCE_FOLDER)/filesystem/ext2.c -o $(OUTPUT_FOLDER)/ext2.o
	@$(CC) $(CFLAGS) $(SOURCE_FOLDER)/memory/paging.c -o $(OUTPUT_FOLDER)/paging.o
	@$(CC) $(CFLAGS) $(SOURCE_FOLDER)/stdlib/string.c -o $(OUTPUT_FOLDER)/string.o
	@$(CC) $(CFLAGS) $(SOURCE_FOLDER)/memory/swap.c -o $(OUTPUT_FOLDER)/swap.o
	@$(CC) $(CFLAGS) $(SOURCE_FOLDER)/process/process.c -o $(OUTPUT_FOLDER)/process.o
	@$(CC) $(CFLAGS) $(SOURCE_FOLDER)/scheduler/scheduler.c -o $(OUTPUT_FOLDER)/scheduler.o
	@$(LIN) $(LFLAGS) bin/*.o -o $(OUTPUT_FOLDER)/kernel
	@echo Linking object files and generate elf32...
	@rm -f *.o

iso: kernel
	@mkdir -p $(OUTPUT_FOLDER)/iso/boot/grub
	@cp $(OUTPUT_FOLDER)/kernel     $(OUTPUT_FOLDER)/iso/boot/
	@cp other/grub1                 $(OUTPUT_FOLDER)/iso/boot/grub/
	@cp $(SOURCE_FOLDER)/menu.lst   $(OUTPUT_FOLDER)/iso/boot/grub/
	@genisoimage -R -b boot/grub/grub1 -no-emul-boot -boot-load-size 4 -A os -input-charset utf8 -quiet -boot-info-table -o $(OUTPUT_FOLDER)/$(ISO_NAME).iso $(OUTPUT_FOLDER)/iso
	@rm -r $(OUTPUT_FOLDER)/iso/

disk:
	@qemu-img create -f raw $(OUTPUT_FOLDER)/$(DISK_NAME).bin 4M

inserter:
	@$(CC) -Wno-builtin-declaration-mismatch -g -I$(SOURCE_FOLDER) \
		$(SOURCE_FOLDER)/stdlib/string.c \
		$(SOURCE_FOLDER)/filesystem/ext2.c \
		$(SOURCE_FOLDER)/external/external-inserter.c \
		-o $(OUTPUT_FOLDER)/inserter

user-shell:
	@$(ASM) $(AFLAGS) $(SOURCE_FOLDER)/crt0.s -o crt0.o
	@$(CC)  $(CFLAGS) -Os -fno-pie $(SOURCE_FOLDER)/user-shell.c -o user-shell.o
	@$(CC)  $(CFLAGS) -Os -fno-pie $(SOURCE_FOLDER)/stdlib/string.c -o string.o
	@$(LIN) -T $(SOURCE_FOLDER)/user-linker.ld -melf_i386 --oformat=binary \
		crt0.o user-shell.o string.o -o $(OUTPUT_FOLDER)/shell
	@$(LIN) -T $(SOURCE_FOLDER)/user-linker.ld -melf_i386 --oformat=elf32-i386 \
		crt0.o user-shell.o string.o -o $(OUTPUT_FOLDER)/shell_elf
	@echo Linking object shell object files and generate flat binary...
	@size --target=binary $(OUTPUT_FOLDER)/shell
	@rm -f *.o

user-hello:
	@$(ASM) $(AFLAGS) $(SOURCE_FOLDER)/crt0.s -o crt0.o
	@$(CC)  $(CFLAGS) -Os -fno-pie $(SOURCE_FOLDER)/user-hello.c -o user-hello.o
	@$(LIN) -T $(SOURCE_FOLDER)/user-linker.ld -melf_i386 --oformat=binary \
		crt0.o user-hello.o -o $(OUTPUT_FOLDER)/hello
	@echo Linking object hello...
	@rm -f *.o

user-loop:
	@$(ASM) $(AFLAGS) $(SOURCE_FOLDER)/crt0.s -o crt0.o
	@$(CC)  $(CFLAGS) -Os -fno-pie $(SOURCE_FOLDER)/user-loop.c -o user-loop.o
	@$(LIN) -T $(SOURCE_FOLDER)/user-linker.ld -melf_i386 --oformat=binary \
		crt0.o user-loop.o -o $(OUTPUT_FOLDER)/loop
	@echo Linking object loop...
	@rm -f *.o

user-clock:
	@$(ASM) $(AFLAGS) $(SOURCE_FOLDER)/crt0.s -o crt0.o
	@$(CC)  $(CFLAGS) -Os -fno-pie $(SOURCE_FOLDER)/user-clock.c -o user-clock.o
	@$(LIN) -T $(SOURCE_FOLDER)/user-linker.ld -melf_i386 --oformat=binary \
		crt0.o user-clock.o -o $(OUTPUT_FOLDER)/clock
	@echo Linking object clock...
	@rm -f *.o

user-fib:
	@$(ASM) $(AFLAGS) $(SOURCE_FOLDER)/crt0.s -o crt0.o
	@$(CC)  $(CFLAGS) -Os -fno-pie $(SOURCE_FOLDER)/user-fib.c -o user-fib.o
	@$(LIN) -T $(SOURCE_FOLDER)/user-linker.ld -melf_i386 --oformat=binary \
		crt0.o user-fib.o -o $(OUTPUT_FOLDER)/fib
	@echo Linking object fib...
	@rm -f *.o
	
insert-shell: inserter user-shell user-hello user-loop user-clock user-fib
	@echo Inserting binaries into root directory...
	@cd $(OUTPUT_FOLDER); ./inserter -f shell 1 $(DISK_NAME).bin
	@cd $(OUTPUT_FOLDER); ./inserter -f hello 1 $(DISK_NAME).bin
	@cd $(OUTPUT_FOLDER); ./inserter -f loop 1 $(DISK_NAME).bin
	@cd $(OUTPUT_FOLDER); ./inserter -f clock 1 $(DISK_NAME).bin
	@cd $(OUTPUT_FOLDER); ./inserter -f fib 1 $(DISK_NAME).bin