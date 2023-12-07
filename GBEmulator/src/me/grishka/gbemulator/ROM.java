package me.grishka.gbemulator;

import java.io.DataInputStream;
import java.io.File;
import java.io.FileInputStream;
import java.io.IOException;

import me.grishka.gbemulator.mappers.MBC1;
import me.grishka.gbemulator.mappers.MBC5;
import me.grishka.gbemulator.mappers.NullMapper;

public class ROM extends MemoryMappedDevice{
	private byte[][] banks;
	private int mbcType;
	private String title;
	private Mapper mapper;

	public ROM(File source){
		System.out.println("Loading ROM from "+source);
		int numBanks=(int)source.length() >> 14;
		System.out.println(numBanks+" ROM banks");
		banks=new byte[numBanks][16384];
		try(DataInputStream in=new DataInputStream(new FileInputStream(source))){
			for(byte[] bank:banks){
				in.readFully(bank);
			}
		}catch(IOException x){
			throw new RuntimeException(x);
		}
		mbcType=(int)banks[0][0x147] & 0xFF;
		System.out.println("ROM mapper type is "+getMbcTypeDescription(mbcType));
		mapper=switch(mbcType){
			case 0x00 -> new NullMapper(banks, false);
			case 0x01 -> new MBC1(banks, false);
			case 0x02, 0x03 -> new MBC1(banks, true);
			case 0x08, 0x09 -> new NullMapper(banks, true);
			case 0x19 -> new MBC5(banks, false);
			case 0x1A, 0x1B -> new MBC5(banks, true);
			default -> throw new UnsupportedOperationException("Unsupported mapper type: "+getMbcTypeDescription(mbcType));
		};
		if(mbcType==0x03 || mbcType==0x1B){
			String sourcePath=source.getAbsolutePath();
			File saveState=new File(sourcePath.substring(0, sourcePath.lastIndexOf('.'))+".sav");
			if(saveState.exists()){
				try(DataInputStream in=new DataInputStream(new FileInputStream(saveState))){
					byte[] ram=new byte[(int)saveState.length()];
					in.readFully(ram);
					mapper.setPersistentRamState(ram);
				}catch(IOException x){
					x.printStackTrace();
				}
			}
		}
		System.out.println("ROM loaded");
		title=new String(banks[0], 0x134, 16).trim();
	}

	@Override
	public int read(int address){
		return mapper.read(address);
	}

	@Override
	public void write(int address, int value){
		mapper.write(address, value);
	}

	public String getTitle(){
		return title;
	}

	public static abstract class Mapper{
		protected final byte[][] romBanks;
		public Mapper(byte[][] romBanks){
			this.romBanks=romBanks;
		}

		public abstract int read(int address);
		public abstract void write(int address, int value);

		public void setPersistentRamState(byte[] ram){}
	}

	private static String getMbcTypeDescription(int type){
		return switch(type){
			case 0x00 -> "ROM ONLY";
			case 0x01 -> "MBC1";
			case 0x02 -> "MBC1+RAM";
			case 0x03 -> "MBC1+RAM+BATTERY";
			case 0x05 -> "MBC2";
			case 0x06 -> "MBC2+BATTERY";
			case 0x08 -> "ROM+RAM";
			case 0x09 -> "ROM+RAM+BATTERY";
			case 0x0B -> "MMM01";
			case 0x0C -> "MMM01+RAM";
			case 0x0D -> "MMM01+RAM+BATTERY";
			case 0x0F -> "MBC3+TIMER+BATTERY";
			case 0x10 -> "MBC3+TIMER+RAM+BATTERY";
			case 0x11 -> "MBC3";
			case 0x12 -> "MBC3+RAM";
			case 0x13 -> "MBC3+RAM+BATTERY";
			case 0x19 -> "MBC5";
			case 0x1A -> "MBC5+RAM";
			case 0x1B -> "MBC5+RAM+BATTERY";
			case 0x1C -> "MBC5+RUMBLE";
			case 0x1D -> "MBC5+RUMBLE+RAM";
			case 0x1E -> "MBC5+RUMBLE+RAM+BATTERY";
			case 0x20 -> "MBC6";
			case 0x22 -> "MBC7+SENSOR+RUMBLE+RAM+BATTERY";
			case 0xFC -> "POCKET CAMERA";
			case 0xFD -> "BANDAI TAMA5";
			case 0xFE -> "HuC3";
			case 0xFF -> "HuC1+RAM+BATTERY";
			default -> String.format("Unknown 0x%02X", type);
		};
	}
}
