#pragma once

#include <coroutine>
#include <functional>

namespace coro_gb
{
	struct interrupt final
	{
		interrupt() noexcept = default;
		interrupt(const interrupt& rhs) = delete;
		interrupt(interrupt&& rhs) = delete;
		interrupt& operator=(const interrupt& rhs) = delete;
		interrupt& operator=(interrupt&& rhs) = delete;

		bool await_ready() noexcept
		{
			return is_triggered;
		}
		void await_resume() noexcept
		{
		}
		void await_suspend(std::coroutine_handle<> handle) noexcept
		{
			bound_function = handle;
		}
		void trigger() noexcept
		{
			if (bound_function)
			{
				// shuffle to local in case the bound function ends up waiting on this interrupt again
				std::function<void()> local_bound_function = std::move(bound_function);
				local_bound_function();
			}
			else
			{
				is_triggered = true;
			}
		}
		void reset() noexcept
		{
			is_triggered = false;
		}
		void set_callback(std::function<void()> to_bind) noexcept
		{
			bound_function = to_bind;
		}

	private:
		std::function<void()> bound_function{ nullptr };
		bool is_triggered{ false };
	};
};
