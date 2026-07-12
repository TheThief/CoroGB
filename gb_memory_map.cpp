#define _SCL_SECURE_NO_WARNINGS 1

#include "gb_memory_map.h"
#include "gb_cycle_scheduler.h"

#include <algorithm>
#include <fstream>
#include <iostream>

namespace coro_gb
{
	memory_map::memory_map(cycle_scheduler& scheduler)
		: scheduler{ scheduler }
	{
		set_mapping(memory_region::wram0, { 0x0FFF, wram.data(), wram.data() });
		set_mapping(memory_region::wram1, { 0x0FFF, wram.data()+0x1000, wram.data()+0x1000 });
	}

	uint8_t memory_map::read8(uint16_t address) const
	{
		if (!boot_rom_disable && address < 0x100)
		{
			return boot_rom[address];
		}

		if (address < 0xFE00)
		{
			return read8_main(address);
		}
		else if (address < 0xFF00)
		{
			return read8_mapping(memory_region::oam, address);
		}
		else
		{
			return read8_mmio(address - 0xFF00);
		}
	}

	uint8_t memory_map::read8_main(uint16_t address) const
	{
		switch (address & 0xF000)
		{
		case 0x0000:
		case 0x1000:
		case 0x2000:
		case 0x3000:
			return read8_mapping(memory_region::rom0, address);
		case 0x4000:
		case 0x5000:
		case 0x6000:
		case 0x7000:
			return read8_mapping(memory_region::rom1, address);
		case 0x8000:
		case 0x9000:
			return read8_mapping(memory_region::vram, address);
		case 0xA000:
		case 0xB000:
			return read8_mapping(memory_region::sram, address);
		case 0xC000:
		case 0xE000:
			return read8_mapping(memory_region::wram0, address);
		case 0xD000:
		case 0xF000:
			return read8_mapping(memory_region::wram1, address);
		default:
			std::unreachable();
		}
	}

	uint8_t memory_map::read8_mapping(memory_region region, uint16_t address) const
	{
		const mapping& mapping = mappings[(size_t)region];

		if (std::holds_alternative<uint8_t*>(mapping.read))
		{
			uint8_t* data = std::get<uint8_t*>(mapping.read);
			if (data)
			{
				return data[address & mapping.mask];
			}
			else
			{
				return 0xFF;
			}
		}
		else
		{
			return std::get<1>(mapping.read)(address);
		}
	}

	uint8_t memory_map::read8_mmio(uint8_t mmio_address) const
	{
		const uint16_t address = 0xFF00 | mmio_address; // purely for ease of programming
		if (address <= 0xFF7F) // known mapped registers
		{
			if (address == 0xFF00)
			{
				return joypad.u8;
			}
			else if (address == 0xFF01)
			{
				// todo - serial port not implemented
				return serial_data;
			}
			else if (address == 0xFF02)
			{
				// todo - serial port not implemented
				return serial_control.u8;
			}
			else if (address == 0xFF03)
			{
				// nothing mapped
			}
			else if (address == 0xFF04)
			{
				timer_div = (uint16_t)(scheduler.get_cycle_counter() - timer_div_reset) >> 8;
				return timer_div;
			}
			else if (address == 0xFF05)
			{
				// todo - timer not implemented
				return timer_counter;
			}
			else if (address == 0xFF06)
			{
				// todo - timer not implemented
				return timer_reset_value;
			}
			else if (address == 0xFF07)
			{
				// todo - timer not implemented
				return timer_control.u8;
			}
			else if (address <= 0xFF0E)
			{
				// nothing mapped
			}
			else if (address == 0xFF0F)
			{
				return interrupt_flag.u8;
			}
			else if (address <= 0xFF23)
			{
				return audio_registers[address - 0xFF10];
			}
			else if (address <= 0xFF26)
			{
				return audio_control[address - 0xFF24];
			}
			else if (address <= 0xFF2F)
			{
				// nothing mapped
			}
			else if (address <= 0xFF3F)
			{
				return audio_wave[address - 0xFF30];
			}
			else if (address <= 0xFF4B)
			{
				return read8_mapping(memory_region::ppu_registers, address);
			}
			// else if (address == 0xFF50) // not readable
			// else if (address <= 0xFF7F) // nothing mapped
		}
		else if (address <= 0xFFFE)
		{
			return hram[address - 0xFF80];
		}
		else if (address == 0xFFFF)
		{
			return interrupt_enable.u8;
		}
		return 0xFF;
	}

	void memory_map::write8(uint16_t address, uint8_t u8)
	{
		// I honestly don't know what happens if you glitch the cpu into this state
		// but it can't happen in normal operation
		// if (!boot_rom_disable && address < 0x100)

		if (address < 0xFE00)
		{
			write8_main(address, u8);
		}
		else if (address < 0xFF00)
		{
			write8_mapping(memory_region::oam, address, u8);
		}
		else
		{
			write8_mmio(address - 0xFF00, u8);
		}
	}

	void memory_map::write8_main(uint16_t address, uint8_t u8)
	{
		switch (address & 0xF000)
		{
		case 0x0000:
		case 0x1000:
		case 0x2000:
		case 0x3000:
			return write8_mapping(memory_region::rom0, address, u8);
		case 0x4000:
		case 0x5000:
		case 0x6000:
		case 0x7000:
			return write8_mapping(memory_region::rom1, address, u8);
		case 0x8000:
		case 0x9000:
			return write8_mapping(memory_region::vram, address, u8);
		case 0xA000:
		case 0xB000:
			return write8_mapping(memory_region::sram, address, u8);
		case 0xC000:
		case 0xE000:
			return write8_mapping(memory_region::wram0, address, u8);
		case 0xD000:
		case 0xF000:
			return write8_mapping(memory_region::wram1, address, u8);
		}
	}

	void memory_map::write8_mapping(memory_region region, uint16_t address, uint8_t u8)
	{
		const mapping& mapping = mappings[(size_t)region];

		if (std::holds_alternative<uint8_t*>(mapping.write))
		{
			uint8_t* data = std::get<uint8_t*>(mapping.write);
			if (data)
			{
				data[address & mapping.mask] = u8;
			}
			else
			{
				// write explicitly ignored by mapping
			}
		}
		else
		{
			std::get<1>(mapping.write)(address, u8);
		}
	}

	void memory_map::write8_mmio(uint8_t mmio_address, uint8_t u8)
	{
		const uint16_t address = 0xFF00 | mmio_address; // purely for ease of programming
		if (address <= 0xFF7F) // known mapped registers
		{
			if (address == 0xFF00)
			{
				joypad.u8 = 0xCF | u8;

				if (joypad.select_dirs == 0)
				{
					joypad.right_a &= (uint8_t)buttons[(uint8_t)button_id::right];
					joypad.left_b &= (uint8_t)buttons[(uint8_t)button_id::left];
					joypad.up_select &= (uint8_t)buttons[(uint8_t)button_id::up];
					joypad.down_start &= (uint8_t)buttons[(uint8_t)button_id::down];
				}
				if (joypad.select_buttons == 0)
				{
					joypad.right_a &= (uint8_t)buttons[(uint8_t)button_id::a];
					joypad.left_b &= (uint8_t)buttons[(uint8_t)button_id::b];
					joypad.up_select &= (uint8_t)buttons[(uint8_t)button_id::select];
					joypad.down_start &= (uint8_t)buttons[(uint8_t)button_id::start];
				}
			}
			else if (address == 0xFF01)
			{
				// todo - serial port not implemented
				serial_data = u8;
			}
			else if (address == 0xFF02)
			{
				// todo - serial port not implemented
				serial_control.u8 = 0x7E | u8;
				if (serial_control.transfer)
				{
					std::cout << (char)serial_data;
					serial_data = 0;
					serial_control.transfer = 0;
				}
			}
			else if (address == 0xFF03)
			{
				// nothing mapped
			}
			else if (address == 0xFF04)
			{
				timer_div_reset = scheduler.get_cycle_counter();
				timer_div = 0;
			}
			else if (address == 0xFF05)
			{
				// todo - timer not implemented
				timer_counter = u8;
			}
			else if (address == 0xFF06)
			{
				// todo - timer not implemented
				timer_reset_value = u8;
			}
			else if (address == 0xFF07)
			{
				// todo - timer not implemented
				timer_control.u8 = 0xF8 | u8;
			}
			else if (address <= 0xFF0E)
			{
				// nothing mapped
			}
			else if (address == 0xFF0F)
			{
				interrupt_flag.u8 = 0xE0 | u8;
			}
			else if (address <= 0xFF23)
			{
				static const uint8_t audio_registers_mask[20] =
				{
					0x80, 0x3F, 0x00, 0x00, 0xB8,
					0xFF, 0x3F, 0x00, 0x00, 0xB8,
					0x7F, 0xFF, 0x9F, 0x00, 0xB8,
					0xFF, 0xFF, 0x00, 0x00, 0xBF,
				};

				audio_registers[address - 0xFF10] = audio_registers_mask[address - 0xFF10] | u8;
			}
			else if (address <= 0xFF26)
			{
				static const uint8_t audio_control_mask[3] =
				{
					0x00, 0x00, 0x70,
				};

				audio_control[address - 0xFF24] = audio_control_mask[address - 0xFF24] | u8;
			}
			else if (address <= 0xFF2F)
			{
				// nothing mapped
			}
			else if (address <= 0xFF3F)
			{
				audio_wave[address - 0xFF30] = u8;
			}
			else if (address <= 0xFF4B)
			{
				write8_mapping(memory_region::ppu_registers, address, u8);
			}
			else if (address <= 0xFF4F)
			{
				// nothing mapped
			}
			else if (address == 0xFF50)
			{
				if (!boot_rom_disable)
				{
					boot_rom_disable = true;
				}
			}
			// else if (address <= 0xFF7F) // nothing mapped
		}
		else if (address <= 0xFFFE)
		{
			hram[address - 0xFF80] = u8;
		}
		else if (address == 0xFFFF)
		{
			interrupt_enable.u8 = u8;
		}
	}

	void memory_map::load_boot_rom(std::filesystem::path boot_rom_path)
	{
		std::ifstream f{ boot_rom_path, std::ios_base::binary };
		uintmax_t boot_rom_size = std::filesystem::file_size(boot_rom_path);
		if (boot_rom_size != 256)
		{
			throw std::runtime_error("bad boot rom file");
		}
		boot_rom.resize((size_t)boot_rom_size);
		f.read((char*)boot_rom.data(), boot_rom_size);

		boot_rom_disable = 0;
	}

	void memory_map::input(button_id button, button_state state)
	{
		buttons[(uint8_t)button] = state;
	}

	void memory_map::set_mapping(memory_region region, memory_map::mapping new_mapping)
	{
		mappings[(size_t)region] = new_mapping;
	}
}
