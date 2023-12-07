package me.grishka.gbemulator;

import java.awt.event.KeyEvent;
import java.awt.event.KeyListener;
import java.io.File;

import javax.swing.JFileChooser;
import javax.swing.JFrame;
import javax.swing.filechooser.FileNameExtensionFilter;

import me.grishka.gbemulator.display.GUIScreenBitmapHolder;
import me.grishka.gbemulator.display.ScreenBitmapHolder;
import me.grishka.gbemulator.runner.GUIEmulatorRunner;

public class GBEmulatorApp{
	private static JFrame window;
	private static ScreenBitmapHolder screen;
	private static Thread emulatorThread;
	private static File currentRomFile;
	private static final boolean isMac=System.getProperty("os.name").equals("Mac OS X");
	private static GUIEmulatorRunner runner;

	public static void main(String[] args) throws Exception{
		window=new JFrame("GBEmulator");
		ScreenView screenView=new ScreenView();
		screen=new GUIScreenBitmapHolder(PPU.SCREEN_W, PPU.SCREEN_H, screenView);
		window.getContentPane().add(screenView);
		window.pack();
		window.setLocationRelativeTo(null);
		window.addKeyListener(new KeyListener(){
			@Override
			public void keyTyped(KeyEvent e){

			}

			@Override
			public void keyPressed(KeyEvent e){
				if(runner!=null){
					InputDevice.Button btn=mapKey(e);
					if(btn!=null)
						runner.buttonPressed(btn);
				}
				boolean isCtrl=isMac ? e.isMetaDown() : e.isControlDown();
				if(isCtrl && e.getKeyCode()==KeyEvent.VK_O){
					chooseFileAndStart();
				}
				if(isCtrl && e.getKeyCode()==KeyEvent.VK_R && runner!=null){
					stop();
					start();
				}
			}

			@Override
			public void keyReleased(KeyEvent e){
				if(runner!=null){
					InputDevice.Button btn=mapKey(e);
					if(btn!=null)
						runner.buttonReleased(btn);
				}
			}
		});
		window.setVisible(true);
		chooseFileAndStart();
	}

	private static InputDevice.Button mapKey(KeyEvent ev){
		return switch(ev.getKeyCode()){
			case KeyEvent.VK_A -> InputDevice.Button.A;
			case KeyEvent.VK_S -> InputDevice.Button.B;
			case KeyEvent.VK_ENTER -> InputDevice.Button.START;
			case KeyEvent.VK_BACK_SPACE -> InputDevice.Button.SELECT;

			case KeyEvent.VK_UP -> InputDevice.Button.UP;
			case KeyEvent.VK_DOWN -> InputDevice.Button.DOWN;
			case KeyEvent.VK_LEFT -> InputDevice.Button.LEFT;
			case KeyEvent.VK_RIGHT -> InputDevice.Button.RIGHT;

			default -> null;
		};
	}

	private static void chooseFileAndStart(){
		stop();
		JFileChooser chooser=new JFileChooser(new File("ROMs"));
		chooser.setDialogTitle("Open ROM");
		chooser.setFileFilter(new FileNameExtensionFilter("GameBoy ROM file", "gb", "gbc"));
		if(chooser.showOpenDialog(window)!=JFileChooser.APPROVE_OPTION)
			return;
		currentRomFile=chooser.getSelectedFile();
		start();
	}

	private static void stop(){
		if(emulatorThread!=null){
			runner.stop();
			try{emulatorThread.join();}catch(Exception ignore){}
			emulatorThread=null;
		}
	}

	private static void start(){
		runner=new GUIEmulatorRunner(currentRomFile, screen, window);
		runner.prepare();
		emulatorThread=new Thread(()->runner.run());
		emulatorThread.start();
	}
}
