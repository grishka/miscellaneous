package me.grishka.gbemulator;

public class BootROMMapper extends MemoryMappedDevice{
	private final MemoryBus bus;
	private final BootROM bootROM;

	public BootROMMapper(MemoryBus bus, BootROM bootROM){
		this.bus=bus;
		this.bootROM=bootROM;
	}

	@Override
	public int read(int address){
		return 0;
	}

	@Override
	public void write(int address, int value){
//		System.out.println("Unmapping boot ROM");
		bus.removeDevice(bootROM);
		bus.removeDevice(this);
	}
}
