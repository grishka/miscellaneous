package me.grishka.gbemulator.display;

import me.grishka.gbemulator.ScreenView;

public class GUIScreenBitmapHolder extends BufferedImageScreenBitmapHolder{
	private final ScreenView screenView;

	public GUIScreenBitmapHolder(int width, int height, ScreenView screenView){
		super(width, height);
		this.screenView=screenView;
	}

	@Override
	public void swapBuffers(){
		super.swapBuffers();
		screenView.update(displayedBuffer);
	}
}
