package me.grishka.gbemulator;

public class InterruptEnableDevice extends MemoryMappedDevice{
	private int flags;

	@Override
	public int read(int address){
		return flags;
	}

	@Override
	public void write(int address, int value){
//		System.out.println("Write interrupt enable: "+value);
		flags=value;
	}
}
