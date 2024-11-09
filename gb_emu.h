#pragma once

#include "gb_cpu.h"
#include "gb_ppu.h"
#include "gb_buttons.h"
#include "gb_cycle_scheduler.h"
#include "gb_memory_mapper.h"
#include "single_future.h"
#include "gb_cart.h"

#include <array>
#include <chrono>
#include <filesystem>

namespace coro_gb
{
	struct emu;

	// 456 Cycles per line
	// 70224 Cycles per frame (16.6ms / 59.73 Hz)
	using cycles = std::chrono::duration<int64_t, std::ratio<1, 4'194'304>>;

	enum class palette_preset : uint8_t
	{
		grey,
		green,
		blue,
		red,
		gbr,
	};

	struct emu final
	{
		emu();
		~emu();

		void start();

		void load_boot_rom(std::filesystem::path boot_rom_path);
		void load_cart(cart& in_cart);

		uint32_t get_cycle_counter() const;
		void tick(uint32_t num_cycles);

		bool is_screen_enabled() const;
		const uint8_t* get_screen_buffer() const;
		const uint32_t* get_palette() const;
		void select_palette(palette_preset in_palette_preset);
		void set_display_callback(std::function<void()> display_callback);

		void input(button_id button, button_state state);

	protected:
		cycle_scheduler scheduler;
		memory_mapper memory_mapper;
		cpu cpu;
		ppu ppu;
		std::array<std::array<uint32_t, 4>, 3> palette;
		cart* loaded_cart = nullptr;
		single_future<void> cpu_running;
		single_future<void> ppu_running;
	};

	inline emu::emu()
		: memory_mapper{ scheduler }
		, cpu{ scheduler, memory_mapper }
		, ppu{ scheduler, memory_mapper }
	{
		select_palette(palette_preset::green);
	}

	inline emu::~emu()
	{
		if (loaded_cart)
		{
			loaded_cart->unmap();
			loaded_cart = nullptr;
		}
	}

	inline void emu::start()
	{
		if (!loaded_cart)
		{
			throw std::runtime_error("no cart loaded!");
		}
		cpu_running = cpu.run();
		ppu_running = ppu.run();
	}

	inline void emu::load_boot_rom(std::filesystem::path boot_rom_path)
	{
		memory_mapper.load_boot_rom(std::move(boot_rom_path));
	}

	inline void emu::load_cart(cart& in_cart)
	{
		loaded_cart = &in_cart;
		in_cart.map(memory_mapper);
	}

	inline uint32_t emu::get_cycle_counter() const
	{
		return scheduler.get_cycle_counter();
	}

	inline void emu::tick(uint32_t num_cycles)
	{
		scheduler.tick(num_cycles);

		if (cpu_running.is_ready())
		{
			cpu_running.get();
		}
		if (ppu_running.is_ready())
		{
			ppu_running.get();
		}
	}

	inline bool emu::is_screen_enabled() const
	{
		return ppu.is_screen_enabled();
	}

	inline const uint8_t* emu::get_screen_buffer() const
	{
		return ppu.get_screen_buffer();
	}

	inline const uint32_t* emu::get_palette() const
	{
		return reinterpret_cast<const uint32_t*>(palette.data());
	}

	inline void emu::set_display_callback(std::function<void()> display_callback)
	{
		ppu.set_display_callback(std::move(display_callback));
	}

	inline void emu::input(button_id button, button_state state)
	{
		memory_mapper.input(button, state);
		memory_mapper.interrupts.cpu_wake.trigger();
	}
}
