package me.grishka.gbemulator;

public abstract class MemoryMappedDevice{
	public abstract int read(int address);
	public abstract void write(int address, int value);
}
