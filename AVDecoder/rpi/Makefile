all: adc_stream usb_init

adc_stream: adc_stream.c rpi_dma_utils.c
	gcc adc_stream.c rpi_dma_utils.c -O3 -o adc_stream

usb_init: usb_init.c
	gcc usb_init.c -o usb_init

clean:
	rm -f *.o adc_stream usb_init
