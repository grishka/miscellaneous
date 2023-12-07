package me.grishka.gbemulator;

import java.util.ArrayList;

public class MemoryBus{
	private ArrayList<DeviceAddressRange> devices=new ArrayList<>();

	public int read(int address){
		DeviceAddressRange ar=findDeviceForAddress(address);
		if(ar==null)
			return 0xFF;
		return ar.dev.read(address);
	}

	public void write(int address, int value){
		DeviceAddressRange ar=findDeviceForAddress(address);
		if(ar==null)
			return;
		ar.dev.write(address, value);
	}

	private DeviceAddressRange findDeviceForAddress(int address){
		for(DeviceAddressRange ar:devices){
			if(ar.start<=address && ar.end>=address)
				return ar;
		}
		System.out.printf("No device mapped for address 0x%04X\n", address);
		return null;
	}

	public void addDevice(MemoryMappedDevice dev, int start, int end){
		devices.add(new DeviceAddressRange(dev, start, end));
	}

	public void removeDevice(MemoryMappedDevice dev){
		for(DeviceAddressRange ar:devices){
			if(ar.dev==dev){
//				System.out.println("Found and removed "+ar);
				devices.remove(ar);
				return;
			}
		}
	}

	private record DeviceAddressRange(MemoryMappedDevice dev, int start, int end){}
}
