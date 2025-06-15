# CoroGB

CoroGB is an experimental gameboy emulator written in C++ w/ coroutines

It is primarily developed for personal advancement, but is hopefully of interest.

## Note

A boot rom must be provided! It should be named dmg_rom.bin and placed in the executable's directory (project root also works when running from Visual Studio)

## Controls

Up / Down / Left / Right / Z / X / RShift / Enter

Numpad+ is speed up

## Tests

### Blargg's tests

| Test         | mooneye-gb | CoroGB |
|--------------|------------|--------|
| cpu instrs   | :+1:       | :o:    |
| dmg sound 2  | :x:        | :x:    |
| instr timing | :+1:       | :x:    |
| mem timing 2 | :+1:       | :x:    |
| oam bug 2    | :x:        | :x:    |
| halt bug     |            | :+1:   |

Notes:

* cpu_instrs #2 fails on CoroGB due to not yet having implemented the timer
* sound is unimplemented in CoroGB

### Mooneye GB acceptance tests

| Test                    | mooneye-gb | CoroGB |
|-------------------------|------------|--------|
| add sp e timing         | :+1:       | :+1:   |
| boot div dmgABCmgb      | :x:        | :x:    |
| boot hwio dmgABCmgb     | :x:        | :x:    |
| boot regs dmgABC        | :+1:       | :+1:   |
| call timing             | :+1:       | :+1:   |
| call timing2            | :+1:       | :+1:   |
| call cc_timing          | :+1:       | :+1:   |
| call cc_timing2         | :+1:       | :+1:   |
| di timing GS            | :+1:       | :+1:   |
| div timing              | :+1:       | :+1:   |
| ei sequence             | :+1:       | :+1:   |
| ei timing               | :+1:       | :+1:   |
| halt ime0 ei            | :+1:       | :+1:   |
| halt ime0 nointr_timing | :+1:       | :+1:   |
| halt ime1 timing        | :+1:       | :x:    |
| halt ime1 timing2 GS    | :+1:       | :+1:   |
| if ie registers         | :+1:       | :+1:   |
| intr timing             | :+1:       | :+1:   |
| jp timing               | :+1:       | :+1:   |
| jp cc timing            | :+1:       | :+1:   |
| ld hl sp e timing       | :+1:       | :+1:   |
| oam dma_restart         | :+1:       | :+1:   |
| oam dma start           | :+1:       | :+1:   |
| oam dma timing          | :+1:       | :+1:   |
| pop timing              | :+1:       | :+1:   |
| push timing             | :+1:       | :+1:   |
| rapid di ei             | :+1:       | :+1:   |
| ret timing              | :+1:       | :+1:   |
| ret cc timing           | :+1:       | :+1:   |
| reti timing             | :+1:       | :+1:   |
| reti intr timing        | :+1:       | :+1:   |
| rst timing              | :+1:       | :+1:   |

Notes:

* boot_hwio-dmgABCmgb needs sound emulation
* halt_ime1_timing needs timer

#### Bits (unusable bits in memory and registers)

| Test           | mooneye-gb | CoroGB |
|----------------|------------|--------|
| mem oam        | :+1:       | :+1:   |
| reg f          | :+1:       | :+1:   |
| unused_hwio GS | :+1:       | :+1:   |

#### Instructions

| Test | mooneye-gb | CoroGB |
|------|------------|--------|
| daa  | :+1:       | :+1:   |

#### Interrupt handling

| Test    | mooneye-gb | CoroGB |
|---------|------------|--------|
| ie push | :+1:       | :+1:   |

#### OAM DMA

| Test               | mooneye-gb | CoroGB |
|--------------------|------------|--------|
| basic              | :+1:       | :+1:   |
| reg_read           | :+1:       | :+1:   |
| sources dmgABCmgbS | :+1:       | :+1:   |

#### PPU

| Test                        | mooneye-gb | CoroGB |
|-----------------------------|------------|--------|
| hblank ly scx timing GS     | :+1:       | :x:    |
| intr 1 2 timing GS          | :+1:       | :+1:   |
| intr 2 0 timing             | :+1:       | :x:    |
| intr 2 mode0 timing         | :+1:       | :+1:   |
| intr 2 mode3 timing         | :+1:       | :+1:   |
| intr 2 oam ok timing        | :+1:       | :x:    |
| intr 2 mode0 timing sprites | :x:        | :x:    |
| lcdon timing dmgABCmgbS     | :x:        | :x:    |
| lcdon write timing GS       | :x:        | :x:    |
| stat irq blocking           | :x:        | :+1:   |
| stat lyc onoff              | :x:        | :x:    |
| vblank stat intr GS         | :+1:       | :x:    |

Notes:

* Yes, CoroGB implements stat IRQ blocking!

#### Serial

| Test                      | mooneye-gb | CoroGB |
|---------------------------|------------|--------|
| boot sclk align dmgABCmgb | :x:        | :x:    |

Notes:

* Serial clock not implemented

#### Timer

| Test                 | mooneye-gb | CoroGB |
|----------------------|------------|--------|
| div write            | :+1:       | :x:    |
| rapid toggle         | :+1:       | :x:    |
| tim00 div trigger    | :+1:       | :x:    |
| tim00                | :+1:       | :x:    |
| tim01 div trigger    | :+1:       | :x:    |
| tim01                | :+1:       | :x:    |
| tim10 div trigger    | :+1:       | :x:    |
| tim10                | :+1:       | :x:    |
| tim11 div trigger    | :+1:       | :x:    |
| tim11                | :+1:       | :x:    |
| tima reload          | :+1:       | :x:    |
| tima write reloading | :+1:       | :x:    |
| tma write reloading  | :+1:       | :x:    |

Notes:

* Timer not implemented

### Mooneye GB emulator-only tests

#### MBC1

| Test              | mooneye-gb | CoroGB |
|-------------------|------------|--------|
| bits bank1        | :+1:       | :+1:   |
| bits bank2        | :+1:       | :+1:   |
| bits mode         | :+1:       | :+1:   |
| bits ramg         | :+1:       | :+1:   |
| rom 512Kb         | :+1:       | :+1:   |
| rom 1Mb           | :+1:       | :+1:   |
| rom 2Mb           | :+1:       | :+1:   |
| rom 4Mb           | :+1:       | :+1:   |
| rom 8Mb           | :+1:       | :+1:   |
| rom 16Mb          | :+1:       | :+1:   |
| ram 64Kb          | :+1:       | :+1:   |
| ram 256Kb         | :+1:       | :+1:   |
| multicart rom 8Mb | :+1:       | :+1:   |

Notes:

* Yes, CoroGB supports MBC1 multicart roms!

#### MBC2

| Test        | mooneye-gb | CoroGB |
|-------------|------------|--------|
| bits ramg   | :+1:       | :+1:   |
| bits romb   | :+1:       | :+1:   |
| bits unused | :+1:       | :+1:   |
| rom 512kb   | :+1:       | :+1:   |
| rom 1Mb     | :+1:       | :+1:   |
| rom 2Mb     | :+1:       | :+1:   |
| ram         | :+1:       | :x:    |

Notes:

* Ram test fails due to not implementing cart ram mirror and not restricting cart ram to 4 bits

#### MBC5

| Test      | mooneye-gb |
|-----------|------------|
| rom 512kb | :+1:       |
| rom 1Mb   | :+1:       |
| rom 2Mb   | :+1:       |
| rom 4Mb   | :+1:       |
| rom 8Mb   | :+1:       |
| rom 16Mb  | :+1:       |
| rom 32Mb  | :+1:       |
| rom 64Mb  | :+1:       |

### Mooneye GB manual tests

| Test            | mooneye-gb | CoroGB |
|-----------------|------------|--------|
| sprite priority | :+1:       | :+1:   |

### Mealybug tests

| Test                              | CoroGB |
|-----------------------------------|--------|
| m2_win_en_toggle                  | :+1:   |
| m3_bgp_change                     | :x:    |
| m3_bgp_change_sprites             | :x:    |
| m3_lcdc_bg_en_change              | :x:    |
| m3_lcdc_bg_en_change2             | :x:    |
| m3_lcdc_bg_map_change             | :x:    |
| m3_lcdc_bg_map_change2            | :x:    |
| m3_lcdc_obj_en_change             | :x:    |
| m3_lcdc_obj_en_change_variant     | :x:    |
| m3_lcdc_obj_size_change           | :x:    |
| m3_lcdc_obj_size_change_scx       | :x:    |
| m3_lcdc_tile_sel_change           | :x:    |
| m3_lcdc_tile_sel_change2          | :x:    |
| m3_lcdc_tile_sel_win_change       | :x:    |
| m3_lcdc_tile_sel_win_change2      | :x:    |
| m3_lcdc_win_en_change_multiple    | :x:    |
| m3_lcdc_win_en_change_multiple_wx | :x:    |
| m3_lcdc_win_map_change            | :x:    |
| m3_lcdc_win_map_change2           | :x:    |
| m3_obp0_change                    | :x:    |
| m3_scx_high_5_bits                | :x:    |
| m3_scx_high_5_bits_change2        | :x:    |
| m3_scx_low_3_bits                 | :x:    |
| m3_scy_change                     | :x:    |
| m3_scy_change2                    | :x:    |
| m3_window_timing                  | :x:    |
| m3_window_timing_wx_0             | :x:    |
| m3_wx_4_change                    | :x:    |
| m3_wx_4_change_sprites            | :x:    |
| m3_wx_5_change                    | :x:    |
| m3_wx_6_change                    | :x:    |

### Other tests

| Test             | CoroGB |
|------------------|--------|
| acid2            | :+1:   |
| dycptest2        | :x:    |
| lyc              | :+1:   |
| opus5            | :+1:   |
| sprite_test_01   | :+1:   |
| wx_split         | :+1:   |
| windows_overlap  | :+1:   |
| window_y_trigger | :+1:   |
