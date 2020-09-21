#include "gb_cart.h"

#include <algorithm>
#include <fstream>

namespace coro_gb
{
	cart::cart(std::filesystem::path rom_path, std::filesystem::path ram_path)
	{
		load_rom(rom_path);
		load_ram(ram_path);
	}

	void cart::map(memory_mapper& in_memory_mapper)
	{
		mbc->map_to(in_memory_mapper);
	}

	void cart::unmap()
	{
		mbc->unmap();
	}

	uint32_t get_ram_size(uint8_t ram_size_code)
	{
		switch (ram_size_code)
		{
			case 0x00: // no ram
				return 0;
				break;
			case 0x01: // 2 KBytes
				return 2048;
				break;
			case 0x02: // 8 Kbytes
				return 8192;
				break;
			case 0x03: // 32 KBytes(4 banks of 8KBytes each)
				return 4 * 8192;
				break;
			case 0x04: // 128 KBytes(16 banks of 8KBytes each)
				return 16 * 8192;
				break;
			case 0x05: // 64 KBytes(8 banks of 8KBytes each)
				return 8 * 8192;
				break;
			default:
				throw std::runtime_error("bad ram size code");
		}
	}

	void cart::load_rom(std::filesystem::path in_rom_path)
	{
		std::ifstream f{ in_rom_path, std::ios_base::binary };
		uintmax_t rom_size = std::filesystem::file_size(in_rom_path);
		if (rom_size < 16384 || rom_size > 2 * 1024 * 1024 || rom_size % 16384 != 0)
		{
			throw std::runtime_error("bad rom file");
		}
		std::vector<uint8_t> rom;
		rom.resize(rom_size);
		f.read((char*)rom.data(), rom_size);

		// 00h  ROM ONLY                 19h  MBC5
		// 01h  MBC1                     1Ah  MBC5+RAM
		// 02h  MBC1+RAM                 1Bh  MBC5+RAM+BATTERY
		// 03h  MBC1+RAM+BATTERY         1Ch  MBC5+RUMBLE
		// 05h  MBC2                     1Dh  MBC5+RUMBLE+RAM
		// 06h  MBC2+BATTERY             1Eh  MBC5+RUMBLE+RAM+BATTERY
		// 08h  ROM+RAM                  20h  MBC6
		// 09h  ROM+RAM+BATTERY          22h  MBC7+SENSOR+RUMBLE+RAM+BATTERY
		// 0Bh  MMM01
		// 0Ch  MMM01+RAM
		// 0Dh  MMM01+RAM+BATTERY
		// 0Fh  MBC3+TIMER+BATTERY
		// 10h  MBC3+TIMER+RAM+BATTERY   FCh  POCKET CAMERA
		// 11h  MBC3                     FDh  BANDAI TAMA5
		// 12h  MBC3+RAM                 FEh  HuC3
		// 13h  MBC3+RAM+BATTERY         FFh  HuC1+RAM+BATTERY
		const uint8_t cartridge_type_code = rom[0x0147];
		switch (cartridge_type_code)
		{
			case 0:
			case 8:
			case 9:
				// todo - the ram in type 8 is not persistent
				mbc = std::make_unique<null_mbc>(std::move(rom));
				break;
			case 1:
			case 2:
			case 3:
				// todo - the ram in type 2 is not persistent
				mbc = std::make_unique<mbc1>(std::move(rom));
				break;
			case 5:
			case 6:
				mbc = std::make_unique<mbc2>(std::move(rom));
				break;
				//case 0x0B:
				//case 0x0C:
				//case 0x0D:
					// mmm01
					// break;
			case 0x0F:
			case 0x10:
				// todo - implement timer for types 0F and 10
				mbc = std::make_unique<mbc3>(std::move(rom));
				break;
			case 0x11:
			case 0x12:
			case 0x13:
				// todo - the ram in type 12 is not persistent
				mbc = std::make_unique<mbc3>(std::move(rom));
				break;
			case 0x19:
			case 0x1A:
			case 0x1B:
			case 0x1C:
			case 0x1D:
			case 0x1E:
				// todo - the ram in type 1A and 1D is not persistent
				// todo - implment rumble for types 1C-1E
				mbc = std::make_unique<mbc5>(std::move(rom));
				break;
			default:
				throw std::runtime_error("unsupported cartridge type");
		}
	}

	void cart::load_ram(std::filesystem::path in_ram_path)
	{
		if (!mbc)
		{
			throw std::runtime_error("please load rom file before ram file");
		}

		mbc->load_ram(in_ram_path);
	}

	////////////////////////////////////////////////////////////////

	cart::mbc_base::mbc_base(std::vector<uint8_t> in_rom)
		: rom{ std::move(in_rom) }
	{
		const uint8_t ram_size_code = rom[0x0149];
		const auto ram_size = get_ram_size(ram_size_code);
		ram.resize(ram_size);
	}

	cart::mbc_base::~mbc_base()
	{
		if (mapped_to)
		{
			unmap();
			//throw std::runtime_error("please unmap cart from memory mapper before destroying it");
		}
	}

	void cart::mbc_base::map_to(memory_mapper& in_memory_mapper)
	{
		if (mapped_to)
		{
			save_ram();
			throw std::runtime_error("please unmap cart from memory mapper before mapping to a new memory_mapper");
		}
		mapped_to = &in_memory_mapper;
	}

	void cart::mbc_base::unmap()
	{
		if (mapped_to)
		{
			save_ram();
			mapped_to = nullptr;
		}
		// todo: we currently rely on memory mapper being destroyed after this - we don't actually remove the mapping
	}

	void cart::mbc_base::load_ram(std::filesystem::path in_ram_path)
	{
		ram_path = in_ram_path;

		if (std::filesystem::exists(ram_path))
		{
			if (!std::filesystem::is_regular_file(ram_path))
			{
				throw std::runtime_error("bad ram file");
			}

			std::ifstream f{ ram_path, std::ios_base::binary };
			uintmax_t ram_size = std::filesystem::file_size(ram_path);

			if (ram_size < ram.size())
			{
				throw std::runtime_error("bad ram file");
			}

			load_ram(f);
		}
	}

	void cart::mbc_base::save_ram()
	{
		if (!ram_path.empty() && ram.size() > 0)
		{
			std::ofstream f{ ram_path, std::ios_base::binary | std::ios_base::trunc };
			save_ram(f);
		}
	}

	void cart::mbc_base::load_ram(std::istream& f)
	{
		f.read((char*)ram.data(), ram.size());
	}

	void cart::mbc_base::save_ram(std::ostream& f)
	{
		f.write((char*)ram.data(), ram.size());
	}

	////////////////////////////////////////////////////////////////

	cart::null_mbc::null_mbc(std::vector<uint8_t> in_rom)
		: mbc_base{ std::move(in_rom) }
	{
	}

	void cart::null_mbc::map_to(memory_mapper& in_memory_mapper)
	{
		mapped_to = &in_memory_mapper;

		mapped_to->set_mapping({ 0x0000, 0x7FFF, rom.data(), nullptr });
		if (ram.size() > 0)
		{
			mapped_to->set_mapping({ 0xA000, (uint16_t)(0xA000 + std::min<size_t>(ram.size() - 1, 0x1FFF)), ram.data(), ram.data() });
		}
	}

	////////////////////////////////////////////////////////////////

	cart::mbc1::mbc1(std::vector<uint8_t> in_rom)
		: mbc_base{ std::move(in_rom) }
	{
	}

	void cart::mbc1::map_to(memory_mapper& in_memory_mapper)
	{
		mbc_base::map_to(in_memory_mapper);
		mapped_to->set_mapping({ 0x0000, 0x3FFF, rom.data(),          [this](uint16_t address, uint8_t value) { handle_write(address, value); } });
		mapped_to->set_mapping({ 0x4000, 0x7FFF, rom.data() + 0x4000, [this](uint16_t address, uint8_t value) { handle_write(address, value); } }); // bank 1

		if (rom.size() > 0x8'0000 && ram.size() > 0x2000)
		{
			throw std::runtime_error("unsupported cartridge! Has an MBC1 with both ram and rom banked with register 4000");
		}

		if (rom.size() == 0x10'0000)
		{
			// possible multi-cart:
			// check for nintendo logo in bank 10 and 20, but with a different game header to bank 0
			// is wired to use both bits of the 4000 register for the upper bits of the rom address, ignoring the top bit of the 2000 register
			// rather than using all 5 bits of the 2000 register and only one of the 4000 register as a normal 1MB cart would

			uint8_t nintendo_logo_data[48] =
			{
				0xCE, 0xED, 0x66, 0x66, 0xCC, 0x0D, 0x00, 0x0B, 0x03, 0x73, 0x00, 0x83, 0x00, 0x0C, 0x00, 0x0D,
				0x00, 0x08, 0x11, 0x1F, 0x88, 0x89, 0x00, 0x0E, 0xDC, 0xCC, 0x6E, 0xE6, 0xDD, 0xDD, 0xD9, 0x99,
				0xBB, 0xBB, 0x67, 0x63, 0x6E, 0x0E, 0xEC, 0xCC, 0xDD, 0xDC, 0x99, 0x9F, 0xBB, 0xB9, 0x33, 0x3E,
			};

			multicart_1MB =
				std::equal(std::begin(nintendo_logo_data), std::end(nintendo_logo_data), &rom[0x10 * 0x4000 + 0x104]) &&
				std::equal(std::begin(nintendo_logo_data), std::end(nintendo_logo_data), &rom[0x20 * 0x4000 + 0x104]);
		}
	}

	void cart::mbc1::handle_write(uint16_t address, uint8_t value)
	{
		if (address <= 0x1FFF) // RAM Enable
		{
			// Practically any value with 0Ah in the lower 4 bits enables RAM, and any other value disables RAM.
			bool old_ram_enabled = ram_enabled;
			ram_enabled = ((value & 0x0F) == 0x0A);

			if (ram_enabled && !old_ram_enabled)
			{
				if (ram.size() > 0)
				{
					if (ram.size() > 0x2000) // banked ram
					{
						uint8_t* ram_data = ram.data() + ((outer_bank * 0x2000) % ram.size());
						mapped_to->set_mapping({ 0xA000, 0xBFFF, ram_data, ram_data });
					}
					else // unbanked ram
					{
						uint8_t* ram_data = ram.data();
						mapped_to->set_mapping({ 0xA000, (uint16_t)(0xA000 + ram.size() - 1), ram_data, ram_data });
					}
				}
			}
			else if (!ram_enabled && old_ram_enabled)
			{
				uint16_t mapping_end = ram.size() >= 0x2000 ? 0xBFFF : (uint16_t)(0xA000 + ram.size() - 1);
				mapped_to->set_mapping({ 0xA000, mapping_end, nullptr, nullptr });
			}
		}
		else if (address <= 0x3FFF) // ROM Bank Number
		{
			// Writing to this address space selects the lower 5 bits of the ROM Bank Number(in range 01 - 1Fh).
			// When 00h is written, the MBC translates that to bank 01h also.
			// But (when using the register below to specify the upper ROM Bank bits), the same happens for Bank 20h, 40h, and 60h.
			// Any attempt to address these ROM Banks will select Bank 21h, 41h, and 61h instead.
			if (!multicart_1MB)
			{
				value = std::max(value & 0x1F, 0x01);
				rom_bank = (rom_bank & 0xE0) | value;
			}
			else
			{
				value = std::max(value & 0x0F, 0x01);
				rom_bank = (rom_bank & 0xF0) | value;
			}

			uint8_t* rom_data = rom.data() + ((rom_bank * 0x4000) % rom.size());
			mapped_to->set_mapping({ 0x4000, 0x7FFF, rom_data, [this](uint16_t address, uint8_t value) { handle_write(address, value); } });
		}
		else if (address <= 0x5FFF) // RAM Bank Number - and/or - Upper Bits of ROM Bank Number
		{
			// This 2bit register can be used to select a RAM Bank in range from 00 - 03h, or to specify the upper two bits of the ROM Bank number (see below.)
			outer_bank = value & 0x03;

			uint8_t adjusted_outer_bank = outer_bank << (!multicart_1MB ? 5 : 4);
			if (!multicart_1MB)
			{
				rom_bank = (rom_bank & 0x1F) | adjusted_outer_bank;
			}
			else
			{
				rom_bank = (rom_bank & 0x0F) | adjusted_outer_bank;
			}

			uint8_t* rom_data = rom.data() + ((rom_bank * 0x4000) % rom.size());
			mapped_to->set_mapping({ 0x4000, 0x7FFF, rom_data, [this](uint16_t address, uint8_t value) { handle_write(address, value); } });

			if (banking_mode == 1)
			{
				// in banking mode 1 the "rom 0" area is banked to the "outer" bank number
				uint8_t* rom0_data = rom.data() + ((adjusted_outer_bank * 0x4000) % rom.size());
				mapped_to->set_mapping({ 0x0000, 0x3FFF, rom0_data, [this](uint16_t address, uint8_t value) { handle_write(address, value); } });

				if (ram_enabled && ram.size() > 0x2000) // don't need to update the ram banking if the ram is not banked!
				{
					uint8_t* ram_data = ram.data() + ((outer_bank * 0x2000) % ram.size());
					mapped_to->set_mapping({ 0xA000, 0xBFFF, ram_data, ram_data });
				}
			}
		}
		else if (address <= 0x7FFF) // ROM / RAM Banking Mode Select
		{
			// This 1bit Register selects whether the two bits of the above register should be and'd with A14 or not
			// If the cart uses mbc1's AA13/14 for RAM then this must be set to 1 to have the ram accessible
			// If the cart uses AA13/14 for ROM, then they become an "outer" bank number which in mode 1 also affects the "rom 0" area (0x0000-0x3FFF)
			uint8_t old_banking_mode = banking_mode;
			banking_mode = (value != 0);
			if (banking_mode != old_banking_mode)
			{
				if (banking_mode == 0)
				{
					// switch to mode 0 - range 0x0000-0x3FFF and ram is unbanked
					mapped_to->set_mapping({ 0x0000, 0x3FFF, rom.data(), [this](uint16_t address, uint8_t value) { handle_write(address, value); } });
					if (ram_enabled && ram.size() > 0x2000 && outer_bank != 0)
					{
						uint8_t* ram_data = ram.data();
						mapped_to->set_mapping({ 0xA000, 0xBFFF, ram_data, ram_data });
					}
				}
				else
				{
					// switch to mode 1 - range 0x0000-0x3FFF and ram are banked

					// in banking mode 1 the "rom 0" area is banked to the "outer" bank number
					uint8_t adjusted_outer_bank = outer_bank << (!multicart_1MB ? 5 : 4);
					uint8_t* rom0_data = rom.data() + ((adjusted_outer_bank * 0x4000) % rom.size());
					mapped_to->set_mapping({ 0x0000, 0x3FFF, rom0_data, [this](uint16_t address, uint8_t value) { handle_write(address, value); } });

					if (ram_enabled && ram.size() > 0x2000) // don't need to update the ram banking if the ram is not banked!
					{
						uint8_t* ram_data = ram.data() + ((outer_bank * 0x2000) % ram.size());
						mapped_to->set_mapping({ 0xA000, 0xBFFF, ram_data, ram_data });
					}
				}
			}
		}
	}

	////////////////////////////////////////////////////////////////

	cart::mbc2::mbc2(std::vector<uint8_t> in_rom)
		: mbc_base{ std::move(in_rom) }
	{
		ram.resize(512);
	}

	void cart::mbc2::map_to(memory_mapper& in_memory_mapper)
	{
		mbc_base::map_to(in_memory_mapper);
		mapped_to->set_mapping({ 0x0000, 0x3FFF, rom.data(),          [this](uint16_t address, uint8_t value) { handle_write(address, value); } });
		mapped_to->set_mapping({ 0x4000, 0x7FFF, rom.data() + 0x4000, nullptr }); // bank 1
	}

	void cart::mbc2::handle_write(uint16_t address, uint8_t value)
	{
		if (address <= 0x1FFF) // RAM Enable
		{
			// Practically any value with 0Ah in the lower 4 bits enables RAM, and any other value disables RAM.
			bool old_ram_enabled = ram_enabled;
			ram_enabled = ((value & 0x0F) == 0x0A);

			if (ram_enabled && !old_ram_enabled)
			{
				uint8_t* ram_data = ram.data();
				mapped_to->set_mapping({ 0xA000, 0xA3FF, ram_data, ram_data }); // todo - this ram is supposed to only be 4 bit
			}
			else if (!ram_enabled && old_ram_enabled)
			{
				mapped_to->set_mapping({ 0xA000, 0xA3FF, nullptr, nullptr });
			}
		}
		else if (address <= 0x3FFF) // ROM Bank Number
		{
			// Writing to this address space selects the ROM Bank Number (in range 01 - 0Fh).
			// When 00h is written, the MBC translates that to bank 01h also.
			value = std::max(value & 0x0F, 0x01);
			rom_bank = (rom_bank & 0xF0) | value;
			uint8_t* rom_data = rom.data() + ((rom_bank * 0x4000) % rom.size());
			mapped_to->set_mapping({ 0x4000, 0x7FFF, rom_data, nullptr });
		}
	}

	////////////////////////////////////////////////////////////////

	cart::mbc3::mbc3(std::vector<uint8_t> in_rom)
		: mbc_base{ std::move(in_rom) }
	{
	}

	void cart::mbc3::map_to(memory_mapper& in_memory_mapper)
	{
		mbc_base::map_to(in_memory_mapper);
		mapped_to->set_mapping({ 0x0000, 0x3FFF, rom.data(),          [this](uint16_t address, uint8_t value) { handle_write(address, value); } });
		mapped_to->set_mapping({ 0x4000, 0x7FFF, rom.data() + 0x4000, [this](uint16_t address, uint8_t value) { handle_write(address, value); } }); // bank 1
	}

	void cart::mbc3::handle_write(uint16_t address, uint8_t value)
	{
		if (address <= 0x1FFF) // RAM Enable
		{
			// Practically any value with 0Ah in the lower 4 bits enables RAM, and any other value disables RAM.
			bool old_ram_enabled = ram_enabled;
			ram_enabled = ((value & 0x0F) == 0x0A);

			if (ram_enabled && !old_ram_enabled)
			{
				if (ram.size() > 0)
				{
					if (ram.size() > 0x2000) // banked ram
					{
						uint8_t* ram_data = ram.data() + ((ram_bank * 0x2000) % ram.size());
						mapped_to->set_mapping({ 0xA000, 0xBFFF, ram_data, ram_data });
					}
					else // unbanked ram
					{
						uint8_t* ram_data = ram.data();
						mapped_to->set_mapping({ 0xA000, (uint16_t)(0xA000 + ram.size() - 1), ram_data, ram_data });
					}
				}
			}
			else if (!ram_enabled && old_ram_enabled)
			{
				uint16_t mapping_end = ram.size() >= 0x2000 ? 0xBFFF : (uint16_t)(0xA000 + ram.size() - 1);
				mapped_to->set_mapping({ 0xA000, mapping_end, nullptr, nullptr });
			}
		}
		else if (address <= 0x3FFF) // ROM Bank Number
		{
			// Writing to this address space selects the 7 bits of the ROM Bank Number(in range 01 - 7Fh).
			// When 00h is written, the MBC translates that to bank 01h also.
			value = std::max(value & 0x7F, 0x01);
			rom_bank = value;
			uint8_t* rom_data = rom.data() + ((rom_bank * 0x4000) % rom.size());
			mapped_to->set_mapping({ 0x4000, 0x7FFF, rom_data, nullptr });
		}
		else if (address <= 0x5FFF) // RAM Bank Number - or - RTC Register Select
		{
			// As for the MBC1s RAM Banking Mode, writing a value in range for 00h-03h maps the corresponding external RAM Bank (if any) into memory at A000-BFFF.
			// When writing a value of 08h-0Ch, this will map the corresponding RTC register into memory at A000-BFFF.
			value = value & 0x0F; // is this correct?
			if (value & 0x08)
			{
				// 08h  RTC S   Seconds   0-59 (0-3Bh)
				// 09h  RTC M   Minutes   0-59 (0-3Bh)
				// 0Ah  RTC H   Hours     0-23 (0-17h)
				// 0Bh  RTC DL  Lower 8 bits of Day Counter (0-FFh)
				// 0Ch  RTC DH  Upper 1 bit of Day Counter, Carry Bit, Halt Flag
				//       Bit 0  Most significant bit of Day Counter (Bit 8)
				//       Bit 6  Halt (0=Active, 1=Stop Timer)
				//       Bit 7  Day Counter Carry Bit (1=Counter Overflow)
				// The Halt Flag is supposed to be set before <writing> to the RTC Registers.
				// The Carry Bit remains set until the program resets it.
				throw std::runtime_error("mbc3 timer not supported");
			}
			ram_bank = value;

			if (ram_enabled && ram.size() > 0x2000) // banked ram
			{
				uint8_t* ram_data = ram.data() + ((ram_bank * 0x2000) % ram.size());
				mapped_to->set_mapping({ 0xA000, 0xBFFF, ram_data, ram_data });
			}
		}
		else if (address <= 0x7FFF) // Latch Clock Data
		{
			if (has_timer)
			{
				// When writing 00h, and then 01h to this register, the current time becomes latched into the RTC registers.
				// The latched data will not change until it becomes latched again, by repeating the write 00h->01h procedure.
				// The clock itself continues to tick in the background.
				throw std::runtime_error("mbc3 timer not supported");
			}
		}
	}

	void cart::mbc3::load_ram(std::istream& f)
	{
		mbc_base::load_ram(f);

		if (has_timer)
		{
			// todo - load timer from save file
		}
	}

	void cart::mbc3::save_ram(std::ostream& f)
	{
		mbc_base::save_ram(f);

		if (has_timer)
		{
			// todo - save timer to save file
		}
	}

	////////////////////////////////////////////////////////////////

	cart::mbc5::mbc5(std::vector<uint8_t> in_rom)
		: mbc_base{ std::move(in_rom) }
	{
	}

	void cart::mbc5::map_to(memory_mapper& in_memory_mapper)
	{
		mbc_base::map_to(in_memory_mapper);
		mapped_to->set_mapping({ 0x0000, 0x3FFF, rom.data(),          [this](uint16_t address, uint8_t value) { handle_write(address, value); } });
		mapped_to->set_mapping({ 0x4000, 0x7FFF, rom.data() + 0x4000, [this](uint16_t address, uint8_t value) { handle_write(address, value); } }); // bank 1
	}

	void cart::mbc5::handle_write(uint16_t address, uint8_t value)
	{
		if (address <= 0x1FFF) // RAM Enable
		{
			// Practically any value with 0Ah in the lower 4 bits enables RAM, and any other value disables RAM.
			bool old_ram_enabled = ram_enabled;
			ram_enabled = ((value & 0x0F) == 0x0A);

			if (ram_enabled && !old_ram_enabled)
			{
				if (ram.size() > 0)
				{
					if (ram.size() > 0x2000) // banked ram
					{
						uint8_t* ram_data = ram.data() + ((ram_bank * 0x2000) % ram.size());
						mapped_to->set_mapping({ 0xA000, 0xBFFF, ram_data, ram_data });
					}
					else // unbanked ram
					{
						uint8_t* ram_data = ram.data();
						mapped_to->set_mapping({ 0xA000, (uint16_t)(0xA000 + ram.size() - 1), ram_data, ram_data });
					}
				}
			}
			else if (!ram_enabled && old_ram_enabled)
			{
				uint16_t mapping_end = ram.size() >= 0x2000 ? 0xBFFF : (uint16_t)(0xA000 + ram.size() - 1);
				mapped_to->set_mapping({ 0xA000, mapping_end, nullptr, nullptr });
			}
		}
		else if (address <= 0x2FFF) // Low 8 bits of ROM Bank Number
		{
			// The lower 8 bits of the ROM bank number goes here. Writing 0 will indeed give bank 0 on MBC5, unlike other MBCs.
			rom_bank = (rom_bank & 0x100) | value;
			uint8_t* rom_data = rom.data() + ((rom_bank * 0x4000) % rom.size());
			mapped_to->set_mapping({ 0x4000, 0x7FFF, rom_data, nullptr });
		}
		else if (address <= 0x3FFF) // High bit of ROM Bank Number
		{
			// The 9th bit of the ROM bank number goes here.
			value = value & 0x01;
			rom_bank = ((uint16_t)value << 8) | (rom_bank & 0xFF);
			uint8_t* rom_data = rom.data() + ((rom_bank * 0x4000) % rom.size());
			mapped_to->set_mapping({ 0x4000, 0x7FFF, rom_data, nullptr });
		}
		else if (address <= 0x5FFF) // RAM Bank Number
		{
			// As for the MBC1s RAM Banking Mode, writing a value in range for 00h-0Fh maps the corresponding external RAM Bank (if any) into memory at A000-BFFF.
			value = value & 0x0F;
			ram_bank = value;

			if (ram_enabled && ram.size() > 0x2000) // banked ram
			{
				uint8_t* ram_data = ram.data() + ((ram_bank * 0x2000) % ram.size());
				mapped_to->set_mapping({ 0xA000, 0xBFFF, ram_data, ram_data });
			}
		}
	}
}
