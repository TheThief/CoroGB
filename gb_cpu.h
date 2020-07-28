#pragma once

#include "gb_cycle_scheduler.h"

#include <cstdint>

template <typename T>
struct single_future;

namespace coro_gb
{
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

		single_future<void> run();

	protected:
		registers_t registers;
		cycle_scheduler& scheduler;
		memory_mapper& memory;

		cycle_scheduler::awaitable_cycles cycles(cycle_scheduler::priority priority, uint32_t wait);
	};
}
