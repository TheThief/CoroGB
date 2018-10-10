#include "gb_cpu.h"
#include "gb_cycle_scheduler.h"
#include "gb_memory_mapper.h"
#include "single_future.h"

namespace coro_gb
{
	cpu::cpu(cycle_scheduler& scheduler, memory_mapper& memory)
		: scheduler(scheduler),
		memory(memory)
	{
	}

	cycle_scheduler::awaitable_cycles cpu::cycles(cycle_scheduler::priority priority, uint32_t wait)
	{
		return scheduler.cycles(cycle_scheduler::unit::cpu, priority, wait);
	}

#define cpu_read8(var, cast, address) \
	co_await cycles(cycle_scheduler::priority::read, 4); \
	var = static_cast<cast>(memory.read8(address));

#define cpu_write8(address, value) \
	co_await cycles(cycle_scheduler::priority::write, 4); \
	memory.write8(address, value);

// todo - technically this should be split 4 cycles for each 8-bit read
#define cpu_read16(var, cast, address) \
	co_await cycles(cycle_scheduler::priority::read, 8); \
	var = static_cast<cast>(memory.read16(address));

// todo - technically this should be split 4 cycles for each 8-bit write
#define cpu_write16(address, value) \
	co_await cycles(cycle_scheduler::priority::write, 8); \
	memory.write16(address, value);

#define cpu_read8_pc(var, cast) \
	cpu_read8(var, cast, registers.PC); \
	++registers.PC;

#define cpu_read16_pc(var, cast) \
	cpu_read16(var, cast, registers.PC); \
	registers.PC += 2;

	// 12 cycles due to combining the 4 cycles from adjusting the SP register with the 8 cycle write
#define cpu_push16(value) \
	registers.SP -= 2; \
	co_await cycles(cycle_scheduler::priority::write, 12); \
	memory.write16(registers.SP, value);

#define cpu_pop16(var, cast) \
	cpu_read16(var, cast, registers.SP); \
	registers.SP += 2;

	single_future<void> cpu::run()
	{
		bool halt_bug = false;
		uint8_t additional_cycles = 0;

		while (true)
		{
			// so many instructions take extra cycles at the end we handle that just once (here) rather than in every instruction that takes more than one machine cycle
			if (additional_cycles)
			{
				co_await cycles(cycle_scheduler::priority::read, additional_cycles);
				additional_cycles = 0;
			}

			// handle interrupts
			if (registers.enable_interrupts)
			{
				memory_mapper::interrupt_bits_t triggered_interrupts = (memory.interrupt_flag & memory.interrupt_enable);
				if ((triggered_interrupts.u8 & 0x1F) != 0)
				{
					registers.enable_interrupts = false;
					registers.enable_interrupts_delay = false;

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
					else //if (triggered_interrupts.Joypad)
					{
						interrupt_dest = 0x60;
						memory.interrupt_flag.joypad = 0;
					}

					co_await cycles(cycle_scheduler::priority::read, 8);
					cpu_push16(registers.PC);
					registers.PC = interrupt_dest;
				}
			}
			else
			{
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

			co_await cycles(cycle_scheduler::priority::read, 4);
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
								cpu_read16_pc(uint16_t address, uint16_t);
								cpu_write16(address, registers.SP);
								continue;
							}

							if (opcode == 0b00011000) // jr
							{
								cpu_read8_pc(int8_t offset, int8_t);
								registers.PC += offset;
								additional_cycles = 4;
								continue;
							}

							if ((opcode & 0b11110111) == 0b00100000) // jr nz/z
							{
								cpu_read8_pc(int8_t offset, int8_t);
								if (registers.F_Zero == ((opcode >> 3) & 0b1))
								{
									registers.PC += offset;
									additional_cycles = 4;
								}
								continue;
							}

							if ((opcode & 0b11110111) == 0b00110000) // jr nc/c
							{
								cpu_read8_pc(int8_t offset, int8_t);
								if (registers.F_Carry == ((opcode >> 3) & 0b1))
								{
									registers.PC += offset;
									additional_cycles = 4;
								}
								continue;
							}
							break;

						case 0b001:
							if ((opcode & 0b11001111) == 0b00000001) // ld r16, m16
							{
								cpu_read16_pc(uint16_t value, uint16_t);

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
								registers.F_Carry = result32 > 0xFFFF;
								registers.F_HalfCarry = ((original & 0x0FFF) + (value & 0x0FFF)) > 0x0FFF;
								registers.F_Subtract = 0;
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
								cpu_write8(address, registers.A);
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
								cpu_read8(registers.A, uint8_t, address);
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
								additional_cycles = 4;
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
								additional_cycles = 4;
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
										cpu_read8(value, uint8_t, registers.HL);
										++value;
										cpu_write8(registers.HL, value);
										break;
									case 7:
										value = ++registers.A;
										break;
								}
								registers.F_HalfCarry = ((value & 0xF) == 0);
								registers.F_Subtract = 0;
								registers.F_Zero = (value == 0);
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
										cpu_read8(value, uint8_t, registers.HL);
										--value;
										cpu_write8(registers.HL, value);
										break;
									case 7:
										value = --registers.A;
										break;
								}
								registers.F_HalfCarry = ((value & 0xF) == 0xF);
								registers.F_Subtract = 1;
								registers.F_Zero = (value == 0);
								continue;
							}
							break;

						case 0b110:
							if ((opcode & 0b11000111) == 0b00000110) // ld r8,m
							{
								cpu_read8_pc(const uint8_t value, uint8_t);

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
										cpu_write8(registers.HL, value);
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
										registers.F_Carry = (registers.A & 0b10000000) != 0;
										registers.A = (registers.A << 1) | registers.F_Carry;
										break;
									case 0b01:
										registers.F_Carry = (registers.A & 0b00000001) != 0;
										registers.A = (registers.A >> 1) | (registers.F_Carry << 7);
										break;
									case 0b10:
									{
										bool new_carry = (registers.A & 0b10000000) != 0;
										registers.A = (registers.A << 1) | registers.F_Carry;
										registers.F_Carry = new_carry;
									}
									break;
									case 0b11:
									{
										bool new_carry = (registers.A & 0b00000001) != 0;
										registers.A = (registers.A >> 1) | (registers.F_Carry << 7);
										registers.F_Carry = new_carry;
									}
									break;
								}

								registers.F_HalfCarry = 0;
								registers.F_Subtract = 0;
								registers.F_Zero = 0;
								continue;
							}
							if (opcode == 0b00100111) // DAA
							{
								uint16_t result16 = registers.A;

								if (registers.F_Subtract)
								{
									if (registers.F_HalfCarry)
									{
										result16 -= 0x06;
										if (!registers.F_Carry)
										{
											result16 &= 0xFF;
										}
									}
								}
								else
								{
									if (registers.F_HalfCarry || (result16 & 0x0F) >= 0xA)
									{
										result16 += 0x06;
									}
								}
								if (registers.F_Subtract)
								{
									if (registers.F_Carry)
									{
										result16 -= 0x60;
									}
								}
								else
								{
									if (registers.F_Carry || result16 >= 0xA0)
									{
										result16 += 0x60;
									}
								}
								registers.A = (uint8_t)result16;
								registers.F_Carry |= result16 > 0xFF;
								registers.F_HalfCarry = 0;
								registers.F_Zero = (registers.A == 0);
								continue;
							}
							if (opcode == 0b00101111) // CPL
							{
								registers.A = ~registers.A;
								registers.F_HalfCarry = 1;
								registers.F_Subtract = 1;
								continue;
							}
							if (opcode == 0b00110111) // SCF
							{
								registers.F_Carry = 1;
								registers.F_HalfCarry = 0;
								registers.F_Subtract = 0;
								continue;
							}
							if (opcode == 0b00111111) // CCF
							{
								registers.F_Carry = !registers.F_Carry;
								registers.F_HalfCarry = 0;
								registers.F_Subtract = 0;
								continue;
							}
							break;
					}
					break;
				case 0b01:
					if (opcode == 0b01110110) // halt
					{
						registers.enable_interrupts = registers.enable_interrupts_delay;

						memory_mapper::interrupt_bits_t pending_interrupts = (memory.interrupt_flag & memory.interrupt_enable);
						if ((pending_interrupts.u8 & 0x1F) == 0)
						{
							uint64_t halt_start_cycles = scheduler.get_cycle_counter();
							memory.interrupts.cpu_wake.reset();
							co_await memory.interrupts.cpu_wake;

							uint64_t halt_total_cycles = scheduler.get_cycle_counter() - halt_start_cycles;
							if (halt_total_cycles % 4 != 0)
							{
								additional_cycles = 4 - (halt_total_cycles % 4); // re-align to 4-cycle boundary
							}

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
								cpu_read8(value, uint8_t, registers.HL);
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
								cpu_write8(registers.HL, value);
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
					uint16_t value;

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
							cpu_read8(value, uint8_t, registers.HL);
							break;
						case 7:
							value = registers.A;
							break;
					}

					const uint8_t original = registers.A;
					switch ((opcode >> 3) & 0b111)
					{
						case 0b001: // adc a,r8
						{
							const uint8_t in_carry = registers.F_Carry;
							const uint16_t result16 = (uint16_t)original + value + in_carry;
							registers.A = (uint8_t)result16;
							registers.F_Carry = (result16 > 0xFF);
							registers.F_HalfCarry = (((original & 0x0F) + (value & 0x0F) + in_carry) > 0xF);
							registers.F_Subtract = 0;
							break;
						}
						case 0b000: // add a,r8
						{
							const uint16_t result16 = (uint16_t)original + value;
							registers.A = (uint8_t)result16;
							registers.F_Carry = (result16 > 0xFF);
							registers.F_HalfCarry = (((original & 0x0F) + (value & 0x0F)) > 0xF);
							registers.F_Subtract = 0;
							break;
						}
						case 0b011: // sbc a,r8
						{
							const uint8_t in_carry = registers.F_Carry;
							const uint16_t result16 = (uint16_t)original - value - in_carry;
							registers.A = (uint8_t)result16;
							registers.F_Carry = (result16 > 0xFF);
							registers.F_HalfCarry = (((int16_t)(original & 0x0F) - (value & 0x0F) - in_carry) < 0);
							registers.F_Subtract = 1;
							break;
						}
						case 0b010: // sub a,r8
						{
							const uint16_t result16 = (uint16_t)original - value;
							registers.A = (uint8_t)result16;
							registers.F_Carry = (result16 > 0xFF);
							registers.F_HalfCarry = (((int16_t)(original & 0x0F) - (value & 0x0F)) < 0);
							registers.F_Subtract = 1;
							break;
						}
						case 0b100: // and a,r8
							registers.A &= value;
							registers.F_Carry = 0;
							registers.F_HalfCarry = 1;
							registers.F_Subtract = 0;
							break;
						case 0b101: // xor a,r8
							registers.A ^= value;
							registers.F_Carry = 0;
							registers.F_HalfCarry = 0;
							registers.F_Subtract = 0;
							break;
						case 0b110: // or a,r8
							registers.A |= value;
							registers.F_Carry = 0;
							registers.F_HalfCarry = 0;
							registers.F_Subtract = 0;
							break;
						case 0b111: // cp a,r8
						{
							const uint16_t result16 = (uint16_t)original - value;
							registers.F_Carry = (result16 > 0xFF);
							registers.F_HalfCarry = (((int16_t)(original & 0x0F) - (value & 0x0F)) < 0);
							registers.F_Subtract = 1;
							registers.F_Zero = ((uint8_t)result16 == 0);
							continue;
						}
					}
					registers.F_Zero = (registers.A == 0);
					continue;
				}
				case 0b11:
					switch (opcode & 0b111)
					{
						case 0b000:
							if ((opcode & 0b11110111) == 0b11000000) // ret nz/z
							{
								// conditional ret has an extra machine cycle delay while it checks the condition
								co_await cycles(cycle_scheduler::priority::read, 4);
								if (registers.F_Zero == ((opcode >> 3) & 0b1))
								{
									cpu_pop16(registers.PC, uint16_t);
									additional_cycles = 4;
								}
								continue;
							}

							if ((opcode & 0b11110111) == 0b11010000) // ret nc/c
							{
								// conditional ret has an extra machine cycle delay while it checks the condition
								co_await cycles(cycle_scheduler::priority::read, 4);
								if (registers.F_Carry == ((opcode >> 3) & 0b1))
								{
									cpu_pop16(registers.PC, uint16_t);
									additional_cycles = 4;
								}
								continue;
							}

							if (opcode == 0b11100000) // ld (0xFF00 + a8), a
							{
								cpu_read8_pc(uint8_t offset, uint8_t);
								cpu_write8(0xFF00 + offset, registers.A);
								continue;
							}

							if (opcode == 0b11110000) // ld a, (0xFF00 + a8)
							{
								cpu_read8_pc(uint8_t offset, uint8_t);
								cpu_read8(registers.A, uint8_t, 0xFF00 + offset);
								continue;
							}

							if (opcode == 0b11101000) // add SP, m8
							{
								cpu_read8_pc(const int8_t value, int8_t);
								const uint16_t original = registers.SP;
								registers.SP = original + value;
								registers.F_Carry = ((original & 0xFF) + (value & 0xFF)) > 0xFF;
								registers.F_HalfCarry = ((original & 0x0F) + (value & 0x0F)) > 0x0F;
								registers.F_Subtract = 0;
								registers.F_Zero = 0;
								additional_cycles = 8;
								continue;
							}

							if (opcode == 0b11111000) // ld HL, SP+m8
							{
								cpu_read8_pc(const int8_t value, int8_t);
								const uint16_t original = registers.SP;
								const uint32_t result32 = (uint32_t)original + value;
								registers.HL = (uint16_t)result32;
								registers.F_Carry = ((original & 0xFF) + (value & 0xFF)) > 0xFF;
								registers.F_HalfCarry = ((original & 0x0F) + (value & 0x0F)) > 0x0F;
								registers.F_Subtract = 0;
								registers.F_Zero = 0;
								additional_cycles = 4;
								continue;
							}
							break;

						case 0b001:
							if ((opcode & 0b11001111) == 0b11000001) // pop
							{
								cpu_pop16(uint16_t value, uint16_t);
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
										registers.F_Padding = 0;
										break;
								}
								continue;
							}

							if (opcode == 0b11001001) // ret
							{
								cpu_pop16(registers.PC, uint16_t);
								additional_cycles = 4;
								continue;
							}

							if (opcode == 0b11011001) // reti
							{
								cpu_pop16(registers.PC, uint16_t);
								registers.enable_interrupts = true;
								registers.enable_interrupts_delay = true;
								additional_cycles = 4;
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
								cpu_read16_pc(uint16_t dest, uint16_t);
								if (registers.F_Zero == ((opcode >> 3) & 0b1))
								{
									registers.PC = dest;
									additional_cycles = 4;
								}
								continue;
							}

							if ((opcode & 0b11110111) == 0b11010010) // jp nc/c
							{
								cpu_read16_pc(uint16_t dest, uint16_t);
								if (registers.F_Carry == ((opcode >> 3) & 0b1))
								{
									registers.PC = dest;
									additional_cycles = 4;
								}
								continue;
							}

							if (opcode == 0b11100010) // ld (0xFF00 + C), A
							{
								cpu_write8(0xFF00 + registers.C, registers.A);
								continue;
							}

							if (opcode == 0b11110010) // ld A, (0xFF00 + C)
							{
								cpu_read8(registers.A, uint8_t, 0xFF00 + registers.C);
								continue;
							}

							if (opcode == 0b11101010) // ld (a16), A
							{
								cpu_read16_pc(uint16_t address, uint16_t);
								cpu_write8(address, registers.A);
								continue;
							}

							if (opcode == 0b11111010) // ld A, (a16)
							{
								cpu_read16_pc(uint16_t address, uint16_t);
								cpu_read8(registers.A, uint8_t, address);
								continue;
							}
							break;

						case 0b011:
							if (opcode == 0b11000011) // jp
							{
								cpu_read16_pc(uint16_t dest, uint16_t);
								registers.PC = dest;
								additional_cycles = 4;
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
								cpu_read16_pc(uint16_t dest, uint16_t);
								if (registers.F_Zero == ((opcode >> 3) & 0b1))
								{
									cpu_push16(registers.PC);
									registers.PC = dest;
								}
								continue;
							}

							if ((opcode & 0b11110111) == 0b11010100) // call nc/c
							{
								cpu_read16_pc(uint16_t dest, uint16_t);
								if (registers.F_Carry == ((opcode >> 3) & 0b1))
								{
									cpu_push16(registers.PC);
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
								cpu_push16(value);
								continue;
							}

							if (opcode == 0b11001101) // call a16
							{
								cpu_read16_pc(uint16_t dest, uint16_t);
								cpu_push16(registers.PC);
								registers.PC = dest;
								continue;
							}
							break;

						case 0b110:
						{
							cpu_read8_pc(uint8_t value, uint8_t);

							const uint8_t original = registers.A;
							switch ((opcode >> 3) & 0b111)
							{
								case 0b001: // adc a,d8
								{
									const uint8_t in_carry = registers.F_Carry;
									const uint16_t result16 = (uint16_t)original + value + in_carry;
									registers.A = (uint8_t)result16;
									registers.F_Carry = (result16 > 0xFF);
									registers.F_HalfCarry = (((original & 0x0F) + (value & 0x0F) + in_carry) > 0xF);
									registers.F_Subtract = 0;
									break;
								}
								case 0b000: // add a,d8
								{
									const uint16_t result16 = (uint16_t)original + value;
									registers.A = (uint8_t)result16;
									registers.F_Carry = (result16 > 0xFF);
									registers.F_HalfCarry = (((original & 0x0F) + (value & 0x0F)) > 0xF);
									registers.F_Subtract = 0;
									break;
								}
								case 0b011: // sbc a,d8
								{
									const uint8_t in_carry = registers.F_Carry;
									const uint16_t result16 = (uint16_t)original - value - in_carry;
									registers.A = (uint8_t)result16;
									registers.F_Carry = (result16 > 0xFF);
									registers.F_HalfCarry = (((int16_t)(original & 0x0F) - (value & 0x0F) - in_carry) < 0);
									registers.F_Subtract = 1;
									break;
								}
								case 0b010: // sub a,d8
								{
									const uint16_t result16 = (uint16_t)original - value;
									registers.A = (uint8_t)result16;
									registers.F_Carry = (result16 > 0xFF);
									registers.F_HalfCarry = (((int16_t)(original & 0x0F) - (value & 0x0F)) < 0);
									registers.F_Subtract = 1;
									break;
								}
								case 0b100: // and a,d8
									registers.A &= value;
									registers.F_Carry = 0;
									registers.F_HalfCarry = 1;
									registers.F_Subtract = 0;
									break;
								case 0b101: // xor a,d8
									registers.A ^= value;
									registers.F_Carry = 0;
									registers.F_HalfCarry = 0;
									registers.F_Subtract = 0;
									break;
								case 0b110: // or a,d8
									registers.A |= value;
									registers.F_Carry = 0;
									registers.F_HalfCarry = 0;
									registers.F_Subtract = 0;
									break;
								case 0b111: // cp a,d8
								{
									const uint16_t result16 = (uint16_t)original - value;
									registers.F_Carry = (result16 > 0xFF);
									registers.F_HalfCarry = (((int16_t)(original & 0x0F) - (value & 0x0F)) < 0);
									registers.F_Subtract = 1;
									registers.F_Zero = ((uint8_t)result16 == 0);
									continue;
								}
							}
							registers.F_Zero = (registers.A == 0);
							continue;
						}
						case 0b111: // rst
						{
							uint16_t dest = (opcode & 0b00111000);
							cpu_push16(registers.PC);
							registers.PC = dest;
							continue;
						}
					}
					break;
			}

			if (opcode == 0b11001011) // bit
			{
				cpu_read8_pc(const uint8_t bitop, uint8_t);

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
								cpu_read8(value, uint8_t, registers.HL);
								break;
							case 7:
								value = registers.A;
								break;
						}

						switch ((bitop >> 3) & 0b111)
						{
							case 0b000:
								registers.F_Carry = (value & 0b10000000) != 0;
								value = (value << 1) | registers.F_Carry;
								break;
							case 0b001:
								registers.F_Carry = (value & 0b00000001) != 0;
								value = (value >> 1) | (registers.F_Carry << 7);
								break;
							case 0b010:
							{
								bool new_carry = (value & 0b10000000) != 0;
								value = (value << 1) | registers.F_Carry;
								registers.F_Carry = new_carry;
								break;
							}
							case 0b011:
							{
								bool new_carry = (value & 0b00000001) != 0;
								value = (value >> 1) | (registers.F_Carry << 7);
								registers.F_Carry = new_carry;
								break;
							}
							case 0b100:
								registers.F_Carry = (value & 0b10000000) != 0;
								value = (value << 1);
								break;
							case 0b101:
								registers.F_Carry = (value & 0b00000001) != 0;
								value = (value & 0b10000000) | (value >> 1);
								break;
							case 0b110:
								registers.F_Carry = 0;
								value = (value << 4) | (value >> 4);
								break;
							case 0b111:
								registers.F_Carry = (value & 0b00000001) != 0;
								value = (value >> 1);
								break;
						}

						registers.F_HalfCarry = 0;
						registers.F_Subtract = 0;
						registers.F_Zero = (value == 0);

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
								cpu_write8(registers.HL, value);
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
								registers.F_Zero = !(registers.B & value);
								break;
							case 1:
								registers.F_Zero = !(registers.C & value);
								break;
							case 2:
								registers.F_Zero = !(registers.D & value);
								break;
							case 3:
								registers.F_Zero = !(registers.E & value);
								break;
							case 4:
								registers.F_Zero = !(registers.H & value);
								break;
							case 5:
								registers.F_Zero = !(registers.L & value);
								break;
							case 6:
							{
								cpu_read8(const uint8_t comparand, uint8_t, registers.HL);
								registers.F_Zero = !(comparand & value);
								break;
							}
							case 7:
								registers.F_Zero = !(registers.A & value);
								break;
						}
						registers.F_HalfCarry = 1; // why?
						registers.F_Subtract = 0;
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
								cpu_read8(const uint8_t original, uint8_t, registers.HL);
								cpu_write8(registers.HL, original & ~value);
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
								cpu_read8(const uint8_t original, uint8_t, registers.HL);
								cpu_write8(registers.HL, original | value);
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
