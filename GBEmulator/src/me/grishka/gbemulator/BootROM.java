package me.grishka.gbemulator;

import java.io.DataInputStream;
import java.io.IOException;
import java.util.Objects;

public class BootROM extends MemoryMappedDevice{
	private byte[] data=new byte[256];

	public BootROM(){
		try(DataInputStream in=new DataInputStream(Objects.requireNonNull(getClass().getClassLoader().getResourceAsStream("dmg_boot.bin")))){
			in.readFully(data);
		}catch(IOException x){
			throw new RuntimeException(x);
		}
	}

	@Override
	public int read(int address){
		return (int)data[address] & 0xFF;
	}

	@Override
	public void write(int address, int value){
		throw new UnsupportedOperationException();
	}
}
