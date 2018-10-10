#define _SCL_SECURE_NO_WARNINGS 1

#include "gb_memory_mapper.h"
#include "gb_cycle_scheduler.h"

#include <algorithm>
#include <fstream>
#include <iostream>

namespace coro_gb
{
	memory_mapper::memory_mapper(cycle_scheduler& scheduler)
		: scheduler{ scheduler }
	{
	}

	uint8_t memory_mapper::read8(uint16_t address) const
	{
		if (const mapping* mapping = find_mapping(address))
		{
			if (std::holds_alternative<uint8_t*>(mapping->read))
			{
				uint8_t* data = std::get<uint8_t*>(mapping->read);
				if (data)
				{
					return data[address - mapping->start_address];
				}
				else
				{
					return 0xFF;
				}
			}
			else
			{
				return std::get<1>(mapping->read)(address);
			}
		}

		if (address >= 0xC000)
		{
			if (address <= 0xDFFF)
			{
				return wram[address - 0xC000];
			}
			else if (address < 0xFF00)
			{
				// mirror of WRAM
				return wram[address - 0xE000];
			}
			else if (address <= 0xFF7F) // known mapped registers
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
				// else if (address == 0xFF50) // not readable
			}
			else if (address <= 0xFFFE)
			{
				return hram[address - 0xFF80];
			}
			else if (address == 0xFFFF)
			{
				return interrupt_enable.u8;
			}
		}
		return 0xFF;
	}
	uint16_t memory_mapper::read16(uint16_t address) const
	{
		// 16-bit reads have to be separated into 8-bit reads to preserve register functionality
		uint16_t result = 0;
		if (const mapping* mapping = find_mapping(address))
		{
			if (std::holds_alternative<uint8_t*>(mapping->read))
			{
				uint8_t* data = std::get<uint8_t*>(mapping->read);
				if (data)
				{
					result = data[address - mapping->start_address];

					if (address + 1 <= mapping->end_address)
					{
						result |= (uint16_t)data[(address - mapping->start_address) + 1] << 8;
						return result;
					}
				}
				else
				{
					result = 0xFF;
					if (address + 1 <= mapping->end_address)
					{
						result |= (uint16_t)0xFF << 8;
						return result;
					}
				}
			}
			else
			{
				result = std::get<1>(mapping->read)(address);
				if (address + 1 <= mapping->end_address)
				{
					result |= (uint16_t)std::get<1>(mapping->read)(address + 1) << 8;
					return result;
				}
			}
		}
		else
		{
			result = read8(address);
		}

		result |= (uint16_t)read8(address + 1) << 8;
		return result;
	}

	void memory_mapper::write8(uint16_t address, uint8_t u8)
	{
		if (const mapping* mapping = find_mapping(address))
		{
			if (std::holds_alternative<uint8_t*>(mapping->write))
			{
				uint8_t* data = std::get<uint8_t*>(mapping->write);
				if (data)
				{
					data[address - mapping->start_address] = u8;
				}
				else
				{
					// write explicitly ignored by mapping
				}
			}
			else
			{
				std::get<1>(mapping->write)(address, u8);
			}
			return;
		}

		if (address >= 0xC000)
		{
			if (address <= 0xDFFF)
			{
				wram[address - 0xC000] = u8;
			}
			else if (address < 0xFF00)
			{
				// mirror of WRAM
				wram[address - 0xE000] = u8;
			}
			else if (address <= 0xFF7F) // known mapped registers
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
				else if (address <= 0xFF4F)
				{
					// nothing mapped
				}
				else if (address == 0xFF50)
				{
					if (!boot_rom_disable)
					{
						boot_rom_disable = true;
						remove_mapping({ 0x0000, 0x00FF });
					}
				}
				else if (address <= 0xFF7F)
				{
					// nothing mapped
				}
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
	}
	void memory_mapper::write16(uint16_t address, uint16_t u16)
	{
		// 16-bit writes have to be separated into 8-bit writes to preserve register functionality
		if (const mapping* mapping = find_mapping(address))
		{
			if (std::holds_alternative<uint8_t*>(mapping->write))
			{
				uint8_t* data = std::get<uint8_t*>(mapping->write);
				if (data)
				{
					data[address - mapping->start_address] = u16 & 0xFF;

					if (address + 1 <= mapping->end_address)
					{
						data[(address - mapping->start_address) + 1] = u16 >> 8;
						return;
					}
				}
				else
				{
					if (address + 1 <= mapping->end_address)
					{
						return; // write explicitly ignored by mapping
					}
				}
			}
			else
			{
				std::get<1>(mapping->write)(address, u16 & 0xFF);
				if (address + 1 <= mapping->end_address)
				{
					std::get<1>(mapping->write)(address + 1, u16 >> 8);
					return;
				}
			}
		}
		else
		{
			write8(address, u16 & 0xFF);
		}

		write8(address + 1, u16 >> 8);
	}

	void memory_mapper::load_boot_rom(std::filesystem::path boot_rom_path)
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
		set_mapping({0x0000, 0x00FF, boot_rom.data(), nullptr });
	}

	void memory_mapper::input(button_id button, button_state state)
	{
		buttons[(uint8_t)button] = state;
	}

	void memory_mapper::set_mapping(memory_mapper::mapping new_mapping)
	{
		auto it = std::lower_bound(mappings.begin(), mappings.end(), new_mapping);
		if (it != mappings.end() &&
			it->start_address == new_mapping.start_address && it->end_address == new_mapping.end_address)
		{
			*it = new_mapping;
		}
		else
		{
			mappings.insert(it, std::move(new_mapping));
		}
	}

	const memory_mapper::mapping* memory_mapper::find_mapping(uint16_t address) const
	{
		//auto end = std::upper_bound(mappings.begin(), mappings.end(), address, [](uint16_t address, const mapping& mapping) { return address < mapping.start_address; });
		//auto found_it = std::find_if(mappings.begin(), end, [address](const mapping& mapping) { return address <= mapping.end_address; });
		auto end = mappings.end(); // std::upper_bound(mappings.begin(), mappings.end(), address, [](uint16_t address, const mapping& mapping) { return address < mapping.start_address; });
		auto found_it = std::find_if(mappings.begin(), end, [address](const mapping& mapping) { return address >= mapping.start_address && address <= mapping.end_address; });
		if (found_it != end)
		{
			return &*found_it;
		}

		return nullptr;
	}

	void memory_mapper::remove_mapping(mapping mapping_to_remove)
	{
		auto it = std::lower_bound(mappings.begin(), mappings.end(), mapping_to_remove);
		if (it != mappings.end() &&
			it->start_address == mapping_to_remove.start_address && it->end_address == mapping_to_remove.end_address)
		{
			mappings.erase(it);
		}
		else
		{
			throw std::runtime_error("no such mapping!");
		}
	}
}
