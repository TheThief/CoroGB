#pragma once

#include "gb_memory_mapper.h"

#include <vector>
#include <filesystem>
#include <functional>
#include <iostream>

namespace coro_gb
{
	struct cart final
	{
		cart(std::filesystem::path rom_path, std::filesystem::path ram_path);

		void map(memory_mapper& in_memory_mapper);
		void unmap();

	protected:
		void load_rom(std::filesystem::path in_rom_path);
		void load_ram(std::filesystem::path in_ram_path);

		struct mbc_base;
		void construct_mbc_from_rom(std::vector<uint8_t> in_rom);

		struct mbc_base
		{
			mbc_base(std::vector<uint8_t> in_rom, uint32_t in_override_ram_size = -1);
			virtual ~mbc_base() = 0;

			virtual void map_to(memory_mapper& in_memory_mapper) = 0;
			void unmap();

			// load/save ram data
			void load_ram(std::filesystem::path in_ram_path);
			void save_ram();

		protected:
			friend cart;
			std::vector<uint8_t> rom;
			std::vector<uint8_t> ram;
			std::filesystem::path ram_path = "";
			memory_mapper* mapped_to = nullptr;

			void map_ram(uint8_t ram_bank);
			void unmap_ram();

			// override these to load extra data from the save file e.g. RTC
			virtual void load_ram(std::istream& f);
			virtual void save_ram(std::ostream& f);
		};
		struct null_mbc final : public mbc_base
		{
			null_mbc(std::vector<uint8_t> in_rom);

			virtual void map_to(memory_mapper& in_memory_mapper) override;
		};
		struct mbc1 final : public mbc_base
		{
			union ram_enable_register
			{
				uint8_t value = 0;
				struct
				{
					uint8_t ram_enable : 4;
				};
			};
			union rom_bank_register
			{
				uint8_t value = 0;
				struct
				{
					uint8_t rom_bank_low : 5;
				};
			};
			union ram_bank_register
			{
				uint8_t value = 0;
				struct
				{
					uint8_t ram_bank_or_rom_bank_high : 2;
				};
			};
			union mode_register
			{
				uint8_t value = 0;
				struct
				{
					uint8_t mode : 1;
				};
			};

			uint8_t rom_bank = 1;   // full rom bank number including five bit rom bank regiser and two outer bits from above
			uint8_t ram_bank = 0;   // two bits  - used as either ram bank number or "outer" rom bank number in large carts
			bool ram_enabled = 0;   // when ram is disabled reads return 0xFF and writes are ignored
			bool banking_mode = 0;  // mode 0 only applies "outer" bank number to rom bank 1, mode 1 additionally applies it to both rom 0 and ram
			bool multicart_1MB = 0; // a 1MB multi-cart (aka MBC1m) only uses four bits from "inner" bank number

			mbc1(std::vector<uint8_t> in_rom);

			virtual void map_to(memory_mapper& in_memory_mapper) override;

			void handle_write(uint16_t address, uint8_t value);
		};
		struct mbc2 final : public mbc_base
		{
			uint16_t rom_bank = 1;
			bool ram_enabled = 0;

			mbc2(std::vector<uint8_t> in_rom);

			virtual void map_to(memory_mapper& in_memory_mapper) override;

			void handle_write(uint16_t address, uint8_t value);

			void map_ram();
			uint8_t handle_ram_read(uint16_t address);
			void handle_ram_write(uint16_t address, uint8_t value);
		};
		struct mbc3 final : public mbc_base
		{
			uint16_t rom_bank = 1;
			uint8_t ram_bank = 0;   // two or three bits - three bit variant is sometimes called MBC30. 4th bit used for timer
			bool ram_enabled = 0;
			bool has_timer = false;
			bool multicart = 0;

			mbc3(std::vector<uint8_t> in_rom);

			virtual void map_to(memory_mapper& in_memory_mapper) override;

			virtual void load_ram(std::istream& f) override;
			virtual void save_ram(std::ostream& f) override;

			void handle_write(uint16_t address, uint8_t value);
		};
		struct mbc5 final : public mbc_base
		{
			uint16_t rom_bank = 1;
			uint8_t ram_bank = 0;
			bool ram_enabled = 0;

			mbc5(std::vector<uint8_t> in_rom);

			virtual void map_to(memory_mapper& in_memory_mapper) override;

			void handle_write(uint16_t address, uint8_t value);
		};
		struct mmm01 final : public mbc_base
		{
			union ram_enable_register
			{
				uint8_t value : 7 = 0;
				struct
				{
					uint8_t ram_enable : 4;
					uint8_t ram_bank_nwrite_enable : 2;
					uint8_t map_enable : 1;
				};
			};
			union rom_bank_register
			{
				uint8_t value : 7 = 0;
				struct
				{
					uint8_t rom_bank_low : 5;
					uint8_t rom_bank_mid : 2;
				};
			};
			union ram_bank_register
			{
				uint8_t value : 7 = 0;
				struct
				{
					uint8_t ram_bank_low : 2;
					uint8_t ram_bank_high : 2;
					uint8_t rom_bank_high : 2;
					uint8_t banking_mode_nwrite_enable : 1;
				};
			};
			union mode_register
			{
				uint8_t value : 7 = 0;
				struct
				{
					uint8_t mode : 1;
					uint8_t : 1;
					uint8_t rom_bank_nwrite_enable : 4;
					uint8_t multiplex : 1;
				};
			};

			union rom_bank_t
			{
				uint16_t value : 9 = 0;
				struct
				{
					uint16_t rom_bank_low : 5;
					uint16_t rom_bank_mid : 2;
					uint16_t rom_bank_high : 2;
				};
			};
			union ram_bank_t
			{
				uint8_t value : 4 = 0;
				struct
				{
					uint8_t ram_bank_low : 2;
					uint8_t ram_bank_high : 2;
				};
			};

			rom_bank_t rom_bank = { 0 }; // nine bits - complete rom bank number
			ram_bank_t ram_bank = { 0 }; // four bits - complete ram bank number
			uint8_t rom_bank_nwrite_enable = 0; // active-low, offset by one bit to match the rom bank number
			uint8_t ram_bank_nwrite_enable = 0; // active-low
			bool ram_enabled = 0;     // when ram is disabled reads return 0xFF and writes are ignored
			bool banking_mode = 0;    // mode 0 only applies "outer" bank number to rom bank 1, mode 1 additionally applies it to both rom 0 and ram
			bool banking_mode_nwrite_enable = 0;
			bool mapped = 0;          // while "unmapped" rom_bank is treated as 1FE/1FF
			bool multiplex = 0;

			mmm01(std::vector<uint8_t> in_rom);

			virtual void map_to(memory_mapper& in_memory_mapper) override;

			void handle_write(uint16_t address, uint8_t value);
		};

		std::unique_ptr<mbc_base> mbc;
		bool reset = false;
	};
}
