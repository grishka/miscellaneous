package me.grishka.gbemulator.mappers;

import me.grishka.gbemulator.ROM;

public class MBC5 extends ROM.Mapper{
	private int selectedRomBank=1, selectedRamBank=0;
	private byte[][] ramBanks;
	private boolean ramEnabled=false;

	public MBC5(byte[][] romBanks, boolean hasRam){
		super(romBanks);
		if(hasRam){
			ramBanks=new byte[16][0x2000];
		}
	}

	@Override
	public int read(int address){
		if(address>=0xA000){
			if(ramEnabled && ramBanks!=null)
				return ((int)ramBanks[selectedRamBank][address-0xA000]) & 0xFF;
			return 0xFF;
		}
		if(address<0x4000)
			return ((int)romBanks[0][address]) & 0xFF;
		return ((int)romBanks[selectedRomBank%romBanks.length][address-0x4000]) & 0xFF;
	}

	@Override
	public void write(int address, int value){
		if(address>=0x2000 && address<=0x2FFF){
//			System.out.println("Switching ROM bank to "+value);
			selectedRomBank=value | (selectedRomBank & 0x100);
			return;
		}else if(address>=0x3000 && address<=0x3FFF){
			selectedRomBank=(selectedRomBank & 0xFF) | ((value & 1) << 8);
			return;
		}else if(address>=0 && address<=0x1FFF){ // RAM enable
			ramEnabled=(value & 0x0F)==0x0A;
			return;
		}else if(address>=0x4000 && address<=0x5FFF){ // RAM bank switch
			selectedRamBank=value & 0x0F;
			return;
		}else if(address>=0xA000 && address<=0xBFFF){ // On-cartridge RAM
			if(ramEnabled)
				ramBanks[selectedRamBank][address-0xA000]=(byte)value;
			return;
		}
//		throw new UnsupportedOperationException("Can't write to ROM "+Integer.toHexString(address)+" value "+Integer.toHexString(value));
	}

	@Override
	public void setPersistentRamState(byte[] ram){
		for(int i=0;i<ram.length;i+=0x2000){
			System.arraycopy(ram, i, ramBanks[i/0x2000], 0, 0x2000);
		}
	}
}
