#include <stdint.h>

#define COM1 0x3F8

static inline void outb(uint16_t port, uint8_t val) {
    asm volatile ("outb %0, %1" : : "a"(val), "Nd"(port));
}

static inline uint8_t inb(uint16_t port) {
    uint8_t ret;
    asm volatile ("inb %1, %0" : "=a"(ret) : "Nd"(port));
    return ret;
}

void serial_init(void) {
    outb(COM1 + 1, 0x00);    // Disable interrupts
    outb(COM1 + 3, 0x80);    // Enable DLAB (set baud rate divisor)
    outb(COM1 + 0, 0x03);    // Divisor low byte (38400 baud)
    outb(COM1 + 1, 0x00);    // Divisor high byte
    outb(COM1 + 3, 0x03);    // 8 bits, no parity, one stop bit
    outb(COM1 + 2, 0xC7);    // Enable FIFO, clear, 14-byte threshold
    outb(COM1 + 4, 0x0B);    // IRQs enabled, RTS/DSR set
}

static int serial_is_transmit_empty(void) {
    return inb(COM1 + 5) & 0x20;
}

void serial_putc(char c) {
    while (!serial_is_transmit_empty());
    outb(COM1, c);
}

void serial_puts(const char *s) {
    while (*s) {
        serial_putc(*s++);
    }
}

void serial_put_hex(uint64_t value) {
    const char *digits = "0123456789ABCDEF";
    serial_puts("0x");
    
    int started = 0;
    for (int i = 60; i >= 0; i -= 4) {
        int digit = (value >> i) & 0xF;
        if (digit != 0 || started || i == 0) {
            serial_putc(digits[digit]);
            started = 1;
        }
    }
}

void serial_put_dec(uint64_t value) {
    if (value == 0) {
        serial_putc('0');
        return;
    }
    
    char buffer[20];
    int i = 0;
    
    while (value > 0) {
        buffer[i++] = '0' + (value % 10);
        value /= 10;
    }
    
    // Print in reverse
    while (i > 0) {
        serial_putc(buffer[--i]);
    }
}