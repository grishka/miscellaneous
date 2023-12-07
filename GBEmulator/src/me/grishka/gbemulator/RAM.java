package me.grishka.gbemulator;

public class RAM extends MemoryMappedDevice{
	private final byte[] memory;
	private final int startAddress;

	public RAM(int size, int startAddress){
		memory=new byte[size];
		this.startAddress=startAddress;
	}

	public RAM(RAM other, int startAddress){
		memory=other.memory;
		this.startAddress=startAddress;
	}

	@Override
	public int read(int address){
		return (int)memory[address-startAddress] & 0xFF;
	}

	@Override
	public void write(int address, int value){
		memory[address-startAddress]=(byte)value;
	}

	public byte[] getRawBytes(){
		return memory;
	}
}
