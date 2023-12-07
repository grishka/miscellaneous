package me.grishka.gbemulator.mappers;

import me.grishka.gbemulator.ROM;

public class MBC1 extends ROM.Mapper{
	private int selectedBank=1, selectedRamBankAndUpperBits=0;
	private byte[][] ramBanks;
	private boolean ramEnabled=false, isAdvancedMode=false;

	public MBC1(byte[][] romBanks, boolean hasRam){
		super(romBanks);
		if(hasRam){
			ramBanks=new byte[4][0x2000];
		}
	}

	@Override
	public int read(int address){
		if(address>=0xA000){
			if(ramEnabled && ramBanks!=null)
				return ((int)ramBanks[selectedRamBankAndUpperBits][address-0xA000]) & 0xFF;
			return 0xFF;
		}
		int upperBits=isAdvancedMode ? (selectedRamBankAndUpperBits << 5) : 0;
		if(address<0x4000)
			return ((int)romBanks[upperBits%romBanks.length][address]) & 0xFF;
		return ((int)romBanks[(selectedBank | upperBits)%romBanks.length][address-0x4000]) & 0xFF;
	}

	@Override
	public void write(int address, int value){
		if(address>=0x2000 && address<=0x3FFF){
			value&=0x1F;
//			System.out.println("Switching ROM bank to "+value);
			if(value==0)
				value=1;
			selectedBank=value%romBanks.length;
			return;
		}else if(address>=0 && address<=0x1FFF){ // RAM enable
			ramEnabled=(value & 0x0F)==0x0A;
			return;
		}else if(address>=0x4000 && address<=0x5FFF){ // RAM bank switch
			if(isAdvancedMode)
				selectedRamBankAndUpperBits=value & 3;
			return;
		}else if(address>=0x6000 && address<=0x7FFF){ // Banking mode
			isAdvancedMode=(value & 1)==1;
			return;
		}else if(address>=0xA000 && address<=0xBFFF){ // On-cartridge RAM
			if(ramEnabled)
				ramBanks[selectedRamBankAndUpperBits][address-0xA000]=(byte)value;
			return;
		}
		throw new UnsupportedOperationException("Can't write to ROM "+Integer.toHexString(address)+" value "+Integer.toHexString(value));
	}
}
