CC = x86_64-linux-gnu-gcc
LD = x86_64-linux-gnu-ld
AS = nasm

CFLAGS = \
	-Wall -Wextra -Wpedantic \
	-O2 -pipe -ffreestanding -fno-stack-protector \
	-fno-pic -mno-80387 -mno-mmx -mno-sse2 \
	-mno-red-zone -mcmodel=kernel

LDFLAGS = \
	-nostdlib -static \
	--no-dynamic-linker -z text \
	-z max-page-size=0x1000

C_SOURCES = $(wildcard src/*.c)
OBJS = $(C_SOURCES:.c=.o)

KERNEL = kernel.elf

all: $(KERNEL)

$(KERNEL): $(OBJS)
	$(LD) $(LDFLAGS) -T linker.ld $(OBJS) -o $@

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -f $(OBJS) $(KERNEL)

.PHONY: all clean