//
//  SignalSource.cpp
//  AVDecoder
//
//  Created by Grishka on 06.11.2025.
//

#include <CoreFoundation/CoreFoundation.h>
#include <stdlib.h>
#include <assert.h>
#include <stdexcept>
#include <string>

#include "SignalSource.hpp"

#pragma GCC diagnostic ignored "-Wdeprecated-declarations"
#define BUFFER_SIZE 524288

ADCSignalSource::ADCSignalSource(){
	
}

ADCSignalSource::~ADCSignalSource(){
	
}

void ADCSignalSource::start(){
	IOReturn res;
	SInt32 vendorID=0x1d6b;
	SInt32 productID=0x0104;
	io_iterator_t iterator=0;
	
	CFMutableDictionaryRef matchingDict=IOServiceMatching(kIOUSBDeviceClassName);
	CFDictionaryAddValue(matchingDict, CFSTR(kUSBVendorID), CFNumberCreate(kCFAllocatorDefault, kCFNumberSInt32Type, &vendorID));
	CFDictionaryAddValue(matchingDict, CFSTR(kUSBProductID), CFNumberCreate(kCFAllocatorDefault, kCFNumberSInt32Type, &productID));
	IOServiceGetMatchingServices(kIOMasterPortDefault, matchingDict, &iterator);
	
	io_service_t usbRef=IOIteratorNext(iterator);
	IOObjectRelease(iterator);
	if(usbRef==0){
		throw std::runtime_error("Device not found");
	}
	
	SInt32 score;
	IOCFPlugInInterface** plugin;
	IOCreatePlugInInterfaceForService(usbRef, kIOUSBDeviceUserClientTypeID, kIOCFPlugInInterfaceID, &plugin, &score);
	IOObjectRelease(usbRef);
	
	IOUSBDeviceInterface650** usbDevice=NULL;
	(*plugin)->QueryInterface(plugin, CFUUIDGetUUIDBytes(kIOUSBDeviceInterfaceID650), (LPVOID*)&usbDevice);
	
	IOUSBFindInterfaceRequest interfaceRequest;
	interfaceRequest.bInterfaceClass = 0xFF;
	interfaceRequest.bInterfaceSubClass = kIOUSBFindInterfaceDontCare;
	interfaceRequest.bInterfaceProtocol = kIOUSBFindInterfaceDontCare;
	interfaceRequest.bAlternateSetting = kIOUSBFindInterfaceDontCare;
	(*usbDevice)->CreateInterfaceIterator(usbDevice, &interfaceRequest, &iterator);
	
	usbRef=IOIteratorNext(iterator);
	IOCreatePlugInInterfaceForService(usbRef, kIOUSBInterfaceUserClientTypeID, kIOCFPlugInInterfaceID, &plugin, &score);
	(*plugin)->QueryInterface(plugin, CFUUIDGetUUIDBytes(kIOUSBInterfaceInterfaceID800), (LPVOID*)&usbInterface);
	(*plugin)->Release(plugin);
	IOObjectRelease(usbRef);
	
	res=(*usbInterface)->USBInterfaceOpen(usbInterface);
	if(res!=kIOReturnSuccess){
		std::string msg="Failed to open USB interface: error code ";
		msg+=res;
		throw std::runtime_error(msg);
	}
	
	thread=new tgvoip::Thread(std::bind(&ADCSignalSource::runThread, this));
	running=true;
	thread->Start();
}

void ADCSignalSource::stop(){
	running=false;
	if(usbInterface){
		(*usbInterface)->USBInterfaceClose(usbInterface);
	}
	if(thread){
		thread->Join();
		delete thread;
		thread=NULL;
	}
}

void ADCSignalSource::runThread(){
	int8_t* buffer=(int8_t*)malloc(BUFFER_SIZE);
	uint8_t* processedBuffer=(uint8_t*)malloc(BUFFER_SIZE);
	while(running){
		UInt32 numBytes=BUFFER_SIZE;
		IOReturn res=(*usbInterface)->ReadPipe(usbInterface, 1, buffer, &numBytes);
		if(res!=kIOReturnSuccess){
			printf("Error reading from device: %d\n", res);
			break;
		}
		for(int i=0;i<BUFFER_SIZE;i+=2){
			processedBuffer[i]=(uint8_t)((int16_t)(buffer[i+1])+128);
			processedBuffer[i+1]=(uint8_t)((int16_t)(buffer[i])+128);
		}
		for(int i=0;i<BUFFER_SIZE;i++){
			processedBuffer[i]-=offset;
		}
		callback(processedBuffer, BUFFER_SIZE);
	}
	free(buffer);
	free(processedBuffer);
}

FileSignalSource::FileSignalSource(std::string fileName): fileName(fileName){
	flacDecoder=FLAC__stream_decoder_new();
	buffer=(uint8_t*)malloc(BUFFER_SIZE);
}

FileSignalSource::~FileSignalSource(){
	FLAC__stream_decoder_delete(flacDecoder);
	free(buffer);
}

void FileSignalSource::start(){
	thread=new tgvoip::Thread(std::bind(&FileSignalSource::runThread, this));
	running=true;
	thread->Start();
}

void FileSignalSource::stop(){
	running=false;
	thread->Join();
	delete thread;
	thread=NULL;
}

void FileSignalSource::runThread(){
	auto writeCb=[](const FLAC__StreamDecoder *decoder, const FLAC__Frame *frame, const FLAC__int32 *const buffer[], void *clientData)->FLAC__StreamDecoderWriteStatus{
		FileSignalSource* s=(FileSignalSource*)clientData;
		if(frame->header.blocksize!=4096){
			printf("Unexpected frame size %u\n", frame->header.blocksize);
			return FLAC__STREAM_DECODER_WRITE_STATUS_ABORT;
		}
		uint32_t offset=s->callbackCount*4096;
		for(uint32_t i=0;i<frame->header.blocksize;i++){
			s->buffer[offset+i]=(uint8_t)(buffer[0][i]+128);
		}
		s->callbackCount=(s->callbackCount+1)%128;
		if(s->callbackCount==0){
			s->callback(s->buffer, BUFFER_SIZE);
			usleep(BUFFER_SIZE/20); // 20 samples per microsecond
		}
		return s->running ? FLAC__STREAM_DECODER_WRITE_STATUS_CONTINUE : FLAC__STREAM_DECODER_WRITE_STATUS_ABORT;
	};
	auto errorCb=[](const FLAC__StreamDecoder *decoder, FLAC__StreamDecoderErrorStatus status, void *clientData){
		//throw std::runtime_error(std::string("Error reading file: ")+FLAC__StreamDecoderErrorStatusString[status]);
		printf("error reading file: %s\n", FLAC__StreamDecoderErrorStatusString[status]);
	};
	FLAC__stream_decoder_init_file(flacDecoder, fileName.c_str(), writeCb, NULL, errorCb, this);
	FLAC__stream_decoder_process_until_end_of_stream(flacDecoder);
}
