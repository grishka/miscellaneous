//
//  Decoder.hpp
//  AVDecoder
//
//  Created by Grishka on 29.08.2025.
//

#ifndef Decoder_hpp
#define Decoder_hpp

#define BUFFER_SIZE 524288

#include <stdlib.h>
#include <stdint.h>
#include <vector>
#include <deque>
#include <CoreFoundation/CoreFoundation.h>

#include "Buffers.h"
#include "threading.h"
#include "BlockingQueue.h"
#include "Filters.hpp"

enum class ColorDisplayMode{
	Full,
	Y,
	Db,
	Dr,
	Raw
};

enum class DisplayLevelsMode{
	Auto,
	BlackIsSync,
	Raw
};

class Decoder{
public:
	Decoder(void* bitmapData, unsigned int bitmapWidth, unsigned int bitmapHeight, unsigned int bitmapStride, CFRunLoopSourceRef runLoopSource);
	void handleSampleData(uint8_t* samples, size_t count);
	tgvoip::Buffer getOutputFrame();
	void startOutput();
	void stopOutput();
	
	ColorDisplayMode colorMode=ColorDisplayMode::Full;
	DisplayLevelsMode levelsMode=DisplayLevelsMode::Auto;
	bool displayFieldsSequentially=false;
	std::vector<float> scopeData1;
	std::vector<float> scopeData2;
	std::vector<float> scopeLines;
	bool includeBlankingIntervalsInOutput=false;
	bool outputEnabled=false;
	int scopeLineIndex=16;
	float whiteLevelRatio=0.43; // ratio between sync to black and black to white
private:
	struct SyncPulse{
		int location;
		int length;
		
		SyncPulse offset(int amount){
			return SyncPulse{
				.location=location+amount,
				.length=length
			};
		}
	};
	
	struct VideoLine{
		std::vector<float> luminance;
		std::vector<float> chrominance;
		std::vector<float> filteredLuminance;
		std::vector<float> raw;
		std::vector<SyncPulse> sync;
		int offsetInBuffer;
	};
	
	struct VideoField{
		std::vector<VideoLine> lines;
		bool isBottom;
		float syncLevel;
		float blackLevel;
	};
	
	void runDecoderThread();
	std::vector<VideoLine> processField(std::vector<float> luminance, std::vector<float> chrominance, std::vector<float> filteredLuminance, std::vector<float> raw, std::vector<SyncPulse> sync, float syncLevel, float blackLevel, float visibleBrightnessRange, bool isBottom);
	void processLine(VideoLine line, int lineIndex, float syncLevel, float blackLevel, float whiteLevel);
	void demodulateColorSubcarrier(float *samples, float *chrominance, int count);
	void outputCapturedFrame();
	VideoLine joinLines(VideoLine a, VideoLine b);
	std::vector<VideoLine> splitLine(VideoLine line, int numParts);
	
	tgvoip::Thread decoderThread;
	tgvoip::BlockingQueue<tgvoip::Buffer> newlyAcquiredDataBuffers=tgvoip::BlockingQueue<tgvoip::Buffer>(10);
	tgvoip::BufferPool<BUFFER_SIZE, 10> bufferPool;
	tgvoip::Semaphore blockingSemaphore=tgvoip::Semaphore(1, 1);
	tgvoip::BufferPool<942*625*4, 10> outputBufferPool;
	tgvoip::BlockingQueue<tgvoip::Buffer> outputQueue=tgvoip::BlockingQueue<tgvoip::Buffer>(10);
	tgvoip::Buffer currentOutputBuffer=tgvoip::Buffer(0);
	std::vector<float> prevLineChroma;
	std::deque<VideoField> fieldQueue;
	void* bitmapData;
	unsigned int bitmapWidth, bitmapHeight, bitmapStride;
	bool running=true;
	int currentLineIndex=0;
	unsigned int currentLineIndexTotal=0;
	float syncLevel=0;
	float syncThreshold=0;
	float blackLevel=0;
	float visibleBrightnessRange=0.99f;
	float nextSyncLevel=0;
	float detectedWhiteLevel=0;
	int fieldsWithoutVITS=10;
	
	float redCenterFreq=4406000/20000000.0f;
	float blueCenterFreq=4250000/20000000.0f;
	float redMaxDeviation=280000/20000000.0f;
	float blueMaxDeviation=230000/20000000.0f;
	int colorLineOffset=0;
	
	BiquadFilter *chromaDeemphasisFilter;
	BiquadFilter *chromaLowpassFilter;
	
	int frameCount=0;
	uint64_t lastFrameTime;
	
	float lastZeroCrossingLocation=0;
	float lastUsedZeroCrossingLocation=0;
	float lastChromaValue=0;
	
	CFRunLoopSourceRef runLoopSource;
	CFRunLoopRef mainThreadRunLoop;
};

#endif /* Decoder_hpp */
