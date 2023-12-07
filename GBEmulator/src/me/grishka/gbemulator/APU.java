package me.grishka.gbemulator;

import java.io.ByteArrayOutputStream;
import java.io.DataOutputStream;
import java.util.Arrays;
import java.util.LinkedList;
import java.util.concurrent.LinkedBlockingQueue;

import javax.sound.sampled.AudioFormat;
import javax.sound.sampled.AudioSystem;
import javax.sound.sampled.LineUnavailableException;
import javax.sound.sampled.SourceDataLine;

public class APU extends MemoryMappedDevice{
	private static final int[][] squareWaveLUT={
			{1, 1, 1, 1, 1, 1, 1, -1}, // 12.5%
			{-1, 1, 1, 1, 1, 1, 1, -1}, // 25%
			{-1, 1, 1, 1, 1, -1, -1, -1}, // 50%,
			{1, -1, -1, -1, -1, -1, -1, 1}
	};

	private SourceDataLine line;
	private boolean audioEnabled;
	private boolean[] channelEnabled=new boolean[4];
	private ChannelPan[] channelPan=new ChannelPan[]{ChannelPan.CENTER, ChannelPan.CENTER, ChannelPan.CENTER, ChannelPan.CENTER};
	private int leftVolume, rightVolume;

	private int ch1LengthTimer, ch1DutyCycle, ch1WaveLength, ch1Volume, ch1InitVolume, ch1EnvelopeSweep, ch1EnvelopeTicksRemain, ch1SweepPace, ch1SweepSlope, ch1SweepRemain;
	private boolean ch1LengthTimerEnabled, ch1EnvelopeIncrease, ch1FreqDecrease;

	private int ch2LengthTimer, ch2DutyCycle, ch2WaveLength, ch2Volume, ch2InitVolume, ch2EnvelopeSweep, ch2EnvelopeTicksRemain;
	private boolean ch2LengthTimerEnabled, ch2EnvelopeIncrease;

	private int ch3LengthTimer, ch3Volume, ch3WaveLength;
	private boolean ch3DacEnabled, ch3LengthTimerEnabled;
	private byte[] waveRam=new byte[16];

	private int ch4LengthTimer, ch4Volume, ch4ClockShift, ch4ClockDivider, ch4EnvelopeSweep, ch4EnvelopeTicksRemain, ch4InitVolume;
	private boolean ch4LengthTimerEnabled, ch4EnvelopeIncrease, ch4Use7BitLFSR, ch4Retrigger;

	private int ticksToEnvelopeTick=4;
	private boolean freqTick;

	private LinkedList<AudioStateSnapshot> statePool=new LinkedList<>();
	private final LinkedBlockingQueue<AudioStateSnapshot> audioQueue=new LinkedBlockingQueue<>();
	private AudioThread audioThread;

	public APU(boolean actuallyOutputAudio){
		if(actuallyOutputAudio){
			try{
				AudioFormat format=new AudioFormat(32_000, 16, 2, true, true);
				line=AudioSystem.getSourceDataLine(format);
				line.open(format, 320*8);
				line.start();
				audioThread=new AudioThread(line, audioQueue, statePool);
				audioThread.start();
			}catch(LineUnavailableException x){
				throw new RuntimeException(x);
			}
		}
	}

	@Override
	public int read(int address){
		if(address>=0xFF30 && address<=0xFF3F){
			return (int)waveRam[address-0xFF30] & 0xff;
		}
//		System.out.println("APU reg read "+Integer.toHexString(offset+0xFF10));
		return switch(address){
			case 0xFF12 -> (ch1InitVolume << 4) | (ch1EnvelopeIncrease ? 8 : 0) | ch1EnvelopeSweep & 7;
			case 0xFF17 -> (ch2InitVolume << 4) | (ch2EnvelopeIncrease ? 8 : 0) | ch2EnvelopeSweep & 7;

			case 0xFF1A -> ch3DacEnabled ? 128 : 0;
			case 0xFF1C -> ch3Volume << 5;
			case 0xFF21 -> (ch4InitVolume << 4) | (ch4EnvelopeIncrease ? 8 : 0) | ch4EnvelopeSweep;

			case 0xFF24 -> rightVolume | (leftVolume << 4);
			case 0xFF25 -> {
				int r=0;
				for(int i=0;i<4;i++){
					r|=switch(channelPan[i]){
						case OFF -> 0x00;
						case CENTER -> 0x11;
						case RIGHT -> 0x01;
						case LEFT -> 0x10;
					} << i;
				}
				yield r;
			}
			case 0xFF26 -> {
				int res=0;
				if(audioEnabled) res|=128;
				for(int i=0;i<4;i++){
					if(channelEnabled[i])
						res|=(1 << i);
				}
				yield res;
			}
			default -> {
				System.out.println("Unimplemented: APU reg read "+Integer.toHexString(address));
				yield 0;
			}
		};
	}

	@Override
	public void write(int address, int value){
		if(address>=0xFF30 && address<=0xFF3F){
			waveRam[address-0xFF30]=(byte) value;
			return;
		}
		switch(address){
			// ch1
			case 0xFF10 -> {
				ch1SweepPace=(value >> 4) & 7;
				ch1FreqDecrease=(value & 8)!=0;
				ch1SweepSlope=value & 7;
			}
			case 0xFF11 -> {
				ch1LengthTimer=value & 63;
				ch1DutyCycle=value >> 6;
			}
			case 0xFF12 -> {
				ch1InitVolume=ch1Volume=value >> 4;
				ch1EnvelopeIncrease=(value & 8)!=0;
				ch1EnvelopeSweep=value & 7;
			}
			case 0xFF13 -> ch1WaveLength=(ch1WaveLength & 0xF00) | value;
			case 0xFF14 -> {
				ch1WaveLength=(ch1WaveLength & 0xFF) | ((value & 7) << 8);
				ch1LengthTimerEnabled=(value & 64)!=0;
				if((value & 128)!=0){
					channelEnabled[0]=true;
					ch1EnvelopeTicksRemain=ch1EnvelopeSweep;
//					ch1Volume=ch1InitVolume;
				}
			}

			// ch2
			case 0xFF16 -> {
				ch2LengthTimer=value & 63;
				ch2DutyCycle=value >> 6;
			}
			case 0xFF17 -> {
				ch2InitVolume=ch2Volume=value >> 4;
				ch2EnvelopeIncrease=(value & 8)!=0;
				ch2EnvelopeSweep=value & 7;
			}
			case 0xFF18 -> ch2WaveLength=(ch2WaveLength & 0xF00) | value;
			case 0xFF19 -> {
				ch2WaveLength=(ch2WaveLength & 0xFF) | ((value & 7) << 8);
				ch2LengthTimerEnabled=(value & 64)!=0;
				if((value & 128)!=0){
					channelEnabled[1]=true;
					ch2EnvelopeTicksRemain=ch2EnvelopeSweep;
//					ch2Volume=ch2InitVolume;
				}
			}

			// ch3
			case 0xFF1A -> ch3DacEnabled=(value & 128)!=0;
			case 0xFF1B -> ch3LengthTimer=value;
			case 0xFF1C -> ch3Volume=(value >> 5) & 3;
			case 0xFF1D -> ch3WaveLength=(ch3WaveLength & 0xF00) | value;
			case 0xFF1E -> {
				ch3WaveLength=(ch3WaveLength & 0xFF) | ((value & 7) << 8);
				ch3LengthTimerEnabled=(value & 64)!=0;
				if((value & 128)!=0){
					channelEnabled[2]=true;
				}
			}

			// ch4
			case 0xFF20 -> ch4LengthTimer=value & 31;
			case 0xFF21 -> {
				ch4InitVolume=ch4Volume=value >> 4;
				ch4EnvelopeIncrease=(value & 8)!=0;
				ch4EnvelopeSweep=value & 7;
			}
			case 0xFF22 -> {
				ch4ClockShift=value >> 4;
				ch4Use7BitLFSR=(value & 8)!=0;
				ch4ClockDivider=value & 7;
			}
			case 0xFF23 -> {
				ch4LengthTimerEnabled=(value & 64)!=0;
				if((value & 128)!=0){
//					System.out.println("ch4 trigger");
					channelEnabled[3]=true;
					ch4EnvelopeTicksRemain=ch4EnvelopeSweep;
					ch4Retrigger=true;
				}
			}

			case 0xFF24 -> {
				rightVolume=value & 7;
				leftVolume=(value >> 4) & 7;
//				System.out.println("Set output volume to "+leftVolume+", "+rightVolume);
			}
			case 0xFF25 -> {
				for(int i=0;i<4;i++){
					channelPan[i]=switch((value >> i) & 0x11){
						case 0x11 -> ChannelPan.CENTER;
						case 0x01 -> ChannelPan.RIGHT;
						case 0x10 -> ChannelPan.LEFT;
						case 0x00 -> ChannelPan.OFF;
						default -> throw new IllegalStateException();
					};
				}
//				System.out.println("Set channel pan to "+Arrays.toString(channelPan));
			}
			case 0xFF26 -> {
				audioEnabled=(value & 128)!=0;
			}
			default -> System.out.println("Unimplemented: APU reg write "+Integer.toHexString(address)+" value "+Integer.toHexString(value));
		}
	}

	public void doClockCycle(){
		if(audioQueue.size()>100)
			return;
		boolean envelopesTick;
		if(--ticksToEnvelopeTick==0){
			ticksToEnvelopeTick=4;
			envelopesTick=true;
		}else{
			envelopesTick=false;
		}
		if(channelEnabled[0]){
			if(ch1WaveLength==0x7FF){
				channelEnabled[0]=false;
			}
			if(ch1LengthTimerEnabled && ch1LengthTimer<64){
				ch1LengthTimer++;
				if(ch1LengthTimer==64){
					channelEnabled[0]=false;
				}
			}
			if(envelopesTick && ch1EnvelopeSweep>0 && ch1EnvelopeTicksRemain>0){
				ch1EnvelopeTicksRemain--;
				if(ch1EnvelopeTicksRemain==0){
					ch1EnvelopeTicksRemain=ch1EnvelopeSweep;
					if(ch1EnvelopeIncrease){
						ch1Volume=Math.min(15, ch1Volume+1);
					}else{
						ch1Volume=Math.max(0, ch1Volume-1);
					}
				}
			}
			if(freqTick){
				ch1SweepRemain--;
				if(ch1SweepRemain<=0 && ch1SweepSlope>0){
					ch1SweepRemain=ch1SweepPace;
					int wavelengthChange=ch1WaveLength/(1 << ch1SweepSlope);
					int newWavelength=ch1FreqDecrease ? (ch1WaveLength-wavelengthChange) : (ch1WaveLength+wavelengthChange);
					if((newWavelength<0 && ch1SweepSlope>0) || newWavelength>0x7FF){
						channelEnabled[0]=false;
					}else{
						ch1WaveLength=newWavelength;
					}
				}
			}
			freqTick=!freqTick;
		}
		if(channelEnabled[1]){
			if(ch2WaveLength==0x7FF){
				channelEnabled[1]=false;
			}
			if(ch2LengthTimerEnabled && ch2LengthTimer<64){
				ch2LengthTimer++;
				if(ch2LengthTimer==64){
					channelEnabled[1]=false;
				}
			}
			if(envelopesTick && ch2EnvelopeSweep>0 && ch2EnvelopeTicksRemain>0){
				ch2EnvelopeTicksRemain--;
				if(ch2EnvelopeTicksRemain==0){
					ch2EnvelopeTicksRemain=ch2EnvelopeSweep;
					if(ch2EnvelopeIncrease){
						ch2Volume=Math.min(15, ch2Volume+1);
					}else{
						ch2Volume=Math.max(0, ch2Volume-1);
					}
				}
			}
		}
		if(channelEnabled[2]){
			if(ch3WaveLength==0x7FF){
				channelEnabled[2]=false;
			}
			if(ch3LengthTimerEnabled && ch3LengthTimer<256){
				ch3LengthTimer++;
				if(ch3LengthTimer==256){
					channelEnabled[2]=false;
				}
			}
		}
		if(channelEnabled[3]){
			if(envelopesTick && ch4EnvelopeSweep>0 && ch4EnvelopeTicksRemain>0){
				ch4EnvelopeTicksRemain--;
				if(ch4EnvelopeTicksRemain==0){
					ch4EnvelopeTicksRemain=ch4EnvelopeSweep;
					if(ch4EnvelopeIncrease){
						ch4Volume=Math.min(15, ch4Volume+1);
					}else{
						ch4Volume=Math.max(0, ch4Volume-1);
					}
				}
			}
			if(ch4LengthTimerEnabled && ch4LengthTimer<32){
				ch4LengthTimer++;
				if(ch4LengthTimer==32){
					channelEnabled[3]=false;
				}
			}
		}

		if(audioThread!=null){
			AudioStateSnapshot s;
			synchronized(audioQueue){
				s=statePool.isEmpty() ? new AudioStateSnapshot() : statePool.pop();
			}
			s.audioEnabled=audioEnabled;
			System.arraycopy(channelEnabled, 0, s.channelEnabled, 0, 4);
			System.arraycopy(channelPan, 0, s.channelPan, 0, 4);
			s.ch1WaveLength=ch1WaveLength;
			s.ch2WaveLength=ch2WaveLength;
			s.ch3WaveLength=ch3WaveLength;
			s.ch1Volume=ch1Volume;
			s.ch2Volume=ch2Volume;
			s.ch3Volume=ch3Volume;
			s.ch4Volume=ch4Volume;
			s.ch1DutyCycle=ch1DutyCycle;
			s.ch2DutyCycle=ch2DutyCycle;
			s.leftVolume=leftVolume;
			s.rightVolume=rightVolume;
			System.arraycopy(waveRam, 0, s.waveRam, 0, 16);
			s.ch3DacEnabled=ch3DacEnabled;
			s.ch4Use7BitLFSR=ch4Use7BitLFSR;
			s.ch4ClockShift=ch4ClockShift;
			s.ch4ClockDivider=ch4ClockDivider;
			s.ch4Retrigger=ch4Retrigger;
			audioQueue.offer(s);
		}

		ch4Retrigger=false;
	}

	public void release(){
		if(audioThread!=null)
			audioThread.close();
	}

	private static class AudioThread extends Thread{
		private final SourceDataLine line;
		private final LinkedBlockingQueue<AudioStateSnapshot> queue;
		private final LinkedList<AudioStateSnapshot> pool;
		private boolean keepRunning=true;

		private AudioThread(SourceDataLine line, LinkedBlockingQueue<AudioStateSnapshot> queue, LinkedList<AudioStateSnapshot> pool){
			this.line=line;
			this.queue=queue;
			this.pool=pool;
		}

		public void close(){
			try{
				keepRunning=false;
				join();
			}catch(InterruptedException ignore){}
		}

		@Override
		public void run(){
//			System.out.println("AudioThread started. Line active "+line.isActive());
			try{
				ByteArrayOutputStream buf=new ByteArrayOutputStream();
				DataOutputStream out=new DataOutputStream(buf);
				int ch1Offset=0;
				int ch2Offset=0;
				int ch3Offset=0;
				int ch4Offset=0;
				int ch4lfsr=0;
				byte[] b=new byte[1024];
				while(line.available()>b.length){
					line.write(b, 0, b.length);
				}

				AudioStateSnapshot s=queue.take();
				while(keepRunning){
					int ch1PeriodInSamples=0;
					int ch2PeriodInSamples=0;
					int ch3PeriodInSamples=0;
					int ch4PeriodInSamples=0;

					for(int i=0;i<500;i++){ // 1/64th of a second at 32khz
						if(i==0 || i==125 || i==250 || i==375){
							AudioStateSnapshot _s=queue.poll();
							if(_s!=null && s!=null){
								synchronized(queue){
									pool.push(s);
								}
							}
							if(_s!=null)
								s=_s;
							// All these periods are scaled by 100 for improved precision
							ch1PeriodInSamples=32000_00/(131072/(2048-s.ch1WaveLength));
							ch1Offset%=Math.max(1, ch1PeriodInSamples);
							ch2PeriodInSamples=32000_00/(131072/(2048-s.ch2WaveLength));
							ch2Offset%=Math.max(1, ch2PeriodInSamples);
							ch3PeriodInSamples=32000_00/(65536/(2048-s.ch3WaveLength));
							ch3Offset%=Math.max(1, ch3PeriodInSamples);
							int ch4div=1 << s.ch4ClockShift;
							if(s.ch4ClockDivider>0)
								ch4div*=s.ch4ClockDivider;
							ch4div=262144/ch4div;
							if(s.ch4ClockDivider==0)
								ch4div>>=1;
							ch4PeriodInSamples=32000_00/ch4div;
//							System.out.println("ch4 period="+ch4PeriodInSamples+" use7 "+s.ch4Use7BitLFSR);
							ch4Offset%=ch4PeriodInSamples;
							if(s.ch4Retrigger)
								ch4lfsr=0;
						}

						float sampleR=0, sampleL=0;
						if(s.audioEnabled){
							if(s.channelEnabled[0] && s.channelPan[0]!=ChannelPan.OFF && ch1PeriodInSamples>0){
								int q=(int)(ch1Offset/(float)ch1PeriodInSamples*8);
								float sample=squareWaveLUT[s.ch1DutyCycle][q]*(s.ch1Volume/15f);
								if(s.channelPan[0]==ChannelPan.CENTER || s.channelPan[0]==ChannelPan.LEFT)
									sampleL+=sample;
								if(s.channelPan[0]==ChannelPan.CENTER || s.channelPan[0]==ChannelPan.RIGHT)
									sampleR+=sample;
								ch1Offset=(ch1Offset+100)%(ch1PeriodInSamples);
							}
							if(s.channelEnabled[1] && s.channelPan[1]!=ChannelPan.OFF && ch2PeriodInSamples>0){
								int q=(int)(ch2Offset/(float)ch2PeriodInSamples*8);
								float sample=squareWaveLUT[s.ch2DutyCycle][q]*(s.ch2Volume/15f);
								if(s.channelPan[1]==ChannelPan.CENTER || s.channelPan[1]==ChannelPan.LEFT)
									sampleL+=sample;
								if(s.channelPan[1]==ChannelPan.CENTER || s.channelPan[1]==ChannelPan.RIGHT)
									sampleR+=sample;
								ch2Offset=(ch2Offset+100)%ch2PeriodInSamples;
							}
							if(s.channelEnabled[2] && s.ch3DacEnabled && s.channelPan[2]!=ChannelPan.OFF && ch3PeriodInSamples>0){
								int q=(int)(ch3Offset/(float)ch3PeriodInSamples*32);
								int _sample=s.waveRam[q>>1];
								if((q & 1)==1){
									_sample&=0x0F;
								}else{
									_sample=(_sample >> 4) & 0x0F;
								}
//								System.out.println("sample="+_sample+" volume="+s.ch3Volume+" length="+s.ch3WaveLength);
								float sample=(_sample >> switch(s.ch3Volume){
									case 0 -> 4;
									case 1 -> 0;
									case 2 -> 1;
									case 3 -> 2;
									default -> throw new IllegalArgumentException();
								})/7.5f-1f;
								if(s.channelPan[2]==ChannelPan.CENTER || s.channelPan[2]==ChannelPan.LEFT)
									sampleL+=sample;
								if(s.channelPan[2]==ChannelPan.CENTER || s.channelPan[2]==ChannelPan.RIGHT)
									sampleR+=sample;
								ch3Offset=(ch3Offset+100)%ch3PeriodInSamples;
							}
							if(s.channelEnabled[3] && s.channelPan[3]!=ChannelPan.OFF && ch4PeriodInSamples>0){
//								System.out.println("ch4offset="+ch4Offset);
								if(ch4Offset<100){
									int bit=~(ch4lfsr ^ (ch4lfsr >> 1)) & 1;
									ch4lfsr=(ch4lfsr & 0x7FFF) | (bit << 15);
									if(s.ch4Use7BitLFSR){
										ch4lfsr=(ch4lfsr & 0xFF7F) | (bit << 7);
									}
									ch4lfsr>>=1;
								}
								if((ch4lfsr & 1)!=0){
									float sample=s.ch4Volume/15f;
									if(s.channelPan[3]==ChannelPan.CENTER || s.channelPan[3]==ChannelPan.LEFT)
										sampleL+=sample;
									if(s.channelPan[3]==ChannelPan.CENTER || s.channelPan[3]==ChannelPan.RIGHT)
										sampleR+=sample;
								}
								ch4Offset=(ch4Offset+100)%ch4PeriodInSamples;
							}
						}
						sampleL=Math.max(-1f, Math.min(1f, sampleL/3f)); // division by 3 avoids clipping when multiple channels' peaks end up close to each other
						sampleR=Math.max(-1f, Math.min(1f, sampleR/3f));
						out.writeShort(Math.round(sampleL*(s.leftVolume/7f)*Short.MAX_VALUE)); // left
						out.writeShort(Math.round(sampleR*(s.rightVolume/7f)*Short.MAX_VALUE)); // right
					}


					line.write(buf.toByteArray(), 0, buf.size());
					buf.reset();
				}
				line.close();
			}catch(Exception x){
				x.printStackTrace();
			}
		}
	}

	private enum ChannelPan{
		OFF,
		RIGHT,
		LEFT,
		CENTER
	}

	private static class AudioStateSnapshot{
		public boolean audioEnabled;
		public boolean[] channelEnabled=new boolean[4];
		public ChannelPan[] channelPan=new ChannelPan[4];
		public int ch1WaveLength, ch2WaveLength, ch3WaveLength;
		public int ch1DutyCycle, ch2DutyCycle;
		public int ch1Volume, ch2Volume, ch3Volume, ch4Volume;
		public int leftVolume, rightVolume;
		public byte[] waveRam=new byte[16];
		public boolean ch3DacEnabled;
		public boolean ch4Use7BitLFSR, ch4Retrigger;
		public int ch4ClockShift, ch4ClockDivider;
	}
}
