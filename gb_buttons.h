#pragma once

#include <cstdint>

namespace coro_gb
{
	enum class button_id : uint8_t
	{
		right,
		left,
		up,
		down,
		a,
		b,
		select,
		start,
	};

	enum class button_state : uint8_t
	{
		down,
		up,
	};
}
