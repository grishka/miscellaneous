package me.grishka.gbemulator;

public class InterruptFlagsDevice extends MemoryMappedDevice{
	private int flags;

	public static final int FLAG_VBLANK=1;
	public static final int FLAG_LCD_STAT=2;
	public static final int FLAG_TIMER=4;
	public static final int FLAG_SERIAL=8;
	public static final int FLAG_JOYPAD=16;

	@Override
	public int read(int address){
//		System.out.println("Read interrupt flags: "+flags);
		return flags;
	}

	@Override
	public void write(int address, int value){
//		System.out.println("Write interrupt flags: "+value);
		flags=value;
	}

	public void set(int flag){
		flags|=flag;
	}

	public void clear(int flag){
		flags&=~flag;
	}
}
