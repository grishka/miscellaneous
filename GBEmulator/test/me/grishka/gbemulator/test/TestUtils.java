package me.grishka.gbemulator.test;

import java.io.File;
import java.nio.charset.StandardCharsets;

import me.grishka.gbemulator.runner.HeadlessEmulatorRunner;

public class TestUtils{
	public static final byte[] MTS_PASSED={3, 5, 8, 13, 21, 34};

	public static String runBlarggTestRomAndGetSerialOutput(String romPath){
		HeadlessEmulatorRunner runner=new HeadlessEmulatorRunner(new File(romPath));
		runner.prepare();
		Thread thread=new Thread(runner::run);
		thread.start();
		while(true){
			String output=new String(runner.getSerialBuffer(), StandardCharsets.US_ASCII);
			if(output.endsWith("\n") && (output.contains("Pass") || output.contains("Fail")))
				break;
			try{Thread.sleep(100);}catch(InterruptedException ignore){}
		}
		runner.stop();
		try{thread.join();}catch(InterruptedException ignore){}
		return new String(runner.getSerialBuffer(), StandardCharsets.US_ASCII);
	}

	public static byte[] runMooneyeTestRomAndGetSerialOutput(String romPath){
		HeadlessEmulatorRunner runner=new HeadlessEmulatorRunner(new File(romPath));
		runner.prepare();
		Thread thread=new Thread(runner::run);
		thread.start();
		while(runner.getSerialBufferSize()<6){
			try{Thread.sleep(100);}catch(InterruptedException ignore){}
		}
		runner.stop();
		try{thread.join();}catch(InterruptedException ignore){}
		return runner.getSerialBuffer();
	}
}
