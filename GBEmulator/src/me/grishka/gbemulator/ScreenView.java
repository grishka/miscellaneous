package me.grishka.gbemulator;

import java.awt.Dimension;
import java.awt.Graphics;
import java.awt.image.BufferedImage;

import javax.swing.JComponent;

public class ScreenView extends JComponent{
	private BufferedImage currentImage;

	@Override
	public void paint(Graphics g){
		if(currentImage!=null){
			g.drawImage(currentImage, 0, 0, 160*2, 144*2, null);
		}
	}

	@Override
	public Dimension getPreferredSize(){
		return new Dimension(160*2, 144*2);
	}

	public void update(BufferedImage img){
		currentImage=img;
		repaint();
	}
}
