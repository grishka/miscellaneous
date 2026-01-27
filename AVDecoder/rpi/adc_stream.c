// Test of parallel AD9226 ADC using Raspberry Pi SMI (Secondary Memory Interface)
// For detailed description, see https://iosoft.blog
//
// Copyright (c) 2020 Jeremy P Bentham
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
//
// v0.06 JPB 16/7/20 Tidied up for Github

#include <stdio.h>
#include <signal.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <termios.h>
#include <fcntl.h>
#include <time.h>
#include <semaphore.h>
#include <linux/serial.h>
#include <sys/ioctl.h>
#include "rpi_dma_utils.h"
#include "rpi_smi_defs.h"

// SMI cycle timings
#define SMI_NUM_BITS    SMI_8_BITS
#define SMI_TIMING      SMI_TIMING_20M

#if PHYS_REG_BASE==PI_4_REG_BASE        // Timings for RPi v4 (1.5 GHz)
#define SMI_TIMING_1M   10, 38, 74, 38  // 1 MS/s
#define SMI_TIMING_10M   6,  6, 13,  6  // 10 MS/s
#define SMI_TIMING_20M   4,  5,  9,  5  // 19.74 MS/s
#define SMI_TIMING_25M   4,  3,  8,  4  // 25 MS/s
#define SMI_TIMING_31M   4,  3,  6,  3  // 31.25 MS/s
#else                                   // Timings for RPi v0-3 (1 GHz)
#define SMI_TIMING_1M   10, 25, 50, 25  // 1 MS/s
#define SMI_TIMING_10M   4,  6, 13,  6  // 10 MS/s
#define SMI_TIMING_20M   2,  6, 13,  6  // 20 MS/s
#define SMI_TIMING_25M   2,  5, 10,  5  // 25 MS/s
#define SMI_TIMING_31M   2,  4,  6,  4  // 31.25 MS/s
#define SMI_TIMING_42M   2,  3,  6,  3  // 41.66 MS/s
#define SMI_TIMING_50M   2,  3,  5,  2  // 50 MS/s
#endif

// Number of raw bytes per ADC sample
#define SAMPLE_SIZE     1

//#define BLOCK_SIZE      16384
//#define BLOCK_SIZE      131072
//#define BLOCK_SIZE      262144
#define BLOCK_SIZE      524288

//#define SEND_BUFFER_SIZE 16384
//#define SEND_BUFFER_COUNT 8
#define SEND_BUFFER_SIZE 131072
#define SEND_BUFFER_COUNT 8

// GPIO pin numbers
#define ADC_D0_PIN      8
#define ADC_NPINS       16
#define SMI_SOE_PIN     6
#define SMI_SWE_PIN     7
#define SMI_DREQ_PIN    24
#define TEST_PIN        25

// DMA request threshold
#define REQUEST_THRESH  4

//#define DMA_CHANNEL DMA_CHAN_A
#define DMA_CHANNEL 1

// Structures for mapped I/O devices, and non-volatile memory
extern MEM_MAP gpio_regs, dma_regs, clk_regs;
MEM_MAP vc_mem, smi_regs;
MEM_MAP buffer1, buffer2;

uint8_t* sendBuffers;
int currentWriteSendBuffer=4, currentReadSendBuffer=0;
int running=1;
uint32_t* rxbuff;

int port=0;

// Pointers to SMI registers
volatile SMI_CS_REG  *smi_cs;
volatile SMI_L_REG   *smi_l;
volatile SMI_A_REG   *smi_a;
volatile SMI_D_REG   *smi_d;
volatile SMI_DMC_REG *smi_dmc;
volatile SMI_DSR_REG *smi_dsr;
volatile SMI_DSW_REG *smi_dsw;
volatile SMI_DCS_REG *smi_dcs;
volatile SMI_DCA_REG *smi_dca;
volatile SMI_DCD_REG *smi_dcd;

void map_devices(void);
void fail(char *s);
void terminate(int sig);
void adc_dma_start(MEM_MAP *mp);
void init_smi(int width, int ns, int setup, int hold, int strobe);
void mode_word(uint32_t *wp, int n, uint32_t mode);

int main(int argc, char *argv[]){
	int i;

	port=open("/dev/ffs-usb0/ep1", O_RDWR);
	if(port<0){
		perror("open");
		return 1;
	}

	sendBuffers=malloc(SEND_BUFFER_SIZE*SEND_BUFFER_COUNT);
	signal(SIGINT, terminate);
	map_devices();

	for (i=0; i<ADC_NPINS; i++)
		gpio_mode(ADC_D0_PIN+i, GPIO_ALT1);
	gpio_mode(SMI_SOE_PIN, GPIO_ALT1);
	init_smi(SMI_NUM_BITS, SMI_TIMING);

	map_uncached_mem(&vc_mem, PAGE_SIZE);
	map_uncached_mem(&buffer1, BLOCK_SIZE*SAMPLE_SIZE);
	map_uncached_mem(&buffer2, BLOCK_SIZE*SAMPLE_SIZE);

	printf("dma cb init=%p\n", MEM_BUS_ADDR((&vc_mem), vc_mem.virt));
	smi_dmc->dmaen = 1;
	smi_cs->enable = 1;
	smi_cs->clear = 1;
	uint32_t currentCb=*REG32(dma_regs, DMA_REG(DMA_CHANNEL, DMA_CONBLK_AD));
	adc_dma_start(&vc_mem);
	printf("dma cb start=%p\n", *REG32(dma_regs, DMA_REG(DMA_CHANNEL, DMA_CONBLK_AD)));

	DMA_CB *cbs=vc_mem.virt;
	uint32_t cb1=MEM_BUS_ADDR((&vc_mem), &cbs[0]);
	uint32_t cb2=MEM_BUS_ADDR((&vc_mem), &cbs[1]);

	struct timespec time={0, 250000};

	while(1){
		uint32_t cb=*REG32(dma_regs, DMA_REG(DMA_CHANNEL, DMA_CONBLK_AD));
		if(cb!=currentCb){
			currentCb=cb;
			
			
			if(cb==cb1){
				rxbuff=buffer2.virt;
			}else if(cb==cb2){
				rxbuff=buffer1.virt;
			}else{
				continue;
			}

			write(port, rxbuff, BLOCK_SIZE);

			cb=*REG32(dma_regs, DMA_REG(DMA_CHANNEL, DMA_CONBLK_AD));
			if(cb!=currentCb)
				printf("!!!!! DMA buffer overrun\n");
		}else{
			nanosleep(&time, NULL);
		}
		smi_l->len=0xffffffff; // Reset length
		smi_cs->start=1;
	}
	smi_cs->enable = smi_dcs->enable = 0;
	terminate(0);
	return(0);
}

// Map GPIO, DMA and SMI registers into virtual mem (user space)
// If any of these fail, program will be terminated
void map_devices(void)
{
	map_periph(&gpio_regs, (void *)GPIO_BASE, PAGE_SIZE);
	map_periph(&dma_regs, (void *)DMA_BASE, PAGE_SIZE);
	map_periph(&clk_regs, (void *)CLK_BASE, PAGE_SIZE);
	map_periph(&smi_regs, (void *)SMI_BASE, PAGE_SIZE);
}

// Catastrophic failure in initial setup
void fail(char *s)
{
	printf(s);
	terminate(0);
}

// Free memory segments and exit
void terminate(int sig)
{
	int i;

	printf("Closing\n");
	if (gpio_regs.virt)
	{
		for (i=0; i<ADC_NPINS; i++)
			gpio_mode(ADC_D0_PIN+i, GPIO_IN);
	}
	if (smi_regs.virt)
		*REG32(smi_regs, SMI_CS) = 0;
	stop_dma(DMA_CHANNEL);
	unmap_periph_mem(&vc_mem);
	unmap_periph_mem(&buffer1);
	unmap_periph_mem(&buffer2);
	unmap_periph_mem(&smi_regs);
	unmap_periph_mem(&dma_regs);
	unmap_periph_mem(&gpio_regs);
	running=0;
	if(port!=0)
		close(port);
	free(sendBuffers);
	exit(0);
}

void adc_dma_start(MEM_MAP *mp)
{
	DMA_CB *cbs=mp->virt;

	smi_l->len=0xffffffff;
	smi_cs->pxldat=1;
	smi_cs->enable=1;
	smi_cs->clear=1;
	smi_cs->seterr=1;
	smi_cs->start=1;

	enable_dma(DMA_CHANNEL);
	cbs[0].ti = DMA_SRCE_DREQ | (DMA_SMI_DREQ << 16) | DMA_CB_DEST_INC;
	cbs[0].tfr_len = (BLOCK_SIZE) * SAMPLE_SIZE;
	cbs[0].srce_ad = REG_BUS_ADDR(smi_regs, SMI_D);
	cbs[0].dest_ad = MEM_BUS_ADDR((&buffer1), buffer1.virt);
	cbs[0].next_cb = MEM_BUS_ADDR(mp, &cbs[1]);

	cbs[1].ti = DMA_SRCE_DREQ | (DMA_SMI_DREQ << 16) | DMA_CB_DEST_INC;
	cbs[1].tfr_len = (BLOCK_SIZE) * SAMPLE_SIZE;
	cbs[1].srce_ad = REG_BUS_ADDR(smi_regs, SMI_D);
	cbs[1].dest_ad = MEM_BUS_ADDR((&buffer2), buffer2.virt);
	cbs[1].next_cb = MEM_BUS_ADDR(mp, &cbs[0]);

	start_dma(mp, DMA_CHANNEL, &cbs[0], 0);
}

// Initialise SMI, given data width, time step, and setup/hold/strobe counts
// Step value is in nanoseconds: even numbers, 2 to 30
void init_smi(int width, int ns, int setup, int strobe, int hold)
{
	int divi = ns / 2;

	smi_cs  = (SMI_CS_REG *) REG32(smi_regs, SMI_CS);
	smi_l   = (SMI_L_REG *)  REG32(smi_regs, SMI_L);
	smi_a   = (SMI_A_REG *)  REG32(smi_regs, SMI_A);
	smi_d   = (SMI_D_REG *)  REG32(smi_regs, SMI_D);
	smi_dmc = (SMI_DMC_REG *)REG32(smi_regs, SMI_DMC);
	smi_dsr = (SMI_DSR_REG *)REG32(smi_regs, SMI_DSR0);
	smi_dsw = (SMI_DSW_REG *)REG32(smi_regs, SMI_DSW0);
	smi_dcs = (SMI_DCS_REG *)REG32(smi_regs, SMI_DCS);
	smi_dca = (SMI_DCA_REG *)REG32(smi_regs, SMI_DCA);
	smi_dcd = (SMI_DCD_REG *)REG32(smi_regs, SMI_DCD);
	smi_cs->value = smi_l->value = smi_a->value = 0;
	smi_dsr->value = smi_dsw->value = smi_dcs->value = smi_dca->value = 0;
	if (*REG32(clk_regs, CLK_SMI_DIV) != divi << 12)
	{
		*REG32(clk_regs, CLK_SMI_CTL) = CLK_PASSWD | (1 << 5);
		usleep(10);
		while (*REG32(clk_regs, CLK_SMI_CTL) & (1 << 7)) ;
		usleep(10);
		*REG32(clk_regs, CLK_SMI_DIV) = CLK_PASSWD | (divi << 12);
		usleep(10);
		*REG32(clk_regs, CLK_SMI_CTL) = CLK_PASSWD | 6 | (1 << 4);
		usleep(10);
		while ((*REG32(clk_regs, CLK_SMI_CTL) & (1 << 7)) == 0) ;
		usleep(100);
	}
	if (smi_cs->seterr)
		smi_cs->seterr = 1;
	smi_dsr->rsetup = smi_dsw->wsetup = setup;
	smi_dsr->rstrobe = smi_dsw->wstrobe = strobe;
	smi_dsr->rhold = smi_dsw->whold = hold;
	smi_dmc->panicr = smi_dmc->panicw = 8;
	smi_dmc->reqr = smi_dmc->reqw = REQUEST_THRESH;
	smi_dsr->rwidth = smi_dsw->wwidth = width;
}

// Get GPIO mode value into 32-bit word
void mode_word(uint32_t *wp, int n, uint32_t mode)
{
	uint32_t mask = 7 << (n * 3);

	*wp = (*wp & ~mask) | (mode << (n * 3));
}

// EOF
