package me.grishka.gbemulator;

import java.awt.image.BufferedImage;
import java.util.Arrays;

import me.grishka.gbemulator.display.ScreenBitmapHolder;

public class PPU extends MemoryMappedDevice{
	private static final int LCDC_BG_ENABLE=1;
	private static final int LCDC_OBJ_ENABLE=(1 << 1);
	private static final int LCDC_OBJ_SIZE=(1 << 2);
	private static final int LCDC_BG_TILE_MAP_SELECT=(1 << 3);
	private static final int LCDC_BG_WIN_TILE_DATA=(1 << 4);
	private static final int LCDC_WINDOW_ENABLE=(1 << 5);
	private static final int LCDC_WIN_TILE_MAP=(1 << 6);
	private static final int LCDC_LCD_ENABLE=(1 << 7);

	private static final int STAT_MODE_HBLANK=0;
	private static final int STAT_MODE_VBLANK=1;
	private static final int STAT_MODE_OAM_RAM=2;
	private static final int STAT_MODE_TRANSFER=3;
	private static final int STAT_MODE_MASK=3;
	private static final int STAT_COINCIDENCE=(1 << 2);
	private static final int STAT_HBLANK_INTERRUPT=(1 << 3);
	private static final int STAT_VBLANK_INTERRUPT=(1 << 4);
	private static final int STAT_OAM_INTERRUPT=(1 << 5);
	private static final int STAT_COINCIDENCE_INTERRUPT=(1 << 6);

	private static final int OBJ_PALETTE_NUM=(1 << 4);
	private static final int OBJ_X_FLIP=(1 << 5);
	private static final int OBJ_Y_FLIP=(1 << 6);
	private static final int OBJ_BELOW_BG_WIN=(1 << 7);

	public static final int SCREEN_W=160;
	public static final int SCREEN_H=144;

	private int lcdc;
	private int status=STAT_MODE_OAM_RAM;
	private int scrollX, scrollY;
	private int windowX, windowY;
	private int currentLineY, currentPixelX;
	private int windowLineCounter;
	private int interruptLineY=0xFF;
//	private int clockCyclesToNextLine=456;
	private int clockCyclesToNextMode=80;
	private int currentLinePenaltiesTaken=0;
	private int stallCounter=0;
	private int bgWinPalette, objPalette0, objPalette1;

	private final MemoryBus bus;
	private final byte[] vram;
	private final byte[] oam;
	private final GatedRAM vramDevice, oamDevice;
	private int[] spritesForCurrentLine=new int[10], spriteXs=new int[10];

	private int oamDmaCyclesLeft=0;
	private int oamDmaSourceAddress;

	private ScreenBitmapHolder screenBitmapHolder;
	private final InterruptFlagsDevice interruptFlags;
	private boolean wasJustDisabled;

	private static final int[] palette={0xFFFFFFFF, 0xFFAAAAAA, 0xFF555555, 0xFF000000};

	public PPU(MemoryBus bus, GatedRAM vram, GatedRAM oam, ScreenBitmapHolder screenBitmapHolder, InterruptFlagsDevice interruptFlags){
		this.bus=bus;
		this.vram=vram.getRawBytes();
		this.oam=oam.getRawBytes();
		this.screenBitmapHolder=screenBitmapHolder;
		this.interruptFlags=interruptFlags;
		this.vramDevice=vram;
		this.oamDevice=oam;
	}

	@Override
	public int read(int address){
//		System.out.printf("PPU read addr 0x%04X\n", address);
		return switch(address){
			case 0xFF40 -> lcdc;
			case 0xFF41 -> status;
			case 0xFF42 -> scrollY;
			case 0xFF43 -> scrollX;
			case 0xFF44 -> currentLineY;
			case 0xFF45 -> interruptLineY;
			case 0xFF47 -> bgWinPalette;
			case 0xFF48 -> objPalette0;
			case 0xFF49 -> objPalette1;
			case 0xFF4A -> windowY;
			case 0xFF4B -> windowX;
			default -> {
				System.out.println("Unimplemented: PPU reg read "+Integer.toHexString(address));
				yield 0;
			}
		};
	}

	@Override
	public void write(int address, int value){
//		System.out.printf("PPU write addr 0x%04X value 0x%02X, mode %d line %d\n", address, value, (status & STAT_MODE_MASK), currentLineY);
		switch(address){
			case 0xFF40 -> {
				if((lcdc & LCDC_LCD_ENABLE)!=(value & LCDC_LCD_ENABLE)){
					if((value & LCDC_LCD_ENABLE)==0){
						wasJustDisabled=true;
						currentLineY=currentPixelX=0;
						clockCyclesToNextMode=80;
//						status=(status & ~STAT_MODE_MASK) | STAT_MODE_OAM_RAM;
						status&=~STAT_MODE_MASK;
						// Mode bits are 0 while LCD is off
					}else{
						status|=STAT_MODE_OAM_RAM;
						oamDevice.setAccessible(false);
					}
				}
				lcdc=value;
			}
			case 0xFF41 -> status=(status & 7) | (value & 0xF8);
			case 0xFF42 -> scrollY=value;
			case 0xFF43 -> scrollX=value;
			case 0xFF45 -> {
				interruptLineY=value;
//				System.out.println("Set coincidence Y "+interruptLineY+", current Y "+currentLineY+", "+(status & STAT_COINCIDENCE));
			}
			case 0xFF46 -> {
				if(oamDmaCyclesLeft>0)
					throw new IllegalStateException("OAM DMA transfer already in progress");
				oamDmaCyclesLeft=640;
				oamDmaSourceAddress=value << 8;
			}
			case 0xFF47 -> bgWinPalette=value;
			case 0xFF48 -> objPalette0=value;
			case 0xFF49 -> objPalette1=value;
			case 0xFF4A -> windowY=value;
			case 0xFF4B -> windowX=value;
			default -> System.out.println("Unimplemented: PPU reg write "+Integer.toHexString(address+0xFF40)+" value "+Integer.toHexString(value));
		}
	}

	public void doClockCycle(){
		if(oamDmaCyclesLeft>0){
			if((oamDmaCyclesLeft & 3)==0){
				int offset=160-(oamDmaCyclesLeft >> 2);
				oam[offset]=(byte) bus.read(oamDmaSourceAddress+offset);
			}
			oamDmaCyclesLeft--;
		}
		if((lcdc & LCDC_LCD_ENABLE)==0){
			if(wasJustDisabled){
				wasJustDisabled=false;
				screenBitmapHolder.fill(0xFFFFFFFF);
				screenBitmapHolder.swapBuffers();
			}
			return;
		}

		if(stallCounter>0){
			stallCounter--;
			return;
		}

		int mode=status & STAT_MODE_MASK;
		clockCyclesToNextMode--;
		if(mode==STAT_MODE_OAM_RAM){
			if(clockCyclesToNextMode==0){
				Arrays.fill(spritesForCurrentLine, -1);
				if(oamDmaCyclesLeft==0 && (lcdc & LCDC_OBJ_ENABLE)!=0){
					boolean doubleHeightSprites=(lcdc & LCDC_OBJ_SIZE)!=0;
					int index=0;
					for(int i=0;i<40;i++){
						int y=((int)oam[i*4] & 0xFF)-16;
						if(y<=currentLineY && y+(doubleHeightSprites ? 16 : 8)>currentLineY){
							int spriteX=((int) oam[index*4+1] & 0xFF);
							spriteXs[index]=spriteX;
							spritesForCurrentLine[index++]=i;
							if(index==10)
								break;
						}
					}
				}

				clockCyclesToNextMode=172;
				status&=~STAT_MODE_MASK;
				status|=STAT_MODE_TRANSFER;
				stallCounter=scrollX%8;
				currentLinePenaltiesTaken+=stallCounter;
				vramDevice.setAccessible(false);
			}
		}else if(mode==STAT_MODE_TRANSFER){
			// WIN and BG
			if(currentPixelX<SCREEN_W){
				boolean bgWinNonTransparent=false;
				if((lcdc & LCDC_WINDOW_ENABLE)!=0 && currentLineY>=windowY && currentPixelX>=windowX-7 && windowLineCounter<SCREEN_H){
					int realX=currentPixelX-windowX+7;
//					int realY=currentLineY-windowY;
					int realY=Math.max(0, windowLineCounter-1);

					int offset=(lcdc & LCDC_WIN_TILE_MAP)==0 ? 0x1800 : 0x1C00;
					int tileIndex=realY/8*32+realX/8;
					tileIndex=(int) vram[offset+tileIndex] & 0xFF;
					int tileDataOffset;
					if((lcdc & LCDC_BG_WIN_TILE_DATA)==0){
						tileIndex=(byte) tileIndex;
						tileDataOffset=tileIndex*16+0x1000;
					}else{
						tileDataOffset=tileIndex*16;
					}
					int xInTile=7-realX%8;
					int yInTile=realY%8;
					int lsb=(int) vram[tileDataOffset+yInTile*2] & 0xFF;
					int msb=(int) vram[tileDataOffset+yInTile*2+1] & 0xFF;
					int colorIndex=switch(((lsb >> xInTile) & 1) | (((msb >> xInTile) & 1) << 1)){
						case 0 -> bgWinPalette & 3;
						case 1 -> (bgWinPalette >> 2) & 3;
						case 2 -> (bgWinPalette >> 4) & 3;
						case 3 -> (bgWinPalette >> 6) & 3;
						default -> throw new IllegalStateException();
					};
					bgWinNonTransparent=colorIndex>0;
					screenBitmapHolder.setPixel(currentPixelX, currentLineY, palette[colorIndex]);
				}else if((lcdc & LCDC_BG_ENABLE)!=0){
					int realX=(currentPixelX+scrollX)%256;
					int realY=(currentLineY+scrollY)%256;

					int offset=(lcdc & LCDC_BG_TILE_MAP_SELECT)==0 ? 0x1800 : 0x1C00;
					int tileIndex=realY/8*32+realX/8;
					tileIndex=(int) vram[offset+tileIndex] & 0xFF;
					int tileDataOffset;
					if((lcdc & LCDC_BG_WIN_TILE_DATA)==0){
						tileIndex=(byte) tileIndex;
						tileDataOffset=tileIndex*16+0x1000;
					}else{
						tileDataOffset=tileIndex*16;
					}
					int xInTile=7-realX%8;
					int yInTile=realY%8;
					int lsb=(int) vram[tileDataOffset+yInTile*2] & 0xFF;
					int msb=(int) vram[tileDataOffset+yInTile*2+1] & 0xFF;
					int colorIndex=switch(((lsb >> xInTile) & 1) | (((msb >> xInTile) & 1) << 1)){
						case 0 -> bgWinPalette & 3;
						case 1 -> (bgWinPalette >> 2) & 3;
						case 2 -> (bgWinPalette >> 4) & 3;
						case 3 -> (bgWinPalette >> 6) & 3;
						default -> throw new IllegalStateException();
					};
					bgWinNonTransparent|=colorIndex>0;
					screenBitmapHolder.setPixel(currentPixelX, currentLineY, palette[colorIndex]);
				}

				// OBJ, also make sure no OAM DMA transfer is active
				if((lcdc & LCDC_OBJ_ENABLE)!=0 && oamDmaCyclesLeft==0){
					boolean doubleHeightSprites=(lcdc & LCDC_OBJ_SIZE)!=0;
					int lastDrawnSpriteX=Integer.MAX_VALUE;
					for(int index:spritesForCurrentLine){
						if(index==-1)
							break;
						int spriteX=((int) oam[index*4+1] & 0xFF)-8;
						if(spriteX>currentPixelX || spriteX+8<currentPixelX)
							continue;

						int spriteY=((int) oam[index*4] & 0xFF)-16;
						int tileIndex=(int) oam[index*4+2] & 0xFF;
						int flags=oam[index*4+3];

						int xInTile;
						if((flags & OBJ_X_FLIP)!=0){
							xInTile=currentPixelX-spriteX;
						}else{
							xInTile=7-(currentPixelX-spriteX);
						}
						int yInTile=currentLineY-spriteY;
						if((flags & OBJ_Y_FLIP)!=0)
							yInTile=(doubleHeightSprites ? 15 : 7)-yInTile;

						if(doubleHeightSprites){
							tileIndex&=0xFE;
							if(yInTile>7){
								yInTile-=8;
								tileIndex|=1;
							}
						}

						int tileDataOffset=tileIndex*16;

						int objPalette=(flags & OBJ_PALETTE_NUM)==0 ? objPalette0 : objPalette1;
						int lsb=(int) vram[Math.max(0, tileDataOffset+yInTile*2)] & 0xFF;
						int msb=(int) vram[Math.max(0, tileDataOffset+yInTile*2)+1] & 0xFF;
						int colorIndex=((lsb >> xInTile) & 1) | (((msb >> xInTile) & 1) << 1);
						if(colorIndex==0)
							continue;
						if(lastDrawnSpriteX!=Integer.MAX_VALUE){ // Already drew a sprite pixel
							if(lastDrawnSpriteX<=spriteX)
								continue;
						}
						lastDrawnSpriteX=spriteX;
						colorIndex=switch(colorIndex){
							case 1 -> (objPalette >> 2) & 3;
							case 2 -> (objPalette >> 4) & 3;
							case 3 -> (objPalette >> 6) & 3;
							default -> throw new IllegalStateException();
						};
						if((flags & OBJ_BELOW_BG_WIN)==0 || !bgWinNonTransparent)
							screenBitmapHolder.setPixel(currentPixelX, currentLineY, palette[colorIndex]);
					}
				}
			}

			currentPixelX++;
			if(clockCyclesToNextMode==0){
				clockCyclesToNextMode=204-currentLinePenaltiesTaken;
				currentLinePenaltiesTaken=0;
				status&=~STAT_MODE_MASK;
				status|=STAT_MODE_HBLANK;
				oamDevice.setAccessible(true);
				vramDevice.setAccessible(true);
				if((status & STAT_HBLANK_INTERRUPT)!=0)
					interruptFlags.set(InterruptFlagsDevice.FLAG_LCD_STAT);
			}
		}else if(mode==STAT_MODE_HBLANK){

			if(clockCyclesToNextMode==0){
				currentLineY++;
				if((lcdc & LCDC_WINDOW_ENABLE)!=0 && currentLineY>=windowY && currentPixelX>=windowX-7){
					windowLineCounter++;
				}
				if(currentLineY==interruptLineY){
					if((status & STAT_COINCIDENCE_INTERRUPT)!=0)
						interruptFlags.set(InterruptFlagsDevice.FLAG_LCD_STAT);
					status|=STAT_COINCIDENCE;
				}else{
					status&=~STAT_COINCIDENCE;
				}
				currentPixelX=0;
				if(currentLineY==144){
					clockCyclesToNextMode=456;
					status&=~STAT_MODE_MASK;
					status|=STAT_MODE_VBLANK;
					screenBitmapHolder.swapBuffers();
					interruptFlags.set(InterruptFlagsDevice.FLAG_VBLANK);
					if((status & STAT_VBLANK_INTERRUPT)!=0)
						interruptFlags.set(InterruptFlagsDevice.FLAG_LCD_STAT);
				}else{
					clockCyclesToNextMode=80;
					status&=~STAT_MODE_MASK;
					status|=STAT_MODE_OAM_RAM;
					if((status & STAT_OAM_INTERRUPT)!=0)
						interruptFlags.set(InterruptFlagsDevice.FLAG_LCD_STAT);
				}
			}
		}else if(mode==STAT_MODE_VBLANK){
			if(clockCyclesToNextMode==0){
				currentLineY++;
				if(currentLineY==interruptLineY){
					status|=STAT_COINCIDENCE;
					if((status & STAT_COINCIDENCE_INTERRUPT)!=0)
						interruptFlags.set(InterruptFlagsDevice.FLAG_LCD_STAT);
				}else{
					status&=~STAT_COINCIDENCE;
				}
				if(currentLineY==154){
					// Next frame
					windowLineCounter=0;
					currentLineY=0;
					clockCyclesToNextMode=80;
					status&=~STAT_MODE_MASK;
					status|=STAT_MODE_OAM_RAM;
					oamDevice.setAccessible(false);
					if(currentLineY==interruptLineY){
						status|=STAT_COINCIDENCE;
						if((status & STAT_COINCIDENCE_INTERRUPT)!=0)
							interruptFlags.set(InterruptFlagsDevice.FLAG_LCD_STAT);
					}else{
						status&=~STAT_COINCIDENCE;
					}
				}else{
					clockCyclesToNextMode=456;
				}
			}
		}
	}
}
