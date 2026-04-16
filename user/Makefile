# Build user programs and generate binary header

CC = gcc
CFLAGS = -ffreestanding -nostdlib -fno-builtin -fno-stack-protector -m64 -mcmodel=small
LDFLAGS = -T user.ld -nostdlib

.PHONY: all clean generate_header

all: init.elf init.bin generate_header

init.o: init.c
	$(CC) $(CFLAGS) -c -o $@ $<

init.elf: init.o user.ld
	ld $(LDFLAGS) -o $@ $<

init.bin: init.elf
	objcopy -O binary $< $@

# Generate C header with binary data
generate_header: init.bin
	@echo "Generating user_bin.h..."
	@echo "/*" > user_bin.h
	@echo " * user_bin.h - Embedded user program binary" >> user_bin.h
	@echo " * Auto-generated from init.bin" >> user_bin.h
	@echo " */" >> user_bin.h
	@echo "#ifndef USER_BIN_H" >> user_bin.h
	@echo "#define USER_BIN_H" >> user_bin.h
	@echo "" >> user_bin.h
	@echo "#define USER_BIN_SIZE $$(wc -c < init.bin)" >> user_bin.h
	@echo "" >> user_bin.h
	@echo "static const unsigned char user_bin_data[] = {" >> user_bin.h
	@xxd -i init.bin >> user_bin.h
	@echo "};" >> user_bin.h
	@echo "" >> user_bin.h
	@echo "#endif" >> user_bin.h
	@echo "user_bin.h generated successfully"

clean:
	rm -f *.o *.elf *.bin user_bin.h