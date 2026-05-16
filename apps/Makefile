CC = arm-none-eabi-gcc
CFLAGS = -c -mthumb -mcpu=cortex-m4 -mfpu=fpv4-sp-d16 -mfloat-abi=hard \
         -mlong-calls -Os \
         -ffreestanding -nostdlib \
         -fno-unwind-tables -fno-asynchronous-unwind-tables -fno-exceptions \
         -fno-common -fno-builtin

SRCS = $(wildcard *.c)
OBJS = $(SRCS:.c=.o)
APP_NAMES = $(SRCS:.c=)
ELF_HEADERS = $(addprefix ../Core/Inc/,$(addsuffix _elf.h,$(APP_NAMES)))

all: $(OBJS) $(ELF_HEADERS)

%.o: %.c
	$(CC) $(CFLAGS) $< -o $@

../Core/Inc/%_elf.h: %.o
	xxd -i -n $*_elf $< | \
		sed -e 's/^unsigned char/static const unsigned char/' \
		    -e 's/\[\] =/[] __attribute__((aligned(4))) =/' \
		    -e 's/^unsigned int/static const unsigned int/' > $@

inspect: $(OBJS)
	@for f in $(OBJS); do \
		echo "\n========== $$f =========="; \
		arm-none-eabi-readelf -h -S -s -r $$f; \
	done

clean:
	rm -f $(OBJS)

.PHONY: all inspect clean
