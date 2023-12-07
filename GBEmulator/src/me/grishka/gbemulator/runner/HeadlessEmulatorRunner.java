package me.grishka.gbemulator.runner;

import java.io.ByteArrayOutputStream;
import java.io.File;
import java.io.OutputStream;

import me.grishka.gbemulator.PPU;
import me.grishka.gbemulator.display.BufferedImageScreenBitmapHolder;

public class HeadlessEmulatorRunner extends EmulatorRunner{
	private BufferedImageScreenBitmapHolder screen;
	private ByteArrayOutputStream serialBuffer=new ByteArrayOutputStream();

	public HeadlessEmulatorRunner(File romFile){
		super(romFile, new BufferedImageScreenBitmapHolder(PPU.SCREEN_W, PPU.SCREEN_H));
		this.screen=(BufferedImageScreenBitmapHolder) super.screen;
	}

	@Override
	protected boolean wantAudioOutput(){
		return false;
	}

	@Override
	protected OutputStream getSerialPortOutput(){
		return serialBuffer;
	}

	public byte[] getSerialBuffer(){
		return serialBuffer.toByteArray();
	}

	public int getSerialBufferSize(){
		return serialBuffer.size();
	}
}
