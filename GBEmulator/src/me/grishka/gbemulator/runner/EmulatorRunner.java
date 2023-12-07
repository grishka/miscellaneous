package me.grishka.gbemulator.runner;

import java.io.File;
import java.io.OutputStream;

import me.grishka.gbemulator.APU;
import me.grishka.gbemulator.BootROM;
import me.grishka.gbemulator.BootROMMapper;
import me.grishka.gbemulator.CPU;
import me.grishka.gbemulator.GatedRAM;
import me.grishka.gbemulator.InputDevice;
import me.grishka.gbemulator.InterruptEnableDevice;
import me.grishka.gbemulator.InterruptFlagsDevice;
import me.grishka.gbemulator.MemoryBus;
import me.grishka.gbemulator.PPU;
import me.grishka.gbemulator.RAM;
import me.grishka.gbemulator.ROM;
import me.grishka.gbemulator.SerialPortDevice;
import me.grishka.gbemulator.TimerDividerDevice;
import me.grishka.gbemulator.display.ScreenBitmapHolder;

public abstract class EmulatorRunner{
	protected InputDevice inputDevice;
	protected final ScreenBitmapHolder screen;
	protected final File romFile;
	protected APU apu;
	protected InterruptFlagsDevice interruptFlags;
	protected TimerDividerDevice timer;
	protected MemoryBus memBus;
	protected SerialPortDevice serialPort;
	protected PPU ppu;
	protected ROM rom;

	protected boolean keepRunning=true;

	protected EmulatorRunner(File romFile, ScreenBitmapHolder screen){
		this.screen=screen;
		this.romFile=romFile;
	}

	protected abstract boolean wantAudioOutput();
	protected abstract OutputStream getSerialPortOutput();

	public void prepare(){
		interruptFlags=new InterruptFlagsDevice();
		timer=new TimerDividerDevice(interruptFlags);
		serialPort=new SerialPortDevice(getSerialPortOutput());
		memBus=new MemoryBus();

		GatedRAM videoRAM=new GatedRAM(0x2000, 0x8000);
		GatedRAM oam=new GatedRAM(0xA0, 0xFE00);
		ppu=new PPU(memBus, videoRAM, oam, screen, interruptFlags);
		BootROM bootROM=new BootROM();
		RAM wram0=new RAM(0x1000, 0xC000);
		RAM wram1=new RAM(0x1000, 0xD000);
		RAM wram0Mirror=new RAM(wram0, 0xE000);
		RAM wram1Mirror=new RAM(wram1, 0xF000);
		RAM hram=new RAM(0x80, 0xFF80);
		inputDevice=new InputDevice(interruptFlags);
		apu=new APU(wantAudioOutput());

		memBus.addDevice(bootROM, 0x0000, 0x00FF);
		rom=new ROM(romFile);
		memBus.addDevice(rom, 0x0000, 0x7FFF);
		memBus.addDevice(videoRAM, 0x8000, 0x9FFF); // VRAM

		memBus.addDevice(rom, 0xA000, 0xBFFF); // ExtRAM

		memBus.addDevice(wram0, 0xC000, 0xCFFF); // WRAM0
		memBus.addDevice(wram1, 0xD000, 0xDFFF); // WRAM1
		memBus.addDevice(wram0Mirror, 0xE000, 0xEFFF);
		memBus.addDevice(wram1Mirror, 0xF000, 0xFDFF);
		memBus.addDevice(oam, 0xFE00, 0xFE9F);
		memBus.addDevice(inputDevice, 0xFF00, 0xFF00);
		memBus.addDevice(serialPort, 0xFF01, 0xFF02);
		memBus.addDevice(timer, 0xFF04, 0xFF07);
		memBus.addDevice(interruptFlags, 0xFF0F, 0xFF0F);
		memBus.addDevice(apu, 0xFF10, 0xFF3F);
		memBus.addDevice(ppu, 0xFF40, 0xFF4B);
		memBus.addDevice(hram, 0xFF80, 0xFFFE); // HRAM
		memBus.addDevice(new InterruptEnableDevice(), 0xFFFF, 0xFFFF);
		memBus.addDevice(new BootROMMapper(memBus, bootROM), 0xFF50, 0xFF50);
	}

	public void run(){
		int nextCpuCycle=4;
		int nextApuCycle=16384;
		CPU cpu=new CPU(memBus, interruptFlags);
		while(keepRunning){
			timer.doClockCycle();
			serialPort.doClockCycle();
			ppu.doClockCycle();
			if(--nextApuCycle==0){
				apu.doClockCycle();
				nextApuCycle=16384; // 256 Hz
			}
			if(--nextCpuCycle==0){
				cpu.doClockCycle();
				nextCpuCycle=4;
			}
			delay();
		}
		apu.release();
	}

	public void stop(){
		keepRunning=false;
	}

	public void buttonPressed(InputDevice.Button button){
		if(inputDevice!=null)
			inputDevice.buttonPressed(button);
	}

	public void buttonReleased(InputDevice.Button button){
		if(inputDevice!=null)
			inputDevice.buttonReleased(button);
	}

	protected void delay(){

	}
}
