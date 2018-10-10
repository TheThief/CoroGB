#pragma once

#include <algorithm>
#include <type_traits>
#include <vector>

namespace utils
{
	template<typename T1, typename T2, typename = std::enable_if_t<std::is_integral_v<T1> && std::is_integral_v<T2>>>
	inline auto ceil_div(T1 x, T2 y)
	{
		return x / y + (T1)(x % y != 0);
	}

	template<typename T, typename Container = std::vector<T>, class Compare = std::less<typename Container::value_type>>
	struct sorted
	{
	public:
		using container_type = Container;
		using value_compare = Compare;
		using value_type = typename Container::value_type;
		using size_type = typename Container::size_type;
		using reference = typename Container::reference;
		using const_reference = typename Container::const_reference;
		using iterator = typename Container::iterator;
		using const_iterator = typename Container::const_iterator;

		// constructors:
		sorted() : sorted(Compare(), Container())
		{ }
		explicit sorted(const Compare& compare)
			: sorted(compare, Container())
		{ }
		sorted(const Compare& compare, const Container& cont)
			: comp(compare)
			, c(cont)
		{ }
		sorted(const Compare& compare, Container&& cont)
			: comp(compare)
			, c(std::move(cont))
		{ }

		~sorted() = default;

		// copy/move
		sorted(const sorted& other) = default;
		sorted(sorted&& other) = default;

		sorted& operator=(const sorted& other) = default;
		sorted& operator=(sorted&& other) = default;

		// Element Access
		reference at(size_type pos)
		{
			return c.at(pos);
		}
		const_reference at(size_type pos) const
		{
			return c.at(pos);
		}

		reference operator[](size_type pos)
		{
			return c[pos];
		}

		const_reference operator[](size_type pos) const
		{
			return c[pos];
		}

		reference front()
		{
			return c.front();
		}

		const_reference front() const
		{
			return c.front();
		}

		reference back()
		{
			return c.back();
		}

		const_reference back() const
		{
			return c.back();
		}

		T* data() noexcept(noexcept(c.data()))
		{
			return c.data();
		}
		const T* data() const noexcept(noexcept(c.data()))
		{
			return c.data();
		}

		// Iterators
		iterator begin() noexcept(noexcept(c.begin()))
		{
			return c.begin();
		}
		const_iterator begin() const noexcept(noexcept(c.begin()))
		{
			return c.begin();
		}
		const_iterator cbegin() const noexcept(noexcept(c.cbegin()))
		{
			return c.cbegin();
		}

		iterator end() noexcept(noexcept(c.end()))
		{
			return c.end();
		}
		const_iterator end() const noexcept(noexcept(c.end()))
		{
			return c.end();
		}
		const_iterator cend() const noexcept(noexcept(c.cend()))
		{
			return c.cend();
		}

		// Capacity
		[[nodiscard]] bool empty() const noexcept(noexcept(c.empty()))
		{
			return c.empty();
		}

		size_type size() const noexcept(noexcept(c.size()))
		{
			return c.size();
		}

		// Modifiers
		iterator erase(const_iterator pos)
		{
			return c.erase(pos);
		}
		iterator erase(const_iterator first, const_iterator last)
		{
			return c.erase(first, last);
		}

		void push(const value_type& value)
		{
			auto it = upper_bound(value);
			c.insert(it, value);
		}

		void push(value_type&& value)
		{
			auto it = upper_bound(value);
			c.insert(it, std::move(value));
		}

		void pop_back()
		{
			c.pop_back();
		}

		//Algorithms
		iterator find(const T& value)
		{
			for (iterator it = lower_bound(value); it != end(); ++it)
			{
				if (*it == value)
				{
					return it;
				}
				if (comp(value, *it))
				{
					break;
				}
			}
			return end();
		}

		iterator lower_bound(const T& value)
		{
			for (iterator it = begin(); it != end(); ++it)
			{
				if (!comp(*it, value))
				{
					return it;
				}
			}
			return end();
		}

		iterator upper_bound(const T& value)
		{
			for (iterator it = begin(); it != end(); ++it)
			{
				if (comp(value, *it))
				{
					return it;
				}
			}
			return end();
		}

	protected:
		Container c;
		Compare comp;
	};
}
