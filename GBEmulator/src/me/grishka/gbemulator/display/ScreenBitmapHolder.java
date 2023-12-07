package me.grishka.gbemulator.display;

public abstract class ScreenBitmapHolder{
	protected final int width;
	protected final int height;

	public ScreenBitmapHolder(int width, int height){
		this.width=width;
		this.height=height;
	}

	public abstract void setPixel(int x, int y, int color);
	public abstract void fill(int color);
	public abstract void swapBuffers();
}
