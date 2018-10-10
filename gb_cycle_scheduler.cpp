#include "gb_cycle_scheduler.h"

namespace coro_gb
{
	void cycle_scheduler::queue(uint32_t at, uint16_t priority, std::function<void()> fn)
	{
		if (std::make_tuple((int32_t)(at - cycle_counter), priority)
			< std::make_tuple((int32_t)(next - cycle_counter), next_priority))
		{
			next = at;
			next_priority = priority;
		}

		queued.push({ at, priority, fn });
	}

	void cycle_scheduler::tick(uint32_t num_cycles)
	{
		end = cycle_counter + num_cycles;
		while (!queued.empty() &&
			(int32_t)(queued.back().wait_until - cycle_counter) <= (int32_t)(end - cycle_counter))
		{
			cycle_wait top = std::move(queued.back());
			queued.pop_back();
			cycle_counter = top.wait_until;
			next = end;
			next_priority = 0;
			if (!queued.empty())
			{
				if ((int32_t)(queued.back().wait_until - cycle_counter) < (int32_t)(end - cycle_counter))
				{
					next = queued.back().wait_until;
					next_priority = queued.back().priority;
				}
			}
			top.queued_function();
		}

		cycle_counter = end;
	}

	////////////////////////////////////////////////////////////////

	void cycle_scheduler::awaitable_cycles_base::await_suspend(std::coroutine_handle<> handle)
	{
		uint16_t priority_value = ((uint16_t)priority << 8 | (uint8_t)unit);
		scheduler.queue(wait_until, priority_value, handle);
	}

	bool cycle_scheduler::awaitable_cycles_interruptible::await_resume()
	{
		awaited_interrupt.set_callback(nullptr);

		uint16_t priority_value = ((uint16_t)priority << 8 | (uint8_t)unit);
		auto it = scheduler.queued.find({ wait_until, priority_value, suspended_coroutine });
		if (it != scheduler.queued.end())
		{
			// still in cycle queue so must be interrupt
			scheduler.queued.erase(it);
			return true;
		}
		else
		{
			// not in cycle queue so must be timeout
			awaitable_cycles_base::await_resume();
			return false;
		}
	}
}
