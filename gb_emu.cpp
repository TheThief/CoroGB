#include "gb_emu.h"

#include <algorithm>

namespace coro_gb
{
	void emu::select_palette(palette_preset in_palette_preset)
	{
		static const constexpr std::array<uint32_t, 4> palette_grey =
		{
			0xFFFFFFFF,
			0xFFAAAAAA,
			0xFF555555,
			0xFF000000,
		};
		static const constexpr std::array<uint32_t, 4> palette_green =
		{
			0xFFe0f8d0,
			0xFF88c070,
			0xFF346856,
			0xFF081820,
		};
		static const constexpr std::array<uint32_t, 4> palette_blue =
		{
			0xFFe5f1f3,
			0xFF7ba8b8,
			0xFF30617b,
			0xFF08263b,
		};
		static const constexpr std::array<uint32_t, 4> palette_red =
		{
			0xFFf3f1e5,
			0xFFb8a87b,
			0xFF7b6130,
			0xFF3b2608,
		};

		switch (in_palette_preset)
		{
		case palette_preset::grey:
			std::fill(palette.begin(), palette.end(), palette_grey);
			break;
		case palette_preset::green:
			std::fill(palette.begin(), palette.end(), palette_green);
			break;
		case palette_preset::blue:
			std::fill(palette.begin(), palette.end(), palette_blue);
			break;
		case palette_preset::red:
			std::fill(palette.begin(), palette.end(), palette_red);
			break;
		case palette_preset::gbr:
			palette[0] = palette_green;
			palette[1] = palette_blue;
			palette[2] = palette_red;
			break;
		}
	}
}
