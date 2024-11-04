package me.grishka.askfmdl;

import javax.swing.SwingUtilities;

public class AskFmDownloaderApp{
	public static void main(String[] args){
		SwingUtilities.invokeLater(()->new MainForm().setVisible(true));
	}
}
