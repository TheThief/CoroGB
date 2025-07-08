#pragma once

#include "gb_cycle_scheduler.h"

#include <cstdint>

template <typename T>
struct single_future;

namespace coro_gb
{
	enum test_status
	{
		running,
		pass,
		fail,
	};

	struct memory_mapper;

	struct registers_t final
	{
		struct flags final
		{
			uint8_t padding    : 4;
			uint8_t carry      : 1;
			uint8_t half_carry : 1;
			uint8_t subtract   : 1;
			uint8_t zero       : 1;
		};

		union
		{
			uint16_t AF;
			struct
			{
				flags F;
				uint8_t A;
			};
		};
		union
		{
			uint16_t BC;
			struct
			{
				uint8_t C;
				uint8_t B;
			};
		};
		union
		{
			uint16_t DE;
			struct
			{
				uint8_t E;
				uint8_t D;
			};
		};
		union
		{
			uint16_t HL;
			struct
			{
				uint8_t L;
				uint8_t H;
			};
		};
		uint16_t SP;
		uint16_t PC = 0;
		bool enable_interrupts = false;
		bool enable_interrupts_delay = false;
	};

	struct cpu final
	{
		cpu(cycle_scheduler& scheduler, memory_mapper& memory);

		single_future<test_status> run();
		test_status get_mooneye_result() const;

	protected:
		registers_t registers;
		cycle_scheduler& scheduler;
		memory_mapper& memory;

		cycle_scheduler::awaitable_cycles cycles(cycle_scheduler::priority priority, uint32_t wait);

	public:
		bool break_on_ld_b_b = false;
	};

	inline test_status cpu::get_mooneye_result() const
	{
		if (
			registers.B == 3 &&
			registers.C == 5 &&
			registers.D == 8 &&
			registers.E == 13 &&
			registers.H == 21 &&
			registers.L == 34)
			return test_status::pass;
		if (
			registers.B == 0x42 &&
			registers.C == 0x42 &&
			registers.D == 0x42 &&
			registers.E == 0x42 &&
			registers.H == 0x42 &&
			registers.L == 0x42)
			return test_status::fail;
		return test_status::running;
	}
}
