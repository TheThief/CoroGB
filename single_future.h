#pragma once

#include <coroutine>
#include <optional>

template<typename T>
struct single_future;

template<typename T>
struct [[nodiscard]] single_future final
{
	struct promise_t final
	{
#if _DEBUG
		~promise_t() noexcept
		{
			value = std::nullopt;
		}
#endif

		single_future<T> get_return_object() noexcept
		{
			return {*this};
		}
		auto initial_suspend() noexcept
		{
			return std::suspend_never();
		}
		auto final_suspend() noexcept
		{
			struct chain
			{
				std::coroutine_handle<> handle;

				bool await_ready() noexcept
				{
					return false;
				}

				std::coroutine_handle<> await_suspend(std::coroutine_handle<>) noexcept
				{
					if (handle)
					{
						return handle;
					}
					return std::noop_coroutine();
				}

				void await_resume() noexcept
				{
				}
			};
			return chain{ then };
		}

		template<typename T>
		void return_value(T&& value) noexcept
		{
			this->value = std::forward<T>(value);
		}

		void unhandled_exception() noexcept
		{
			exception = std::current_exception();
			value = std::nullopt;
		}

	private:
		std::coroutine_handle<> then{ nullptr };
		std::exception_ptr exception{ nullptr };
		std::optional<T> value{ std::nullopt };

		friend single_future<T>;
	};

	using promise_type = promise_t;
	using handle_type = std::coroutine_handle<promise_type>;

	bool await_ready() noexcept
	{
		return handle.done();
	}
	T await_resume()
	{
		return get();
	}
	void await_suspend(std::coroutine_handle<> handle) noexcept
	{
		promise->then = handle;
	}

	single_future() noexcept = default;
	single_future(promise_type& promise) noexcept
		: promise{ &promise }
		, handle{ std::coroutine_handle<promise_type>::from_promise(promise) }
	{
	}
	single_future(const single_future& rhs) = delete;
	single_future(single_future&& rhs) noexcept
		: promise{ rhs.promise }
		, handle{ std::move(rhs.handle) }
	{
		rhs.handle = nullptr;
	}
	single_future& operator=(const single_future& rhs) = delete;
	single_future& operator=(single_future&& rhs) noexcept
	{
		promise = rhs.promise;
		handle = std::move(rhs.handle);

		rhs.promise = nullptr;
		rhs.handle = nullptr;
		return *this;
	}
	~single_future() noexcept
	{
		if (handle) handle.destroy();
	}
	bool is_ready() const noexcept
	{
		return handle.done();
	}
	T get()
	{
		auto exception = std::move(promise->exception);
		if (exception)
		{
			handle.destroy();
			promise = nullptr;
			handle = nullptr;
			std::rethrow_exception(exception);
		}
		auto value = std::move(*promise->value);
		handle.destroy();
		promise = nullptr;
		handle = nullptr;
		return value;
	}

private:
	promise_type* promise{ nullptr };
	handle_type handle{ nullptr };

	friend promise_type;
};

template<>
struct single_future<void> final
{
	struct promise_t final
	{
		single_future<void> get_return_object() noexcept
		{
			return {*this};
		}
		auto initial_suspend() noexcept
		{
			return std::suspend_never();
		}
		auto final_suspend() noexcept
		{
			struct chain
			{
				std::coroutine_handle<> handle;

				bool await_ready() noexcept
				{
					return false;
				}

				std::coroutine_handle<> await_suspend(std::coroutine_handle<>) noexcept
				{
					if (handle)
					{
						return handle;
					}
					return std::noop_coroutine();
				}

				void await_resume() noexcept
				{
				}
			};
			return chain{ then };
		}

		void return_void() noexcept
		{
		}

		void unhandled_exception() noexcept
		{
			exception = std::current_exception();
		}

	private:
		std::coroutine_handle<> then{ nullptr };
		std::exception_ptr exception{ nullptr };

		friend single_future<void>;
	};

	using promise_type = promise_t;
	using handle_type = std::coroutine_handle<promise_type>;

	bool await_ready() noexcept
	{
		return handle.done();
	}
	void await_resume()
	{
		get();
	}
	void await_suspend(std::coroutine_handle<> handle) noexcept
	{
		promise->then = handle;
	}

	single_future() = default;
	single_future(promise_type& promise) noexcept
		: promise{ &promise }
		, handle{ std::coroutine_handle<promise_type>::from_promise(promise) }
	{
	}
	single_future(const single_future& rhs) = delete;
	single_future(single_future&& rhs) noexcept
		: promise{ rhs.promise }
		, handle{ std::move(rhs.handle) }
	{
		rhs.promise = nullptr;
		rhs.handle = nullptr;
	}
	single_future& operator=(const single_future& rhs) = delete;
	single_future& operator=(single_future&& rhs) noexcept
	{
		promise = rhs.promise;
		handle = std::move(rhs.handle);

		rhs.promise = nullptr;
		rhs.handle = nullptr;
		return *this;
	}
	~single_future() noexcept
	{
		if (handle) handle.destroy();
	}

	bool is_ready() const noexcept
	{
		return handle.done();
	}
	void get()
	{
		auto exception = std::move(promise->exception);
		if (promise->exception)
		{
			handle.destroy();
			promise = nullptr;
			handle = nullptr;
			std::rethrow_exception(exception);
		}
		handle.destroy();
		promise = nullptr;
		handle = nullptr;
	}

private:
	promise_type * promise{ nullptr };
	handle_type handle{ nullptr };

	friend promise_type;
};
