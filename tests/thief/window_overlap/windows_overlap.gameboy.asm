.setting "LaunchCommand", "G:\\Projects\\GB_Emu\\GB_Emu\\x64\\Release\\GB_Emu.exe \"{0}\""
.target "Gameboy"
.setting "OutputFileType", "gb"
.setting "GameboyTitle", "WINDOW OVERLAP"
.setting "GameboyLicenseeCode", "TR"

.bank 0, 16, $0000

.org $0000
ret
.org $0008
ret
.org $0010
ret
.org $0018
ret
.org $0020
ret
.org $0028
ret
.org $0030
ret
.org $0038
ret
.org $0040
ret
.org $0048
ret
.org $0050
ret
.org $0058
ret
.org $0060
ret

.org $0150
xor a
ld ($ff40), a

ld hl, $9FFF
ld a,$01
@windowmap
ld (hl-), a
bit 2,h
jr nz, @windowmap
;ld hl, $9BFF
xor a
@bgmap
ld (hl-), a
bit 2,h
jr z, @bgmap
ld hl, $FE9F
xor a
@sprites
ld (hl-), a
bit 0,h
jr z, @sprites
ld hl, $81FF
xor a
@tiles
ld (hl-), a
bit 1,h
jr z, @tiles


tiles
ld hl, $8000
@tile0
xor a
ld (hl+), a
ld (hl+), a
cpl a
ld (hl+), a
ld (hl+), a
bit 4,l
jr z, @tile0
xor a
@tile1
cpl a
ld (hl+), a
ld (hl+), a
xor a
ld (hl+), a
ld (hl+), a
bit 4,l
jr nz, @tile1
@tile2
xor a
ld (hl+), a
cpl a
ld (hl+), a
bit 4,l
jr z, @tile2

ld hl, $fe00
ld a, 84 ; sprite y
ld (hl+), a
ld a, 84 ; sprite x
ld (hl+), a
ld a, 2  ; sprite tile
ld (hl+), a
ld a, $80 ; sprite flags
ld (hl+), a

ld a, 87 ; window x
ld ($ff4B), a
ld a, $e4 ; sprite palette
ld ($ff48), a

ld a, $F3 ; lcdc
ld ($ff40), a
@stop
jp @stop