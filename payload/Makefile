# Build the BC-250 SMU metrics-patch payload (run in bc250-xtensa container):
#   smu_patch.bin / SmuPatchPayload.h  native Xtensa payload embedded by the DXE
#   SmuPatchMeta.h                     link base + smu_patch_entry offset
#
# NOTE: integer/float only; do NOT link libgcc (FP-assist is absent on the SMU).
CC      = xtensa-esp32-elf-gcc
OBJCOPY = xtensa-esp32-elf-objcopy

CFLAGS  = -mlongcalls -mtext-section-literals -nostdlib -ffreestanding \
          -fno-builtin -fno-stack-protector -Os -Wall -Wextra
LDFLAGS = -nostdlib -T linker.ld -Wl,--gc-sections -Wl,-Map=smu_patch.map

OBJS = src/entry.o src/smu_patch.o

all: smu_patch.bin SmuPatchPayload.h

src/entry.o: src/entry.S src/smu_patch_layout.h
	$(CC) $(CFLAGS) -c -o $@ $<
src/smu_patch.o: src/smu_patch.c src/sram.h src/smu_patch_map.h
	$(CC) $(CFLAGS) -c -o $@ $<

smu_patch.elf: $(OBJS) linker.ld
	$(CC) -o $@ $(OBJS) $(LDFLAGS)
smu_patch.bin: smu_patch.elf
	$(OBJCOPY) -O binary $< $@
	@sz=$$(wc -c < $@); pad=$$(( (4 - (sz % 4)) % 4 )); \
	 if [ $$pad -ne 0 ]; then dd if=/dev/zero bs=1 count=$$pad >> $@ 2>/dev/null; fi
	@printf 'smu_patch.bin size: '; wc -c < $@
SmuPatchPayload.h: smu_patch.bin
	xxd -i -n smu_patch_payload $< > $@

clean:
	rm -f src/*.o smu_patch.elf smu_patch.bin SmuPatchPayload.h smu_patch.map
.PHONY: all clean
