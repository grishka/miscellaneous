package me.grishka.gbemulator.mappers;

import me.grishka.gbemulator.ROM;

public class NullMapper extends ROM.Mapper{
	private final byte[] ram;
	public NullMapper(byte[][] romBanks, boolean hasRAM){
		super(romBanks);
		ram=hasRAM ? new byte[0x2000] : null;
	}

	@Override
	public int read(int address){
		if(address>=0xA000){
			return ram!=null ? ((int)ram[address-0xA000] & 0xFF) : 0xFF;
		}
		return (int)(address<0x4000 ? romBanks[0][address] : romBanks[1][address-0x4000]) & 0xFF;
	}

	@Override
	public void write(int address, int value){
		if(address>=0xA000 && ram!=null){
			ram[address-0xA000]=(byte)value;
		}
	}
}
