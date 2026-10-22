# ce-kernel build
#
#   make          bin/KERNEL.8xp for a real calculator (runs from TI-OS via arTIfiCE)
#   make rom      bin/kernel.rom, a bare 4 MB image that boots in CEmu with no TI code
#   make smoke    boots the ROM in headless CEmu and checks the shell responds
#
# Needs the CE C toolchain (https://github.com/CE-Programming/toolchain) on PATH.

NAME = KERNEL
DESCRIPTION = "ce-kernel takeover kernel"
COMPRESSED = NO
ARCHIVED = NO

CFLAGS = -Wall -Wextra -Oz
CXXFLAGS = -Wall -Wextra -Oz

include $(shell cedev-config --makefile)

# ---------------------------------------------------------------------------
# Bare ROM build. Same kernel and drivers, but our own reset code (rom/) in
# place of TI-OS and the toolchain's startup code. Linked with the pure libc
# and compiler runtime only, never the *_os variants that call into TI-OS.

ROM_OBJDIR = build/rom
ROM_IMAGE = bin/kernel.rom
ROM_ELF = $(ROM_OBJDIR)/kernel.elf

ROM_CSOURCES = $(filter-out src/tios/%,$(call rwildcard,src,*.c)) $(wildcard rom/*.c)
ROM_ASOURCES = $(wildcard rom/*.S)
ROM_OBJECTS = $(ROM_CSOURCES:%=$(ROM_OBJDIR)/%.o) $(ROM_ASOURCES:%=$(ROM_OBJDIR)/%.o)

rom: $(ROM_IMAGE)

$(ROM_OBJDIR)/%.c.o: %.c $(MAKEFILE_LIST)
	$(Q)$(call MKDIR,$(@D))
	$(Q)echo [cc] $<
	$(Q)$(CC) -S -MD -MT $@ -MF $(@:.o=.d) $(EZCFLAGS) $< -o $(@:.o=.s)
	$(Q)$(AS) $(EZASFLAGS) $(@:.o=.s) -o $@

$(ROM_OBJDIR)/%.S.o: %.S $(MAKEFILE_LIST)
	$(Q)$(call MKDIR,$(@D))
	$(Q)echo [as] $<
	$(Q)$(CC) -E $(EZCFLAGS) $< -o $(@:.o=.s)
	$(Q)$(AS) $(EZASFLAGS) $(@:.o=.s) -o $@

$(ROM_ELF): $(ROM_OBJECTS) rom/rom.ld rom/check_no_os_calls.py
	$(Q)echo [linking] $@
	$(Q)$(LD) -static --entry=__reset --no-warn-rwx-segments --gc-sections \
		-T rom/rom.ld -Map=$(ROM_OBJDIR)/kernel.map \
		$(ROM_OBJECTS) \
		--start-group \
		$(CEDEV_TOOLCHAIN)/lib/crt/libcrt.a \
		$(CEDEV_TOOLCHAIN)/lib/libc/libc.a \
		--end-group \
		-o $@
	$(Q)python3 rom/check_no_os_calls.py $@ $(BINUTILS_BIN)/z80-none-elf-objdump

$(ROM_IMAGE): $(ROM_ELF)
	$(Q)$(call MKDIR,$(@D))
	$(Q)$(BINUTILS_BIN)/z80-none-elf-objcopy -O binary --gap-fill 0xFF --pad-to 0x400000 $< $@
	$(Q)echo [success] $@

smoke: rom
	$(Q)python3 tests/smoke.py --rom $(ROM_IMAGE)

clean: clean-rom
clean-rom:
	$(Q)$(call RMDIR,build)

-include $(ROM_OBJECTS:.o=.d)

.PHONY: rom smoke clean-rom
