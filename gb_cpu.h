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
		union
		{
			uint16_t AF;
			struct
			{
				union
				{
					uint8_t F;
					struct
					{
						uint8_t F_Padding: 4;
						uint8_t F_Carry : 1;
						uint8_t F_HalfCarry : 1;
						uint8_t F_Subtract : 1;
						uint8_t F_Zero : 1;
					};
				};
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
