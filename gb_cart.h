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

		struct mbc_base
		{
			std::vector<uint8_t> rom;
			std::vector<uint8_t> ram;
			std::filesystem::path ram_path = "";
			memory_mapper* mapped_to = nullptr;

			mbc_base(std::vector<uint8_t> in_rom);
			virtual ~mbc_base() = 0;

			virtual void map_to(memory_mapper& in_memory_mapper) = 0;
			void unmap();

			// load/save ram data
			void load_ram(std::filesystem::path in_ram_path);
			void save_ram();

		protected:
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
			uint8_t outer_bank = 0;   // two bits  - used as either ram bank number or "outer" rom bank number in large carts
			uint8_t rom_bank = 1;     // five bits - normally "inner" rom bank number
			bool ram_enabled = 0;     // when ram is disabled reads return 0xFF and writes are ignored
			uint8_t banking_mode = 0; // mode 0 only applies "outer" bank number to rom bank 1, mode 1 additionally applies it to both rom 0 and ram
			bool multicart_1MB = 0;   // only uses four bits from "inner" bank number

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
		};
		struct mbc3 final : public mbc_base
		{
			uint16_t rom_bank = 1;
			bool ram_enabled = 0;
			uint8_t ram_bank = 0;
			bool has_timer = false;

			mbc3(std::vector<uint8_t> in_rom);

			virtual void map_to(memory_mapper& in_memory_mapper) override;

			virtual void load_ram(std::istream& f) override;
			virtual void save_ram(std::ostream& f) override;

			//uint8_t handle_read(uint16_t address);
			void handle_write(uint16_t address, uint8_t value);
		};
		struct mbc5 final : public mbc_base
		{
			uint16_t rom_bank = 1;
			bool ram_enabled = 0;
			uint8_t ram_bank = 0;

			mbc5(std::vector<uint8_t> in_rom);

			virtual void map_to(memory_mapper& in_memory_mapper) override;

			void handle_write(uint16_t address, uint8_t value);
		};

		std::unique_ptr<mbc_base> mbc;
	};
}
