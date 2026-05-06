CC = arm-none-eabi-gcc
CFLAGS = -c -mthumb -mcpu=cortex-m4 -mfloat-abi=soft -mlong-calls -Os \
         -ffreestanding -nostdlib \
         -fno-unwind-tables -fno-asynchronous-unwind-tables -fno-exceptions \
         -fno-common -fno-builtin

SRCS = $(wildcard *.c)
OBJS = $(SRCS:.c=.o)

all: $(OBJS)

%.o: %.c
	$(CC) $(CFLAGS) $< -o $@

inspect: $(OBJS)
	@for f in $(OBJS); do \
		echo "\n========== $$f =========="; \
		arm-none-eabi-readelf -h -S -s -r $$f; \
	done

clean:
	rm -f $(OBJS)

.PHONY: all inspect clean
