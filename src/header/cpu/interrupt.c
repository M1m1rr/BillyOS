#include "header/cpu/interrupt.h"
#include "header/cpu/portio.h"
#include "header/driver/keyboard.h"
#include "header/memory/swap.h"
#include "header/cpu/gdt.h"
#include "header/scheduler/scheduler.h"
#include "header/filesystem/ext2.h"
#include "header/driver/framebuffer.h"
#include "header/driver/keyboard.h"
#include "header/driver/cmos.h"
#include "header/driver/vga.h"
#include "header/filesystem/bfs.h"
#include "header/memory/heap.h"

void io_wait(void) {
    out(0x80, 0);
}

void pic_ack(uint8_t irq) {
    if (irq >= 8) out(PIC2_COMMAND, PIC_ACK);
    out(PIC1_COMMAND, PIC_ACK);
}

void pic_remap(void) {
    out(PIC1_COMMAND, ICW1_INIT | ICW1_ICW4);
    io_wait();
    out(PIC2_COMMAND, ICW1_INIT | ICW1_ICW4);
    io_wait();
    out(PIC1_DATA, PIC1_OFFSET);
    io_wait();
    out(PIC2_DATA, PIC2_OFFSET);
    io_wait();
    out(PIC1_DATA, 0b0100);
    io_wait();
    out(PIC2_DATA, 0b0010);
    io_wait();
    out(PIC1_DATA, ICW4_8086);
    io_wait();
    out(PIC2_DATA, ICW4_8086);
    io_wait();

    out(PIC1_DATA, PIC_DISABLE_ALL_MASK);
    out(PIC2_DATA, PIC_DISABLE_ALL_MASK);
}

void activate_keyboard_interrupt(void) {
    out(PIC1_DATA, in(PIC1_DATA) & ~(1 << IRQ_KEYBOARD));
}

#define PIT_MAX_FREQUENCY                 1193182
#define PIT_TIMER_FREQUENCY               1000
#define PIT_TIMER_COUNTER                 (PIT_MAX_FREQUENCY / PIT_TIMER_FREQUENCY)
#define PIT_COMMAND_REGISTER_PIO          0x43
#define PIT_COMMAND_VALUE_BINARY_MODE     0b0
#define PIT_COMMAND_VALUE_OPR_SQUARE_WAVE (0b011 << 1)
#define PIT_COMMAND_VALUE_ACC_LOHIBYTE    (0b11  << 4)
#define PIT_COMMAND_VALUE_CHANNEL         (0b00  << 6)
#define PIT_COMMAND_VALUE (PIT_COMMAND_VALUE_BINARY_MODE | \
                           PIT_COMMAND_VALUE_OPR_SQUARE_WAVE | \
                           PIT_COMMAND_VALUE_ACC_LOHIBYTE | \
                           PIT_COMMAND_VALUE_CHANNEL)
#define PIT_CHANNEL_0_DATA_PIO            0x40
void activate_timer_interrupt(void) {
    __asm__ volatile("cli");

    uint32_t pit_timer_counter_to_fire = PIT_TIMER_COUNTER;
    out(PIT_COMMAND_REGISTER_PIO, PIT_COMMAND_VALUE);
    out(PIT_CHANNEL_0_DATA_PIO, (uint8_t) (pit_timer_counter_to_fire & 0xFF));
    out(PIT_CHANNEL_0_DATA_PIO, (uint8_t) ((pit_timer_counter_to_fire >> 8) & 0xFF));

    out(PIC1_DATA, in(PIC1_DATA) & ~(1 << IRQ_TIMER));
}

// Penghitung waktu sejak boot dalam milidetik (PIT 1000 Hz)
static volatile uint32_t system_ticks_ms = 0;

static void syscall(struct InterruptFrame frame) {
    switch (frame.cpu.general.eax) {
        case 0:
            *((int8_t*) frame.cpu.general.ecx) = read(*(struct EXT2DriverRequest*) frame.cpu.general.ebx);
            break;
        case 1:
            *((int8_t*) frame.cpu.general.ecx) = read_directory((struct EXT2DriverRequest*) frame.cpu.general.ebx);
            break;
        case 2:
            *((int8_t*) frame.cpu.general.ecx) = write((struct EXT2DriverRequest*) frame.cpu.general.ebx);
            break;
        case 3:
            *((int8_t*) frame.cpu.general.ecx) = delete(*(struct EXT2DriverRequest*) frame.cpu.general.ebx);
            break;
        case 4:
            get_keyboard_buffer((char*) frame.cpu.general.ebx);
            break;
        case 5:
            console_putchar((char) frame.cpu.general.ebx, (uint8_t) frame.cpu.general.ecx);
            break;
        case 6:
            console_puts((char*) frame.cpu.general.ebx, frame.cpu.general.ecx, (uint8_t) frame.cpu.general.edx);
            break;
        case 7:
            keyboard_state_activate();
            break;
        case 8:
            framebuffer_clear();
            console_set_position(0, 0);
            framebuffer_set_cursor(0, 0);
            break;
        case 10: {
            struct ProcessControlBlock *current_pcb = process_get_current_running_pcb_pointer();
            if (current_pcb != NULL) {
                process_destroy(current_pcb->metadata.pid);
            }
            scheduler_switch_to_next_process();
            break;
        }
        case 11: {
            struct EXT2DriverRequest *req = (struct EXT2DriverRequest*) frame.cpu.general.ebx;
            const char *args = (const char*) frame.cpu.general.edx;
            int32_t res = process_create_user_process(*req, args);
            if (res == PROCESS_CREATE_SUCCESS) {
                console_puts("Proses dibuat: ", 15, 0x0A);
                console_puts(req->name, req->name_len, 0x0E);
                console_putchar('\n', 0x0F);
            }
            if (frame.cpu.general.ecx != 0) {
                *((int32_t*) frame.cpu.general.ecx) = res;
            }
            break;
        }
        case 12: {
            uint32_t target_pid = (uint32_t) frame.cpu.general.ebx;
            struct ProcessControlBlock *current_pcb = process_get_current_running_pcb_pointer();
            bool is_self = (current_pcb != NULL && current_pcb->metadata.pid == target_pid);

            if (is_self) {
                // Tulis hasil SEBELUM destroy: setelah itu page directory user sudah dibebaskan
                if (frame.cpu.general.ecx != 0) {
                    *((bool*) frame.cpu.general.ecx) = true;
                }
                process_destroy(target_pid);
                scheduler_switch_to_next_process();
            }

            bool success = process_destroy(target_pid);
            if (frame.cpu.general.ecx != 0) {
                *((bool*) frame.cpu.general.ecx) = success;
            }
            break;
        }
        case 13: {
            struct ProcessMetadata *dest = (struct ProcessMetadata*) frame.cpu.general.ebx;
            uint32_t active_count = 0;
            for (uint32_t i = 0; i < PROCESS_COUNT_MAX; i++) {
                dest[i] = _process_list[i].metadata;
                if (_process_list[i].metadata.state != PROCESS_STATE_INACTIVE) {
                    active_count++;
                }
            }
            if (frame.cpu.general.ecx != 0) {
                *((uint32_t*) frame.cpu.general.ecx) = active_count;
            }
            break;
        }
        case 14:
            if (frame.cpu.general.ebx != 0) {
                cmos_read_rtc((struct RTCTime*) frame.cpu.general.ebx);
            }
            break;
        case 15: {
            const char *str = (const char*) frame.cpu.general.ebx;
            uint8_t row = (frame.cpu.general.ecx >> 16) & 0xFF;
            uint8_t col = frame.cpu.general.ecx & 0xFF;
            uint8_t color = (uint8_t) frame.cpu.general.edx;
            if (str != NULL) {
                for (uint32_t i = 0; str[i] != '\0'; i++) {
                    framebuffer_write(row, col + i, str[i], color, 0);
                }
            }
            break;
        }
        case 16:
            vga_put_pixel(frame.cpu.general.ebx, frame.cpu.general.ecx, (uint8_t) frame.cpu.general.edx);
            break;
        case 17:
            vga_draw_rect(
                (int) (frame.cpu.general.ebx >> 16),
                (int) (frame.cpu.general.ebx & 0xFFFF),
                (int) (frame.cpu.general.ecx >> 16),
                (int) (frame.cpu.general.ecx & 0xFFFF),
                (uint8_t) (frame.cpu.general.edx & 0xFF),
                (bool) ((frame.cpu.general.edx >> 8) & 0x1)
            );
            break;
        case 18:
            vga_draw_line(
                (int) (frame.cpu.general.ebx >> 16),
                (int) (frame.cpu.general.ebx & 0xFFFF),
                (int) (frame.cpu.general.ecx >> 16),
                (int) (frame.cpu.general.ecx & 0xFFFF),
                (uint8_t) (frame.cpu.general.edx & 0xFF)
            );
            break;
        case 19: {
            // Syscall 19: BillyFS & B+ Tree Indexing Interface Multiplexer
            // EBX = pointer struct BFSRequest
            // ECX = pointer int32_t ret (hasil eksekusi)
            struct BFSRequest *req = (struct BFSRequest*) frame.cpu.general.ebx;
            int32_t ret = -1;

            if (req != NULL) {
                switch (req->cmd) {
                    case 0: // Info Superblock
                        if (req->buf != NULL) {
                            read_blocks(req->buf, BFS_START_LBA, 1);
                            ret = 0;
                        }
                        break;
                    case 1: // Create
                        if (req->name != NULL) {
                            ret = bfs_create(req->parent_id, req->name, req->is_dir);
                        }
                        break;
                    case 2: // Write
                        if (req->name != NULL && req->buf != NULL) {
                            ret = bfs_write(req->parent_id, req->name, req->buf, req->size);
                        }
                        break;
                    case 3: // Read
                        if (req->name != NULL && req->buf != NULL) {
                            ret = bfs_read(req->parent_id, req->name, req->buf, req->size);
                        }
                        break;
                    case 4: // Delete
                        if (req->name != NULL) {
                            ret = bfs_delete(req->parent_id, req->name);
                        }
                        break;
                    case 5: // Read Directory
                        if (req->buf != NULL) {
                            ret = bfs_read_dir(req->parent_id, (struct BFSEntry*) req->buf, req->size);
                        }
                        break;
                    case 6: // B+ Tree Index Find O(log N)
                        if (req->name != NULL) {
                            ret = bfs_find_indexed(req->name);
                        }
                        break;
                    default:
                        ret = -1;
                        break;
                }
            }

            // Kembalikan status eksekusi via pointer di ECX (standar arsitektur BillyOS)
            if (frame.cpu.general.ecx != 0) {
                *((int32_t*) frame.cpu.general.ecx) = ret;
            }
            break;
        }
        case 20: {
            // Syscall 20: Dynamic Heap Memory Manager
            // EBX = Action: 0 = user_malloc, 1 = user_free
            // ECX = Pointer hasil / parameter (void** ptr untuk malloc, bool* ok untuk free)
            // EDX = Argument (uint32_t size untuk malloc, void* ptr untuk free)
            if (frame.cpu.general.ebx == 0) {
                void **out_ptr = (void**) frame.cpu.general.ecx;
                if (out_ptr != NULL) {
                    *out_ptr = user_malloc(frame.cpu.general.edx);
                }
            } else if (frame.cpu.general.ebx == 1) {
                bool *out_ok = (bool*) frame.cpu.general.ecx;
                if (out_ok != NULL) {
                    *out_ok = user_free((void*) frame.cpu.general.edx);
                }
            }
            break;
        }
        case 22:
            // Syscall 22: milidetik sejak boot. EBX = uint32_t* hasil
            if (frame.cpu.general.ebx != 0) {
                *((uint32_t*) frame.cpu.general.ebx) = system_ticks_ms;
            }
            break;
        case 23:
            // Syscall 23: status tombol fisik. EBX = uint8_t[256] hasil
            if (frame.cpu.general.ebx != 0) {
                keyboard_get_key_state((uint8_t*) frame.cpu.general.ebx);
            }
            break;
        case 21: {
            // Syscall 21: Antarmuka VGA Mode 13h (320x200, 256 Warna)
            // EBX = Perintah (0: Set Mode 13h, 1: Set Palet 256, 2: Blit Frame 256, 3: Restore Mode 12h)
            // ECX = Parameter pointer buffer (palet atau frame)
            uint32_t cmd = frame.cpu.general.ebx;
            if (cmd == 0) {
                vga_set_mode_13h();
            } else if (cmd == 1) {
                vga_set_palette_256((const uint8_t*) frame.cpu.general.ecx);
            } else if (cmd == 2) {
                vga_blit_256((const uint8_t*) frame.cpu.general.ecx);
            } else if (cmd == 3) {
                vga_init_graphics_mode(); // Kembali ke Mode 12h 640x480 BillyOS
            }
            break;
        }
    }   
}

static void kprint(const char *s, uint8_t color) {
    for (; *s != '\0'; s++) console_putchar(*s, color);
}

static void kprint_hex(uint32_t v, uint8_t color) {
    static const char digits[] = "0123456789ABCDEF";
    kprint("0x", color);
    for (int shift = 28; shift >= 0; shift -= 4) {
        console_putchar(digits[(v >> shift) & 0xF], color);
    }
}

// Exception fatal: proses user dibunuh, exception di kernel = panic.
static void handle_fatal_exception(const struct InterruptFrame *frame) {
    bool from_user = (frame->int_stack.cs & 0x3) == 0x3;

    kprint("\n[kernel] Exception ", 0x0C);
    kprint_hex(frame->int_number, 0x0C);
    kprint(" di EIP=", 0x0C);
    kprint_hex(frame->int_stack.eip, 0x0C);

    if (from_user) {
        kprint(" -> proses dihentikan\n", 0x0C);
        struct ProcessControlBlock *pcb = process_get_current_running_pcb_pointer();
        if (pcb != NULL) {
            process_destroy(pcb->metadata.pid);
        }
        scheduler_switch_to_next_process();
    }

    kprint("\nKERNEL PANIC\n", 0x0C);
    __asm__ volatile("cli");
    while (true) {
        __asm__ volatile("hlt");
    }
}

void main_interrupt_handler(struct InterruptFrame frame) {
    switch (frame.int_number) {
        case PIC1_OFFSET + IRQ_TIMER: {
            system_ticks_ms++;
            pic_ack(IRQ_TIMER);
            struct Context current_ctx = {
                .cpu                         = frame.cpu,
                .eip                         = frame.int_stack.eip,
                .eflags                      = frame.int_stack.eflags,
                .page_directory_virtual_addr = paging_get_current_page_directory_addr(),
            };
            scheduler_save_context_to_current_running_pcb(current_ctx);

            scheduler_switch_to_next_process();
            break;
        }
        case PIC1_OFFSET + IRQ_KEYBOARD:
            keyboard_isr();
            break;
        case 14: // Page Fault Exception (Vector 0x0E)
            if (!page_fault_handler()) {
                handle_fatal_exception(&frame);
            }
            break;
        case 0x30:
            // Interrupt gate sudah meng-clear IF saat masuk handler (popf di stub hanya memulihkan IF=0),
            // jadi cli di bawah hanya pengaman; iret nanti mengembalikan IF milik user.
            __asm__ volatile("cli");
            syscall(frame);
            break;
        default:
            // Semua exception CPU (kecuali NMI) fatal; jangan di-iret ke instruksi yang sama
            if (frame.int_number < 32 && frame.int_number != 2) {
                handle_fatal_exception(&frame);
            }
            break;
    }
}

struct TSSEntry _interrupt_tss_entry = {
    .ss0 = GDT_KERNEL_DATA_SEGMENT_SELECTOR,
};
void set_tss_kernel_current_stack(void) {
    uint32_t stack_ptr;
    __asm__ volatile ("mov %%ebp, %0": "=r"(stack_ptr) : /* <Empty> */);
    _interrupt_tss_entry.esp0 = stack_ptr + 8;
}