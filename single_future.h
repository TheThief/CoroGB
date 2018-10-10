#pragma once

#include <experimental/coroutine>
#include <optional>

namespace std
{
	using experimental::suspend_always;
	using experimental::suspend_never;
	using experimental::coroutine_handle;
}

template<typename T>
struct single_future;

template<typename T>
struct [[nodiscard]] single_future final
{
	struct promise final
	{
		single_future<T> get_return_object()
		{
			return {*this};
		}
		auto initial_suspend() { return std::suspend_never(); }
		auto final_suspend()
		{
			struct chain
			{
				std::coroutine_handle<> handle;

				bool await_ready() { return false; }

				void await_suspend(std::coroutine_handle<>)
				{
					if (handle)
					{
						handle.resume();
					}
				}

				void await_resume() {}
			};
			return chain{ future->then };
		}

		template<typename T>
		void return_value(T&& value)
		{
			future->value = std::forward<T>(value);
		}

		void unhandled_exception()
		{
			future->exception = std::current_exception();
			future->value = std::nullopt;
		}

	private:
		single_future<T>* future;

		friend single_future<T>;
	};

	using promise_type = promise;
	using handle_type = std::coroutine_handle<promise_type>;

	bool await_ready()
	{
		return handle.done();
	}
	T await_resume()
	{
		return get();
	}
	void await_suspend(std::coroutine_handle<> handle)
	{
		then = handle;
	}

	single_future() = default;
	single_future(promise_type& promise)
		: promise{ &promise }
		, handle{ std::coroutine_handle<promise_type>::from_promise(promise) }
	{
		promise.future = this;
	}
	single_future(const single_future& rhs) = delete;
	single_future(single_future&& rhs)
		: promise{ rhs.promise }
		, handle{ std::move(rhs.handle) }
		, then{ rhs.then }
		, exception{ std::move(rhs.exception) }
		, value{ std::move(rhs.value) }
	{
		rhs.handle = nullptr;
		if (promise)
		{
			promise->future = this;
		}
	}
	single_future& operator=(const single_future& rhs) = delete;
	single_future& operator=(single_future&& rhs)
	{
		promise = rhs.promise;
		handle = std::move(rhs.handle);
		then = rhs.then;
		exception = std::move(rhs.exception);
		value = std::move(rhs.value);

		rhs.handle = nullptr;
		if (promise)
		{
			promise->future = this;
		}
		return *this;
	}
	~single_future()
	{
		if (handle) handle.destroy();
	}
	bool is_ready() const
	{
		return handle.done();
	}
	T get()
	{
		if (exception)
		{
			std::rethrow_exception(exception);
		}
		return std::move(*value);
	}

private:
	promise_type* promise{ nullptr };
	handle_type handle{ nullptr };
	std::coroutine_handle<> then{ nullptr };
	std::exception_ptr exception{ nullptr };
	std::optional<T> value{ std::nullopt };

	friend promise_type;
};

template<>
struct single_future<void> final
{
	struct promise final
	{
		single_future<void> get_return_object()
		{
			return {*this};
		}
		auto initial_suspend() { return std::suspend_never(); }
		auto final_suspend()
		{
			struct chain
			{
				std::coroutine_handle<> handle;

				bool await_ready() { return false; }

				void await_suspend(std::coroutine_handle<>)
				{
					if (handle)
					{
						handle.resume();
					}
				}

				void await_resume() {}
			};
			return chain{ future->then };
		}

		void return_void()
		{
		}

		void unhandled_exception()
		{
			future->exception = std::current_exception();
		}

	private:
		single_future<void>* future;

		friend single_future<void>;
	};

	using promise_type = promise;
	using handle_type = std::coroutine_handle<promise_type>;

	bool await_ready()
	{
		return handle.done();
	}
	void await_resume()
	{
		get();
	}
	void await_suspend(std::coroutine_handle<> handle)
	{
		then = handle;
	}

	single_future() = default;
	single_future(promise_type& promise)
		: promise{ &promise }
		, handle{ std::coroutine_handle<promise_type>::from_promise(promise) }
	{
		promise.future = this;
	}
	single_future(const single_future& rhs) = delete;
	single_future(single_future&& rhs)
		: promise{ rhs.promise }
		, handle{ std::move(rhs.handle) }
		, then{ rhs.then }
		, exception{ std::move(rhs.exception) }
	{
		rhs.handle = nullptr;
		if (promise)
		{
			promise->future = this;
		}
	}
	single_future& operator=(const single_future& rhs) = delete;
	single_future& operator=(single_future&& rhs)
	{
		promise = rhs.promise;
		handle = std::move(rhs.handle);
		then = rhs.then;
		exception = std::move(rhs.exception);

		rhs.handle = nullptr;
		if (promise)
		{
			promise->future = this;
		}
		return *this;
	}
	~single_future()
	{
		if (handle) handle.destroy();
	}

	bool is_ready() const
	{
		return handle.done();
	}
	void get()
	{
		if (exception)
		{
			std::rethrow_exception(exception);
		}
	}

private:
	promise_type * promise{ nullptr };
	handle_type handle{ nullptr };
	std::coroutine_handle<> then{ nullptr };
	std::exception_ptr exception{ nullptr };

	friend promise_type;
};
