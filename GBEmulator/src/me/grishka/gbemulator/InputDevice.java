package me.grishka.gbemulator;

import java.util.EnumSet;

public class InputDevice extends MemoryMappedDevice{
	private EnumSet<Button> heldButtons=EnumSet.noneOf(Button.class);
	private boolean selectAction, selectDirection;
	private final InterruptFlagsDevice interruptFlags;

	public InputDevice(InterruptFlagsDevice interruptFlags){
		this.interruptFlags=interruptFlags;
	}

	@Override
	public int read(int address){
		int res=0;
		if(selectAction){
			res|=32;
			if(heldButtons.contains(Button.A))
				res|=1;
			if(heldButtons.contains(Button.B))
				res|=2;
			if(heldButtons.contains(Button.SELECT))
				res|=4;
			if(heldButtons.contains(Button.START))
				res|=8;
		}

		if(selectDirection){
			res|=16;
			if(heldButtons.contains(Button.RIGHT))
				res|=1;
			if(heldButtons.contains(Button.LEFT))
				res|=2;
			if(heldButtons.contains(Button.UP))
				res|=4;
			if(heldButtons.contains(Button.DOWN))
				res|=8;
		}

		res=(~res) & 0xCF;
		return res;
	}

	@Override
	public void write(int address, int value){
		selectAction=(value & 32)==0;
		selectDirection=(value & 16)==0;
	}

	public void buttonPressed(Button button){
		heldButtons.add(button);
		if((selectAction && button.isAction()) || (selectDirection && button.isDirection())){
			interruptFlags.set(InterruptFlagsDevice.FLAG_JOYPAD);
		}
	}

	public void buttonReleased(Button button){
		heldButtons.remove(button);
	}

	public enum Button{
		A,
		B,
		START,
		SELECT,
		LEFT,
		RIGHT,
		UP,
		DOWN;

		public boolean isAction(){
			return this==A || this==B || this==START || this==SELECT;
		}

		public boolean isDirection(){
			return this==LEFT || this==RIGHT || this==UP || this==DOWN;
		}
	}
}
