; Bare ROM entry: reset vector, model header, C runtime setup.
;
; The CPU comes out of reset at address 0 in Z80 (16-bit) mode. The first
; instruction switches to ADL (24-bit) mode, then we set up a stack and the C
; runtime and hand over to rom_main().

	.section	.reset, "ax", @progbits
	.assume	adl=0
	.global	__reset
__reset:
	jp.lil	__rom_start

; Model header. CEmu reads this at 0x020000 to decide which calculator to
; emulate (TI-OS images carry the same fields in their OS header); without it
; the emulator refuses the image. Fields: 0x800F wrapper, model 0x13 (CE
; family), three fields the emulator skips, device 0 (TI-84 Plus CE). The
; wrapper also covers 16 bytes of padding: CEmu's parser undercounts the
; bytes left as it walks the fields, and real OS headers have more fields
; after these.
	.section	.cert, "a", @progbits
	.db	0x80, 0x0F, 0x00, 0x00, 0x00, 18 + 16
	.db	0x80, 0x12, 0x13, 0x00
	.db	0x80, 0x21, 0x00
	.db	0x80, 0x32, 0x00, 0x00
	.db	0x80, 0xA1, 0x00
	.db	0x80, 0xC2, 0x00, 0x00
	.fill	16, 1, 0

	.section	.text.__rom_start, "ax", @progbits
	.assume	adl=1
__rom_start:
	di
	im	1
	ld	sp, __stack_top

	ld	a, 3				; CPU clock: 48 MHz
	out0	(0x01), a

	; copy initialized data from flash to RAM
	ld	bc, ___data_len
	sbc	hl, hl
	adc	hl, bc
	jr	z, .Lno_data
	ld	hl, ___data_lma
	ld	de, ___data_vma
	ldir
.Lno_data:

	; zero .bss
	ld	bc, ___bss_len
	sbc	hl, hl
	adc	hl, bc
	jr	z, .Lno_bss
	ld	hl, ___bss_low
	ld	(hl), 0
	dec	bc
	sbc	hl, hl
	adc	hl, bc
	jr	z, .Lno_bss
	ld	hl, ___bss_low
	ld	de, ___bss_low + 1
	ldir
.Lno_bss:

	call	_rom_main
.Lhalt:
	jr	.Lhalt

	.extern	_rom_main
