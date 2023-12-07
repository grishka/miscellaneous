package me.grishka.gbemulator;

public class TimerDividerDevice extends MemoryMappedDevice{
	/**
	 * a.k.a. TMA
	 */
	private int counterModulo;
	/**
	 * a.k.a. TIMA
	 */
	private int counter;
	private int control;
	private int divider;

	private boolean counterEnabled;
	private int counterDividerShift=9;
	private int clockCyclesToCounterReload=0;
	private boolean counterInputIsHigh;

	private final InterruptFlagsDevice interruptFlags;

	public TimerDividerDevice(InterruptFlagsDevice interruptFlags){
		this.interruptFlags=interruptFlags;
	}

	@Override
	public int read(int address){
//		System.out.printf("Read timer reg 0x%04X\n", address);
		return switch(address){
			// DIV
			case 0xFF04 -> divider >> 8;

			// TIMA
			case 0xFF05 -> {
//				System.out.println("read counter is "+counter);
				yield counter;
			}

			// TMA
			case 0xFF06 -> counterModulo;

			// TAC
			case 0xFF07 -> control;

			default -> throw new UnsupportedOperationException();
		};
	}

	@Override
	public void write(int address, int value){
//		System.out.printf("Timer reg write 0x%04X value %02X\n", address, value);
		switch(address){
			case 0xFF04 -> {
				divider=0;
				maybeIncrementCounter();
			}
			case 0xFF05 -> {
				if(clockCyclesToCounterReload>4){
					counter=value;
					clockCyclesToCounterReload=0;
				}else if(clockCyclesToCounterReload>0){
					// ignore write
				}else{
					counter=value;
				}
			}
			case 0xFF06 -> {
				counterModulo=value;
				if(clockCyclesToCounterReload<=4 && clockCyclesToCounterReload>0){
					counter=value;
				}
			}
			case 0xFF07 -> {
				control=value;
				counterEnabled=(control & 4)==4;
				counterDividerShift=switch(control & 3){
					case 0 -> 9;
					case 1 -> 3;
					case 2 -> 5;
					case 3 -> 7;
					default -> throw new IllegalStateException();
				};
				maybeIncrementCounter();
			}
		}
	}

	public void doClockCycle(){
		divider=(divider+1) & 0xFFFF;
		maybeIncrementCounter();
		if(clockCyclesToCounterReload>0){
			clockCyclesToCounterReload--;
			if(clockCyclesToCounterReload==4){
				counter=counterModulo;
				interruptFlags.set(InterruptFlagsDevice.FLAG_TIMER);
			}
		}
	}

	private void maybeIncrementCounter(){
		boolean inputIsHigh=((divider >> counterDividerShift) & 1)==1 && counterEnabled;
		// Counter increments on falling edge of the selected bit of the divider
		if(counterInputIsHigh && !inputIsHigh){
			counter++;
			if(counter>0xFF){
				clockCyclesToCounterReload=8;
				counter=0;
			}
		}
		counterInputIsHigh=inputIsHigh;
	}
}
