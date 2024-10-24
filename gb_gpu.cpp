#define _SCL_SECURE_NO_WARNINGS 1

#include "gb_gpu.h"
#include "gb_cycle_scheduler.h"
#include "gb_memory_mapper.h"
#include "single_future.h"

#include <algorithm>
#include <immintrin.h>

namespace coro_gb
{
	gpu::gpu(cycle_scheduler& scheduler, memory_mapper& memory)
		: scheduler{ scheduler }
		, memory{ memory }
	{
		// vram
		memory.set_mapping({ 0x8000, 0x9FFF, vram.data(), vram.data() });

		//oam
		memory.set_mapping({ 0xFE00, 0xFEA0, (uint8_t*)oam.data(), (uint8_t*)oam.data() });

		// registers
		memory.set_mapping({ 0xFF40, 0xFF4B, [this](uint16_t address)->uint8_t { return on_register_read(address); }, [this](uint16_t address, uint8_t value) { on_register_write(address, value); } });
	}

	cycle_scheduler::awaitable_cycles gpu::cycles(cycle_scheduler::priority priority, uint32_t wait)
	{
		return scheduler.cycles(cycle_scheduler::unit::gpu, priority, wait);
	}

	cycle_scheduler::awaitable_cycles_interruptible gpu::interruptible_cycles(cycle_scheduler::priority priority, uint32_t wait)
	{
		return scheduler.interruptible_cycles(interrupts.lcd_enable, cycle_scheduler::unit::gpu, priority, wait);
	}

	void gpu::fifo_apply_bg(fifo_entry* fifo, uint8_t low_bits, uint8_t high_bits)
	{
		uint64_t out = _pdep_u64(low_bits, 0x0101010101010101) | _pdep_u64(high_bits, 0x0202020202020202);
		*(uint64_t*)fifo = _byteswap_uint64(out);
	}

	void gpu::fifo_apply_sprite_pixel(fifo_entry& fifo, sprite_attributes::flags_t flags, const fifo_entry sprite_pixel)
	{
		if (sprite_pixel.colour != 0 && fifo.type == 0 && (!flags.priority || fifo.colour == 0))
		{
			fifo = { sprite_pixel.colour, flags.palette, 1 };
		}
	}

	void gpu::fifo_apply_sprite(fifo_entry* fifo, uint8_t low_bits, uint8_t high_bits, sprite_attributes::flags_t flags)
	{
		fifo_entry sprite_fifo[8];
		uint64_t out = _pdep_u64(low_bits, 0x0101010101010101) | _pdep_u64(high_bits, 0x0202020202020202);
		if (!flags.flip_x)
		{
			out = _byteswap_uint64(out);
		}
		*(uint64_t*)sprite_fifo = out;

		fifo_apply_sprite_pixel(fifo[0], flags, sprite_fifo[0]);
		fifo_apply_sprite_pixel(fifo[1], flags, sprite_fifo[1]);
		fifo_apply_sprite_pixel(fifo[2], flags, sprite_fifo[2]);
		fifo_apply_sprite_pixel(fifo[3], flags, sprite_fifo[3]);
		fifo_apply_sprite_pixel(fifo[4], flags, sprite_fifo[4]);
		fifo_apply_sprite_pixel(fifo[5], flags, sprite_fifo[5]);
		fifo_apply_sprite_pixel(fifo[6], flags, sprite_fifo[6]);
		fifo_apply_sprite_pixel(fifo[7], flags, sprite_fifo[7]);
	}

	single_future<void> gpu::run()
	{
		dma_task = run_dma();

		struct gpu_tranform_fifo_to_output
		{
			gpu::registers_t::palette bg_palette;
			gpu::registers_t::palette sprite_palettes[2];

			uint8_t operator()(fifo_entry pixel) const
			{
				if (pixel.type == 0)
				{
					return (bg_palette.u8 >> (pixel.colour * 2)) & 0x03;
				}
				else //if (pixel.type == 1)
				{
					return ((pixel.palette << 2) + 0x4) | ((sprite_palettes[pixel.palette].u8 >> (pixel.colour * 2)) & 0x03);
				}
			}
		};

		while (true)
		{
			bool bLCDOnBug = false;
			if (!registers.lcd_control.lcd_enable)
			{
				stat_flag = false;
				vblank_flag = false;
				registers.lcd_y = 0;
				registers.lcd_stat.mode = lcd_mode::power_off;
				registers.lcd_stat.coincidence = false;
				interrupts.lcd_enable.reset();
				co_await interrupts.lcd_enable;
				bLCDOnBug = true;
			}

			uint8_t window_line = 0;
			bool window_triggered = false;

			for (uint8_t y = 0; y < 144; ++y)
			{
				uint32_t line_start = scheduler.get_cycle_counter();
				if (!bLCDOnBug)
				{ }
				else
				{
					line_start -= 8;
				}

				std::vector<sprite_attributes> sprites;
				uint8_t sprite_size = 0;
				if (!bLCDOnBug)
				{
					// sort sprites
					update_stat(lcd_mode::oam_search, y);

					sprite_size = registers.lcd_control.sprite_size ? 16 : 8;
					if (registers.lcd_control.sprite_enable)
					{
						for (sprite_attributes sprite : oam)
						{
							if (sprite.y - 16 <= y && sprite.y - 16 + sprite_size > y)
							{
								if (registers.lcd_control.sprite_size)
									sprite.tile_index &= 0xFE;
								sprites.push_back(sprite);
							}
						}

						if (sprites.size() > 10)
						{
							sprites.resize(10);
						}
						std::stable_sort(std::begin(sprites), std::end(sprites), [](const sprite_attributes& lhs, const sprite_attributes& rhs) { return lhs.x < rhs.x; });
					}

					co_await cycles(cycle_scheduler::priority::write, 80);
					sprite_size = registers.lcd_control.sprite_size ? 16 : 8;
				}
				else
				{
					// LCD On Bug
					update_stat(lcd_mode::initial_power_on, y);

					co_await cycles(cycle_scheduler::priority::write, 72);
					bLCDOnBug = false;
				}

				// draw line
				update_stat(lcd_mode::lcd_write, y);

				const uint16_t tiledata_base_addr_low = registers.lcd_control.tiledata_select ? 0x0000 : 0x1000;
				const uint16_t tiledata_base_addr_high = 0x0000;
				const uint16_t bg_tilemap_base_addr = registers.lcd_control.bg_tilemap_select ? 0x1C00 : 0x1800;
				const uint16_t spritedata_base_addr = 0x0000;
				window_triggered = (window_triggered || y == registers.window_y);
				const bool window_enable = registers.lcd_control.window_enable && window_triggered && registers.window_x < 167;
				const uint16_t window_tilemap_base_addr = registers.lcd_control.window_tilemap_select ? 0x1C00 : 0x1800;

				bool bg_enable = registers.lcd_control.bg_enable;
				uint8_t tile_x = registers.lcd_scroll_x / 8;
				uint8_t tile_y = (((uint16_t)y + registers.lcd_scroll_y) / 8) % 32;
				uint16_t sub_tile_y = ((uint16_t)y + registers.lcd_scroll_y) % 8;

				std::array<fifo_entry, 16> fifo; // 16 pixel FIFO
				std::fill_n(&fifo[0], 8, fifo_entry{ 0, 0 ,0 });
				uint8_t fifo_count = 8;

				if (bg_enable)
				{
					uint8_t tile_index = vram[bg_tilemap_base_addr + tile_y * 32 + tile_x];
					uint16_t tile_data_base_addr = (tile_index < 0x80 ? tiledata_base_addr_low : tiledata_base_addr_high);
					uint16_t tile_data_index = tile_data_base_addr + ((uint16_t)tile_index * 8 + sub_tile_y) * 2;
					uint8_t low_bits = vram[tile_data_index];
					uint8_t high_bits = vram[tile_data_index + 1];
					fifo_apply_bg(&fifo[8], low_bits, high_bits);

					fifo_count = 16;
					//co_await cycles(12); // ?
				}
				else
				{
					std::fill_n(&fifo[8], 8, fifo_entry{ 0, 0 ,0 });
					fifo_count = 16;
				}

				uint8_t subtile_scroll_x = registers.lcd_scroll_x % 8;
				std::copy_n(&fifo[subtile_scroll_x], fifo_count - subtile_scroll_x, &fifo[0]);
				fifo_count -= subtile_scroll_x;

				bool in_window = false;
				uint8_t window_x = -1;

				// discard first 8 pixels to allow the window to be at 0-6 position
				for (uint8_t x = 0; x < 8; )
				{
					uint8_t complete = std::min<uint8_t>(fifo_count - 8, 8 - x);
					if (window_enable && !in_window)
					{
						complete = std::min<uint8_t>(complete, registers.window_x - window_x);
					}
					//std::transform(&fifo[0], &fifo[complete], &screen[y * 160 + x], gpu_tranform_fifo_to_output{ registers.background_palette, memory.obj_palette[0], memory.obj_palette[1] });
					std::copy_n(&fifo[complete], fifo_count - complete, &fifo[0]);
					fifo_count -= complete;
					x += complete;
					window_x += complete;

					if (window_enable && !in_window && window_x == registers.window_x)
					{
						in_window = true;
						tile_y = (window_line / 8) % 32;
						sub_tile_y = window_line % 8;
						++window_line;

						{
							tile_x = 0;
							uint8_t tile_index = vram[window_tilemap_base_addr + tile_y * 32 + tile_x];
							uint16_t tile_data_base_addr = (tile_index < 0x80 ? tiledata_base_addr_low : tiledata_base_addr_high);
							uint16_t tile_data_index = tile_data_base_addr + ((uint16_t)tile_index * 8 + sub_tile_y) * 2;
							uint8_t low_bits = vram[tile_data_index];
							uint8_t high_bits = vram[tile_data_index + 1];
							fifo_apply_bg(&fifo[8], low_bits, high_bits);
							fifo_count = 16;
							//co_await cycles(6);
							tile_x = 1;
						}
					}
					else if (fifo_count == 8)
					{
						if (bg_enable)
						{
							uint8_t tile_index = vram[bg_tilemap_base_addr + tile_y * 32 + tile_x];
							uint16_t tile_data_base_addr = (tile_index < 0x80 ? tiledata_base_addr_low : tiledata_base_addr_high);
							uint16_t tile_data_index = tile_data_base_addr + ((uint16_t)tile_index * 8 + sub_tile_y) * 2;
							uint8_t low_bits = vram[tile_data_index];
							uint8_t high_bits = vram[tile_data_index + 1];
							fifo_apply_bg(&fifo[8], low_bits, high_bits);
							fifo_count = 16;
							//co_await cycles(6);
							tile_x = (tile_x + 1) % 32;
						}
						else
						{
							std::fill_n(&fifo[8], 8, fifo_entry{ 0, 0 ,0 });
							fifo_count = 16;
						}
					}
				}

				uint8_t current_sprite = 0;
				uint8_t sprite_x = 0;

				// discard first 8 pixels to allow sprites to "scroll on"
				for (uint8_t x = 0; x < 8; )
				{
					while (current_sprite < sprites.size() && sprites[current_sprite].x == sprite_x)
					{
						uint8_t sprite_suby = sprites[current_sprite].flags.flip_y ? sprite_size - 1 - (y - (sprites[current_sprite].y - 16)) : y - (sprites[current_sprite].y - 16);
						uint16_t tile_data_index = spritedata_base_addr + ((uint16_t)sprites[current_sprite].tile_index * 8 + sprite_suby) * 2;
						uint8_t low_bits = vram[tile_data_index];
						uint8_t high_bits = vram[tile_data_index + 1];

						fifo_apply_sprite(&fifo[0], low_bits, high_bits, sprites[current_sprite].flags);
						++current_sprite;
					}

					uint8_t complete = std::min<uint8_t>(fifo_count - 8, 8 - x);
					if (window_enable && !in_window)
					{
						complete = std::min<uint8_t>(complete, registers.window_x - window_x);
					}
					if (current_sprite < sprites.size())
					{
						complete = std::min<uint8_t>(complete, sprites[current_sprite].x - sprite_x);
					}
					//std::transform(&fifo[0], &fifo[complete], &screen[y * 160 + x], gpu_tranform_fifo_to_output{ registers.background_palette, memory.obj_palette[0], memory.obj_palette[1] });
					std::copy_n(&fifo[complete], fifo_count - complete, &fifo[0]);
					fifo_count -= complete;
					x += complete;
					window_x += complete;
					sprite_x += complete;

					if (window_enable && !in_window && window_x == registers.window_x)
					{
						in_window = true;
						tile_y = (window_line / 8) % 32;
						sub_tile_y = window_line % 8;
						++window_line;

						{
							tile_x = 0;
							uint8_t tile_index = vram[window_tilemap_base_addr + tile_y * 32 + tile_x];
							uint16_t tile_data_base_addr = (tile_index < 0x80 ? tiledata_base_addr_low : tiledata_base_addr_high);
							uint16_t tile_data_index = tile_data_base_addr + ((uint16_t)tile_index * 8 + sub_tile_y) * 2;
							uint8_t low_bits = vram[tile_data_index];
							uint8_t high_bits = vram[tile_data_index + 1];
							fifo_apply_bg(&fifo[8], low_bits, high_bits);
							fifo_count = 16;
							//co_await cycles(6);
							tile_x = 1;
						}
					}
					else if (fifo_count == 8)
					{
						if (in_window)
						{
							uint8_t tile_index = vram[window_tilemap_base_addr + tile_y * 32 + tile_x];
							uint16_t tile_data_base_addr = (tile_index < 0x80 ? tiledata_base_addr_low : tiledata_base_addr_high);
							uint16_t tile_data_index = tile_data_base_addr + ((uint16_t)tile_index * 8 + sub_tile_y) * 2;
							uint8_t low_bits = vram[tile_data_index];
							uint8_t high_bits = vram[tile_data_index + 1];
							fifo_apply_bg(&fifo[8], low_bits, high_bits);
							fifo_count = 16;
							//co_await cycles(6);
							tile_x = (tile_x + 1) % 32;
						}
						else if (bg_enable)
						{
							uint8_t tile_index = vram[bg_tilemap_base_addr + tile_y * 32 + tile_x];
							uint16_t tile_data_base_addr = (tile_index < 0x80 ? tiledata_base_addr_low : tiledata_base_addr_high);
							uint16_t tile_data_index = tile_data_base_addr + ((uint16_t)tile_index * 8 + sub_tile_y) * 2;
							uint8_t low_bits = vram[tile_data_index];
							uint8_t high_bits = vram[tile_data_index + 1];
							fifo_apply_bg(&fifo[8], low_bits, high_bits);
							fifo_count = 16;
							//co_await cycles(6);
							tile_x = (tile_x + 1) % 32;
						}
						else
						{
							std::fill_n(&fifo[8], 8, fifo_entry{ 0, 0 ,0 });
							fifo_count = 16;
						}
					}
				}

				// draw 160 pixels
				uint8_t x = 0;
				for (; x < 160; )
				{
					while (current_sprite < sprites.size() && sprites[current_sprite].x == sprite_x)
					{
						uint8_t sprite_suby = sprites[current_sprite].flags.flip_y ? sprite_size - 1 - (y - (sprites[current_sprite].y - 16)) : y - (sprites[current_sprite].y - 16);
						uint16_t tile_data_index = spritedata_base_addr + ((uint16_t)sprites[current_sprite].tile_index * 8 + sprite_suby) * 2;
						uint8_t low_bits = vram[tile_data_index];
						uint8_t high_bits = vram[tile_data_index + 1];

						fifo_apply_sprite(&fifo[0], low_bits, high_bits, sprites[current_sprite].flags);
						++current_sprite;
					}

					uint8_t complete = std::min<uint8_t>(fifo_count - 8, 160 - x);
					if (window_enable && !in_window)
					{
						complete = std::min<uint8_t>(complete, registers.window_x - window_x);
					}
					if (current_sprite < sprites.size())
					{
						complete = std::min<uint8_t>(complete, sprites[current_sprite].x - sprite_x);
					}
					std::transform(&fifo[0], &fifo[complete], &screen[y * 160 + x], gpu_tranform_fifo_to_output{ registers.background_palette, { registers.obj_palette[0], registers.obj_palette[1] } });
					std::copy_n(&fifo[complete], fifo_count - complete, &fifo[0]);
					fifo_count -= complete;
					x += complete;
					window_x += complete;
					sprite_x += complete;

					if (window_enable && !in_window && window_x == registers.window_x)
					{
						in_window = true;
						tile_y = (window_line / 8) % 32;
						sub_tile_y = window_line % 8;
						++window_line;

						{
							tile_x = 0;
							uint8_t tile_index = vram[window_tilemap_base_addr + tile_y * 32 + tile_x];
							uint16_t tile_data_base_addr = (tile_index < 0x80 ? tiledata_base_addr_low : tiledata_base_addr_high);
							uint16_t tile_data_index = tile_data_base_addr + ((uint16_t)tile_index * 8 + sub_tile_y) * 2;
							uint8_t low_bits = vram[tile_data_index];
							uint8_t high_bits = vram[tile_data_index + 1];
							fifo_apply_bg(&fifo[8], low_bits, high_bits);
							fifo_count = 16;
							//co_await cycles(6);
							tile_x = 1;
						}
					}
					else if (fifo_count == 8)
					{
						if (in_window)
						{
							uint8_t tile_index = vram[window_tilemap_base_addr + tile_y * 32 + tile_x];
							uint16_t tile_data_base_addr = (tile_index < 0x80 ? tiledata_base_addr_low : tiledata_base_addr_high);
							uint16_t tile_data_index = tile_data_base_addr + ((uint16_t)tile_index * 8 + sub_tile_y) * 2;
							uint8_t low_bits = vram[tile_data_index];
							uint8_t high_bits = vram[tile_data_index + 1];
							fifo_apply_bg(&fifo[8], low_bits, high_bits);
							fifo_count = 16;
							//co_await cycles(6);
							tile_x = (tile_x + 1) % 32;
						}
						else if (bg_enable)
						{
							uint8_t tile_index = vram[bg_tilemap_base_addr + tile_y * 32 + tile_x];
							uint16_t tile_data_base_addr = (tile_index < 0x80 ? tiledata_base_addr_low : tiledata_base_addr_high);
							uint16_t tile_data_index = tile_data_base_addr + ((uint16_t)tile_index * 8 + sub_tile_y) * 2;
							uint8_t low_bits = vram[tile_data_index];
							uint8_t high_bits = vram[tile_data_index + 1];
							fifo_apply_bg(&fifo[8], low_bits, high_bits);
							fifo_count = 16;
							//co_await cycles(6);
							tile_x = (tile_x + 1) % 32;
						}
						else
						{
							std::fill_n(&fifo[8], 8, fifo_entry{ 0, 0 ,0 });
							fifo_count = 16;
						}
					}
				}

				co_await cycles(cycle_scheduler::priority::write, 174); //? Geikko says this should be 173.5

				// h blank
				update_stat(lcd_mode::h_blank, y);

				co_await cycles(cycle_scheduler::priority::write, (line_start + 456) - scheduler.get_cycle_counter());
			}

			display_callback();

			//v blank
			update_stat(lcd_mode::v_blank, 144);

			co_await interruptible_cycles(cycle_scheduler::priority::write, 456);
			if (!registers.lcd_control.lcd_enable)
			{
				continue;
			}

			for (uint8_t y = 145; y < 153; ++y)
			{
				update_stat(lcd_mode::v_blank, y);

				co_await interruptible_cycles(cycle_scheduler::priority::write, 456);
				if (!registers.lcd_control.lcd_enable)
				{
					break;
				}
			}

			if (!registers.lcd_control.lcd_enable)
			{
				continue;
			}

			// line 153 is weird
			update_stat(lcd_mode::v_blank, 153);
			co_await interruptible_cycles(cycle_scheduler::priority::write, 4);
			if (!registers.lcd_control.lcd_enable)
			{
				continue;
			}

			registers.lcd_y = 0;
			co_await interruptible_cycles(cycle_scheduler::priority::write, 4);
			if (!registers.lcd_control.lcd_enable)
			{
				continue;
			}

			registers.lcd_stat.coincidence = 0;
			update_stat(lcd_mode::v_blank, 0);
			co_await interruptible_cycles(cycle_scheduler::priority::write, 456-8);
		}
	}

	uint8_t gpu::on_register_read(uint16_t address) const
	{
		if (address == 0xFF40)
		{
			return registers.lcd_control.u8;
		}
		else if (address == 0xFF41)
		{
			return registers.lcd_stat.u8;
		}
		else if (address == 0xFF42)
		{
			return registers.lcd_scroll_y;
		}
		else if (address == 0xFF43)
		{
			return registers.lcd_scroll_x;
		}
		else if (address == 0xFF44)
		{
			return registers.lcd_y;
		}
		else if (address == 0xFF45)
		{
			return registers.lcd_yc;
		}
		else if (address == 0xFF46)
		{
			return registers.dma_start;
		}
		else if (address == 0xFF47)
		{
			return registers.background_palette.u8;
		}
		else if (address <= 0xFF49)
		{
			return registers.obj_palette[address - 0xFF48].u8;
		}
		else if (address == 0xFF4A)
		{
			return registers.window_y;
		}
		else if (address == 0xFF4B)
		{
			return registers.window_x;
		}

		throw std::runtime_error("No such gpu register");
	}

	void gpu::on_register_write(uint16_t address, uint8_t u8)
	{
		if (address == 0xFF40)
		{
			bool old_lcdc_lcd_enable = registers.lcd_control.lcd_enable;
			registers.lcd_control.u8 = u8;
			if (registers.lcd_control.lcd_enable != old_lcdc_lcd_enable)
			{
				interrupts.lcd_enable.trigger();
			}
			return;
		}
		else if (address == 0xFF41)
		{
			registers.lcd_stat.u8 = 0x80 | (registers.lcd_stat.u8 & 0x07) | (0xFF & 0x78); // LCD Stat write bug - briefly enables all interrupts!
			update_interrupt_flags(registers.lcd_stat.mode);
			registers.lcd_stat.u8 = 0x80 | (registers.lcd_stat.u8 & 0x07) | (u8 & 0x78);
			update_interrupt_flags(registers.lcd_stat.mode);
			return;
		}
		else if (address == 0xFF42)
		{
			registers.lcd_scroll_y = u8;
			return;
		}
		else if (address == 0xFF43)
		{
			registers.lcd_scroll_x = u8;
			return;
		}
		else if (address == 0xFF44)
		{
			//registers.lcd_y = 0; // Some documentation claims writing to this resets lcd_y to 0, but that's probably innaccurate and this is probably a read-only register
			return;
		}
		else if (address == 0xFF45)
		{
			registers.lcd_yc = u8;
			return;
		}
		else if (address == 0xFF46)
		{
			registers.dma_start = u8;
			interrupts.dma_trigger.trigger();
			return;
		}
		else if (address == 0xFF47)
		{
			registers.background_palette.u8 = u8;
			return;
		}
		else if (address <= 0xFF49)
		{
			registers.obj_palette[address - 0xFF48].u8 = u8;
			return;
		}
		else if (address == 0xFF4A)
		{
			registers.window_y = u8;
			return;
		}
		else if (address == 0xFF4B)
		{
			registers.window_x = u8;
			return;
		}

		throw std::runtime_error("No such gpu register");
	}

	void gpu::update_interrupt_flags(lcd_mode mode)
	{
		bool old_stat_flag = stat_flag;
		stat_flag = false;

		if (mode == lcd_mode::h_blank && registers.lcd_stat.hblank_ienable)
		{
			stat_flag = true;
		}
		// should we be triggering an oam interrupt at the start of vblank? Sources vary on this
		else if (mode == lcd_mode::v_blank && registers.lcd_stat.vblank_ienable)
		{
			stat_flag = true;
		}
		else if (mode == lcd_mode::oam_search && registers.lcd_stat.oam_ienable)
		{
			stat_flag = true;
		}
		else if (registers.lcd_stat.coincidence && registers.lcd_stat.coincidence_ienable)
		{
			stat_flag = true;
		}

		bool old_vblank_flag = vblank_flag;
		vblank_flag = false;
		if (mode == lcd_mode::v_blank)
		{
			vblank_flag = true;
		}

		const bool trigger_stat = !old_stat_flag && stat_flag;
		const bool trigger_vblank = !memory.interrupt_flag.vblank && !old_vblank_flag && vblank_flag;
		if (trigger_stat)
		{
			memory.interrupt_flag.stat = true;
		}
		if (trigger_vblank)
		{
			memory.interrupt_flag.vblank = true;
		}

		if (trigger_stat || trigger_vblank)
		{
			// wake CPU if we just triggered an enabled interrupt
			memory_mapper::interrupt_bits_t pending_interrupts = (memory.interrupt_flag & memory.interrupt_enable);
			if ((pending_interrupts.u8 & 0x1F) != 0)
			{
				memory.interrupts.cpu_wake.trigger();
			}
		}
	}

	void gpu::update_stat(lcd_mode mode, uint8_t y)
	{
		if (registers.lcd_y != y)
		{
			registers.lcd_y = y;
			registers.lcd_stat.coincidence = 0;
		}
		switch (mode)
		{
		case lcd_mode::power_off:
		case lcd_mode::initial_power_on:
		case lcd_mode::h_blank:
			memory.set_mapping({ 0xFE00, 0xFEA0, (uint8_t*) oam.data(), (uint8_t*) oam.data() }); // restore access to oam
			memory.set_mapping({ 0x8000, 0x9FFF, vram.data(), vram.data() });                     // restore access to vram
			break;
		case lcd_mode::v_blank:
			break;
		case lcd_mode::oam_search:
			//memory.set_mapping({ 0xFE00, 0xFEA0, nullptr, nullptr }); // block access to oam
			break;
		case lcd_mode::lcd_write:
			//memory.set_mapping({ 0x8000, 0x9FFF, nullptr, nullptr }); // block access to vram
			break;
		}
		update_interrupt_flags(mode);
		scheduler.queue(scheduler.get_cycle_counter() + 4, cycle_scheduler::unit::gpu, cycle_scheduler::priority::write,
			[this, mode]() {
				registers.lcd_stat.mode = mode; // truncates to 2 bits
				if (mode == lcd_mode::h_blank || mode == lcd_mode::v_blank || mode == lcd_mode::oam_search || mode == lcd_mode::initial_power_on)
				{
					registers.lcd_stat.coincidence = (registers.lcd_yc == registers.lcd_y);
					update_interrupt_flags(mode);
				}
			});
	}

	single_future<void> gpu::run_dma()
	{
		while (true)
		{
			interrupts.dma_trigger.reset();
			co_await interrupts.dma_trigger;

			bool was_interrupted = false;
			uint8_t shadow_dma_start;
			do
			{
				co_await scheduler.cycles(cycle_scheduler::unit::dma, cycle_scheduler::priority::write, 8);

				shadow_dma_start = registers.dma_start;
				if (shadow_dma_start >= 0xE0)
				{
					// trying to DMA from 0xE000-0xFFFF will actually read from 0xC000-0xDFFF (wram mirroring)
					// DMA'ing from 0xFE00 will actually read from 0xDE00 not OAM!
					shadow_dma_start -= 0x20;
				}

				// block access to oam
				memory.set_mapping({ 0xFE00, 0xFEA0, nullptr, nullptr });

				was_interrupted = co_await scheduler.interruptible_cycles(interrupts.dma_trigger, cycle_scheduler::unit::dma, cycle_scheduler::priority::write, 640);
			} while (was_interrupted);

			// perform DMA copy
			for (uint8_t offset = 0; offset < 0xA0; ++offset)
			{
				((uint8_t*)&oam[0])[offset] = memory.read8(shadow_dma_start * 0x100 + offset);
			}

			// restore access to oam
			memory.set_mapping({ 0xFE00, 0xFEA0, (uint8_t*)oam.data(), (uint8_t*)oam.data() });
		};
	}
}
