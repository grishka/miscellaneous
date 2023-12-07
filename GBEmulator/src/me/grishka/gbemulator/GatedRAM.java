package me.grishka.gbemulator;

public class GatedRAM extends RAM{
	private boolean isAccessible=true;

	public GatedRAM(int size, int startAddress){
		super(size, startAddress);
	}

	public void setAccessible(boolean accessible){
		isAccessible=accessible;
	}

	@Override
	public int read(int address){
		if(!isAccessible)
			return 0xFF;
		return super.read(address);
	}

	@Override
	public void write(int address, int value){
		if(isAccessible)
			super.write(address, value);
	}
}
