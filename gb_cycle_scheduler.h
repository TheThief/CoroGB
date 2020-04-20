#pragma once

#include <experimental/coroutine>

#include "gb_interrupt.h"
#include "utils.h"

namespace std
{
	using experimental::suspend_always;
	using experimental::coroutine_handle;
}

namespace coro_gb
{
	struct interrupt;

	struct cycle_scheduler final
	{
	public:
		enum class unit : uint8_t
		{
			debug,
			dma,
			cpu, // cpu clocks on the rising edge
			gpu, // gpu clocks on the falling edge (inverted clock)
			//serial,
			//sound,
		};

		enum class priority : uint8_t
		{
			read,
			write=read, // because cpu and gpu clock on different edges, I don't think we actually need different priority for reads/writes
		};

		struct awaitable_cycles_base
		{
			awaitable_cycles_base(cycle_scheduler& scheduler, cycle_scheduler::unit unit, cycle_scheduler::priority priority, uint32_t wait);

			bool await_ready();
			void await_resume();
			void await_suspend(std::coroutine_handle<> handle);

		protected:
			cycle_scheduler& scheduler;
			uint32_t wait_until;
			unit unit;
			priority priority;
		};

		struct awaitable_cycles final : protected awaitable_cycles_base
		{
			awaitable_cycles(cycle_scheduler& scheduler, cycle_scheduler::unit unit, cycle_scheduler::priority priority, uint32_t wait);

			using awaitable_cycles_base::await_ready;
			using awaitable_cycles_base::await_resume;
			using awaitable_cycles_base::await_suspend;
		};

		awaitable_cycles cycles(unit unit, priority priority, uint32_t wait)
		{
			return { *this, unit, priority, wait };
		}

		struct awaitable_cycles_interruptible final : protected awaitable_cycles_base
		{
			awaitable_cycles_interruptible(cycle_scheduler& scheduler, interrupt& awaited_interrupt, cycle_scheduler::unit unit, cycle_scheduler::priority priority, uint32_t wait);

			bool await_ready();
			bool await_resume(); // returns true if interrupted
			void await_suspend(std::coroutine_handle<> handle);

		protected:
			interrupt& awaited_interrupt;
			std::coroutine_handle<> suspended_coroutine;
		};

		awaitable_cycles_interruptible interruptible_cycles(interrupt& interrupt, unit unit, priority priority, uint32_t wait)
		{
			return awaitable_cycles_interruptible{ *this, interrupt, unit, priority, wait };
		}

		uint32_t get_cycle_counter() const
		{
			return cycle_counter;
		}

		void queue(uint32_t at, uint16_t priority, std::function<void()> fn);

		void tick(uint32_t num_cycles);

	private:
		struct cycle_wait final
		{
			uint32_t wait_until;
			uint16_t priority;
			std::function<void()> queued_function;

			friend bool operator==(const cycle_wait& lhs, const cycle_wait& rhs)
			{
				return std::make_tuple(lhs.wait_until, lhs.priority)
					== std::make_tuple(rhs.wait_until, rhs.priority);
			}
		};

		struct cycle_comparator final
		{
			cycle_comparator(const cycle_scheduler& scheduler) :
				scheduler{ &scheduler }
			{
			}

			bool operator ()(const cycle_wait& lhs, const cycle_wait& rhs) const
			{
				// ordered by closest to the present time, then by priority
				return std::make_tuple((int32_t)(lhs.wait_until - scheduler->cycle_counter), lhs.priority)
					> std::make_tuple((int32_t)(rhs.wait_until - scheduler->cycle_counter), rhs.priority);
			}

			const cycle_scheduler* scheduler;
		};

		uint32_t cycle_counter = 0;
		unit current_unit = unit::debug;
		uint32_t next = 0;
		uint16_t next_priority = 0;
		uint32_t end = 0;
		utils::sorted< cycle_wait, std::vector<cycle_wait>, cycle_comparator> queued{ cycle_comparator{*this} };

		friend awaitable_cycles;
		friend awaitable_cycles_interruptible;
	};

	////////////////////////////////////////////////////////////////

	inline cycle_scheduler::awaitable_cycles_base::awaitable_cycles_base(cycle_scheduler& scheduler, cycle_scheduler::unit unit, cycle_scheduler::priority priority, uint32_t wait) :
		scheduler{ scheduler },
		wait_until{ scheduler.cycle_counter + wait },
		unit{ unit },
		priority{ priority }
	{
	}

	inline bool cycle_scheduler::awaitable_cycles_base::await_ready()
	{
		if (scheduler.current_unit == unit &&
			std::make_tuple((int32_t)(wait_until - scheduler.cycle_counter), ((uint16_t)priority << 8 | (uint8_t)unit))
			< std::make_tuple((int32_t)(scheduler.next - scheduler.cycle_counter), scheduler.next_priority))
		{
			scheduler.cycle_counter = wait_until;
			return true;
		}
		return false;
	}

	inline void cycle_scheduler::awaitable_cycles_base::await_resume()
	{
		//scheduler.cycle_counter = wait_until;
		scheduler.current_unit = unit;
	}

	////////////////////////////////////////////////////////////////

	inline cycle_scheduler::awaitable_cycles::awaitable_cycles(cycle_scheduler& scheduler, cycle_scheduler::unit unit, cycle_scheduler::priority priority, uint32_t wait) :
		awaitable_cycles_base{ scheduler, unit, priority, wait }
	{
	}

	////////////////////////////////////////////////////////////////

	inline cycle_scheduler::awaitable_cycles_interruptible::awaitable_cycles_interruptible(cycle_scheduler& scheduler, interrupt& awaited_interrupt, cycle_scheduler::unit unit, cycle_scheduler::priority priority, uint32_t wait)
		: awaitable_cycles_base{ scheduler, unit, priority, wait }
		, awaited_interrupt{ awaited_interrupt }
	{
	}

	inline bool cycle_scheduler::awaitable_cycles_interruptible::await_ready()
	{
		return awaitable_cycles_base::await_ready() || awaited_interrupt.await_ready();
	}

	inline void cycle_scheduler::awaitable_cycles_interruptible::await_suspend(std::coroutine_handle<> handle)
	{
		awaitable_cycles_base::await_suspend(handle);
		awaited_interrupt.await_suspend(handle);
		suspended_coroutine = handle;
	}
}
