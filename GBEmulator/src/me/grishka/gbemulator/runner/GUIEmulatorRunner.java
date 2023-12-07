package me.grishka.gbemulator.runner;

import java.io.File;
import java.io.IOException;
import java.io.OutputStream;

import javax.swing.JFrame;

import me.grishka.gbemulator.display.ScreenBitmapHolder;

public class GUIEmulatorRunner extends EmulatorRunner{
	private final JFrame window;
	private int cyclesToDelay=100;
	private long lastDelayTime=System.nanoTime();
	private static final long HUNDRED_CYCLE_DURATION=23870;

	public GUIEmulatorRunner(File romFile, ScreenBitmapHolder screen, JFrame window){
		super(romFile, screen);
		this.window=window;
	}

	@Override
	protected boolean wantAudioOutput(){
		return true;
	}

	@Override
	protected OutputStream getSerialPortOutput(){
		return new OutputStream(){
			@Override
			public void write(int b) throws IOException{
				System.out.write(b);
				System.out.flush();
			}
		};
	}

	@Override
	public void prepare(){
		super.prepare();
		window.setTitle(rom.getTitle());
	}

	@Override
	protected void delay(){
		if(--cyclesToDelay==0){
			 cyclesToDelay=100;

			 long timeToSleep;
			 do{
				 timeToSleep=HUNDRED_CYCLE_DURATION-(System.nanoTime()-lastDelayTime);
			 }while(timeToSleep>0);
			 lastDelayTime=System.nanoTime();
		}
	}
}
