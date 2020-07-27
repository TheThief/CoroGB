#pragma once

#include "gb_buttons.h"
#include "gb_interrupt.h"

#include <array>
#include <vector>
#include <filesystem>
#include <functional>
#include <variant>

namespace coro_gb
{
	struct cycle_scheduler;

	struct memory_mapper final
	{
	protected:
		cycle_scheduler& scheduler;

	public:
		memory_mapper(cycle_scheduler& scheduler);

		uint8_t read8(uint16_t address) const;
		void write8(uint16_t address, uint8_t u8);

		void load_boot_rom(std::filesystem::path boot_rom_path);

	public:
		void input(button_id button, button_state state);

		struct mapping
		{
			uint16_t start_address;
			uint16_t end_address; // inclusive
			std::variant<uint8_t*, std::function<uint8_t(uint16_t)>> read;
			std::variant<uint8_t*, std::function<void(uint16_t, uint8_t)>> write;

			friend bool operator<(const mapping& lhs, const mapping& rhs)
			{
				return std::tie(lhs.start_address, lhs.end_address) < std::tie(rhs.start_address, rhs.end_address);
			}
		};
	protected:
		std::vector<mapping> mappings;
	public:
		void set_mapping(mapping new_mapping);
		void remove_mapping(mapping new_mapping);
	protected:
		const mapping* find_mapping(uint16_t address) const;

		// 0x0000 - 0x3FFF: Permanently - mapped ROM bank
		// 0x4000 - 0x7FFF: Area for switchable ROM banks
		// 0x8000 - 0x9FFF: Video RAM
		// 0xA000 - 0xBFFF: Area for switchable external RAM banks
		// 0xC000 - 0xDFFF: Game Boy’s working RAM bank 0 / 1
		// 0xE000 - 0xFDFF: Mirror of WRAM
		// 0xFE00 - 0xFEA0: Sprite Attribute Table
		// 0xFF00 - 0xFF7F: Memory-mapped registers
			// Interrupt Flag 0xFF0F
		// 0xFF80 - 0xFFFE: High RAM Area
		// 0xFFFF : Interrupt Enable Register

		std::vector<uint8_t> boot_rom; // mapped at 0x0000 (over cartridge rom) until boot is complete
		std::array<uint8_t, 8192> wram;

	public:
		// 0xFF00 - 0xFF7F: Devices’ Mappings.Used to access I / O devices.

		// 0xFF00 - P1/JOYP - Joypad
		union joypad_t
		{
			uint8_t u8 = 0xCF;
			struct
			{
				uint8_t right_a    : 1; // Bit 0 - P10 Input Right or Button A (0 = Pressed) (Read Only)
				uint8_t left_b     : 1; // Bit 1 - P11 Input Left  or Button B (0 = Pressed) (Read Only)
				uint8_t up_select  : 1; // Bit 2 - P12 Input Up    or Select   (0 = Pressed) (Read Only)
				uint8_t down_start : 1; // Bit 3 - P13 Input Down  or Start    (0 = Pressed) (Read Only)

				uint8_t select_dirs    : 1; // Bit 4 - P14 Select Direction Keys(0 = Select)
				uint8_t select_buttons : 1; // Bit 5 - P15 Select Button Keys(0 = Select)
			};
		} joypad;

		// Serial port
		// 0xFF01 - SB - Serial transfer data
		// 8 Bits of data to be read / written
		uint8_t serial_data = 0;
		// 0xFF02 - SC - Serial Transfer Control
		union serial_control_t
		{
			uint8_t u8 = 0x7E;
			struct
			{
				uint8_t clock_source : 1; // Bit 0: Shift Clock (0 = External Clock, 1 = Internal Clock)
				uint8_t : 6;
				uint8_t transfer     : 1; // Bit 7: Transfer Start Flag (0 = No Transfer, 1 = Start)
			};
		} serial_control;

		// 0xFF03
		uint8_t _ff03 = 0xFF;

		// Timer
		// 0xFF04 - DIV - Timer Divider Register
		// This register is incremented at rate of 16384Hz. Writing any value to this register resets it to 00h.
		mutable uint8_t timer_div = 0;
		// 0xFF05 - TIMA - Timer counter
		// This timer is incremented by a clock frequency specified by the TAC register ($FF07). When the value overflows(gets bigger than FFh) then it will be reset to the value specified in TMA(FF06), and an interrupt will be requested.
		uint8_t timer_counter = 0;
		// 0xFF06 - TMA - Timer Modulo
		// When the TIMA overflows, this data will be loaded.
		uint8_t timer_reset_value = 0;
		// 0xFF07 - TAC - Timer Control
		union timer_control_t
		{
			uint8_t u8 = 0xF8;
			struct
			{
				uint8_t rate   : 2; // Bits 0-1: 00: 4096 Hz, 01: 262144 Hz, 10: 65536 Hz, 11: 16384 Hz
				uint8_t enable : 1; // Bit 2: 0 = Stop, 1 = Start
			};
		} timer_control;

		// 0xFF08-0xFF0E
		uint8_t _ff08[7] = { 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF };

		// 0xFF0F - Interrupt Flag
		union interrupt_bits_t
		{
			uint8_t u8 = 0xE0;
			struct
			{
				uint8_t vblank : 1; // Bit 0: V-Blank  Interrupt Request (INT 40h)  (1 = Request)
				uint8_t stat   : 1; // Bit 1: LCD STAT Interrupt Request (INT 48h)  (1 = Request)
				uint8_t timer  : 1; // Bit 2: Timer    Interrupt Request (INT 50h)  (1 = Request)
				uint8_t serial : 1; // Bit 3: Serial   Interrupt Request (INT 58h)  (1 = Request)
				uint8_t joypad : 1; // Bit 4: Joypad   Interrupt Request (INT 60h)  (1 = Request)
			};

			friend interrupt_bits_t operator&(interrupt_bits_t lhs, interrupt_bits_t rhs)
			{
				interrupt_bits_t result;
				result.u8 = lhs.u8 & rhs.u8;
				return result;
			}
		} interrupt_flag;

		// Audio
		// 0xFF10 - 0xFF23
		uint8_t audio_registers[20] =
		{
			0x80, 0x3F, 0x00, 0x00, 0xB8,
			0xFF, 0x3F, 0x00, 0x00, 0xB8,
			0x7F, 0xFF, 0x9F, 0x00, 0xB8,
			0xFF, 0xFF, 0x00, 0x00, 0xBF,
		};
		// 0xFF24 - 0xFF26
		uint8_t audio_control[3] =
		{
			0x00, 0x00, 0x70,
		};
		// 0xFF27 - 0xFF2F
		uint8_t _ff27[9] = { 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF };
		// 0xFF30 - 0xFF3F
		uint8_t audio_wave[16] = { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, };

		// 0xFF40 - 0xFF4B - PPU registers

		// 0xFF50 boot rom disable
		uint8_t boot_rom_disable = 1;

		std::array<uint8_t, 127> hram; // 0xFF80 - 0xFFFE: High RAM Area.

		// 0xFFFF : Interrupt Enable Register.
		interrupt_bits_t interrupt_enable;

		uint32_t timer_div_reset = 0;

		button_state buttons[8] = { button_state::up, button_state::up, button_state::up, button_state::up, button_state::up, button_state::up, button_state::up, button_state::up };

	public:
		struct
		{
			interrupt cpu_wake;
		} interrupts;
	};
}
