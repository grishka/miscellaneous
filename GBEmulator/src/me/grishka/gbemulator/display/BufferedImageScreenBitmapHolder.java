package me.grishka.gbemulator.display;

import java.awt.image.BufferedImage;

public class BufferedImageScreenBitmapHolder extends ScreenBitmapHolder{
	protected BufferedImage currentBuffer, displayedBuffer;

	public BufferedImageScreenBitmapHolder(int width, int height){
		super(width, height);
		currentBuffer=new BufferedImage(width, height, BufferedImage.TYPE_INT_ARGB);
		displayedBuffer=new BufferedImage(width, height, BufferedImage.TYPE_INT_ARGB);
	}

	@Override
	public void setPixel(int x, int y, int color){
		currentBuffer.setRGB(x, y, color);
	}

	@Override
	public void fill(int color){
		for(int y=0;y<height;y++){
			for(int x=0;x<width;x++){
				currentBuffer.setRGB(x, y, color);
			}
		}
	}

	@Override
	public void swapBuffers(){
		BufferedImage tmp=displayedBuffer;
		displayedBuffer=currentBuffer;
		currentBuffer=tmp;
	}

	public BufferedImage getDisplayedBuffer(){
		return displayedBuffer;
	}
}
