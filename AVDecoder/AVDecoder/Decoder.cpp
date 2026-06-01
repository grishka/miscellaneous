//
//  Decoder.cpp
//  AVDecoder
//
//  Created by Grishka on 29.08.2025.
//

#include "Decoder.hpp"
#include "Filters.hpp"
#include <stdio.h>
#include <mach/mach_time.h>

// https://www.batsocks.co.uk/readme/video_timing.htm
#define DEFAULT_LINE_DURATION 1280 // 64 us
#define MIN_LINE_DURATION 1250
#define MAX_LINE_DURATION 1300
#define DEFAULT_FRAME_DURATION (DEFAULT_LINE_DURATION*625)
#define DEFAULT_FIELD_DURATION (DEFAULT_FRAME_DURATION/2)
#define MIN_FRAME_DURATION (DEFAULT_LINE_DURATION*620)
#define MAX_FRAME_DURATION (DEFAULT_LINE_DURATION*626)
#define LINE_LONG_SYNC_DURATION 94 // 4.7 us
#define LINE_SYNC_DURATION 47 // 2.35 us
#define FIELD_SYNC_DURATION 546 // 27.3 us
#define LINE_SYNC_WINDOW (LINE_SYNC_DURATION*3/6)
#define FIELD_SYNC_WINDOW (FIELD_SYNC_DURATION*3/4)

#define MIN_SYNC_DURATION 35 // 1.75 us
#define LINE_SYNC_MIN_DURATION 70 // 3.5 us
#define LINE_SYNC_MAX_DURATION 129 // 6.45 us

class BaseSignalBuffers{
public:
	float *luminance;
	float *chrominance[2];
	float *filteredLuminance;
	float *raw;
	
	void setFrom(BaseSignalBuffers *other, int offset){
		luminance=other->luminance+offset;
		chrominance[0]=other->chrominance[0]+offset;
		chrominance[1]=other->chrominance[1]+offset;
		filteredLuminance=other->filteredLuminance+offset;
		raw=other->raw+offset;
	}
};

class SignalBuffers: public BaseSignalBuffers{
public:
	SignalBuffers(){
		int size=getBufferSize();
		luminance=(float*)malloc(size*sizeof(float));
		chrominance[0]=(float*)malloc(size*sizeof(float));
		chrominance[1]=(float*)malloc(size*sizeof(float));
		filteredLuminance=(float*)malloc(size*sizeof(float));
		raw=(float*)malloc(size*sizeof(float));
	}
	
	virtual ~SignalBuffers(){
		free(luminance);
		free(chrominance[0]);
		free(chrominance[1]);
		free(filteredLuminance);
		free(raw);
	}
	
protected:
	virtual int getBufferSize(){
		return BUFFER_SIZE;
	}
};

class VideoField: public SignalBuffers{
public:
	std::vector<VideoLine> lines;
	bool isBottom;
	float syncLevel;
	float blackLevel;
	int numSamples=0;

	void appendSamples(BaseSignalBuffers *buf, int srcOffset, int count){
		assert(numSamples+count<=getBufferSize());
		
		memcpy(luminance+numSamples, buf->luminance+srcOffset, count*sizeof(float));
		memcpy(chrominance[0]+numSamples, buf->chrominance[0]+srcOffset, count*sizeof(float));
		memcpy(chrominance[1]+numSamples, buf->chrominance[1]+srcOffset, count*sizeof(float));
		memcpy(filteredLuminance+numSamples, buf->filteredLuminance+srcOffset, count*sizeof(float));
		memcpy(raw+numSamples, buf->raw+srcOffset, count*sizeof(float));
		numSamples+=count;
	}
	
protected:
	int getBufferSize() override{
		return MAX_LINE_DURATION*625;
	}
};

class VideoLine: public BaseSignalBuffers{
public:
	std::vector<Decoder::SyncPulse> sync;
	int offsetInBuffer;
	int numSamples;
	
	VideoLine(){}
	
	VideoLine(BaseSignalBuffers *buf, int offset, int length, vector<Decoder::SyncPulse> sync):sync(sync), numSamples(length){
		setFrom(buf, offset);
	}
};

BiquadFilter chromaLowpass(-1.3698978138079605, 0.5254518355382106, 0.038888505432562503, 0.07777701086512501, 0.038888505432562503);

Decoder::Decoder(void* bitmapData, unsigned int bitmapWidth, unsigned int bitmapHeight, unsigned int bitmapStride, CFRunLoopSourceRef runLoopSource) :bitmapData(bitmapData), bitmapWidth(bitmapWidth), bitmapHeight(bitmapHeight), bitmapStride(bitmapStride/4), decoderThread(std::bind(&Decoder::runDecoderThread, this)), runLoopSource(runLoopSource){
	
	colorDecoder=new ColorDecoderSECAM();
	
	currentOutputBuffer=outputBufferPool.Get();
	mainThreadRunLoop=CFRunLoopGetMain();
	interpolatedField=new VideoField();
	for(int i=0;i<625;i++){
		VideoLine line;
		line.setFrom(interpolatedField, DEFAULT_LINE_DURATION*i);
		line.numSamples=DEFAULT_LINE_DURATION;
		interpolatedField->lines.push_back(line);
	}

	decoderThread.SetName("Decoder");
	decoderThread.Start();
}

Decoder::~Decoder(){
	delete colorDecoder;
	delete interpolatedField;
}

void Decoder::handleSampleData(uint8_t* samples, size_t count){
	blockingSemaphore.Acquire();
	tgvoip::Buffer buf;
	try{
		buf=bufferPool.Get();
	}catch(std::bad_alloc& x){
		printf("Can't keep up with real time, dropping buffer!\n");
		blockingSemaphore.Release();
		return;
	}
	buf.CopyFrom(samples, 0, count);
	blockingSemaphore.Release();
	newlyAcquiredDataBuffers.Put(std::move(buf));
}

void Decoder::runDecoderThread(){
	blackLevel=1;
	
	float *samples=(float*)calloc(BUFFER_SIZE, sizeof(float));
	float *nextSamples=(float*)calloc(BUFFER_SIZE, sizeof(float));
	prevLineChroma=(float*)calloc(DEFAULT_LINE_DURATION, sizeof(float));
	
	vector<SyncPulse> currentFieldSyncPulses;
	vector<SyncPulse> syncPulseLocations;
	vector<SyncPulse> fixedSyncPulseLocations;
	
	vector<VideoLine> lineBuffer;
	
	SignalBuffers *b=new SignalBuffers();
	SignalBuffers *prevBuf=new SignalBuffers();
	SignalBuffers *nextBuf=new SignalBuffers();
	
	for(int i=0;i<6;i++){
		fieldPool.push_back(new VideoField());
	}
	
	VideoField *currentField=fieldPool.front();
	fieldPool.pop_front();
	
	BiquadFilter syncLowpass(-1.69096225, 0.73269106, 0.01043220, 0.02086441, 0.01043220);
	BiquadFilter luminanceLowpass(-0.76129177, 0.27595249, 0.18599880, 0.23921641, 0.08944551);

	int bufferCount=0;
	
	int lastLongSyncPulseLocation=0;
	int earliestNextLongSyncPulseLocation=-1;
	bool nextFieldIsBottom=false;
	
	while(running){
		if(syncPulseLocations.size()>10){
			syncPulseLocations.erase(syncPulseLocations.begin(), syncPulseLocations.end()-10);
			for(SyncPulse& sp:syncPulseLocations){
				sp.location-=BUFFER_SIZE;
			}
		}else{
			while(syncPulseLocations.size()<10)
				syncPulseLocations.push_back({-1, 0});
		}
		tgvoip::Buffer buf=newlyAcquiredDataBuffers.GetBlocking();
		
		//blockingSemaphore.Acquire();
		
		SignalBuffers *tmpB=prevBuf;
		prevBuf=b;
		b=nextBuf;
		nextBuf=tmpB;
		
		float *tmp=nextSamples;
		nextSamples=samples;
		samples=tmp;
		
		memcpy(b->raw, nextSamples, BUFFER_SIZE*sizeof(float));
		
		for(int i=0;i<BUFFER_SIZE;i++){
			int32_t sampleInt=(int32_t)buf[i];
			nextSamples[i]=sampleInt/255.0;
		}
		
		float *subcarrier=colorDecoder->separateSubcarrier(samples, nextSamples);
		for(int i=0;i<BUFFER_SIZE;i++){
			nextBuf->luminance[i]=samples[i]-subcarrier[i];
		}
		
		syncLowpass.process(nextBuf->luminance, nextBuf->filteredLuminance, BUFFER_SIZE);
		luminanceLowpass.process(nextBuf->luminance, nextBuf->luminance, BUFFER_SIZE);

		colorDecoder->demodulateSubcarrier(subcarrier, nextBuf);

		float minLevels[128];
		for(int i=0;i<128;i++)
			minLevels[i]=1;
		for(int i=0;i<BUFFER_SIZE;i++){
			float sample=b->luminance[i];
			int index=i>>12;
			minLevels[index]=std::min(sample, minLevels[index]);
		}
		float minLevel=0;
		for(int i=0;i<128;i++)
			minLevel+=minLevels[i];
		minLevel/=128.0f;
		//printf("minLevel %f, maxLevel %f\n", minLevel, maxLevel);
		float threshold=minLevel+0.1;
		float lowSampleSum=0;
		int lowSampleCount=0;
		for(int i=0;i<BUFFER_SIZE;i++){
			float s=b->luminance[i];
			if(s<threshold){
				lowSampleSum+=s;
				lowSampleCount++;
			}
		}
		if(lowSampleCount>0){
			syncLevel=lowSampleSum/lowSampleCount;
			syncThreshold=syncLevel+0.08;
		}
		int samplesForBlackLevel=0;
		int samplesUntilBlackLevelIsSampled=0;
		float blackSampleSum[128]={0};
		int blackSampleCount[128]={0};
		for(int i=0;i<BUFFER_SIZE;i++){
			float s=b->luminance[i];
			if(s<syncThreshold){
				samplesUntilBlackLevelIsSampled=15;
			}else if(samplesUntilBlackLevelIsSampled>0){
				samplesUntilBlackLevelIsSampled--;
				samplesForBlackLevel=40;
			}else if(samplesForBlackLevel>0){
				samplesForBlackLevel--;
				if(s<0.5){
					int index=i>>12;
					blackSampleSum[index]+=s;
					blackSampleCount[index]++;
				}
			}
		}
		float blackSampleAverage=0;
		int blackSampleNonZeroCount=0;
		for(int i=0;i<128;i++){
			if(blackSampleCount[i]){
				blackSampleSum[i]/=(float)blackSampleCount[i];
				blackSampleAverage+=blackSampleSum[i];
				blackSampleNonZeroCount++;
			}
		}
		if(blackSampleNonZeroCount>0){
			blackSampleAverage/=blackSampleNonZeroCount;
			float finalBlackSum=0;
			int finalBlackCount=0;
			for(int i=0;i<128;i++){
				if(blackSampleSum[i]<=blackSampleAverage){
					finalBlackSum+=blackSampleSum[i];
					finalBlackCount++;
				}
			}
			if(finalBlackCount){
				float newBlackLevel=finalBlackSum/finalBlackCount;
				syncThreshold=syncLevel+(newBlackLevel-syncLevel)*0.25;
				float newVisibleBrightnessRange=std::min((newBlackLevel-syncLevel)/whiteLevelRatio, 0.99f-newBlackLevel);
				if(fabsf(newBlackLevel-blackLevel)>=0.005f || fabsf(newVisibleBrightnessRange-visibleBrightnessRange)>=0.005f){
					blackLevel=newBlackLevel;
					visibleBrightnessRange=newVisibleBrightnessRange;
					//printf("sync %f, black %f, white %f\n", syncLevel, blackLevel, blackLevel+visibleBrightnessRange);
				}
			}
		}
		
		bool insidePulse=false;
		int pulseStart=0;
		for(int i=0;i<BUFFER_SIZE+DEFAULT_LINE_DURATION;i++){
			float s;
			if(i<BUFFER_SIZE)
				s=b->filteredLuminance[i];
			else
				s=nextBuf->filteredLuminance[i-BUFFER_SIZE];
			if(insidePulse){
				if(s>syncThreshold){
					insidePulse=false;
					int length=i-pulseStart;
					if(length>=MIN_SYNC_DURATION){
						syncPulseLocations.push_back({pulseStart, length});
					}
				}
			}else{
				if(s<syncThreshold){
					insidePulse=true;
					pulseStart=i;
				}
			}
		}
		
		for(int i=10;i<syncPulseLocations.size();i++){
			int loc=syncPulseLocations[i].location;
			if(loc>=BUFFER_SIZE)
				break;
			if(syncPulseLocations[i].length>LINE_SYNC_MAX_DURATION && loc>=earliestNextLongSyncPulseLocation){
				int prevLineSyncPos=0;
				for(int j=1;j<10;j++){
					int length=syncPulseLocations[i-j].length;
					if(length>LINE_SYNC_MIN_DURATION && length<LINE_SYNC_MAX_DURATION){
						prevLineSyncPos=syncPulseLocations[i-j].location;
						break;
					}
				}
				float linesSinceLastLineSync=(loc-prevLineSyncPos)/(float)DEFAULT_LINE_DURATION;
				float phaseRelativeToLineSync=linesSinceLastLineSync-(int)linesSinceLastLineSync;
				int fieldDuration=loc-lastLongSyncPulseLocation;
				bool nextIsBottom=phaseRelativeToLineSync>0.25f && phaseRelativeToLineSync<0.75f;
				if(nextIsBottom)
					fieldDuration-=DEFAULT_LINE_DURATION/2;
				//printf("long pulse: %d samples (%f / %d lines) location %d last %d, %s field, add %d samples\n", fieldDuration, fieldDuration/(float)DEFAULT_LINE_DURATION, (int)roundf(fieldDuration/(float)DEFAULT_LINE_DURATION), loc, lastLongSyncPulseLocation, nextFieldIsBottom ? "bottom" : "top", lastLongSyncPulseLocation+fieldDuration);
				
				VideoField *nextField=fieldPool.front();
				fieldPool.pop_front();
				assert(nextField->numSamples==0);
				if(lastLongSyncPulseLocation+fieldDuration<0){
					int samplesToMove=-(lastLongSyncPulseLocation+fieldDuration);
					//printf("moving %d samples\n", samplesToMove);
					int offset=currentField->numSamples-samplesToMove;
					nextField->appendSamples(currentField, offset, samplesToMove);
					currentField->numSamples-=samplesToMove;
				}else{
					int offset=std::max(0, lastLongSyncPulseLocation);
					int count=fieldDuration+std::min(lastLongSyncPulseLocation, 0);
					currentField->appendSamples(b, offset, count);
				}
				assert(currentField->numSamples==fieldDuration);
				for(int j=10;j<i;j++){
					if(syncPulseLocations[j].location>=lastLongSyncPulseLocation)
						currentFieldSyncPulses.push_back(syncPulseLocations[j].offset(-lastLongSyncPulseLocation));
				}
				vector<VideoLine> field=processField(currentField, currentFieldSyncPulses, syncLevel, blackLevel, visibleBrightnessRange, nextFieldIsBottom);
				currentField->numSamples=0;
				fieldPool.push_back(currentField);
				currentField=nextField;
				currentFieldSyncPulses.clear();
				
				nextFieldIsBottom=nextIsBottom;
				lastLongSyncPulseLocation+=fieldDuration;
				earliestNextLongSyncPulseLocation=loc+DEFAULT_LINE_DURATION*310;
				if(earliestNextLongSyncPulseLocation>=BUFFER_SIZE)
					break;
			}
		}
		
		int unprocessedSamplesRemain=BUFFER_SIZE-lastLongSyncPulseLocation;
		//printf("remaining: %d, last %d\n", unprocessedSamplesRemain, lastLongSyncPulseLocation);
		int nextFieldDuration;
		if(nextFieldIsBottom)
			nextFieldDuration=DEFAULT_LINE_DURATION*313;
		else
			nextFieldDuration=DEFAULT_LINE_DURATION*312;
		while(unprocessedSamplesRemain>=nextFieldDuration){
			int offset=std::max(0, lastLongSyncPulseLocation);
			int count=nextFieldDuration+std::min(lastLongSyncPulseLocation, 0);

			currentField->appendSamples(b, offset, count);

			assert(currentField->numSamples==nextFieldDuration);
			for(int j=10;j<syncPulseLocations.size();j++){
				if(syncPulseLocations[j].location>=std::min(BUFFER_SIZE, lastLongSyncPulseLocation+nextFieldDuration))
					break;
				if(syncPulseLocations[j].location>=lastLongSyncPulseLocation)
					currentFieldSyncPulses.push_back(syncPulseLocations[j].offset(-lastLongSyncPulseLocation));
			}
			vector<VideoLine> field=processField(currentField, currentFieldSyncPulses, syncLevel, blackLevel, visibleBrightnessRange, nextFieldIsBottom);
			currentField->numSamples=0;
			currentFieldSyncPulses.clear();

			lastLongSyncPulseLocation+=nextFieldDuration;
			earliestNextLongSyncPulseLocation=lastLongSyncPulseLocation+DEFAULT_LINE_DURATION*10;
			unprocessedSamplesRemain-=nextFieldDuration;
			//printf("desync, adding field, last location %d, remain %d, %s field\n", lastLongSyncPulseLocation, unprocessedSamplesRemain, nextFieldIsBottom ? "bottom" : "top");
			nextFieldIsBottom=!nextFieldIsBottom;
			if(nextFieldIsBottom)
				nextFieldDuration=DEFAULT_LINE_DURATION*313;
			else
				nextFieldDuration=DEFAULT_LINE_DURATION*312;
		}
		if(unprocessedSamplesRemain){
			int offset=BUFFER_SIZE-unprocessedSamplesRemain;
			currentField->appendSamples(b, offset, unprocessedSamplesRemain);

			for(int j=10;j<syncPulseLocations.size();j++){
				if(syncPulseLocations[j].location>=BUFFER_SIZE)
					break;
				if(syncPulseLocations[j].location>=lastLongSyncPulseLocation)
					currentFieldSyncPulses.push_back(syncPulseLocations[j].offset(-lastLongSyncPulseLocation));
			}
		}
		
		lastLongSyncPulseLocation-=BUFFER_SIZE;
		earliestNextLongSyncPulseLocation-=BUFFER_SIZE;
		
		//blockingSemaphore.Release();
		bufferCount++;
	}
	
	delete b;
	delete prevBuf;
	delete nextBuf;
	free(prevLineChroma);
	free(samples);
	free(nextSamples);
	for(VideoField* f:fieldQueue){
		delete f;
	}
	for(VideoField* f:fieldPool){
		delete f;
	}
}

VideoLine Decoder::joinLines(VideoLine a, VideoLine b){
	assert(a.luminance+a.numSamples==b.luminance);
	assert(a.chrominance[0]+a.numSamples==b.chrominance[0]);
	assert(a.filteredLuminance+a.numSamples==b.filteredLuminance);
	assert(a.raw+a.numSamples==b.raw);
	
	vector<SyncPulse> sync=a.sync;
	for(SyncPulse sp:b.sync){
		sync.push_back(sp.offset(a.numSamples));
	}
	//printf("join %d + %d -> %d\n", (int)a.luminance.size(), (int)b.luminance.size(), (int)luminance.size());
	return VideoLine(&a, 0, a.numSamples+b.numSamples, sync);
}

std::vector<VideoLine> Decoder::splitLine(VideoLine line, int numParts){
	float partLength=(float)line.numSamples/numParts;
	//printf("split into %d parts %f samples each\n", numParts, partLength);
	vector<VideoLine> parts;
	for(int i=0;i<numParts;i++){
		int offset=(int)(partLength*i);
		int length=i==numParts-1 ? (int)line.numSamples-offset : (int)partLength;
		assert(length>0);
		vector<SyncPulse> sync;
		for(SyncPulse sp:line.sync){
			if(sp.location>=offset && sp.location<offset+length)
				sync.push_back(sp);
		}
		parts.emplace_back(&line, offset, length, sync);
	}
	return parts;
}

vector<VideoLine> Decoder::processField(VideoField *field, std::vector<SyncPulse> sync, float syncLevel, float blackLevel, float visibleBrightnessRange, bool isBottom){
	SyncPulse nextSyncPulse=sync.size() ? sync[0] : SyncPulse{0, 0};
	int syncPulseIndex=0;
	VideoLine currentLine(field, 0, 0, {});
	vector<VideoLine> lines;
	int nextLineStartEarliestLocation=MIN_LINE_DURATION;
	float syncLevelSum=0, blackLevelSum=0;
	int syncLevelSampleCount=0, syncSamplesToAdd=0, blackLevelSampleCount=0, blackSamplesToAdd=0, blackSampleDelay=0;
	for(int i=0;i<field->numSamples;i++){
		currentLine.numSamples++;
		if(i==nextSyncPulse.location){
			if(nextSyncPulse.location>=nextLineStartEarliestLocation){
				lines.push_back(currentLine);
				currentLine=VideoLine(field, i+1, 0, {});
				nextLineStartEarliestLocation=nextSyncPulse.location+MIN_LINE_DURATION;
			}
			syncSamplesToAdd=nextSyncPulse.length;
			if(nextSyncPulse.length<=LINE_SYNC_MAX_DURATION){
				blackSamplesToAdd=40;
				blackSampleDelay=nextSyncPulse.length+15;
			}
			syncPulseIndex++;
			if(syncPulseIndex<sync.size())
				nextSyncPulse=sync[syncPulseIndex];
		}
		if(syncSamplesToAdd){
			syncLevelSum+=field->filteredLuminance[i];
			syncSamplesToAdd--;
			syncLevelSampleCount++;
		}
		if(blackSamplesToAdd){
			if(blackSampleDelay){
				blackSampleDelay--;
			}else{
				blackLevelSum+=field->filteredLuminance[i];
				blackSamplesToAdd--;
				blackLevelSampleCount++;
			}
		}
	}
	if(currentLine.numSamples>=MIN_LINE_DURATION){
		lines.push_back(currentLine);
	}
	
	int j=0;
	for(auto line=lines.begin();line!=lines.end() && j<313;line++, j++){
		if(line->numSamples>MAX_LINE_DURATION){
			//printf("line of length %d offset %d\n", (int)line->luminance.size(), line->offsetInBuffer);
			VideoLine joined=*line;
			vector<VideoLine>::iterator replaceBegin=line, replaceEnd=line+1;
			if(line!=lines.begin()){
				VideoLine prev=*(line-1);
				if(prev.numSamples>MAX_LINE_DURATION){
					joined=joinLines(prev, joined);
					replaceBegin=line-1;
				}
			}
			int numParts=(int)roundf(joined.numSamples/(float)DEFAULT_LINE_DURATION);
			if(numParts>1){
				vector<VideoLine> parts=splitLine(joined, numParts);
				line=lines.erase(replaceBegin, replaceEnd);
				line=lines.insert(line, parts.begin(), parts.end());
			}
		}
	}
	
	if(lines.size()>313){
		//printf("field too long with %d lines\n", (int)lines.size());
		lines.erase(lines.begin()+313, lines.end());
	}
	
	field->lines=lines;
	field->isBottom=isBottom;
	field->syncLevel=syncLevelSampleCount ? syncLevelSum/(float)syncLevelSampleCount : syncLevel;
	field->blackLevel=blackLevelSampleCount ? blackLevelSum/(float)blackLevelSampleCount : blackLevel;
	fieldQueue.push_back(field);
	
	if(fieldQueue.size()==4){
		bool allAreTop=true, allAreBottom=true;
		for(VideoField *field:fieldQueue){
			if(field->isBottom)
				allAreTop=false;
			else
				allAreBottom=false;
		}
		if(allAreTop){
			fieldQueue[1]->isBottom=true;
			fieldQueue[3]->isBottom=true;
		}else if(allAreBottom){
			fieldQueue[0]->isBottom=false;
			fieldQueue[2]->isBottom=false;
		}
		VideoField *field=fieldQueue.front();
		fieldQueue.pop_front();
		float defaultWhiteLevel=field->blackLevel+std::min((field->blackLevel-field->syncLevel)/whiteLevelRatio, 0.99f-field->blackLevel);
		float whiteLevel=fieldsWithoutVITS>5 ? defaultWhiteLevel : detectedWhiteLevel;
		fieldsWithoutVITS++;
		// Try to sample white level from VITS signals transmitted by most channels
		int vitsLineIndex=field->isBottom ? 17 : 16;
		if(field->lines.size()>vitsLineIndex){
			float sumForWhiteLevel=0;
			int sampleCount=0;
			VideoLine line=field->lines[vitsLineIndex];
			for(int k=120;k<line.numSamples;k++){
				if(line.filteredLuminance[k]>defaultWhiteLevel){
					sumForWhiteLevel+=line.filteredLuminance[k];
					sampleCount++;
				}
			}
			if(sampleCount>5){
				detectedWhiteLevel=whiteLevel=sumForWhiteLevel/(float)sampleCount;
				fieldsWithoutVITS=0;
			}
		}
		
		// Precisely align lines relative to each other by offsetting and interpolating them such that the edges of the sync pulses either end of the line
		// end up at exact known X coordinates in the framebuffer
		float leadingThreshold=field->syncLevel+(field->blackLevel-field->syncLevel)*0.2f;
		float trailingThreshold=field->syncLevel+(field->blackLevel-field->syncLevel)*0.6f;
		float lineLeadingOffsets[field->lines.size()], lineTrailingOffsets[field->lines.size()], lineTrailingAlignPositions[field->lines.size()];
		int lineLeadingAlignDestinations[field->lines.size()];
		for(int j=0;j<field->lines.size();j++){
			int lineIndex=j+(field->isBottom ? 312 : 0);
			if(lineIndex<625){
				VideoLine& line=field->lines[j];
				int numSamples=line.numSamples;

				float leadingOffset=0;
				float trailingOffset=0;
				float leadingAlign;
				float trailingAlign;
				int leadingAlignDest, trailingAlignDest;
				if((lineIndex>4 && lineIndex<310) || (lineIndex>317 && lineIndex<623)){
					// These lines contain field sync pulses, which are shorter than normal line sync
					leadingAlignDest=LINE_LONG_SYNC_DURATION-LINE_SYNC_WINDOW+15;
				}else{
					leadingAlignDest=LINE_SYNC_DURATION-LINE_SYNC_WINDOW+15;
				}
				leadingAlign=numSamples*(leadingAlignDest/(float)DEFAULT_LINE_DURATION);
				trailingAlignDest=DEFAULT_LINE_DURATION-12;
				trailingAlign=numSamples*(trailingAlignDest/(float)DEFAULT_LINE_DURATION);
				
				// Find the rising edge of the leading sync pulse, going right to left
				//      _______
				// ____/ <----
				for(int i=leadingAlign+15;i>std::max(1, (int)leadingAlign-30);i--){
					float prevSample=line.luminance[i-1];
					float curSample=line.luminance[i];
					if(curSample>leadingThreshold && prevSample<=leadingThreshold){
						leadingOffset=i-leadingAlign+(leadingThreshold-prevSample)/(curSample-prevSample);
						break;
					}
				}
				
				// Find the falling edge of the trailing sync pulse, going left to right
				// _____
				// ---> \____
				for(int i=trailingAlign-5;i<numSamples;i++){
					float prevSample=line.luminance[i-1];
					float curSample=line.luminance[i];
					if(curSample<trailingThreshold && prevSample>=trailingThreshold){
						trailingOffset=i-trailingAlign+(trailingThreshold-prevSample)/(curSample-prevSample);
						break;
					}
				}
				
				lineLeadingOffsets[j]=leadingOffset;
				lineTrailingOffsets[j]=trailingOffset;
				lineTrailingAlignPositions[j]=trailingAlign;
				lineLeadingAlignDestinations[j]=leadingAlignDest;
			}
		}
		
		for(int j=0;j<field->lines.size();j++){
			int lineIndex=j+(field->isBottom ? 312 : 0);
			if(lineIndex<625){
				interpolateLine(field->lines[j], interpolatedField->lines[j], lineLeadingOffsets[j], lineTrailingOffsets[j], lineLeadingAlignDestinations[j], lineTrailingAlignPositions[j]);
				processLine(interpolatedField->lines[j], lineIndex, field->syncLevel, field->blackLevel, whiteLevel);
			}
		}

		if(vbiDataCallback && field->lines.size()>32){
			uint8_t vbiData[DEFAULT_LINE_DURATION*16];
			int offset=0;
			for(int i=0;i<16;i++){
				VideoLine line=field->lines[(field->isBottom ? 14 : 13)+i];
				int lineLength=std::min(line.numSamples, DEFAULT_LINE_DURATION);
				for(int j=0;j<lineLength;j++){
					vbiData[offset+j]=(uint8_t)roundf(std::clamp((line.raw[j]-blackLevel)/(whiteLevel-blackLevel)*0.8f+0.2f, 0.0f, 1.0f)*255.0f);
				}
				offset+=DEFAULT_LINE_DURATION;
			}
			vbiDataCallback(vbiData, DEFAULT_LINE_DURATION*16);
		}
		
		if(field->isBottom && outputEnabled){
			outputCapturedFrame();
		}
		CFRunLoopSourceSignal(runLoopSource);
		CFRunLoopWakeUp(mainThreadRunLoop);
	}

	return lines;
}

void interpolateVector(float *src, float *indexes, int unused1, float *dst, int unused2, int dstLen, int srcLen){
	for(int x=0;x<dstLen;x++){
		float sampleIndex=indexes[x];
		float k=sampleIndex-(int)sampleIndex;
		
		float sample1Y=src[((int)sampleIndex)];
		float sample2Y=src[(int)sampleIndex+1];
		dst[x]=sample2Y*k+sample1Y*(1.0f-k);
	}
}

void Decoder::interpolateLine(VideoLine const& src, VideoLine const& dst, float leadingOffset, float trailingOffset, int leadingAlignDest, float trailingAlign){
	float interpolationIndexes[DEFAULT_LINE_DURATION];
	
	for(int x=0;x<DEFAULT_LINE_DURATION;x++){
		float sampleOffsetK=std::clamp((x-leadingAlignDest)/(float)trailingAlign, 0.0f, 1.0f);
		float sampleOffset=trailingOffset*sampleOffsetK+leadingOffset*(1.0f-sampleOffsetK);
		interpolationIndexes[x]=std::clamp(x/(float)DEFAULT_LINE_DURATION*src.numSamples+sampleOffset, 0.0f, (float)src.numSamples-1);
	}
	vDSP_vlint(src.luminance, interpolationIndexes, 1, dst.luminance, 1, DEFAULT_LINE_DURATION, src.numSamples);
	vDSP_vlint(src.chrominance[0], interpolationIndexes, 1, dst.chrominance[0], 1, DEFAULT_LINE_DURATION, src.numSamples);
	vDSP_vlint(src.chrominance[1], interpolationIndexes, 1, dst.chrominance[1], 1, DEFAULT_LINE_DURATION, src.numSamples);
	if(colorMode==ColorDisplayMode::Raw){
		vDSP_vlint(src.raw, interpolationIndexes, 1, dst.raw, 1, DEFAULT_LINE_DURATION, src.numSamples);
	}
}

void Decoder::processLine(VideoLine line, int lineIndex, float syncLevel, float blackLevel, float whiteLevel){
	int numSamples=line.numSamples;

	uint32_t* bitmapPixels=(uint32_t*)bitmapData;
	float visibleBrightnessRange=whiteLevel-blackLevel;
	
	uint32_t* bitmapLine;
	if(displayFieldsSequentially)
		bitmapLine=bitmapPixels+((lineIndex)*bitmapStride);
	else
		bitmapLine=bitmapPixels+((lineIndex*2%bitmapHeight)*bitmapStride);

	bool isRedLine=(lineIndex+colorLineOffset)%2==0;
	if(((lineIndex>=6 && lineIndex<15) || (lineIndex>=319 && lineIndex<328)) && numSamples>=MIN_LINE_DURATION){
		float centerSum=0;
		float min=10;
		float max=-10;
		for(int i=150;i<250;i++){
			centerSum+=line.chrominance[0][i];
			min=std::min(line.chrominance[0][i], min);
			max=std::max(line.chrominance[0][i], max);
		}
		float centerFreq=centerSum/100.0;
		float maxSum=0;
		for(int i=1000;i<1200;i++){
			maxSum+=line.chrominance[0][i];
		}
		float maxFreq=maxSum/200.0;
		float freqDiff=centerFreq-maxFreq;
		if(centerFreq>3500000 && centerFreq<4500000 && fabsf(freqDiff)>270000.0f){
			bool isActuallyRedLine=maxFreq>centerFreq;
			if(isActuallyRedLine!=isRedLine){
				colorLineOffset=colorLineOffset==1 ? 0 : 1;
				isRedLine=isActuallyRedLine;
			}
		}
	}
	float centerFreq=isRedLine ? redCenterFreq : blueCenterFreq;
	float maxDeviation=isRedLine ? redMaxDeviation : blueMaxDeviation;
	for(int i=0;i<DEFAULT_LINE_DURATION;i++){
		line.chrominance[0][i]=std::clamp((line.chrominance[0][i]-centerFreq)/maxDeviation, -1.0f, 1.0f);
	}

	if(lineIndex==scopeLineIndex){
		scopeData1.clear();
		for(int i=0;i<numSamples;i++){
			float v=line.luminance[i];
			scopeData1.push_back((float)v);
		}
		scopeData2.clear();
		for(int i=0;i<numSamples;i++){
			float v=line.chrominance[0][i];
			scopeData2.push_back((float)v/2+0.5);
		}
		scopeLines.clear();
		scopeLines.push_back(syncLevel);
		scopeLines.push_back(blackLevel);
		scopeLines.push_back(whiteLevel);
	}

	float* samplesForDisplay;
	switch(colorMode){
		case ColorDisplayMode::Full:
			samplesForDisplay=line.luminance;
			break;
		case ColorDisplayMode::Raw:
			samplesForDisplay=line.raw;
			break;
		case ColorDisplayMode::Y:
			samplesForDisplay=line.luminance;
			break;
		case ColorDisplayMode::Db:
			samplesForDisplay=isRedLine ? prevLineChroma : line.chrominance[0];
			break;
		case ColorDisplayMode::Dr:
			samplesForDisplay=isRedLine ? line.chrominance[0] : prevLineChroma;
			break;
	}
	float* crSamples=isRedLine ? line.chrominance[0] : prevLineChroma;
	float* cbSamples=isRedLine ? prevLineChroma : line.chrominance[0];

	for(int x=0;x<bitmapWidth;x++){
		float sampleIndex=std::clamp(x/(float)bitmapWidth*DEFAULT_LINE_DURATION, 0.0f, (float)DEFAULT_LINE_DURATION-2);
		float sample1=samplesForDisplay[((int)sampleIndex)];
		float sample2=samplesForDisplay[(int)sampleIndex+1];
		float k=sampleIndex-(int)sampleIndex;
		float interpolatedSample=sample2*k+sample1*(1.0f-k);
		
		if(colorMode==ColorDisplayMode::Full){
			float y=(interpolatedSample-blackLevel)/visibleBrightnessRange;
			float db1=cbSamples[(int)sampleIndex], db2=cbSamples[(int)sampleIndex+1];
			float db=db2*k+db1*(1.0f-k);
			float dr1=crSamples[(int)sampleIndex], dr2=crSamples[(int)sampleIndex+1];
			float dr=dr2*k+dr1*(1.0f-k);
			
			float r=std::clamp(y-(10*dr/19), 0.0f, 1.0f);
			float g=std::clamp(y+(2*(250*dr-209*db))/3363, 0.0f, 1.0f);
			float b=std::clamp(y+(2*db/3), 0.0f, 1.0f);
			
			int ri=std::round(r*255);
			int gi=std::round(g*255);
			int bi=std::round(b*255);
			bitmapLine[x]=ri | (gi << 8) | (bi << 16) | 0xff000000;
		}else{
			int pixelColor;
			
			if(colorMode==ColorDisplayMode::Dr || colorMode==ColorDisplayMode::Db){
				interpolatedSample=(interpolatedSample+1.0f)/2.0f;
				pixelColor=(int)(std::clamp(interpolatedSample, 0.0f, 1.0f)*255);
			}else{
				switch(levelsMode){
					case DisplayLevelsMode::Auto:
						pixelColor=(int)(std::clamp((interpolatedSample-blackLevel)/visibleBrightnessRange, 0.0f, 1.0f)*255);
						break;
					case DisplayLevelsMode::BlackIsSync:
						pixelColor=(int)(std::clamp((interpolatedSample-syncLevel)/(1.0f-syncLevel), 0.0f, 1.0f)*255);
						break;
					case DisplayLevelsMode::Raw:
						pixelColor=(int)(std::clamp(interpolatedSample, 0.0f, 1.0f)*255);
						break;
				}
			}
			bitmapLine[x]=pixelColor | (pixelColor << 8) | (pixelColor << 16) | 0xff000000;
		}
	}
	
	if(outputEnabled){
		int lineStartOffset=includeBlankingIntervalsInOutput ? 0 : 197;
		int lineLength=includeBlankingIntervalsInOutput ? 1280 : 1043;
		int outputWidth, outputHeight, yOffset;
		if(includeBlankingIntervalsInOutput){
			outputWidth=942;
			outputHeight=625;
			yOffset=0;
		}else{
			outputWidth=768;
			outputHeight=576;
			yOffset=44;
		}
		int y=lineIndex*2%625-yOffset;
		if(y>=0){
			uint32_t *outputLine=((uint32_t*)*currentOutputBuffer)+(y*outputWidth);
			for(int x=0;x<outputWidth;x++){
				float sampleIndex=std::clamp(x/(float)outputWidth*lineLength, 0.0f, (float)lineLength-2)+lineStartOffset;
				float sample1=line.luminance[((int)sampleIndex)];
				float sample2=line.luminance[(int)sampleIndex+1];
				float k=sampleIndex-(int)sampleIndex;
				float interpolatedSample=sample2*k+sample1*(1.0f-k);
				float y=(interpolatedSample-blackLevel)/visibleBrightnessRange;
				float db1=cbSamples[(int)sampleIndex], db2=cbSamples[(int)sampleIndex+1];
				float db=db2*k+db1*(1.0f-k);
				float dr1=crSamples[(int)sampleIndex], dr2=crSamples[(int)sampleIndex+1];
				float dr=dr2*k+dr1*(1.0f-k);
				
				float r=std::clamp(y-(10*dr/19), 0.0f, 1.0f);
				float g=std::clamp(y+(2*(250*dr-209*db))/3363, 0.0f, 1.0f);
				float b=std::clamp(y+(2*db/3), 0.0f, 1.0f);
				
				int ri=std::round(r*255);
				int gi=std::round(g*255);
				int bi=std::round(b*255);
				outputLine[x]=bi | (gi << 8) | (ri << 16) | 0xff000000;
			}
		}
	}
	
	memcpy(prevLineChroma, line.chrominance[0], sizeof(float)*DEFAULT_LINE_DURATION);
}

tgvoip::Buffer Decoder::getOutputFrame(){
	return outputQueue.GetBlocking();
}

void Decoder::outputCapturedFrame(){
	outputQueue.Put(std::move(currentOutputBuffer));
	currentOutputBuffer=outputBufferPool.Get();
}

void Decoder::startOutput(){
	outputEnabled=true;
}

void Decoder::stopOutput(){
	outputEnabled=false;
	outputQueue.Put(tgvoip::Buffer(0));
}

#pragma mark - Color decoder

/*

FIR filter designed with
http://t-filter.appspot.com

sampling frequency: 20000000 Hz

* 0 Hz - 3600000 Hz
  gain = 0
  desired attenuation = -70 dB
  actual attenuation = -70.02479785554213 dB

* 3800000 Hz - 4756000 Hz
  gain = 1
  desired ripple = 0.5 dB
  actual ripple = 0.370798682096415 dB

* 4856000 Hz - 10000000 Hz
  gain = 0
  desired attenuation = -35 dB
  actual attenuation = -35.02479785554213 dB

*/
Decoder::ColorDecoderSECAM::ColorDecoderSECAM():chromaSeparationFilter({
	-0.0018962302677754082, 0.003950611398639031, -0.0016534033029806199, -0.003176139586031761, 0.0032688460425483986, 0.00023006401209703413, 0.0001466177509746152, -0.002720364468382116, 0.001248882032462522,
	0.0009462714034337175, 0.0009271257887485181, -0.002225020994475672, -0.00011778529621897669, 0.0008405260756064897, 0.0013884642183898805, -0.0013928104113798444, -0.0007448142413401177, 0.000412736147720719,
	0.0013668300544331935, -0.0007200848812143278, -0.0008424277150040855, 0.00014021391178503195, 0.0011429102502775387, -0.00038134848307765535, -0.0007744005310222772, 0.00009875043053340458, 0.000953007997450536,
	-0.0003445847595856648, -0.0007898689406711629, 0.0002714129522061503, 0.001060810609933758, -0.0004549889922254634, -0.0012136701655465828, 0.0002922923884703703, 0.0016367036374398782, -0.00009135522872924137,
	-0.0018501615245101607, -0.00045670954901793075, 0.0019697572341126545, 0.000989384031270424, -0.0016881906806871178, -0.0014936567974165081, 0.001232953096690125, 0.0016288627954875548, -0.0006757988892053997,
	-0.0014688219816270456, 0.00038705831036749973, 0.0010785271155727913, -0.0004515544729362874, -0.0008324869411163227, 0.0008698395653131523, 0.0009610757084917677, -0.0013001949522536433, -0.0015840223522855757,
	0.0014002674557862514, 0.0024431123021287165, -0.0008943059995372198, -0.003140471073618629, -0.00010622051123299201, 0.0032463925492173897, 0.0012218997991766795, -0.002669674632214367, -0.0019165916637667335,
	0.0016922595916570762, 0.0018752950425931318, -0.0008852530212835082, -0.0012080680439562465, 0.0007636680175561146, 0.00044778847269262795, -0.001452338586096091, -0.00025498591060130513, 0.0025375713952437403,
	0.0010238492526771276, -0.0032834908370427815, -0.0025632560415515446, 0.00304025154604492, 0.004157420255077792, -0.001684745466480226, -0.004957270546977276, -0.00022281819131605532, 0.004497261659762502,
	0.0017213943963140896, -0.0030414304015213313, -0.0020288368024595216, 0.001519478724198008, 0.001046546264443793, -0.0009847914266443462, 0.0004800607341402902, 0.001953761040046366, -0.0013499360957387203,
	-0.003994593632054771, 0.0006190968540959582, 0.005903909019727752, 0.0017373411288900554, -0.006373954810265053, -0.004707116407092756, 0.004834491411142638, 0.006757625279153752, -0.0019016367756048808,
	-0.0067655477969968195, -0.0008755446145923028, 0.0047855522386597935, 0.0019066964734936332, -0.002151338390381147, -0.000584346064724723, 0.0007621817212805526, -0.0022047264826829185, -0.0019179719445728259,
	0.004471214959359502, 0.005400902916533009, -0.004277770041999389, -0.009419934679447595, 0.0009943849588642246, 0.011573753643001863, 0.004122537482488149, -0.010312527992440776, -0.00850741424524085,
	0.006046396849519402, 0.00976787009914436, -0.0011260836939139918, -0.007249639350728203, -0.0014272942358176106, 0.002687521291619149, -0.00021262041903212696, 0.0005948208722361291, 0.005327259801520477,
	0.00038687792380681153, -0.010745077796730474, -0.006302023055722296, 0.012525438232329688, 0.014719086720462772, -0.008364586129589308, -0.02119186253485477, -0.0006682655398852609, 0.021783275659938532,
	0.010282731353404639, -0.01567916818030248, -0.01516930282560849, 0.006247906330828493, 0.012242486207311375, 0.00031213767766190227, -0.003050458557851076, 0.0016401918257577138, -0.006176014163439691,
	-0.01353555938237824, 0.007317930452123534, 0.030709564040704622, 0.005233417163064666, -0.04370919810640573, -0.03076576071322528, 0.04247569947629088, 0.06127065361348215, -0.021571678750758626,
	-0.08414411395896411, -0.016252627523119876, 0.0876267512079551, 0.060131027170092645, -0.06643052667595725, -0.09511521939154348, 0.02478114232379585, 0.10844107113109855, 0.02478114232379585,
	-0.09511521939154348, -0.06643052667595725, 0.060131027170092645, 0.0876267512079551, -0.016252627523119876, -0.08414411395896411, -0.021571678750758626, 0.06127065361348215, 0.04247569947629088,
	-0.03076576071322528, -0.04370919810640573, 0.005233417163064666, 0.030709564040704622, 0.007317930452123534, -0.01353555938237824, -0.006176014163439691, 0.0016401918257577138, -0.003050458557851076,
	0.00031213767766190227, 0.012242486207311375, 0.006247906330828493, -0.01516930282560849, -0.01567916818030248, 0.010282731353404639, 0.021783275659938532, -0.0006682655398852609, -0.02119186253485477,
	-0.008364586129589308, 0.014719086720462772, 0.012525438232329688, -0.006302023055722296, -0.010745077796730474, 0.00038687792380681153, 0.005327259801520477, 0.0005948208722361291, -0.00021262041903212696,
	0.002687521291619149, -0.0014272942358176106, -0.007249639350728203, -0.0011260836939139918, 0.00976787009914436, 0.006046396849519402, -0.00850741424524085, -0.010312527992440776, 0.004122537482488149,
	0.011573753643001863, 0.0009943849588642246, -0.009419934679447595, -0.004277770041999389, 0.005400902916533009, 0.004471214959359502, -0.0019179719445728259, -0.0022047264826829185, 0.0007621817212805526,
	-0.000584346064724723, -0.002151338390381147, 0.0019066964734936332, 0.0047855522386597935, -0.0008755446145923028, -0.0067655477969968195, -0.0019016367756048808, 0.006757625279153752, 0.004834491411142638,
	-0.004707116407092756, -0.006373954810265053, 0.0017373411288900554, 0.005903909019727752, 0.0006190968540959582, -0.003994593632054771, -0.0013499360957387203, 0.001953761040046366, 0.0004800607341402902,
	-0.0009847914266443462, 0.001046546264443793, 0.001519478724198008, -0.0020288368024595216, -0.0030414304015213313, 0.0017213943963140896, 0.004497261659762502, -0.00022281819131605532, -0.004957270546977276,
	-0.001684745466480226, 0.004157420255077792, 0.00304025154604492, -0.0025632560415515446, -0.0032834908370427815, 0.0010238492526771276, 0.0025375713952437403, -0.00025498591060130513, -0.001452338586096091,
	0.00044778847269262795, 0.0007636680175561146, -0.0012080680439562465, -0.0008852530212835082, 0.0018752950425931318, 0.0016922595916570762, -0.0019165916637667335, -0.002669674632214367, 0.0012218997991766795,
	0.0032463925492173897, -0.00010622051123299201, -0.003140471073618629, -0.0008943059995372198, 0.0024431123021287165, 0.0014002674557862514, -0.0015840223522855757, -0.0013001949522536433, 0.0009610757084917677,
	0.0008698395653131523, -0.0008324869411163227, -0.0004515544729362874, 0.0010785271155727913, 0.00038705831036749973, -0.0014688219816270456, -0.0006757988892053997, 0.0016288627954875548, 0.001232953096690125,
	-0.0014936567974165081, -0.0016881906806871178, 0.000989384031270424, 0.0019697572341126545, -0.00045670954901793075, -0.0018501615245101607, -0.00009135522872924137, 0.0016367036374398782, 0.0002922923884703703,
	-0.0012136701655465828, -0.0004549889922254634, 0.001060810609933758, 0.0002714129522061503, -0.0007898689406711629, -0.0003445847595856648, 0.000953007997450536, 0.00009875043053340458, -0.0007744005310222772,
	-0.00038134848307765535, 0.0011429102502775387, 0.00014021391178503195, -0.0008424277150040855, -0.0007200848812143278, 0.0013668300544331935, 0.000412736147720719, -0.0007448142413401177, -0.0013928104113798444,
	0.0013884642183898805, 0.0008405260756064897, -0.00011778529621897669, -0.002225020994475672, 0.0009271257887485181, 0.0009462714034337175, 0.001248882032462522, -0.002720364468382116, 0.0001466177509746152,
	0.00023006401209703413, 0.0032688460425483986, -0.003176139586031761, -0.0016534033029806199, 0.003950611398639031, -0.0018962302677754082
}),
	chromaDeemphasisFilter(-0.973646758810919, 0, 0.01317662059454045, 0.01317662059454045, 0),
	hilbertTransform(19){
	const int filterSize=chromaSeparationFilter.getSize();
	samples=(float*)calloc(BUFFER_SIZE+filterSize*2, sizeof(float));
	subcarrier=(float*)calloc(BUFFER_SIZE+filterSize, sizeof(float));
}

Decoder::ColorDecoderSECAM::~ColorDecoderSECAM(){
	free(samples);
	free(subcarrier);
}
	
float *Decoder::ColorDecoderSECAM::separateSubcarrier(float *rawSignal, float *nextBuffer){
	const int filterSize=chromaSeparationFilter.getSize();
	const int filterDelay=chromaSeparationFilter.getDelay();
	memcpy(samples, samples+BUFFER_SIZE, filterSize*sizeof(float));
	memcpy(samples+filterDelay+BUFFER_SIZE, nextBuffer, filterDelay*sizeof(float));
	memcpy(samples+filterDelay, rawSignal, BUFFER_SIZE*sizeof(float));
	chromaSeparationFilter.process(samples, subcarrier, BUFFER_SIZE+filterDelay);
	return subcarrier;
}

void Decoder::ColorDecoderSECAM::demodulateSubcarrier(float *samples, SignalBuffers *buf){
	hilbertTransform.process(samples, buf->chrominance[1]);
	float gain=20000000.0f/(2*(float)M_PI);
	for(int i=0;i<BUFFER_SIZE-1;i++){
		float a=samples[i];
		float b=-buf->chrominance[1][i]; // conj
		float c=samples[i+1];
		float d=buf->chrominance[1][i+1];

		// complex multiply
		float re=a*c-b*d;
		float im=b*c+a*d;

		float angle=atan2f(im, re);
		buf->chrominance[0][i]=std::clamp(angle*gain, 3400000.0f, 4600000.0f);
	}
	chromaDeemphasisFilter.process(buf->chrominance[0], buf->chrominance[0], BUFFER_SIZE);
	chromaLowpass.process(buf->chrominance[0], buf->chrominance[0], BUFFER_SIZE);
}
