#pragma once

#include <experimental/coroutine>
#include <functional>

namespace std
{
	using experimental::suspend_always;
	using experimental::suspend_never;
	using experimental::coroutine_handle;
}

namespace coro_gb
{
	struct interrupt final
	{
		interrupt() = default;
		interrupt(const interrupt& rhs) = delete;
		interrupt(interrupt&& rhs) = delete;
		interrupt& operator=(const interrupt& rhs) = delete;
		interrupt& operator=(interrupt&& rhs) = delete;

		bool await_ready()
		{
			return is_triggered;
		}
		void await_resume()
		{
		}
		void await_suspend(std::coroutine_handle<> handle)
		{
			bound_function = handle;
		}
		void trigger()
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
		void reset()
		{
			is_triggered = false;
		}
		void set_callback(std::function<void()> to_bind)
		{
			bound_function = to_bind;
		}

	private:
		std::function<void()> bound_function{ nullptr };
		bool is_triggered{ false };
	};
};
