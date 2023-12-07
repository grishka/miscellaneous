package me.grishka.gbemulator;

import java.util.function.IntConsumer;
import java.util.function.IntSupplier;

public class CPU{
	private final MemoryBus memoryBus;

	private static final int FLAG_ZERO=1 << 7;
	private static final int FLAG_SUBTRACTION=1 << 6;
	private static final int FLAG_HALF_CARRY=1 << 5;
	private static final int FLAG_CARRY=1 << 4;

	private int pc=0;
	private int sp=0xFFFE;
//	private int flags=0;
	private int regA, regB, regC, regD, regE, regH, regL;
	private boolean flagZ, flagN, flagH, flagC;

	private int delayCycles=0;
	private boolean interruptsEnabled=true;
	private boolean halted;
	private InterruptFlagsDevice interruptFlags;

	public CPU(MemoryBus memoryBus, InterruptFlagsDevice interruptFlags){
		this.memoryBus=memoryBus;
		this.interruptFlags=interruptFlags;
	}

	public void doClockCycle(){
		if(delayCycles>0){
			delayCycles--;
			return;
		}
		if(memoryBus.read(0xFF0F)!=0){
			halted=false;
			int masked=(memoryBus.read(0xFFFF) & memoryBus.read(0xFF0F));
			if(interruptsEnabled){
				if((masked & InterruptFlagsDevice.FLAG_VBLANK)!=0){
					interruptFlags.clear(InterruptFlagsDevice.FLAG_VBLANK);
					doInterrupt(0x40);
				}else if((masked & InterruptFlagsDevice.FLAG_LCD_STAT)!=0){
					interruptFlags.clear(InterruptFlagsDevice.FLAG_LCD_STAT);
					doInterrupt(0x48);
				}else if((masked & InterruptFlagsDevice.FLAG_TIMER)!=0){
					interruptFlags.clear(InterruptFlagsDevice.FLAG_TIMER);
					doInterrupt(0x50);
				}else if((masked & InterruptFlagsDevice.FLAG_SERIAL)!=0){
					interruptFlags.clear(InterruptFlagsDevice.FLAG_SERIAL);
					doInterrupt(0x58);
				}else if((masked & InterruptFlagsDevice.FLAG_JOYPAD)!=0){
					interruptFlags.clear(InterruptFlagsDevice.FLAG_JOYPAD);
					doInterrupt(0x60);
				}
			}
		}
		if(halted){
			return;
		}
		if((regA >> 8)!=0 || (regB >> 8)!=0 || (regC >> 8)!=0 || (regD >> 8)!=0 || (regE >> 8)!=0 || (regH >> 8)!=0 || (regL >> 8)!=0)
			throw new IllegalStateException();

		int instr=memoryBus.read(pc++);

		if(instr>=0x40 && instr<0xC0){
			int value=switch(instr & 7){
				case 0 -> regB;
				case 1 -> regC;
				case 2 -> regD;
				case 3 -> regE;
				case 4 -> regH;
				case 5 -> regL;
				case 6 -> {
					delayCycles=1;
					yield memoryBus.read(getRegHL());
				}
				case 7 -> regA;
				default -> throw new IllegalStateException();
			};
			switch(instr & 0xF8){
				case 0x40 -> regB=value;
				case 0x48 -> regC=value;
				case 0x50 -> regD=value;
				case 0x58 -> regE=value;
				case 0x60 -> regH=value;
				case 0x68 -> regL=value;
				case 0x70 -> {
					if(instr==0x76){
//						System.out.println("halt "+interruptsEnabled);
						halted=true;
						if(!interruptsEnabled)
							pc++;
					}else{
						memoryBus.write(getRegHL(), value);
						delayCycles++;
					}
				}
				case 0x78 -> regA=value;

				case 0x80 -> doAddInstr(value);
				case 0x88 -> doAdcInstr(value);
				case 0x90 -> doSubInstr(value);
				case 0x98 -> doSbcInstr(value);
				case 0xA0 -> doAndInstr(value);
				case 0xA8 -> doXorInstr(value);
				case 0xB0 -> {
					regA|=value;
					flagC=flagH=flagN=false;
					flagZ=regA==0;
				}
				case 0xB8 -> doCpInstr(value);
				default -> throw new UnsupportedOperationException(String.format("Unimplemented opcode 0x%02X (%s) at pc=0x%04X", instr, Integer.toBinaryString(instr), pc-1));
			}
			return;
		}

		switch(instr){
			case 0x00 -> {} // NOP

			case 0x10 -> { // STOP
//				throw new RuntimeException("CPU stopped with STOP instruction, PC="+Integer.toHexString(pc-1));
				pc++;
			}

			// 16-bit load immediate
			case 0x01 -> { // LD BC, imm16
				regC=memoryBus.read(pc++);
				regB=memoryBus.read(pc++);
				delayCycles=2;
			}
			case 0x11 -> { // LD DE, imm16
				regE=memoryBus.read(pc++);
				regD=memoryBus.read(pc++);
				delayCycles=2;
			}
			case 0x21 -> { // LD HL, imm16
				regL=memoryBus.read(pc++);
				regH=memoryBus.read(pc++);
				delayCycles=2;
			}
			case 0x31 -> { // LD SP, imm16
				sp=memoryBus.read(pc++) | (memoryBus.read(pc++) << 8);
//				System.out.println("LD SP, "+Integer.toHexString(sp));
				delayCycles=2;
			}

			// 8-bit load immediate
			case 0x0E -> { // LD C, imm8
				regC=memoryBus.read(pc++);
				delayCycles=1;
			}
			case 0x1E -> { // LD E, imm8
				regE=memoryBus.read(pc++);
				delayCycles=1;
			}
			case 0x2E -> { // LD L, imm8
				regL=memoryBus.read(pc++);
				delayCycles=1;
			}
			case 0x3E -> { // LD A, imm8
				regA=memoryBus.read(pc++);
				delayCycles=1;
			}
			case 0x06 -> { // LD B, imm8
				regB=memoryBus.read(pc++);
				delayCycles=1;
			}
			case 0x16 -> { // LD D, imm8
				regD=memoryBus.read(pc++);
				delayCycles=1;
			}
			case 0x26 -> { // LD H, imm8
				regH=memoryBus.read(pc++);
				delayCycles=1;
			}

			// Jumps
			case 0xC3 -> { // JP a16
				pc=memoryBus.read(pc++) | (memoryBus.read(pc++) << 8);
				delayCycles=3;
			}
			case 0xCA -> { // JP Z, a16
				int addr=memoryBus.read(pc++) | (memoryBus.read(pc++) << 8);
				if(flagZ){
					pc=addr;
					delayCycles=3;
				}else{
					delayCycles=2;
				}
			}
			case 0xDA -> { // JP C, a16
				int addr=memoryBus.read(pc++) | (memoryBus.read(pc++) << 8);
				if(flagC){
					pc=addr;
					delayCycles=3;
				}else{
					delayCycles=2;
				}
			}
			case 0xC2 -> { // JP NZ, a16
				int addr=memoryBus.read(pc++) | (memoryBus.read(pc++) << 8);
				if(!flagZ){
					pc=addr;
					delayCycles=3;
				}else{
					delayCycles=2;
				}
			}
			case 0xD2 -> { // JP NC, a16
				int addr=memoryBus.read(pc++) | (memoryBus.read(pc++) << 8);
				if(!flagC){
					pc=addr;
					delayCycles=3;
				}else{
					delayCycles=2;
				}
			}
			case 0x18 -> { // JR e
				int e=(byte)memoryBus.read(pc++);
//				System.out.println("JR "+e);
				pc+=e;
			}
			case 0x20 -> { // JR NZ, e
				int e=(byte)memoryBus.read(pc++);
//				System.out.println("JR NZ, "+e+", PC="+Integer.toHexString(pc));
				if(!flagZ){
					pc+=e;
					delayCycles=2;
				}else{
					delayCycles=1;
				}
			}
			case 0x30 -> { // JR NC, e
				int e=(byte)memoryBus.read(pc++);
//				System.out.println("JR NC, "+e+", PC="+Integer.toHexString(pc));
				if(!flagC){
					pc+=e;
					delayCycles=2;
				}else{
					delayCycles=1;
				}
			}
			case 0x28 -> { // JR Z, e
				int e=(byte)memoryBus.read(pc++);
//				System.out.println("JR Z, "+e);
				if(flagZ){
					pc+=e;
					delayCycles=2;
				}else{
					delayCycles=1;
				}
			}
			case 0x38 -> { // JR C, e
				int e=(byte)memoryBus.read(pc++);
//				System.out.printf("%04x: JR C, %d\n", pc-2, e);
				if(flagC){
					pc+=e;
					delayCycles=2;
				}else{
					delayCycles=1;
				}
			}
			case 0xE9 -> { // JP HL
				pc=getRegHL();
			}

			// 8-bit A <-> memory
			case 0xE0 -> { // LDH (imm8), A
				int n=memoryBus.read(pc++);
				memoryBus.write(0xFF00 | n, regA);
				delayCycles=2;
			}
			case 0xF0 -> { // LDH A, (imm8)
				int n=memoryBus.read(pc++);
				regA=memoryBus.read(0xFF00 | n);
				delayCycles=2;
			}
			case 0xE2 -> { // LDH (C), A
				memoryBus.write(0xFF00 | regC, regA);
				delayCycles=1;
			}
			case 0xF2 -> { // LDH A, (C)
				regA=memoryBus.read(0xFF00 | regC);
				delayCycles=1;
			}
			case 0xEA -> { // LD (imm16), A
				int addr=memoryBus.read(pc++) | (memoryBus.read(pc++) << 8);
				memoryBus.write(addr, regA);
				delayCycles=3;
			}
			case 0xFA -> { // LD A, (imm16)
				int addr=memoryBus.read(pc++) | (memoryBus.read(pc++) << 8);
				regA=memoryBus.read(addr);
				delayCycles=3;
			}
			case 0x0A -> { // LD A, (BC)
				regA=memoryBus.read(getRegBC());
				delayCycles=1;
			}
			case 0x1A -> { // LD A, (DE)
				regA=memoryBus.read(getRegDE());
				delayCycles=1;
			}
			case 0x2A -> { // LD A, (HL+)
				int hl=getRegHL();
				regA=memoryBus.read(hl);
				setRegHL(hl+1);
				delayCycles=1;
			}
			case 0x3A -> { // LD A, (HL-)
				int hl=getRegHL();
				regA=memoryBus.read(hl);
				setRegHL(hl-1);
				delayCycles=1;
			}
			case 0x02 -> { // LD (BC), A
				memoryBus.write(getRegBC(), regA);
				delayCycles=1;
			}
			case 0x12 -> { // LD (DE), A
				memoryBus.write(getRegDE(), regA);
				delayCycles=1;
			}
			case 0x22 -> { // LD (HL+), A
				int hl=getRegHL();
				memoryBus.write(hl, regA);
				setRegHL(hl+1);
				delayCycles=1;
			}
			case 0x32 -> { // LD (HL-), A
				int hl=getRegHL();
				memoryBus.write(hl, regA);
				setRegHL(hl-1);
				delayCycles=1;
			}
			case 0x36 -> { // LD (HL), imm8
				memoryBus.write(getRegHL(), memoryBus.read(pc++));
				delayCycles=2;
			}

			case 0xF3 -> { // DI
//				System.out.println("DI");
				interruptsEnabled=false;
			}

			case 0xFB -> { // EI
//				System.out.println("EI");
				interruptsEnabled=true;
			}

			// Function calls
			case 0xCD -> { // CALL imm16
				int addr=memoryBus.read(pc++) | (memoryBus.read(pc++) << 8);
				sp--;
				memoryBus.write(sp--, pc >> 8);
				memoryBus.write(sp, pc & 0xFF);
				pc=addr;
				delayCycles=5;
			}
			case 0xC4 -> { // CALL NZ, imm16
				int addr=memoryBus.read(pc++) | (memoryBus.read(pc++) << 8);
				if(!flagZ){
					sp--;
					memoryBus.write(sp--, pc >> 8);
					memoryBus.write(sp, pc & 0xFF);
					pc=addr;
					delayCycles=5;
				}else{
					delayCycles=2;
				}
			}
			case 0xD4 -> { // CALL NC, imm16
				int addr=memoryBus.read(pc++) | (memoryBus.read(pc++) << 8);
				if(!flagC){
					sp--;
					memoryBus.write(sp--, pc >> 8);
					memoryBus.write(sp, pc & 0xFF);
					pc=addr;
					delayCycles=5;
				}else{
					delayCycles=2;
				}
			}
			case 0xCC -> { // CALL Z, imm16
				int addr=memoryBus.read(pc++) | (memoryBus.read(pc++) << 8);
				if(flagZ){
					sp--;
					memoryBus.write(sp--, pc >> 8);
					memoryBus.write(sp, pc & 0xFF);
					pc=addr;
					delayCycles=5;
				}else{
					delayCycles=2;
				}
			}
			case 0xDC -> { // CALL C, imm16
				int addr=memoryBus.read(pc++) | (memoryBus.read(pc++) << 8);
				if(flagC){
					sp--;
					memoryBus.write(sp--, pc >> 8);
					memoryBus.write(sp, pc & 0xFF);
					pc=addr;
					delayCycles=5;
				}else{
					delayCycles=2;
				}
			}
			case 0xC9 -> { // RET
				pc=memoryBus.read(sp++) | (memoryBus.read(sp++) << 8);
				delayCycles=3;
			}
			case 0xC0 -> { // RET NZ
				if(!flagZ){
					pc=memoryBus.read(sp++) | (memoryBus.read(sp++) << 8);
					delayCycles=4;
				}else{
					delayCycles=1;
				}
			}
			case 0xC8 -> { // RET Z
				if(flagZ){
					pc=memoryBus.read(sp++) | (memoryBus.read(sp++) << 8);
					delayCycles=4;
				}else{
					delayCycles=1;
				}
			}
			case 0xD0 -> { // RET NC
				if(!flagC){
					pc=memoryBus.read(sp++) | (memoryBus.read(sp++) << 8);
					delayCycles=4;
				}else{
					delayCycles=1;
				}
			}
			case 0xD8 -> { // RET C
				if(flagC){
					pc=memoryBus.read(sp++) | (memoryBus.read(sp++) << 8);
					delayCycles=4;
				}else{
					delayCycles=1;
				}
			}
			case 0xD9 -> { // RETI
				interruptsEnabled=true;
				pc=memoryBus.read(sp++) | (memoryBus.read(sp++) << 8);
				delayCycles=3;
			}

			// Stack
			case 0xC5 -> { // PUSH BC
				sp--;
				memoryBus.write(sp--, regB);
				memoryBus.write(sp, regC);
				delayCycles=3;
			}
			case 0xD5 -> { // PUSH DE
				sp--;
				memoryBus.write(sp--, regD);
				memoryBus.write(sp, regE);
				delayCycles=3;
			}
			case 0xE5 -> { // PUSH HL
				sp--;
				memoryBus.write(sp--, regH);
				memoryBus.write(sp, regL);
				delayCycles=3;
			}
			case 0xF5 -> { // PUSH AF
//				System.out.println("PUSH AF");
				sp--;
				memoryBus.write(sp--, regA);
				memoryBus.write(sp, getFlags());
				delayCycles=3;
			}
			case 0xC1 -> { // POP BC
				regC=memoryBus.read(sp++);
				regB=memoryBus.read(sp++);
				delayCycles=2;
			}
			case 0xD1 -> { // POP DE
				regE=memoryBus.read(sp++);
				regD=memoryBus.read(sp++);
				delayCycles=2;
			}
			case 0xE1 -> { // POP HL
				regL=memoryBus.read(sp++);
				regH=memoryBus.read(sp++);
				delayCycles=2;
			}
			case 0xF1 -> { // POP AF
				setFlags(memoryBus.read(sp++) & 0xF0);
				regA=memoryBus.read(sp++);
				delayCycles=2;
			}
			case 0x08 -> { // LD (a16), SP
				int n=memoryBus.read(pc++) | (memoryBus.read(pc++) << 8);
				memoryBus.write(n, sp & 0xFF);
				memoryBus.write(n+1, sp >> 8);
				delayCycles=4;
			}
			case 0xE8 -> { // ADD SP, r8
				int prev=sp;
				int x=(byte)memoryBus.read(pc++);
				int newSP=(sp+x) & 0xFFFF;
				flagZ=flagN=false;
				flagC=(((sp & 0xFF)+(x & 0xFF))>0xFF);
				flagH=(((sp & 0xF)+(x & 0xF))>0xF);
				sp=newSP;
				delayCycles=3;
			}
			case 0xF9 -> { // LD SP, HL
				delayCycles=1;
				sp=getRegHL();
			}
			case 0xF8 -> { // LD HL, SP+imm8
				int e=(byte)memoryBus.read(pc++);
				int newSP=sp+e;
				setRegHL(newSP);
				flagZ=flagN=false;
				flagC=(((sp & 0xFF)+(e & 0xFF))>0xFF);
				flagH=(((sp & 0xF)+(e & 0xF))>0xF);

				delayCycles=2;
			}

			// 8-bit increments
			case 0x04 -> { // INC B
				int prev=regB;
				regB=(regB+1) & 0xFF;
				updateFlagsForIncrement(prev, regB);
			}
			case 0x14 -> { // INC D
				int prev=regD;
				regD=(regD+1) & 0xFF;
				updateFlagsForIncrement(prev, regD);
			}
			case 0x24 -> { // INC H
				int prev=regH;
				regH=(regH+1) & 0xFF;
				updateFlagsForIncrement(prev, regH);
			}
			case 0x0C -> { // INC C
				int prev=regC;
				regC=(regC+1) & 0xFF;
				updateFlagsForIncrement(prev, regC);
			}
			case 0x1C -> { // INC E
				int prev=regE;
				regE=(regE+1) & 0xFF;
				updateFlagsForIncrement(prev, regE);
			}
			case 0x2C -> { // INC L
				int prev=regL;
				regL=(regL+1) & 0xFF;
				updateFlagsForIncrement(prev, regL);
			}
			case 0x3C -> { // INC A
				int prev=regA;
				regA=(regA+1) & 0xFF;
				updateFlagsForIncrement(prev, regA);
			}
			case 0x34 -> { // INC (HL)
				int hl=getRegHL();
				int v=memoryBus.read(hl);
				int prev=v;
				v=(v+1) & 0xFF;
				updateFlagsForIncrement(prev, v);
				memoryBus.write(hl, v);
				delayCycles=2;
			}

			// 8-bit decrements
			case 0x05 -> { // DEC B
				int prev=regB;
				regB=(regB-1) & 0xFF;
				updateFlagsForDecrement(prev, regB);
			}
			case 0x15 -> { // DEC D
				int prev=regD;
				regD=(regD-1) & 0xFF;
				updateFlagsForDecrement(prev, regD);
			}
			case 0x25 -> { // DEC H
				int prev=regH;
				regH=(regH-1) & 0xFF;
				updateFlagsForDecrement(prev, regH);
			}
			case 0x0D -> { // DEC C
				int prev=regC;
				regC=(regC-1) & 0xFF;
				updateFlagsForDecrement(prev, regC);
			}
			case 0x1D -> { // DEC E
				int prev=regE;
				regE=(regE-1) & 0xFF;
				updateFlagsForDecrement(prev, regE);
			}
			case 0x2D -> { // DEC L
				int prev=regL;
				regL=(regL-1) & 0xFF;
				updateFlagsForDecrement(prev, regL);
			}
			case 0x3D -> { // DEC A
				int prev=regA;
				regA=(regA-1) & 0xFF;
				updateFlagsForDecrement(prev, regA);
			}
			case 0x35 -> { // DEC (HL)
				int hl=getRegHL();
				int prev=memoryBus.read(hl);
				int n=prev-1;
				memoryBus.write(hl, n);
				updateFlagsForDecrement(prev, n);
				delayCycles=2;
			}

			// 16-bit increments
			case 0x03 -> { // INC BC
				setRegBC(getRegBC()+1);
				delayCycles=1;
			}
			case 0x13 -> { // INC DE
				setRegDE(getRegDE()+1);
				delayCycles=1;
			}
			case 0x23 -> { // INC HL
				setRegHL(getRegHL()+1);
				delayCycles=1;
			}
			case 0x33 -> { // INC SP
				sp=(sp+1) & 0xFFFF;
				delayCycles=1;
			}

			// 16-bit decrements
			case 0x0B -> { // DEC BC
				setRegBC(getRegBC()-1);
				delayCycles=1;
			}
			case 0x1B -> { // DEC DE
				setRegDE(getRegDE()-1);
				delayCycles=1;
			}
			case 0x2B -> { // DEC HL
				setRegHL(getRegHL()-1);
				delayCycles=1;
			}
			case 0x3B -> { // DEC SP
				sp=(sp-1) & 0xFFFF;
				delayCycles=1;
			}

			// Math with A, imm8
			case 0xC6 -> { // add A, d8
				doAddInstr(memoryBus.read(pc++));
				delayCycles=1;
			}
			case 0xD6 -> { // SUB d8
//				int n=memoryBus.read(pc++);
//				int prev=regA;
//				regA=(regA-n) & 0xFF;
//				updateFlagsForSubtraction(prev, regA);
				doSubInstr(memoryBus.read(pc++));
				delayCycles=1;
			}
			case 0xE6 -> { // AND d8
//				regA&=memoryBus.read(pc++);
//				flags=regA==0 ? (FLAG_ZERO | FLAG_HALF_CARRY) : FLAG_HALF_CARRY;
				doAndInstr(memoryBus.read(pc++));
				delayCycles=1;
			}
			case 0xF6 -> { // OR d8
				regA|=memoryBus.read(pc++);
				flagC=flagH=flagN=false;
				flagZ=regA==0;
			}
			case 0xCE -> { // ADC A, d8
//				System.out.println("ADC");
				int n=memoryBus.read(pc++);
				doAdcInstr(n);
				int tmpFlags=0;
//				doAddInstr(n+((flags & FLAG_CARRY)!=0 ? 1 : 0));
//				doAddInstr(n);
//				if(carry){
//					int tmpFlags=flags;
//					doAddInstr(1);
//					flags|=tmpFlags;
//					regA=(regA+1) & 0xFF;
//				}
				delayCycles=1;
			}
			case 0xFE -> { // CP d8
				doCpInstr(memoryBus.read(pc++));
				delayCycles=1;
			}
			case 0x2F -> { // CPL
				regA=(~regA) & 0xFF;
				flagH=flagN=true;
			}
			case 0xEE -> { // XOR d8
				doXorInstr(memoryBus.read(pc++));
				delayCycles=1;
			}
			case 0xDE -> { // SBC A, d8
				doSbcInstr(memoryBus.read(pc++));
				delayCycles=1;
			}

			// Bit shifts
			case 0x1F -> { // RRA
				if(flagC)
					regA|=0x100;
				flagC=(regA & 1)==1;
				flagH=flagN=flagZ=false;
				regA>>=1;
			}
			case 0x17 -> { // RLA
				regA<<=1;
				if(flagC){
					regA|=1;
				}
				flagH=flagN=flagZ=false;
				if((regA & 0x100)!=0){
					flagC=true;
					regA&=0xFF;
				}else{
					flagC=false;
				}
			}
			case 0x07 -> { // RLCA
				regA=((regA << 1) | (regA >> 7)) & 0xFF;
				flagH=flagN=flagZ=false;
				flagC=((regA & 1)==1);
			}
			case 0x0F -> { // RRCA
				flagH=flagN=flagZ=false;
				flagC=(regA & 1)==1;
				regA=((regA & 1) << 7) | (regA >> 1);
			}

			// 16-bit math
			case 0x09 -> { // ADD HL, BC
				int hl=getRegHL();
				int bc=getRegBC();
				int prev=hl;
				hl+=bc;
				flagN=false;
				flagH=((prev & 0xFFF)+(bc & 0xFFF)>0xFFF);
				flagC=(hl>0xFFFF);
				setRegHL(hl);
				delayCycles=1;
			}
			case 0x19 -> { // ADD HL, DE
				int hl=getRegHL();
				int de=getRegDE();
				int prev=hl;
				hl+=de;
				flagN=false;
				flagH=((prev & 0xFFF)+(de & 0xFFF)>0xFFF);
				flagC=(hl>0xFFFF);
				setRegHL(hl);
				delayCycles=1;
			}
			case 0x29 -> { // ADD HL, HL
				int hl=getRegHL();
				int prev=hl;
				hl+=hl;
				flagN=false;
				flagH=((prev & 0xFFF)+(prev & 0xFFF)>0xFFF);
				flagC=(hl>0xFFFF);
				setRegHL(hl);
				delayCycles=1;
			}
			case 0x39 -> { // ADD HL, SP
				int hl=getRegHL();
				int prev=hl;
				hl+=sp;
				flagN=false;
				flagH=((prev & 0xFFF)+(sp & 0xFFF)>0xFFF);
				flagC=(hl>0xFFFF);
				setRegHL(hl);
				delayCycles=1;
			}

			// RST n
			case 0xC7 -> doRstInstr(0x00);
			case 0xCF -> doRstInstr(0x08);
			case 0xD7 -> doRstInstr(0x10);
			case 0xDF -> doRstInstr(0x18);
			case 0xE7 -> doRstInstr(0x20);
			case 0xEF -> doRstInstr(0x28);
			case 0xF7 -> doRstInstr(0x30);
			case 0xFF -> doRstInstr(0x38);

			// Flags
			case 0x37 -> { // SCF
				flagC=true;
				flagH=flagN=false;
			}
			case 0x3F -> { // CCF
				flagC=!flagC;
				flagH=flagN=false;
			}
			case 0x27 -> { // DAA
//				System.out.println("DAA");
				boolean c=flagC;
				boolean h=flagH;
				boolean n=flagN;
				flagH=false;

				if(!n){
					if(c || regA>0x99){
						regA+=0x60;
						flagC=true;
					}
					if(h || (regA & 0x0f)>9){
						regA+=0x06;
					}
				}else{
					if(c){
						regA-=0x60;
					}
					if(h){
						regA-=0x06;
					}
				}
				regA&=0xFF;

				flagZ=(regA==0);
			}

			// Extended
			case 0xCB -> doExtendedInstruction(memoryBus.read(pc++));

			default -> throw new UnsupportedOperationException(String.format("Unimplemented opcode 0x%02X (%s) at pc=0x%04X", instr, Integer.toBinaryString(instr), pc-1));
		}
	}

	private void doExtendedInstruction(int instr){
		IntSupplier getter=switch(instr & 7){
			case 0 -> ()->regB;
			case 1 -> ()->regC;
			case 2 -> ()->regD;
			case 3 -> ()->regE;
			case 4 -> ()->regH;
			case 5 -> ()->regL;
			case 6 -> ()->{
				delayCycles++;
				return memoryBus.read(getRegHL());
			};
			case 7 -> ()->regA;
			default -> throw new IllegalArgumentException();
		};
		IntConsumer setter=switch(instr & 7){
			case 0 -> v->regB=v;
			case 1 -> v->regC=v;
			case 2 -> v->regD=v;
			case 3 -> v->regE=v;
			case 4 -> v->regH=v;
			case 5 -> v->regL=v;
			case 6 -> v->{
				delayCycles++;
				memoryBus.write(getRegHL(), v);
			};
			case 7 -> v->regA=v;
			default -> throw new IllegalArgumentException();
		};

		switch(instr & 0xF8){
			case 0x00 -> { // RLC r8
				int res=getter.getAsInt();
				res=((res << 1) | (res >> 7)) & 0xFF;
				flagH=flagN=false;
				flagZ=(res==0);
				flagC=((res & 1)==1);
				setter.accept(res);
			}
			case 0x08 -> { // RRC r8
				int res=getter.getAsInt();
				flagH=flagN=false;
				flagC=(res & 1)==1;
				res=((res & 1) << 7) | (res >> 1);
				flagZ=(res==0);
				setter.accept(res);
			}
			case 0x10 -> { // RL r8
				int res=getter.getAsInt() << 1;
				if(flagC){
					res|=1;
				}
				flagH=flagN=false;
				//noinspection AssignmentUsedAsCondition
				if(flagC=((res & 0x100)!=0)){
					res&=0xFF;
				}
				flagZ=(res==0);
				setter.accept(res);
			}
			case 0x18 -> { // RR r8
				int res=getter.getAsInt();
				if(flagC)
					res|=0x100;
				flagH=flagN=false;
				flagC=(res & 1)==1;
				res>>=1;
				flagZ=(res==0);
				setter.accept(res);
			}
			case 0x20 -> { // SLA r8
				int res=getter.getAsInt() << 1;
				flagH=flagN=false;
				//noinspection AssignmentUsedAsCondition
				if(flagC=((res & 0x100)!=0)){
					res&=0xFF;
				}
				flagZ=(res==0);
				setter.accept(res);
			}
			case 0x28 -> { // SRA r8
				int res=getter.getAsInt();
				flagH=flagN=false;
				flagC=(res & 1)==1;
				res=(res & 0x80) | (res >> 1);
				flagZ=(res==0);
				setter.accept(res);
			}
			case 0x30 -> { // SWAP r8
				int res=getter.getAsInt();
				res=((res & 0xF) << 4) | ((res & 0xF0) >> 4);
				flagC=flagH=flagN=false;
				flagZ=res==0;
				setter.accept(res);
			}
			case 0x38 -> { // SRL r8
				int res=getter.getAsInt();
				flagH=flagN=false;
				flagC=(res & 1)==1;
				res>>=1;
				flagZ=(res==0);
				setter.accept(res);
			}
			// BIT x, r8
			case 0x40 -> doBitInstr(getter.getAsInt(), 0);
			case 0x48 -> doBitInstr(getter.getAsInt(), 1);
			case 0x50 -> doBitInstr(getter.getAsInt(), 2);
			case 0x58 -> doBitInstr(getter.getAsInt(), 3);
			case 0x60 -> doBitInstr(getter.getAsInt(), 4);
			case 0x68 -> doBitInstr(getter.getAsInt(), 5);
			case 0x70 -> doBitInstr(getter.getAsInt(), 6);
			case 0x78 -> doBitInstr(getter.getAsInt(), 7);
			// RES x, r8
			case 0x80 -> setter.accept(doResInstr(getter.getAsInt(), 0));
			case 0x88 -> setter.accept(doResInstr(getter.getAsInt(), 1));
			case 0x90 -> setter.accept(doResInstr(getter.getAsInt(), 2));
			case 0x98 -> setter.accept(doResInstr(getter.getAsInt(), 3));
			case 0xA0 -> setter.accept(doResInstr(getter.getAsInt(), 4));
			case 0xA8 -> setter.accept(doResInstr(getter.getAsInt(), 5));
			case 0xB0 -> setter.accept(doResInstr(getter.getAsInt(), 6));
			case 0xB8 -> setter.accept(doResInstr(getter.getAsInt(), 7));
			// SET x, r8
			case 0xC0 -> setter.accept(doSetInstr(getter.getAsInt(), 0));
			case 0xC8 -> setter.accept(doSetInstr(getter.getAsInt(), 1));
			case 0xD0 -> setter.accept(doSetInstr(getter.getAsInt(), 2));
			case 0xD8 -> setter.accept(doSetInstr(getter.getAsInt(), 3));
			case 0xE0 -> setter.accept(doSetInstr(getter.getAsInt(), 4));
			case 0xE8 -> setter.accept(doSetInstr(getter.getAsInt(), 5));
			case 0xF0 -> setter.accept(doSetInstr(getter.getAsInt(), 6));
			case 0xF8 -> setter.accept(doSetInstr(getter.getAsInt(), 7));
			default -> throw new UnsupportedOperationException(String.format("Unimplemented opcode 0xCB_%02X (%s) at pc=0x%04X", instr, Integer.toBinaryString(instr), pc-2));
		}
	}

	private void doBitInstr(int value, int bit){
		flagN=false;
		flagH=true;
		flagZ=((value & (1 << bit))==0);
	}

	private int doResInstr(int value, int bit){
		return value&~(1 << bit);
	}

	private int doSetInstr(int value, int bit){
		return value|(1 << bit);
	}

	private void doAddInstr(int value){
		int sum=regA+value;
//		int noCarrySum=regA ^ value;
//		int carryInto=sum ^ noCarrySum;
//		System.out.println(Integer.toHexString(regA)+"+"+Integer.toHexString(value)+" carry into "+Integer.toHexString(carryInto));
		flagH=(regA & 0x0F)+(value & 0x0F)>0x0F;
		regA=sum & 0xFF;
		flagN=false;
//		flagC=((carryInto & 0x100)!=0 /*|| sum>0xFF*/);
		flagC=sum>0xFF;
		flagZ=(regA==0);
//		flagH=((carryInto & 0x10)!=0);
	}

	private void doAdcInstr(int value){
		int carry=flagC ? 1 : 0;
		int sum=regA+value+carry;
		flagN=false;
		flagC=sum>0xFF;
		flagH=(regA & 0x0F)+(value & 0x0F)+carry>0x0F;
		regA=sum & 0xFF;
		flagZ=(regA==0);
	}

	private void doSubInstr(int value){
		int x=(regA-value) & 0xFF;
		flagN=true;
		flagC=(regA<value);
		flagZ=(x==0);
		flagH=((regA & 0xF)<(value & 0xF));
		regA=x;
	}

	private void doSbcInstr(int value){
		int carry=flagC ? 1 : 0;
		int res=regA-value-carry;
		flagN=true;
		flagC=res<0;
		int prev=regA;
		regA=res & 0xFF;
		flagH=((prev ^ value ^ regA) & 0x10)!=0;
		flagZ=(regA==0);
	}

	private void doCpInstr(int value){
		int x=regA-value;
		flagN=true;
		//noinspection AssignmentUsedAsCondition
		if(flagC=(x >> 8)!=0){
			x&=0xFF;
		}
		flagZ=(x==0);
		flagH=((regA & 0xF)<(value & 0xF));
	}

	private void doXorInstr(int value){
		regA^=value;
		flagC=flagN=flagH=false;
		flagZ=(regA==0);
	}

	private void doAndInstr(int value){
		regA&=value;
		flagC=flagN=false;
		flagH=true;
		flagZ=(regA==0);
	}

	private void doRstInstr(int vec){
		sp--;
		memoryBus.write(sp--, pc >> 8);
		memoryBus.write(sp, pc & 0xFF);
		pc=vec;
		delayCycles=3;
	}

	private int getRegBC(){
		return (regB << 8) | regC;
	}

	private void setRegBC(int value){
		regB=(value >> 8) & 0xFF;
		regC=value & 0xFF;
	}

	private int getRegDE(){
		return (regD << 8) | regE;
	}

	private void setRegDE(int value){
		regD=(value >> 8) & 0xFF;
		regE=value & 0xFF;
	}

	private int getRegHL(){
		return (regH << 8) | regL;
	}

	private void setRegHL(int value){
		regH=(value >> 8) & 0xFF;
		regL=value & 0xFF;
	}

	private int getRegAF(){
		return (regA << 8) | getFlags();
	}

	private void setRegAF(int value){
		regA=(value >> 8) & 0xFF;
		setFlags(value & 0xFF);
	}

	private int getFlags(){
		int flags=0;
		if(flagC)
			flags|=FLAG_CARRY;
		if(flagH)
			flags|=FLAG_HALF_CARRY;
		if(flagN)
			flags|=FLAG_SUBTRACTION;
		if(flagZ)
			flags|=FLAG_ZERO;
		return flags;
	}

	private void setFlags(int flags){
		flagC=(flags & FLAG_CARRY)!=0;
		flagH=(flags & FLAG_HALF_CARRY)!=0;
		flagN=(flags & FLAG_SUBTRACTION)!=0;
		flagZ=(flags & FLAG_ZERO)!=0;
	}

	private void updateFlagsForIncrement(int prevVal, int newVal){
		flagZ=newVal==0;
		flagH=(prevVal & 0xF0)!=(newVal & 0xF0);
		flagN=false;
	}

	private void updateFlagsForDecrement(int prevVal, int newVal){
		flagZ=newVal==0;
		flagH=(prevVal & 0xF0)!=(newVal & 0xF0);
		flagN=true;
	}

	private void doInterrupt(int vector){
//		System.out.printf("Interrupt -> %02x\n", vector);
		interruptsEnabled=false;
		halted=false;
		sp--;
		memoryBus.write(sp--, pc >> 8);
		memoryBus.write(sp, pc & 0xFF);
		pc=vector;
		delayCycles=5;
	}
}
