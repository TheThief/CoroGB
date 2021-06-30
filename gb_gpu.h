#pragma once

#include "gb_cycle_scheduler.h"
#include "gb_interrupt.h"
#include "single_future.h"

#include <array>
#include <cstdint>
#include <functional>

namespace coro_gb
{
	struct memory_mapper;

	struct gpu final
	{
		gpu(cycle_scheduler& scheduler, memory_mapper& memory);

		single_future<void> run();

		void set_display_callback(std::function<void()> display_callback);

		bool is_screen_enabled() const;
		const uint8_t* get_screen_buffer() const;

	protected:
		cycle_scheduler::awaitable_cycles cycles(cycle_scheduler::priority priority, uint32_t wait);
		cycle_scheduler::awaitable_cycles_interruptible interruptible_cycles(cycle_scheduler::priority priority, uint32_t wait);

		struct sprite_attributes
		{
			uint8_t y;
			uint8_t x;
			uint8_t tile_index;
			union flags_t
			{
				uint8_t u8 = 0x0F;
				struct
				{
					uint8_t : 4;
					uint8_t palette  : 1;
					uint8_t flip_x   : 1;
					uint8_t flip_y   : 1;
					uint8_t priority : 1;
				};
			} flags;
		};

		struct fifo_entry
		{
			uint8_t colour : 2; // colour in palette
			uint8_t palette : 1; // sprites only, selects sprite palette
			uint8_t type : 1; // 0 = bg, 1 = sprite
		};

		static void fifo_apply_bg(fifo_entry* fifo, uint8_t low_bits, uint8_t high_bits);
		static void fifo_apply_sprite_pixel(fifo_entry& fifo, sprite_attributes::flags_t flags, fifo_entry sprite_pixel);
		static void fifo_apply_sprite(fifo_entry* fifo, uint8_t low_bits, uint8_t high_bits, sprite_attributes::flags_t flags);

		uint8_t on_register_read(uint16_t address) const;
		void on_register_write(uint16_t address, uint8_t value);

		enum class lcd_mode : uint8_t
		{
			// Mode 0 : The LCD controller is in the H - Blank period
			// the CPU can access both the display RAM(8000h - 9FFFh) and OAM(FE00h - FE9Fh)
			h_blank = 0x00,
			// Mode 1 : The LCD contoller is in the V - Blank period (or the display is disabled)
			// the CPU can access both the display RAM(8000h - 9FFFh) and OAM(FE00h - FE9Fh)
			v_blank = 0x01,
			// Mode 2 : The LCD controller is reading from OAM memory.
			// The CPU <cannot> access OAM memory(FE00h - FE9Fh) during this period.
			oam_search = 0x02,
			// Mode 3 : Transfering Data to LCD Driver.
			// The LCD controller is reading from both OAM and VRAM,
			// The CPU <cannot> access OAM and VRAM during this period.
			// CGB Mode : Cannot access Palette Data(FF69, FF6B) either.
			lcd_write = 0x03,

			initial_power_on = 0x80,
			power_off = 0xF0,

			// The following are typical when the display is enabled :
			// Mode 2  2_____2_____2_____2_____2_____2___________________2____
			// Mode 3  _33____33____33____33____33____33__________________3___
			// Mode 0  ___000___000___000___000___000___000________________000
			// Mode 1  ____________________________________11111111111111_____
		};

		bool stat_flag = false;
		bool vblank_flag = false;
		void update_interrupt_flags(lcd_mode mode);
		void update_stat(lcd_mode mode, uint8_t y);

		single_future<void> run_dma();

		cycle_scheduler& scheduler;
		memory_mapper& memory;

		std::function<void()> display_callback;

		std::array<uint8_t, 160 * 144> screen;

		single_future<void> dma_task;

		// LCD Interrupts:
		struct interrupts_t
		{
			interrupt lcd_enable;
			interrupt dma_trigger;
		};
		interrupts_t interrupts;

		// LCD VRAM
		std::array<std::uint8_t, 8192> vram;
		std::array<sprite_attributes, 40> oam;

		// LCD Registers:
		struct registers_t
		{
			// 0xFF40 - LCDC - LCD Control
			union lcd_control_t
			{
				uint8_t u8 = 0x0;
				struct
				{
					uint8_t bg_enable : 1; // Bit 0 - BG Display (0 = Off, 1 = On)
					uint8_t sprite_enable : 1; // Bit 1 - OBJ(Sprite) Display Enable(0 = Off, 1 = On)
					uint8_t sprite_size : 1; // Bit 2 - OBJ(Sprite) Size(0 = 8x8, 1 = 8x16)
					uint8_t bg_tilemap_select : 1; // Bit 3 - BG Tile Map Display Select(0 = 9800 - 9BFF, 1 = 9C00 - 9FFF)
					uint8_t tiledata_select : 1; // Bit 4 - BG & Window Tile Data Select(0 = 8800 - 97FF, 1 = 8000 - 8FFF)
					uint8_t window_enable : 1; // Bit 5 - Window Display Enable(0 = Off, 1 = On)
					uint8_t window_tilemap_select : 1; // Bit 6 - Window Tile Map Display Select(0 = 9800 - 9BFF, 1 = 9C00 - 9FFF)
					uint8_t lcd_enable : 1; // Bit 7 - LCD Display Enable(0 = Off, 1 = On)
				};
			} lcd_control;

			// 0xFF41 - STAT - LCDC Status (R/W)
			union lcd_stat_t
			{
				uint8_t u8 = 0x80;
				struct
				{
					lcd_mode mode : 2; // Bits 0:1 - Mode Flag (Read Only)
					uint8_t coincidence : 1; // Bit 2 - Coincidence Flag(0:LYC<>LY, 1 : LYC = LY) (Read Only)
					uint8_t hblank_ienable : 1; // Bit 3 - Mode 0 H - Blank Interrupt(1 = Enable) (Read / Write)
					uint8_t vblank_ienable : 1; // Bit 4 - Mode 1 V - Blank Interrupt(1 = Enable) (Read / Write)
					uint8_t oam_ienable : 1; // Bit 5 - Mode 2 OAM Interrupt(1 = Enable) (Read / Write)
					uint8_t coincidence_ienable : 1; // Bit 6 - LYC = LY Coincidence Interrupt(1 = Enable) (Read / Write)
				};
			} lcd_stat;

			// Specifies the position in the 256x256 pixels BG map(32x32 tiles) which is to be displayed at the upper / left LCD display position.
			// 0xFF42 - SCY - Scroll Y
			uint8_t lcd_scroll_y = 0;
			// 0xFF43 - SCX - Scroll X
			uint8_t lcd_scroll_x = 0;
			// 0xFF44 - LY - LCD Y-Coordinate
			uint8_t lcd_y;
			// 0xFF45 - LYC - LY Compare
			uint8_t lcd_yc;
			// 0xFF46 - DMA - DMA Transfer and Start Address
			uint8_t dma_start;
			// 0xFF47 - BG Palette Data
			union palette
			{
				uint8_t u8;
				struct
				{
					uint8_t _0 : 2;
					uint8_t _1 : 2;
					uint8_t _2 : 2;
					uint8_t _3 : 2;
				};
			};
			palette background_palette;
			// 0xFF48-0xFF49 - Obj Palette 0/1 Data
			palette obj_palette[2];
			// 0xFF4A - Window Y Pos
			uint8_t window_y;
			// 0xFF4B - Window X Pos
			uint8_t window_x;
		};
		registers_t registers;
	};

	inline void gpu::set_display_callback(std::function<void()> new_display_callback)
	{
		display_callback = std::move(new_display_callback);
	}

	inline bool gpu::is_screen_enabled() const
	{
		return registers.lcd_control.lcd_enable;
	}

	inline const uint8_t* gpu::get_screen_buffer() const
	{
		return screen.data();
	}
}
