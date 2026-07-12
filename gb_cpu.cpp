#include "gb_cpu.h"
#include "gb_cycle_scheduler.h"
#include "gb_memory_map.h"
#include "single_future.h"

#include <cassert>
#include <coroutine>

namespace coro_gb
{
	cpu::cpu(cycle_scheduler& scheduler, memory_map& memory)
		: scheduler(scheduler),
		memory(memory)
	{
	}

	cycle_scheduler::awaitable_cycles cpu::cycles(cycle_scheduler::priority priority, uint32_t wait)
	{
		return scheduler.cycles(cycle_scheduler::unit::cpu, priority, wait + std::exchange(additional_cycles, 0));
	}

	std::suspend_never cpu::dummy_wait(int32_t wait)
	{
		additional_cycles += wait;
		return {};
	}

	struct cpu::awaitable_cpu_op : protected cycle_scheduler::awaitable_cycles_base
	{
	protected:
		awaitable_cpu_op(cpu& in_cpu, cycle_scheduler::unit unit, cycle_scheduler::priority priority, uint32_t cycles) noexcept
			: awaitable_cycles_base(in_cpu.scheduler, unit, priority, cycles)
			, _cpu(in_cpu)
		{
		}

		cpu& _cpu;
	};

	// Template base class for 16-bit awaitable operations using CRTP
	// FirstByteResultType: the return type of first_byte.await_resume() (uint8_t for reads, void for writes)
	// If void, std::monostate is used as a placeholder in the optional as optional<void> isn't valid
	template<typename Derived, typename FirstByteResultType>
	struct cpu::awaitable_16bit_base : awaitable_cpu_op
	{
	protected:
		using result_type = std::conditional_t<std::is_void_v<FirstByteResultType>, std::monostate, FirstByteResultType>;
		std::optional<result_type> completed_first;

		awaitable_16bit_base(cpu& in_cpu, cycle_scheduler::unit unit, cycle_scheduler::priority priority, uint32_t cycles) noexcept
			: awaitable_cpu_op(in_cpu, unit, priority, cycles)
		{
		}

	public:
		bool await_ready() noexcept
		{
			Derived* self = static_cast<Derived*>(this);
			if (self->first_byte.await_ready())
			{
				if constexpr (std::is_void_v<FirstByteResultType>) {
					self->first_byte.await_resume();
					self->completed_first = std::monostate{};
				} else {
					self->completed_first = self->first_byte.await_resume();
				}
				return self->awaitable_cpu_op::await_ready();
			}
			return false;
		}

		void await_suspend(std::coroutine_handle<> handle) noexcept
		{
			Derived* self = static_cast<Derived*>(this);
			if (self->completed_first.has_value())
			{
				awaitable_cpu_op::await_suspend(handle);
			}
			else
			{
				self->first_byte.await_suspend(
					[self, handle]() mutable
					{
						if constexpr (std::is_void_v<FirstByteResultType>) {
							self->first_byte.await_resume();
							self->completed_first = std::monostate{};
						} else {
							self->completed_first = self->first_byte.await_resume();
						}

						if (self->await_ready())
						{
							handle.resume();
						}
						else
						{
							self->awaitable_cpu_op::await_suspend(handle);
						}
					});
			}
		}
	};

	struct cpu::awaitable_read8 final : awaitable_cpu_op
	{
		awaitable_read8(cpu& in_cpu, uint16_t address, int32_t additional_cycles) noexcept
			: awaitable_cpu_op(in_cpu, cycle_scheduler::unit::cpu, cycle_scheduler::priority::read, 4 + additional_cycles)
			, address(address)
		{
		}

		uint16_t address;

		using awaitable_cpu_op::await_ready;
		using awaitable_cpu_op::await_suspend;

		uint8_t await_resume() noexcept
		{
			awaitable_cpu_op::await_resume();
			return _cpu.memory.read8(address);
		}
	};

	struct cpu::awaitable_write8 final : awaitable_cpu_op
	{
		awaitable_write8(cpu& in_cpu, uint16_t address, uint8_t value, int32_t additional_cycles) noexcept
			: awaitable_cpu_op(in_cpu, cycle_scheduler::unit::cpu, cycle_scheduler::priority::write, 4 + additional_cycles)
			, address(address)
			, value(value)
		{
		}

		uint16_t address;
		uint8_t value;

		using awaitable_cpu_op::await_ready;
		using awaitable_cpu_op::await_suspend;

		void await_resume() noexcept
		{
			awaitable_cpu_op::await_resume();
			_cpu.memory.write8(address, value);
		}
	};

	struct cpu::awaitable_read16 final : awaitable_16bit_base<awaitable_read16, uint8_t>
	{
		awaitable_read16(cpu& in_cpu, uint16_t address, int32_t additional_cycles) noexcept
			: awaitable_16bit_base(in_cpu, cycle_scheduler::unit::cpu, cycle_scheduler::priority::read, 8 + additional_cycles)
			, first_byte(in_cpu, address, additional_cycles)
			, second_address(address + 1)
		{
		}

		awaitable_read8 first_byte;
		uint16_t second_address;

		using awaitable_16bit_base::await_ready;
		using awaitable_16bit_base::await_suspend;

		uint16_t await_resume() noexcept
		{
			awaitable_cpu_op::await_resume();

			assert(completed_first.has_value());
			uint16_t result = completed_first.value();
			result |= _cpu.memory.read8(second_address) << 8;
			return result;
		}
	};

	struct cpu::awaitable_write16 final : awaitable_16bit_base<awaitable_write16, void>
	{
		awaitable_write16(cpu& in_cpu, uint16_t first_address, uint8_t first_value, uint16_t second_address, uint8_t second_value, int32_t additional_cycles) noexcept
			: awaitable_16bit_base(in_cpu, cycle_scheduler::unit::cpu, cycle_scheduler::priority::write, 8 + additional_cycles)
			, first_byte(in_cpu, first_address, first_value, additional_cycles)
			, second_address(second_address)
			, second_value(second_value)
		{
		}

		awaitable_write8 first_byte;
		uint16_t second_address;
		uint8_t second_value;

		using awaitable_16bit_base::await_ready;
		using awaitable_16bit_base::await_suspend;

		void await_resume() noexcept
		{
			awaitable_cpu_op::await_resume();
			assert(completed_first.has_value());
			_cpu.memory.write8(second_address, second_value);
		}
	};

	cpu::awaitable_read8 cpu::read8(uint16_t address)
	{
		return cpu::awaitable_read8(*this, address, std::exchange(additional_cycles, 0));
	}

	cpu::awaitable_write8 cpu::write8(uint16_t address, uint8_t value)
	{
		return cpu::awaitable_write8(*this, address, value, std::exchange(additional_cycles, 0));
	}

	cpu::awaitable_read16 cpu::read16(uint16_t address)
	{
		return cpu::awaitable_read16(*this, address, std::exchange(additional_cycles, 0));
	}

	cpu::awaitable_write16 cpu::write16(uint16_t address, uint16_t value)
	{
		// Write low byte first, then high byte
		return cpu::awaitable_write16(
			*this,
			address,
			static_cast<uint8_t>(value & 0xFF),
			address + 1,
			static_cast<uint8_t>(value >> 8),
			std::exchange(additional_cycles, 0));
	}

	cpu::awaitable_read8 cpu::fetch8()
	{
		return read8(registers.PC++);
	}

	cpu::awaitable_read16 cpu::fetch16()
	{
		uint16_t address = registers.PC;
		registers.PC += 2;
		return read16(address);
	}

	cpu::awaitable_write16 cpu::push16(uint16_t value)
	{
		// The gameboy CPU doesn't have pre-decrement, so we need an additional M cycle to decrement before the first write
		// The write also happens in reverse order to every other 16-bit operation in the CPU
		// M1 - no write, post-decrement SP
		// M2 - write high byte and post-decrement SP
		// M3 - write low byte
		registers.SP -= 2;
		additional_cycles += 4;

		// Write high byte first, then low byte
		return cpu::awaitable_write16(
			*this,
			registers.SP + 1,
			static_cast<uint8_t>(value >> 8),
			registers.SP,
			static_cast<uint8_t>(value & 0xFF),
			std::exchange(additional_cycles, 0));
	}

	cpu::awaitable_read16 cpu::pop16()
	{
		uint16_t address = registers.SP;
		registers.SP += 2;
		return read16(address);
	}

	struct alu_result
	{
		uint8_t value;
		registers_t::flags flags;
	};

	__forceinline constexpr alu_result run_alu(uint8_t value_a, uint8_t value_b, bool subtract, bool carry_in)
	{
		if (subtract)
		{
			carry_in = !carry_in;
			value_b = ~value_b;
		}
		alu_result result = {
			.value = (uint8_t)(value_a + value_b + carry_in),
			.flags = {
				.padding = 0,
				.carry = (value_a + value_b + carry_in > 0xFF),
				.half_carry = ((value_a & 0x0F) + (value_b & 0x0F) + carry_in > 0x0F),
				.subtract = subtract,
				.zero = (result.value == 0),
			}
		};
		if (subtract)
		{
			result.flags.carry = !result.flags.carry;
			result.flags.half_carry = !result.flags.half_carry;
		}
		return result;
	}

	single_future<test_status> cpu::run()
	{
		bool halt_bug = false;
		additional_cycles = 0;

		// The CPU has one dummy M cycle on reset
		co_await dummy_wait(4);

		while (true)
		{
			// The cpu has one level of pipelining, where memory reads and instruction execution are overlapped
			// The read/write macros above wait before completing the read/write on the first rising edge of the new M-cycle
			// Effectively, before a wait we are still on the rising edge at the start of the previous M-cycle
			// When we get back to the top of the loop we are still on the initial rising edge of the final M-cycle of the last instruction executed!
			// We don't get into the new M-cycle until after the first call to read!
			// External bus reads are technically latched on the last T-cycle of the M-cycle before, but we don't have anything timing-sensitive on the external bus
			// so we just emulate all reads/writes as occuring on the first T-cycle of the new M-cycle

			// handle interrupts
			if (registers.enable_interrupts)
			{
				// interrupts are checked on the 3rd T-cycle (2) of the last M-cycle of the prior instruction
				co_await cycles(cycle_scheduler::priority::read, 2);

				memory_map::interrupt_bits_t triggered_interrupts = (memory.interrupt_flag & memory.interrupt_enable);
				if ((triggered_interrupts.u8 & 0x1F) != 0)
				{
					registers.enable_interrupts = false;
					registers.enable_interrupts_delay = false;

					co_await dummy_wait(2); // realign to 4-cycle clock
					co_await dummy_wait(4); // discard pipelined opcode read
					co_await dummy_wait(4); // pre-decrement SP
					registers.SP--;
					co_await write8(registers.SP--, (registers.PC) >> 8);
					co_await cycles(cycle_scheduler::priority::read, 2);
					triggered_interrupts = (memory.interrupt_flag & memory.interrupt_enable); // interrupts are re-checked
					co_await cycles(cycle_scheduler::priority::write, 2);
					memory.write8(registers.SP, (registers.PC) & 0xFF);

					// Bit 0: V-Blank  Interrupt Request (INT 40h)
					// Bit 1: LCD STAT Interrupt Request (INT 48h)
					// Bit 2: Timer    Interrupt Request (INT 50h)
					// Bit 3: Serial   Interrupt Request (INT 58h)
					// Bit 4: Joypad   Interrupt Request (INT 60h)
					uint16_t interrupt_dest;
					if (triggered_interrupts.vblank)
					{
						interrupt_dest = 0x40;
						memory.interrupt_flag.vblank = 0;
					}
					else if (triggered_interrupts.stat)
					{
						interrupt_dest = 0x48;
						memory.interrupt_flag.stat = 0;
					}
					else if (triggered_interrupts.timer)
					{
						interrupt_dest = 0x50;
						memory.interrupt_flag.timer = 0;
					}
					else if (triggered_interrupts.serial)
					{
						interrupt_dest = 0x58;
						memory.interrupt_flag.serial = 0;
					}
					else if (triggered_interrupts.joypad)
					{
						interrupt_dest = 0x60;
						memory.interrupt_flag.joypad = 0;
					}
					else // interrupt bug!
					{
						interrupt_dest = 0x00;
					}

					registers.PC = interrupt_dest;
					co_await dummy_wait(2); // realign to T-cycle 2 ready for CPU to read opcode
				}
			}
			else
			{
				// interrupts are checked on T-cycle 2 of an instruction, but as they are disabled we'll just dummy the two cycles
				co_await dummy_wait(2);
				registers.enable_interrupts = registers.enable_interrupts_delay;
			}

#if _DEBUG
			static volatile uint16_t debug_break_PC = 0xFFFF;
			if (registers.PC == debug_break_PC)
			{
				__debugbreak();
			}
			static volatile uint32_t debug_break_cycle_counter = 0xFFFFFFFF;
			if (scheduler.get_cycle_counter() >= debug_break_cycle_counter)
			{
				__debugbreak();
			}
#endif

			co_await cycles(cycle_scheduler::priority::read, 2);
			const uint8_t opcode = memory.read8(registers.PC);
			if (!halt_bug)
			{
				++registers.PC;
			}
			else
			{
				halt_bug = false;
			}

			switch (opcode >> 6)
			{
				case 0b00:
					switch (opcode & 0b111)
					{
						case 0b000:
							if (opcode == 0b00000000) // NOP
							{
								continue;
							}
							if (opcode == 0b00010000) // STOP
							{
								throw std::runtime_error("STOP not implemented"); // ???
							}

							if (opcode == 0b00001000) // ld (a16), sp
							{
								const uint16_t address = co_await fetch16();
								co_await cpu::write16(address, registers.SP);
								continue;
							}

							if (opcode == 0b00011000) // jr
							{
								const int8_t offset = (int8_t)co_await fetch8();
								registers.PC += offset;
								co_await dummy_wait(4);
								continue;
							}

							if ((opcode & 0b11110111) == 0b00100000) // jr nz/z
							{
								const int8_t offset = (int8_t)co_await fetch8();
								if (registers.F.zero == ((opcode >> 3) & 0b1))
								{
									registers.PC += offset;
									co_await dummy_wait(4);
								}
								continue;
							}

							if ((opcode & 0b11110111) == 0b00110000) // jr nc/c
							{
								const int8_t offset = (int8_t)co_await fetch8();
								if (registers.F.carry == ((opcode >> 3) & 0b1))
								{
									registers.PC += offset;
									co_await dummy_wait(4);
								}
								continue;
							}
							break;

						case 0b001:
							if ((opcode & 0b11001111) == 0b00000001) // ld r16, m16
							{
								const uint16_t value = co_await fetch16();

								switch ((opcode >> 4) & 0b11)
								{
									case 0:
										registers.BC = value;
										break;
									case 1:
										registers.DE = value;
										break;
									case 2:
										registers.HL = value;
										break;
									case 3:
										registers.SP = value;
										break;
								}
								continue;
							}

							if ((opcode & 0b11001111) == 0b00001001) // add hl, r16
							{
								uint16_t value;
								switch ((opcode >> 4) & 0b11)
								{
									case 0:
										value = registers.BC;
										break;
									case 1:
										value = registers.DE;
										break;
									case 2:
										value = registers.HL;
										break;
									case 3:
										value = registers.SP;
										break;
								}
								const uint16_t original = registers.HL;
								const uint32_t result32 = (uint32_t)original + value;
								registers.HL = (uint16_t)result32;
								registers.F.carry = result32 > 0xFFFF;
								registers.F.half_carry = ((original & 0x0FFF) + (value & 0x0FFF)) > 0x0FFF;
								registers.F.subtract = 0;
								continue;
							}
							break;

						case 0b010:
							if ((opcode & 0b11001111) == 0b00000010) // ld (r16), a
							{
								uint16_t address;
								switch ((opcode >> 4) & 0b11)
								{
									case 0:
										address = registers.BC;
										break;
									case 1:
										address = registers.DE;
										break;
									case 2:
										address = registers.HL++;
										break;
									case 3:
										address = registers.HL--;
										break;
								}
								co_await write8(address, registers.A);
								continue;
							}

							if ((opcode & 0b11001111) == 0b00001010) // ld a, (r16)
							{
								uint16_t address;
								switch ((opcode >> 4) & 0b11)
								{
									case 0:
										address = registers.BC;
										break;
									case 1:
										address = registers.DE;
										break;
									case 2:
										address = registers.HL++;
										break;
									case 3:
										address = registers.HL--;
										break;
								}
								registers.A = co_await read8(address);
								continue;
							}
							break;

						case 0b011:
							if ((opcode & 0b11001111) == 0b00000011) // inc r16
							{
								switch ((opcode >> 4) & 0b11)
								{
									case 0:
										++registers.BC;
										break;
									case 1:
										++registers.DE;
										break;
									case 2:
										++registers.HL;
										break;
									case 3:
										++registers.SP;
										break;
								}
								co_await dummy_wait(4);
								continue;
							}

							if ((opcode & 0b11001111) == 0b00001011) // dec r16
							{
								switch ((opcode >> 4) & 0b11)
								{
									case 0:
										--registers.BC;
										break;
									case 1:
										--registers.DE;
										break;
									case 2:
										--registers.HL;
										break;
									case 3:
										--registers.SP;
										break;
								}
								co_await dummy_wait(4);
								continue;
							}
							break;

						case 0b100:
							if ((opcode & 0b11000111) == 0b00000100) // inc r8
							{
								uint8_t value;
								switch ((opcode >> 3) & 0b111)
								{
									case 0:
										value = ++registers.B;
										break;
									case 1:
										value = ++registers.C;
										break;
									case 2:
										value = ++registers.D;
										break;
									case 3:
										value = ++registers.E;
										break;
									case 4:
										value = ++registers.H;
										break;
									case 5:
										value = ++registers.L;
										break;
									case 6:
										value = co_await read8(registers.HL);
										++value;
										co_await write8(registers.HL, value);
										break;
									case 7:
										value = ++registers.A;
										break;
								}
								registers.F.half_carry = ((value & 0xF) == 0);
								registers.F.subtract = 0;
								registers.F.zero = (value == 0);
								continue;
							}
							break;

						case 0b101:
							if ((opcode & 0b11000111) == 0b00000101) // dec r8
							{
								uint8_t value;
								switch ((opcode >> 3) & 0b111)
								{
									case 0:
										value = --registers.B;
										break;
									case 1:
										value = --registers.C;
										break;
									case 2:
										value = --registers.D;
										break;
									case 3:
										value = --registers.E;
										break;
									case 4:
										value = --registers.H;
										break;
									case 5:
										value = --registers.L;
										break;
									case 6:
										value = co_await read8(registers.HL);
										--value;
										co_await write8(registers.HL, value);
										break;
									case 7:
										value = --registers.A;
										break;
								}
								registers.F.half_carry = ((value & 0xF) == 0xF);
								registers.F.subtract = 1;
								registers.F.zero = (value == 0);
								continue;
							}
							break;

						case 0b110:
							if ((opcode & 0b11000111) == 0b00000110) // ld r8,m
							{
								const uint8_t value = co_await fetch8();

								switch ((opcode >> 3) & 0b111)
								{
									case 0:
										registers.B = value;
										break;
									case 1:
										registers.C = value;
										break;
									case 2:
										registers.D = value;
										break;
									case 3:
										registers.E = value;
										break;
									case 4:
										registers.H = value;
										break;
									case 5:
										registers.L = value;
										break;
									case 6:
										co_await write8(registers.HL, value);
										break;
									case 7:
										registers.A = value;
										break;
								}
								continue;
							}
							break;

						case 0b111:
							if ((opcode & 0b11100111) == 0b00000111) // RLC/RRC/RL/RR A
							{
								switch (opcode >> 3)
								{
									case 0b00:
										registers.F.carry = (registers.A & 0b10000000) != 0;
										registers.A = (registers.A << 1) | registers.F.carry;
										break;
									case 0b01:
										registers.F.carry = (registers.A & 0b00000001) != 0;
										registers.A = (registers.A >> 1) | (registers.F.carry << 7);
										break;
									case 0b10:
									{
										bool new_carry = (registers.A & 0b10000000) != 0;
										registers.A = (registers.A << 1) | registers.F.carry;
										registers.F.carry = new_carry;
									}
									break;
									case 0b11:
									{
										bool new_carry = (registers.A & 0b00000001) != 0;
										registers.A = (registers.A >> 1) | (registers.F.carry << 7);
										registers.F.carry = new_carry;
									}
									break;
								}

								registers.F.half_carry = 0;
								registers.F.subtract = 0;
								registers.F.zero = 0;
								continue;
							}
							if (opcode == 0b00100111) // DAA
							{
								uint8_t correction = 0;
								if (registers.F.half_carry ||
									(!registers.F.subtract && (registers.A & 0x0f) > 0x09))
								{
									correction |= 0x06;
								}
								if (registers.F.carry ||
									(!registers.F.subtract && (registers.A & 0xff) > 0x99))
								{
									correction |= 0x60;
									registers.F.carry = true;
								}
								alu_result result = run_alu(registers.A, correction, registers.F.subtract, false);
								registers.A = result.value;
								registers.F.half_carry  = 0;
								registers.F.zero = result.flags.zero;
								continue;
							}
							if (opcode == 0b00101111) // CPL
							{
								registers.A = ~registers.A;
								registers.F.half_carry = 1;
								registers.F.subtract = 1;
								continue;
							}
							if (opcode == 0b00110111) // SCF
							{
								registers.F.carry = 1;
								registers.F.half_carry = 0;
								registers.F.subtract = 0;
								continue;
							}
							if (opcode == 0b00111111) // CCF
							{
								registers.F.carry = !registers.F.carry;
								registers.F.half_carry = 0;
								registers.F.subtract = 0;
								continue;
							}
							break;
					}
					break;
				case 0b01:
					if (opcode == 0b01110110) // halt
					{
						registers.enable_interrupts = registers.enable_interrupts_delay;

						memory_map::interrupt_bits_t pending_interrupts = (memory.interrupt_flag & memory.interrupt_enable);
						if ((pending_interrupts.u8 & 0x1F) == 0)
						{
							uint64_t halt_start_cycles = scheduler.get_cycle_counter();
							memory.interrupts.cpu_wake.reset();
							co_await memory.interrupts.cpu_wake;

							uint64_t halt_total_cycles = scheduler.get_cycle_counter() - halt_start_cycles;

							// re-align to 4-cycle boundary
							// during halt interrupts are tested on cycle 0, rather than the usual cycle 2
							// as ppu is ticked on the falling edge, an interrupt triggered by the ppu on cycle 0 doesn't show until the next M-cycle on the cpu
							// 0->+4, 1->+3, 2->+2, 3->+1
							co_await dummy_wait(4 - (halt_total_cycles % 4));

							// jump to interrupt handler is handled by the interrupt handling code at the start of the loop
							continue;
						}
						else
						{
							if (!registers.enable_interrupts)
							{
								// oh no!
								halt_bug = true;
							}
							continue;
						}
					}

					//if ((opcode & 0b11000000) == 0b01000000) // ld r8,r8
					{
						if (break_on_ld_b_b && (opcode & 0b111111) == 0b000000 &&
							get_mooneye_result() != test_status::running)
						{
							co_return get_mooneye_result();
						}

						uint8_t value;

						switch (opcode & 0b111)
						{
							case 0:
								value = registers.B;
								break;
							case 1:
								value = registers.C;
								break;
							case 2:
								value = registers.D;
								break;
							case 3:
								value = registers.E;
								break;
							case 4:
								value = registers.H;
								break;
							case 5:
								value = registers.L;
								break;
							case 6:
								value = co_await read8(registers.HL);
								break;
							case 7:
								value = registers.A;
								break;
						}

						switch ((opcode >> 3) & 0b111)
						{
							case 0:
								registers.B = value;
								break;
							case 1:
								registers.C = value;
								break;
							case 2:
								registers.D = value;
								break;
							case 3:
								registers.E = value;
								break;
							case 4:
								registers.H = value;
								break;
							case 5:
								registers.L = value;
								break;
							case 6:
								co_await write8(registers.HL, value);
								break;
							case 7:
								registers.A = value;
								break;
						}
						continue;
					}
					break;
				case 0b10:
				{
					uint8_t value;

					switch (opcode & 0b111)
					{
						case 0:
							value = registers.B;
							break;
						case 1:
							value = registers.C;
							break;
						case 2:
							value = registers.D;
							break;
						case 3:
							value = registers.E;
							break;
						case 4:
							value = registers.H;
							break;
						case 5:
							value = registers.L;
							break;
						case 6:
							value = co_await read8(registers.HL);
							break;
						case 7:
							value = registers.A;
							break;
					}

					switch ((opcode >> 3) & 0b111)
					{
						case 0b001: // adc a,r8
						{
							alu_result result = run_alu(registers.A, value, false, registers.F.carry);
							registers.A = result.value;
							registers.F = result.flags;
							break;
						}
						case 0b000: // add a,r8
						{
							alu_result result = run_alu(registers.A, value, false, false);
							registers.A = result.value;
							registers.F = result.flags;
							break;
						}
						case 0b011: // sbc a,r8
						{
							alu_result result = run_alu(registers.A, value, true, registers.F.carry);
							registers.A = result.value;
							registers.F = result.flags;
							break;
						}
						case 0b010: // sub a,r8
						{
							alu_result result = run_alu(registers.A, value, true, false);
							registers.A = result.value;
							registers.F = result.flags;
							break;
						}
						case 0b100: // and a,r8
						{
							registers.A &= value;
							registers.F.carry = 0;
							registers.F.half_carry = 1;
							registers.F.subtract = 0;
							registers.F.zero = (registers.A == 0);
							break;
						}
						case 0b101: // xor a,r8
						{
							registers.A ^= value;
							registers.F.carry = 0;
							registers.F.half_carry = 0;
							registers.F.subtract = 0;
							registers.F.zero = (registers.A == 0);
							break;
						}
						case 0b110: // or a,r8
						{
							registers.A |= value;
							registers.F.carry = 0;
							registers.F.half_carry = 0;
							registers.F.subtract = 0;
							registers.F.zero = (registers.A == 0);
							break;
						}
						case 0b111: // cp a,r8
						{
							alu_result result = run_alu(registers.A, value, true, false);
							registers.F = result.flags;
							break;
						}
					}
					continue;
				}
				case 0b11:
					switch (opcode & 0b111)
					{
						case 0b000:
							if ((opcode & 0b11110111) == 0b11000000) // ret nz/z
							{
								// conditional ret has an extra machine cycle delay while it checks the condition
								co_await dummy_wait(4);
								if (registers.F.zero == ((opcode >> 3) & 0b1))
								{
									registers.PC = co_await pop16();
									co_await dummy_wait(4);
								}
								continue;
							}

							if ((opcode & 0b11110111) == 0b11010000) // ret nc/c
							{
								// conditional ret has an extra machine cycle delay while it checks the condition
								co_await dummy_wait(4);
								if (registers.F.carry == ((opcode >> 3) & 0b1))
								{
									registers.PC = co_await pop16();
									co_await dummy_wait(4);
								}
								continue;
							}

							if (opcode == 0b11100000) // ld (0xFF00 + a8), a
							{
								const uint8_t offset = co_await fetch8();
								co_await write8(0xFF00 + offset, registers.A);
								continue;
							}

							if (opcode == 0b11110000) // ld a, (0xFF00 + a8)
							{
								const uint8_t offset = co_await fetch8();
								registers.A = co_await read8(0xFF00 + offset);
								continue;
							}

							if (opcode == 0b11101000) // add SP, m8
							{
								const int8_t value = (int8_t)co_await fetch8();
								const uint16_t original = registers.SP;
								registers.SP = original + value;
								registers.F.carry = ((original & 0xFF) + (value & 0xFF)) > 0xFF;
								registers.F.half_carry = ((original & 0x0F) + (value & 0x0F)) > 0x0F;
								registers.F.subtract = 0;
								registers.F.zero = 0;
								co_await dummy_wait(8);
								continue;
							}

							if (opcode == 0b11111000) // ld HL, SP+m8
							{
								const int8_t value = (int8_t)co_await fetch8();
								const uint16_t original = registers.SP;
								registers.HL = original + value;
								registers.F.carry = ((original & 0xFF) + (value & 0xFF)) > 0xFF;
								registers.F.half_carry = ((original & 0x0F) + (value & 0x0F)) > 0x0F;
								registers.F.subtract = 0;
								registers.F.zero = 0;
								co_await dummy_wait(4);
								continue;
							}
							break;

						case 0b001:
							if ((opcode & 0b11001111) == 0b11000001) // pop
							{
								const uint16_t value = co_await pop16();
								switch ((opcode >> 4) & 0b11)
								{
									case 0:
										registers.BC = value;
										break;
									case 1:
										registers.DE = value;
										break;
									case 2:
										registers.HL = value;
										break;
									case 3:
										registers.AF = value;
										registers.F.padding = 0;
										break;
								}
								continue;
							}

							if (opcode == 0b11001001) // ret
							{
								registers.PC = co_await pop16();
								co_await dummy_wait(4);
								continue;
							}

							if (opcode == 0b11011001) // reti
							{
								registers.PC = co_await pop16();
								registers.enable_interrupts = true;
								registers.enable_interrupts_delay = true;
								co_await dummy_wait(4);
								continue;
							}

							if (opcode == 0b11101001) // jp HL
							{
								registers.PC = registers.HL;
								continue;
							}

							if (opcode == 0b11111001) // ld SP,HL
							{
								registers.SP = registers.HL;
								continue;
							}
							break;

						case 0b010:
							if ((opcode & 0b11110111) == 0b11000010) // jp nz/z
							{
								const uint16_t dest = co_await fetch16();
								if (registers.F.zero == ((opcode >> 3) & 0b1))
								{
									registers.PC = dest;
									co_await dummy_wait(4);
								}
								continue;
							}

							if ((opcode & 0b11110111) == 0b11010010) // jp nc/c
							{
								const uint16_t dest = co_await fetch16();
								if (registers.F.carry == ((opcode >> 3) & 0b1))
								{
									registers.PC = dest;
									co_await dummy_wait(4);
								}
								continue;
							}

							if (opcode == 0b11100010) // ld (0xFF00 + C), A
							{
								co_await write8(0xFF00 + registers.C, registers.A);
								continue;
							}

							if (opcode == 0b11110010) // ld A, (0xFF00 + C)
							{
								registers.A = co_await read8(0xFF00 + registers.C);
								continue;
							}

							if (opcode == 0b11101010) // ld (a16), A
							{
								const uint16_t address = co_await fetch16();
								co_await write8(address, registers.A);
								continue;
							}

							if (opcode == 0b11111010) // ld A, (a16)
							{
								const uint16_t address = co_await fetch16();
								registers.A = co_await read8(address);
								continue;
							}
							break;

						case 0b011:
							if (opcode == 0b11000011) // jp
							{
								const uint16_t dest = co_await fetch16();
								registers.PC = dest;
								co_await dummy_wait(4);
								continue;
							}

							if (opcode == 0b11110011) // di
							{
								registers.enable_interrupts = false;
								registers.enable_interrupts_delay = false;
								continue;
							}

							if (opcode == 0b11111011) // ei
							{
								registers.enable_interrupts_delay = true;
								continue;
							}

							break;

						case 0b100:
							if ((opcode & 0b11110111) == 0b11000100) // call nz/z
							{
								const uint16_t dest = co_await fetch16();
								if (registers.F.zero == ((opcode >> 3) & 0b1))
								{
									co_await cpu::push16(registers.PC);
									registers.PC = dest;
								}
								continue;
							}

							if ((opcode & 0b11110111) == 0b11010100) // call nc/c
							{
								const uint16_t dest = co_await fetch16();
								if (registers.F.carry == ((opcode >> 3) & 0b1))
								{
									co_await cpu::push16(registers.PC);
									registers.PC = dest;
								}
								continue;
							}
							break;

						case 0b101:
							if ((opcode & 0b11001111) == 0b11000101) // push
							{
								uint16_t value;
								switch ((opcode >> 4) & 0b11)
								{
									case 0:
										value = registers.BC;
										break;
									case 1:
										value = registers.DE;
										break;
									case 2:
										value = registers.HL;
										break;
									case 3:
										value = registers.AF;
										break;
								}
								co_await cpu::push16(value);
								continue;
							}

							if (opcode == 0b11001101) // call a16
							{
								const uint16_t dest = co_await fetch16();
								co_await cpu::push16(registers.PC);
								registers.PC = dest;
								continue;
							}
							break;

						case 0b110:
						{
							const uint8_t value = co_await fetch8();
							switch ((opcode >> 3) & 0b111)
							{
								case 0b001: // adc a,d8
								{
									alu_result result = run_alu(registers.A, value, false, registers.F.carry);
									registers.A = result.value;
									registers.F = result.flags;
									break;
								}
								case 0b000: // add a,d8
								{
									alu_result result = run_alu(registers.A, value, false, false);
									registers.A = result.value;
									registers.F = result.flags;
									break;
								}
								case 0b011: // sbc a,d8
								{
									alu_result result = run_alu(registers.A, value, true, registers.F.carry);
									registers.A = result.value;
									registers.F = result.flags;
									break;
								}
								case 0b010: // sub a,d8
								{
									alu_result result = run_alu(registers.A, value, true, false);
									registers.A = result.value;
									registers.F = result.flags;
									break;
								}
								case 0b100: // and a,d8
								{
									registers.A &= value;
									registers.F.carry = 0;
									registers.F.half_carry = 1;
									registers.F.subtract = 0;
									registers.F.zero = (registers.A == 0);
									break;
								}
								case 0b101: // xor a,d8
								{
									registers.A ^= value;
									registers.F.carry = 0;
									registers.F.half_carry = 0;
									registers.F.subtract = 0;
									registers.F.zero = (registers.A == 0);
									break;
								}
								case 0b110: // or a,d8
								{
									registers.A |= value;
									registers.F.carry = 0;
									registers.F.half_carry = 0;
									registers.F.subtract = 0;
									registers.F.zero = (registers.A == 0);
									break;
								}
								case 0b111: // cp a,d8
								{
									alu_result result = run_alu(registers.A, value, true, false);
									registers.F = result.flags;
									break;
								}
							}
							continue;
						}
						case 0b111: // rst
						{
							uint16_t dest = (opcode & 0b00111000);
							co_await cpu::push16(registers.PC);
							registers.PC = dest;
							continue;
						}
					}
					break;
			}

			if (opcode == 0b11001011) // bit
			{
				const uint8_t bitop = co_await fetch8();
				switch (bitop >> 6)
				{
					case 0b00: // rotates/shifts
					{
						uint8_t value;

						switch (bitop & 0b111)
						{
							case 0:
								value = registers.B;
								break;
							case 1:
								value = registers.C;
								break;
							case 2:
								value = registers.D;
								break;
							case 3:
								value = registers.E;
								break;
							case 4:
								value = registers.H;
								break;
							case 5:
								value = registers.L;
								break;
							case 6:
								value = co_await read8(registers.HL);
								break;
							case 7:
								value = registers.A;
								break;
						}

						switch ((bitop >> 3) & 0b111)
						{
							case 0b000:
								registers.F.carry = (value & 0b10000000) != 0;
								value = (value << 1) | registers.F.carry;
								break;
							case 0b001:
								registers.F.carry = (value & 0b00000001) != 0;
								value = (value >> 1) | (registers.F.carry << 7);
								break;
							case 0b010:
							{
								bool new_carry = (value & 0b10000000) != 0;
								value = (value << 1) | registers.F.carry;
								registers.F.carry = new_carry;
								break;
							}
							case 0b011:
							{
								bool new_carry = (value & 0b00000001) != 0;
								value = (value >> 1) | (registers.F.carry << 7);
								registers.F.carry = new_carry;
								break;
							}
							case 0b100:
								registers.F.carry = (value & 0b10000000) != 0;
								value = (value << 1);
								break;
							case 0b101:
								registers.F.carry = (value & 0b00000001) != 0;
								value = (value & 0b10000000) | (value >> 1);
								break;
							case 0b110:
								registers.F.carry = 0;
								value = (value << 4) | (value >> 4);
								break;
							case 0b111:
								registers.F.carry = (value & 0b00000001) != 0;
								value = (value >> 1);
								break;
						}

						registers.F.half_carry = 0;
						registers.F.subtract = 0;
						registers.F.zero = (value == 0);

						switch (bitop & 0b111)
						{
							case 0:
								registers.B = value;
								break;
							case 1:
								registers.C = value;
								break;
							case 2:
								registers.D = value;
								break;
							case 3:
								registers.E = value;
								break;
							case 4:
								registers.H = value;
								break;
							case 5:
								registers.L = value;
								break;
							case 6:
								co_await write8(registers.HL, value);
								break;
							case 7:
								registers.A = value;
								break;
						}
						continue;
					}
					case 0b01: // bit test
					{
						const uint8_t bit_index = (bitop >> 3) & 0b111;
						const uint8_t value = 1 << bit_index;
						switch (bitop & 0b111)
						{
							case 0:
								registers.F.zero = !(registers.B & value);
								break;
							case 1:
								registers.F.zero = !(registers.C & value);
								break;
							case 2:
								registers.F.zero = !(registers.D & value);
								break;
							case 3:
								registers.F.zero = !(registers.E & value);
								break;
							case 4:
								registers.F.zero = !(registers.H & value);
								break;
							case 5:
								registers.F.zero = !(registers.L & value);
								break;
							case 6:
							{
								const uint8_t comparand = co_await read8(registers.HL);
								registers.F.zero = !(comparand & value);
								break;
							}
							case 7:
								registers.F.zero = !(registers.A & value);
								break;
						}
						registers.F.half_carry = 1; // why?
						registers.F.subtract = 0;
						continue;
					}
					case 0b10: // bit reset
					{
						const uint8_t bit_index = (bitop >> 3) & 0b111;
						const uint8_t value = 1 << bit_index;
						switch (bitop & 0b111)
						{
							case 0:
								registers.B &= ~value;
								break;
							case 1:
								registers.C &= ~value;
								break;
							case 2:
								registers.D &= ~value;
								break;
							case 3:
								registers.E &= ~value;
								break;
							case 4:
								registers.H &= ~value;
								break;
							case 5:
								registers.L &= ~value;
								break;
							case 6:
							{
								const uint8_t original = co_await read8(registers.HL);
								co_await write8(registers.HL, original & ~value);
								break;
							}
							case 7:
								registers.A &= ~value;
								break;
						}
						continue;
					}
					case 0b11: // bit set
					{
						const uint8_t bit_index = (bitop >> 3) & 0b111;
						const uint8_t value = 1 << bit_index;
						switch (bitop & 0b111)
						{
							case 0:
								registers.B |= value;
								break;
							case 1:
								registers.C |= value;
								break;
							case 2:
								registers.D |= value;
								break;
							case 3:
								registers.E |= value;
								break;
							case 4:
								registers.H |= value;
								break;
							case 5:
								registers.L |= value;
								break;
							case 6:
							{
								const uint8_t original = co_await read8(registers.HL);
								co_await write8(registers.HL, original | value);
								break;
							}
							case 7:
								registers.A |= value;
								break;
						}
						continue;
					}
				}
			}

			throw std::runtime_error("unknown opcode");
		}
	}
}
