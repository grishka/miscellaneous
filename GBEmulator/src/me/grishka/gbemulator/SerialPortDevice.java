package me.grishka.gbemulator;

import java.io.IOException;
import java.io.OutputStream;

public class SerialPortDevice extends MemoryMappedDevice{
	private int data, control;

	private int FLAG_INTERNAL_CLOCK=1;
	private int FLAG_START_TRANSFER=128;
	private final OutputStream output;

	public SerialPortDevice(OutputStream output){
		this.output=output;
	}

	@Override
	public int read(int address){
		return address==0xFF01 ? data : control;
	}

	@Override
	public void write(int address, int value){
		if(address==0xFF01){
			data=value;
		}else{
			control=value;
			if((control & FLAG_START_TRANSFER)==FLAG_START_TRANSFER){
				try{output.write(data);}catch(IOException ignore){}
			}
		}
	}

	public void doClockCycle(){
		if((control & FLAG_START_TRANSFER)==FLAG_START_TRANSFER){
			control&=~FLAG_START_TRANSFER;
		}
	}
}
