//
//  SignalSource.hpp
//  AVDecoder
//
//  Created by Grishka on 06.11.2025.
//

#ifndef SignalSource_hpp
#define SignalSource_hpp

#include <functional>
#include <string>
#include <IOKit/usb/IOUSBLib.h>
#include <IOKit/IOCFPlugIn.h>
#include <FLAC/stream_decoder.h>

#include "threading.h"

class SignalSource{
public:
	virtual ~SignalSource(){};
	virtual void start()=0;
	virtual void stop()=0;
	
	std::function<void(uint8_t*, size_t)> callback;
};

class ADCSignalSource: public SignalSource{
public:
	ADCSignalSource();
	virtual ~ADCSignalSource();
	virtual void start();
	virtual void stop();
	int offset=15;
private:
	void runThread();
	tgvoip::Thread* thread=NULL;
	IOUSBInterfaceInterface800** usbInterface=NULL;
	bool running=false;
};

class FileSignalSource: public SignalSource{
public:
	FileSignalSource(std::string fileName);
	virtual ~FileSignalSource();
	virtual void start();
	virtual void stop();
private:
	void runThread();
	std::string fileName;
	tgvoip::Thread* thread;
	FLAC__StreamDecoder* flacDecoder=NULL;
	bool running=false;
	uint8_t* buffer;
	uint32_t callbackCount=0;
};

#endif /* SignalSource_hpp */
