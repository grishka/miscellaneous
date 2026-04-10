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

Decoder::Decoder(void* bitmapData, unsigned int bitmapWidth, unsigned int bitmapHeight, unsigned int bitmapStride, CFRunLoopSourceRef runLoopSource) :bitmapData(bitmapData), bitmapWidth(bitmapWidth), bitmapHeight(bitmapHeight), bitmapStride(bitmapStride/4), decoderThread(std::bind(&Decoder::runDecoderThread, this)), runLoopSource(runLoopSource){
	
	chromaDeemphasisFilter=new BiquadFilter(-1.09184352, 0.12286846, 0.58102424, -1.09184352, 0.54184422);
	chromaLowpassFilter=new BiquadFilter(-1.47543442, 0.58687001, 0.02785890, 0.05571779, 0.02785890);
	colorDecoder=new ColorDecoderSECAM();
	
	currentOutputBuffer=outputBufferPool.Get();
	mainThreadRunLoop=CFRunLoopGetMain();

	decoderThread.SetName("Decoder");
	decoderThread.Start();
}

Decoder::~Decoder(){
	delete colorDecoder;
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
	
	float *luminance=(float*)calloc(BUFFER_SIZE, sizeof(float));
	float *chrominance=(float*)calloc(BUFFER_SIZE, sizeof(float));
	float *prevLuminance=(float*)calloc(BUFFER_SIZE, sizeof(float));
	float *prevChrominance=(float*)calloc(BUFFER_SIZE, sizeof(float));
	float *nextLuminance=(float*)calloc(BUFFER_SIZE, sizeof(float));
	float *nextChrominance=(float*)calloc(BUFFER_SIZE, sizeof(float));
	float *raw=(float*)calloc(BUFFER_SIZE, sizeof(float));

	float *luminanceForSync=(float*)calloc(BUFFER_SIZE, sizeof(float));
	float *nextLuminanceForSync=(float*)calloc(BUFFER_SIZE, sizeof(float));
	
	for(int i=0;i<6;i++){
		VideoField field;
		int size=MAX_LINE_DURATION*625;
		field.luminance=(float*)calloc(size, sizeof(float));
		field.filteredLuminance=(float*)calloc(size, sizeof(float));
		field.chrominance=(float*)calloc(size, sizeof(float));
		field.raw=(float*)calloc(size, sizeof(float));
		field.numSamples=0;
		fieldPool.push_back(field);
	}
	
	VideoField currentField=fieldPool.front();
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
		
		float *tmp=prevLuminance;
		prevLuminance=luminance;
		luminance=nextLuminance;
		nextLuminance=tmp;
		
		tmp=prevChrominance;
		prevChrominance=chrominance;
		chrominance=nextChrominance;
		nextChrominance=tmp;
		
		tmp=nextLuminanceForSync;
		nextLuminanceForSync=luminanceForSync;
		luminanceForSync=tmp;
		
		tmp=nextSamples;
		nextSamples=samples;
		samples=tmp;
		
		memcpy(raw, nextSamples, BUFFER_SIZE*sizeof(float));
		
		for(int i=0;i<BUFFER_SIZE;i++){
			int32_t sampleInt=(int32_t)buf[i];
			nextSamples[i]=sampleInt/255.0;
		}
		
		float *subcarrier=colorDecoder->separateSubcarrier(samples, nextSamples);
		for(int i=0;i<BUFFER_SIZE;i++){
			nextLuminance[i]=samples[i]-subcarrier[i];
		}
		
		syncLowpass.process(nextLuminance, nextLuminanceForSync, BUFFER_SIZE);
		luminanceLowpass.process(nextLuminance, nextLuminance, BUFFER_SIZE);

		demodulateColorSubcarrier(subcarrier, nextChrominance, BUFFER_SIZE);

		float minLevels[128];
		for(int i=0;i<128;i++)
			minLevels[i]=1;
		for(int i=0;i<BUFFER_SIZE;i++){
			float sample=luminance[i];
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
			float s=luminance[i];
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
			float s=luminance[i];
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
				s=luminanceForSync[i];
			else
				s=nextLuminanceForSync[i-BUFFER_SIZE];
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
				
				VideoField nextField=fieldPool.front();
				fieldPool.pop_front();
				assert(nextField.numSamples==0);
				if(lastLongSyncPulseLocation+fieldDuration<0){
					int samplesToMove=-(lastLongSyncPulseLocation+fieldDuration);
					//printf("moving %d samples\n", samplesToMove);
					int offset=currentField.numSamples-samplesToMove;
					memcpy(nextField.luminance, currentField.luminance+offset, samplesToMove*sizeof(float));
					memcpy(nextField.chrominance, currentField.chrominance+offset, samplesToMove*sizeof(float));
					memcpy(nextField.filteredLuminance, currentField.filteredLuminance+offset, samplesToMove*sizeof(float));
					memcpy(nextField.raw, currentField.raw+offset, samplesToMove*sizeof(float));

					currentField.numSamples-=samplesToMove;
					nextField.numSamples=samplesToMove;
				}else{
					int offset=std::max(0, lastLongSyncPulseLocation);
					int count=fieldDuration+std::min(lastLongSyncPulseLocation, 0);
					
					memcpy(currentField.luminance+currentField.numSamples, luminance+offset, count*sizeof(float));
					memcpy(currentField.chrominance+currentField.numSamples, chrominance+offset, count*sizeof(float));
					memcpy(currentField.filteredLuminance+currentField.numSamples, luminanceForSync+offset, count*sizeof(float));
					memcpy(currentField.raw+currentField.numSamples, raw+offset, count*sizeof(float));

					currentField.numSamples+=count;
				}
				assert(currentField.numSamples==fieldDuration);
				for(int j=10;j<i;j++){
					if(syncPulseLocations[j].location>=lastLongSyncPulseLocation)
						currentFieldSyncPulses.push_back(syncPulseLocations[j].offset(-lastLongSyncPulseLocation));
				}
				vector<VideoLine> field=processField(currentField, currentFieldSyncPulses, syncLevel, blackLevel, visibleBrightnessRange, nextFieldIsBottom);
				currentField.numSamples=0;
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

			memcpy(currentField.luminance+currentField.numSamples, luminance+offset, count*sizeof(float));
			memcpy(currentField.chrominance+currentField.numSamples, chrominance+offset, count*sizeof(float));
			memcpy(currentField.filteredLuminance+currentField.numSamples, luminanceForSync+offset, count*sizeof(float));
			memcpy(currentField.raw+currentField.numSamples, raw+offset, count*sizeof(float));
			currentField.numSamples+=count;

			assert(currentField.numSamples==nextFieldDuration);
			for(int j=10;j<syncPulseLocations.size();j++){
				if(syncPulseLocations[j].location>=std::min(BUFFER_SIZE, lastLongSyncPulseLocation+nextFieldDuration))
					break;
				if(syncPulseLocations[j].location>=lastLongSyncPulseLocation)
					currentFieldSyncPulses.push_back(syncPulseLocations[j].offset(-lastLongSyncPulseLocation));
			}
			vector<VideoLine> field=processField(currentField, currentFieldSyncPulses, syncLevel, blackLevel, visibleBrightnessRange, nextFieldIsBottom);
			currentField.numSamples=0;
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
			memcpy(currentField.luminance+currentField.numSamples, luminance+offset, unprocessedSamplesRemain*sizeof(float));
			memcpy(currentField.chrominance+currentField.numSamples, chrominance+offset, unprocessedSamplesRemain*sizeof(float));
			memcpy(currentField.filteredLuminance+currentField.numSamples, luminanceForSync+offset, unprocessedSamplesRemain*sizeof(float));
			memcpy(currentField.raw+currentField.numSamples, raw+offset, unprocessedSamplesRemain*sizeof(float));
			currentField.numSamples+=unprocessedSamplesRemain;

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
	
	free(luminance);
	free(chrominance);
	free(nextLuminance);
	free(nextChrominance);
	free(prevLuminance);
	free(prevChrominance);
	free(luminanceForSync);
	free(nextLuminanceForSync);
	free(prevLineChroma);
	free(samples);
	free(nextSamples);
	for(VideoField& f:fieldQueue){
		free(f.luminance);
		free(f.chrominance);
		free(f.filteredLuminance);
		free(f.raw);
	}
	for(VideoField& f:fieldPool){
		free(f.luminance);
		free(f.chrominance);
		free(f.filteredLuminance);
		free(f.raw);
	}
}

Decoder::VideoLine Decoder::joinLines(VideoLine a, VideoLine b){
	assert(a.luminance+a.numSamples==b.luminance);
	assert(a.chrominance+a.numSamples==b.chrominance);
	assert(a.filteredLuminance+a.numSamples==b.filteredLuminance);
	assert(a.raw+a.numSamples==b.raw);
	
	vector<SyncPulse> sync=a.sync;
	for(SyncPulse sp:b.sync){
		sync.push_back(sp.offset(a.numSamples));
	}
	//printf("join %d + %d -> %d\n", (int)a.luminance.size(), (int)b.luminance.size(), (int)luminance.size());
	return VideoLine{
		.luminance=a.luminance,
		.chrominance=a.chrominance,
		.filteredLuminance=a.filteredLuminance,
		.raw=a.raw,
		.sync=sync,
		.numSamples=a.numSamples+b.numSamples
	};
}

std::vector<Decoder::VideoLine> Decoder::splitLine(VideoLine line, int numParts){
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
		VideoLine part={
			.luminance=line.luminance+offset,
			.chrominance=line.chrominance+offset,
			.filteredLuminance=line.filteredLuminance+offset,
			.raw=line.raw+offset,
			.sync=sync,
			.numSamples=length
		};
		parts.push_back(part);
	}
	return parts;
}

void Decoder::demodulateColorSubcarrier(float *samples, float *chrominance, int count){
	float lastZeroCrossingLocation=this->lastZeroCrossingLocation;
	float lastUsedZeroCrossingLocation=this->lastUsedZeroCrossingLocation;
	float lastChromaValue=this->lastChromaValue;
	for(int x=0;x<count-1;x++){
		float s1=samples[x];
		float s2=samples[x+1];
		if((s1>0 && s2<0)){
			float k=-s1/(s2-s1);
			k=std::clamp(k, 0.0f, 1.0f);
			float location=x+k;
			float diff=location-lastZeroCrossingLocation;
			if(diff<6 && diff>2){
				float chromaValue=1/diff;
				for(int i=std::max((int)lastUsedZeroCrossingLocation, 0);i<(int)location;i++){
					float k=(i-(int)lastUsedZeroCrossingLocation)/(location-lastUsedZeroCrossingLocation);
					chrominance[i]=lastChromaValue*(1.0-k)+chromaValue*k;
				}
				lastUsedZeroCrossingLocation=location;
				lastChromaValue=chromaValue;
			}
			lastZeroCrossingLocation=location;
		}
	}
	
	this->lastZeroCrossingLocation=lastZeroCrossingLocation-count;
	this->lastUsedZeroCrossingLocation=lastUsedZeroCrossingLocation-count;
	this->lastChromaValue=lastChromaValue;
	
	chromaDeemphasisFilter->process(chrominance, chrominance, count);
}

vector<Decoder::VideoLine> Decoder::processField(VideoField field, std::vector<SyncPulse> sync, float syncLevel, float blackLevel, float visibleBrightnessRange, bool isBottom){
	SyncPulse nextSyncPulse=sync.size() ? sync[0] : SyncPulse{0, 0};
	int syncPulseIndex=0;
	VideoLine currentLine={
		.luminance=field.luminance,
		.chrominance=field.chrominance,
		.filteredLuminance=field.filteredLuminance,
		.raw=field.raw,
		.sync=vector<SyncPulse>(),
		.offsetInBuffer=0,
		.numSamples=0
	};
	vector<VideoLine> lines;
	int nextLineStartEarliestLocation=MIN_LINE_DURATION;
	float syncLevelSum=0, blackLevelSum=0;
	int syncLevelSampleCount=0, syncSamplesToAdd=0, blackLevelSampleCount=0, blackSamplesToAdd=0, blackSampleDelay=0;
	for(int i=0;i<field.numSamples;i++){
		currentLine.numSamples++;
		if(i==nextSyncPulse.location){
			if(nextSyncPulse.location>=nextLineStartEarliestLocation){
				lines.push_back(currentLine);
				currentLine={
					.luminance=field.luminance+i+1,
					.chrominance=field.chrominance+i+1,
					.filteredLuminance=field.filteredLuminance+i+1,
					.raw=field.raw+i+1,
					.sync=vector<SyncPulse>(),
					.offsetInBuffer=0,
					.numSamples=0
				};
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
			syncLevelSum+=field.filteredLuminance[i];
			syncSamplesToAdd--;
			syncLevelSampleCount++;
		}
		if(blackSamplesToAdd){
			if(blackSampleDelay){
				blackSampleDelay--;
			}else{
				blackLevelSum+=field.filteredLuminance[i];
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
	
	fieldQueue.push_back(VideoField{
		.lines=lines,
		.isBottom=isBottom,
		.syncLevel=syncLevelSampleCount ? syncLevelSum/(float)syncLevelSampleCount : syncLevel,
		.blackLevel=blackLevelSampleCount ? blackLevelSum/(float)blackLevelSampleCount : blackLevel
	});
	
	if(fieldQueue.size()==4){
		bool allAreTop=true, allAreBottom=true;
		for(VideoField field:fieldQueue){
			if(field.isBottom)
				allAreTop=false;
			else
				allAreBottom=false;
		}
		if(allAreTop){
			fieldQueue[1].isBottom=true;
			fieldQueue[3].isBottom=true;
		}else if(allAreBottom){
			fieldQueue[0].isBottom=false;
			fieldQueue[2].isBottom=false;
		}
		VideoField field=fieldQueue.front();
		fieldQueue.pop_front();
		float defaultWhiteLevel=field.blackLevel+std::min((field.blackLevel-field.syncLevel)/whiteLevelRatio, 0.99f-field.blackLevel);
		float whiteLevel=fieldsWithoutVITS>5 ? defaultWhiteLevel : detectedWhiteLevel;
		fieldsWithoutVITS++;
		for(int j=0;j<field.lines.size();j++){
			int lineIndex=j+(field.isBottom ? 312 : 0);
			if(lineIndex==16 || lineIndex==329){ // Try to sample white level from VITS signals transmitted by most channels
				float sumForWhiteLevel=0;
				int sampleCount=0;
				VideoLine line=field.lines[j];
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
			if(lineIndex<625){
				processLine(field.lines[j], lineIndex, field.syncLevel, field.blackLevel, whiteLevel);
			}
		}
		if(vbiDataCallback && field.lines.size()>32){
			uint8_t vbiData[DEFAULT_LINE_DURATION*16];
			int offset=0;
			for(int i=0;i<16;i++){
				VideoLine line=field.lines[(field.isBottom ? 14 : 13)+i];
				int lineLength=std::min(line.numSamples, DEFAULT_LINE_DURATION);
				for(int j=0;j<lineLength;j++){
					vbiData[offset+j]=(uint8_t)roundf(std::clamp((line.raw[j]-blackLevel)/(whiteLevel-blackLevel)*0.8f+0.2f, 0.0f, 1.0f)*255.0f);
				}
				offset+=DEFAULT_LINE_DURATION;
			}
			vbiDataCallback(vbiData, DEFAULT_LINE_DURATION*16);
		}
		if(field.isBottom && outputEnabled){
			outputCapturedFrame();
		}
		CFRunLoopSourceSignal(runLoopSource);
		CFRunLoopWakeUp(mainThreadRunLoop);
	}

	return lines;
}

void Decoder::processLine(Decoder::VideoLine line, int lineIndex, float syncLevel, float blackLevel, float whiteLevel){
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
			centerSum+=line.chrominance[i];
			min=std::min(line.chrominance[i], min);
			max=std::max(line.chrominance[i], max);
		}
		float centerFreq=centerSum/100.0;
		float maxSum=0;
		for(int i=1000;i<1200;i++){
			maxSum+=line.chrominance[i];
		}
		float maxFreq=maxSum/200.0;
		float freqDiff=centerFreq-maxFreq;
		if(centerFreq>0.175 && centerFreq<0.225 && fabsf(freqDiff)>270000.0f/20000000.0f){
			bool isActuallyRedLine=maxFreq>centerFreq;
			if(isActuallyRedLine!=isRedLine){
				colorLineOffset=colorLineOffset==1 ? 0 : 1;
				isRedLine=isActuallyRedLine;
			}
		}
	}
	float centerFreq=isRedLine ? redCenterFreq : blueCenterFreq;
	float maxDeviation=isRedLine ? redMaxDeviation : blueMaxDeviation;
	for(int i=0;i<numSamples;i++){
		line.chrominance[i]=std::clamp((line.chrominance[i]-centerFreq)/maxDeviation, -1.0f, 1.0f);
	}
	chromaLowpassFilter->process(line.chrominance, line.chrominance, numSamples);

	// Precisely align lines relative to each other by offsetting and interpolating them such that the edges of the sync pulses either end of the line
	// end up at exact known X coordinates in the framebuffer
	float leadingOffset=0;
	float trailingOffset=0;
	float leadingAlign;
	float trailingAlign;
	float leadingThreshold=syncLevel+(blackLevel-syncLevel)*0.2f;
	float trailingThreshold=syncLevel+(blackLevel-syncLevel)*0.7f;
	int leadingAlignDest, trailingAlignDest;
	if((lineIndex>4 && lineIndex<310) || (lineIndex>317 && lineIndex<623)){
		// These lines contain field sync pulses, which are shorter than normal line sync
		leadingAlignDest=LINE_LONG_SYNC_DURATION-LINE_SYNC_WINDOW+15;
		leadingAlign=numSamples*(leadingAlignDest/(float)DEFAULT_LINE_DURATION);
	}else{
		leadingAlignDest=LINE_SYNC_DURATION-LINE_SYNC_WINDOW+15;
		leadingAlign=numSamples*(leadingAlignDest/(float)DEFAULT_LINE_DURATION);
	}
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
	
	float interpolatedLuminance[DEFAULT_LINE_DURATION];
	float interpolatedChrominance[DEFAULT_LINE_DURATION];
	
	for(int x=0;x<DEFAULT_LINE_DURATION;x++){
		float sampleOffsetK=std::clamp((x-leadingAlignDest)/(float)trailingAlign, 0.0f, 1.0f);
		float sampleOffset=trailingOffset*sampleOffsetK+leadingOffset*(1.0f-sampleOffsetK);
		float sampleIndex=std::clamp(x/(float)DEFAULT_LINE_DURATION*numSamples+sampleOffset, 0.0f, (float)numSamples-2);
		float k=sampleIndex-(int)sampleIndex;

		float sample1Y=line.luminance[((int)sampleIndex)];
		float sample2Y=line.luminance[(int)sampleIndex+1];
		interpolatedLuminance[x]=sample2Y*k+sample1Y*(1.0f-k);
		
		float sample1C=line.chrominance[((int)sampleIndex)];
		float sample2C=line.chrominance[(int)sampleIndex+1];
		interpolatedChrominance[x]=sample2C*k+sample1C*(1.0f-k);
	}
	
	if(colorMode==ColorDisplayMode::Raw && numSamples<DEFAULT_LINE_DURATION){
		for(int i=numSamples;i<DEFAULT_LINE_DURATION;i++){
			line.raw[i]=0;
		}
	}

	if(lineIndex==scopeLineIndex){
		scopeData1.clear();
		for(int i=0;i<numSamples;i++){
			float v=line.luminance[i];
			scopeData1.push_back((float)v);
		}
		scopeData2.clear();
		for(int i=0;i<numSamples;i++){
			float v=line.chrominance[i];
			scopeData2.push_back((float)v/2+0.5);
		}
		scopeLines.clear();
		scopeLines.push_back(syncLevel);
		scopeLines.push_back(blackLevel);
		scopeLines.push_back(whiteLevel);
	}

	float* samplesForDisplay;
	int numSamplesForDisplay;
	switch(colorMode){
		case ColorDisplayMode::Full:
			samplesForDisplay=interpolatedLuminance;
			numSamplesForDisplay=DEFAULT_LINE_DURATION;
			break;
		case ColorDisplayMode::Raw:
			samplesForDisplay=line.raw;
			numSamplesForDisplay=numSamples;
			break;
		case ColorDisplayMode::Y:
			samplesForDisplay=interpolatedLuminance;
			numSamplesForDisplay=DEFAULT_LINE_DURATION;
			break;
		case ColorDisplayMode::Db:
			samplesForDisplay=isRedLine ? prevLineChroma : interpolatedChrominance;
			numSamplesForDisplay=DEFAULT_LINE_DURATION;
			break;
		case ColorDisplayMode::Dr:
			samplesForDisplay=isRedLine ? interpolatedChrominance : prevLineChroma;
			numSamplesForDisplay=DEFAULT_LINE_DURATION;
			break;
	}
	float* crSamples=isRedLine ? interpolatedChrominance : prevLineChroma;
	float* cbSamples=isRedLine ? prevLineChroma : interpolatedChrominance;

	for(int x=0;x<bitmapWidth;x++){
		float sampleIndex=std::clamp(x/(float)bitmapWidth*numSamplesForDisplay, 0.0f, (float)numSamplesForDisplay-2);
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
				float sample1=interpolatedLuminance[((int)sampleIndex)];
				float sample2=interpolatedLuminance[(int)sampleIndex+1];
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
	
	memcpy(prevLineChroma, interpolatedChrominance, sizeof(float)*DEFAULT_LINE_DURATION);
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

* 0 Hz - 3700000 Hz
  gain = 0
  desired attenuation = -35 dB
  actual attenuation = -35.411533154436164 dB

* 3800000 Hz - 4756000 Hz
  gain = 1
  desired ripple = 0.5 dB
  actual ripple = 0.35464659718176483 dB

* 4856000 Hz - 10000000 Hz
  gain = 0
  desired attenuation = -35 dB
  actual attenuation = -35.411533154436164 dB

*/
Decoder::ColorDecoderSECAM::ColorDecoderSECAM():chromaSeparationFilter({
	-0.006452105894518418, 0.003983026474459068, 0.003429473494516816, 0.0022285247760404935, 0.0011853741115800448, 0.0009548680892419512, 0.0008300048915853866, 0.000420774478514697, 0.0005146661530813266, 0.0010436699740755866,
	0.0005280708198815686, -0.000956768070512709, -0.0009594522556201064, 0.00113368338293867, 0.002048622083391921, -0.000270887639715945, -0.002485159435458123, -0.0008068958794424991, 0.0023869658735495244, 0.0019661323295239,
	-0.0015007134485464705, -0.002530829215458603, 0.0003145817946935235, 0.002342386308112988, 0.0006791597135050718, -0.0015125455786355033, -0.0009979058848191866, 0.0005574964481650076, 0.0005657420083707418, -0.00004281513953790099,
	0.00027174830997308923, 0.0002751911618193786, -0.000935395563452344, -0.0011274083918726687, 0.0009266463396891097, 0.0020792764245218132, -0.00016322625554340134, -0.0025313724868107432, -0.000993819164841841, 0.0021635244595125205,
	0.001902510147331796, -0.0011522641080418641, -0.00204855009083113, 0.00008542056897488325, 0.0013663672927468175, 0.0003746401268217804, -0.0003104377397048547, 0.00010456438831586011, -0.0003755197175695685, -0.001278431694707578,
	0.00010859533627237019, 0.0024343968436937903, 0.0011661328775996423, -0.002778904046875478, -0.0028850341956466073, 0.001921559161627858, 0.0041577843530587804, -0.00014312170015328787, -0.004275809408563589, -0.0017216486628049753,
	0.0031441463885070794, 0.002732507979365003, -0.001393304300506093, -0.00241142865927784, 0.00003759603702705271, 0.0010472220633574577, 0.00010132548858608131, 0.00042462395545289237, 0.0010973240457947447, -0.0009418053763149555,
	-0.0029303733259454195, -0.00003923071426521127, 0.004247060351054372, 0.0021932087332278624, -0.004108213864284433, -0.0044473696801485794, 0.0023781907591731728, 0.005587382444676615, 0.00014242678166029782, -0.004983212007292624,
	-0.0021362973650225197, 0.0030029051849081827, 0.002524964970093736, -0.0008739045588965111, -0.0011546924818686593, -0.000015116280357111925, -0.0010512919194413438, -0.0010578083944437914, 0.0025678854769133706, 0.003654885829314124,
	-0.002143772286197777, -0.006338843167347792, -0.0004010381602523958, 0.007477607522219695, 0.003992855117806994, -0.006200775938870265, -0.006839359417492538, 0.002990194549285108, 0.007449536460130379, 0.0004934715301202309,
	-0.0055711055081935894, -0.002339503275470328, 0.0024205294866188245, 0.0015052777924539301, -0.00007901302089504693, 0.0014729654667895548, 0.0002998529273729419, -0.0046592979639329965, -0.003400002594569198, 0.0057876495919874284,
	0.00795201731863244, -0.0035941955682399873, -0.011471027274047127, -0.0013225894819717716, 0.011823817052502072, 0.006670341858089295, -0.008557731650575384, -0.009706059925519718, 0.003327264119074448, 0.008841586076154187,
	0.0008946350441841072, -0.004724999183876257, -0.001481080562665143, 0.0000993243829292326, -0.0021938378033898426, 0.0016455451167622134, 0.008148391209382592, 0.0015456016028449321, -0.01267760277297782, -0.008986357884008466,
	0.012362103773336972, 0.01729042446909641, -0.006182711008788704, -0.022058654720406808, -0.0035366893519028684, 0.020399445595693025, 0.012068114124900279, -0.012884400960028826, -0.01476077634759942, 0.0037656059078107072,
	0.00987256190731224, 0.0009389602475001234, -0.00017829794189682025, 0.003273451369957372, -0.007760733758954619, -0.01644105193210246, 0.0066278942679170744, 0.03308290149423704, 0.007794812001816846, -0.044075436621044704,
	-0.03362935473822945, 0.04059379994204465, 0.06266545583414253, -0.01858678179796364, -0.08319833962831256, -0.01850089317052141, 0.0849144926913675, 0.060253541299211344, -0.06362016800957712, -0.09303378713726013,
	0.023602244912563543, 0.10543556137489422, 0.023602244912563543, -0.09303378713726013, -0.06362016800957712, 0.060253541299211344, 0.0849144926913675, -0.01850089317052141, -0.08319833962831256, -0.01858678179796364,
	0.06266545583414253, 0.04059379994204465, -0.03362935473822945, -0.044075436621044704, 0.007794812001816846, 0.03308290149423704, 0.0066278942679170744, -0.01644105193210246, -0.007760733758954619, 0.003273451369957372,
	-0.00017829794189682025, 0.0009389602475001234, 0.00987256190731224, 0.0037656059078107072, -0.01476077634759942, -0.012884400960028826, 0.012068114124900279, 0.020399445595693025, -0.0035366893519028684, -0.022058654720406808,
	-0.006182711008788704, 0.01729042446909641, 0.012362103773336972, -0.008986357884008466, -0.01267760277297782, 0.0015456016028449321, 0.008148391209382592, 0.0016455451167622134, -0.0021938378033898426, 0.0000993243829292326,
	-0.001481080562665143, -0.004724999183876257, 0.0008946350441841072, 0.008841586076154187, 0.003327264119074448, -0.009706059925519718, -0.008557731650575384, 0.006670341858089295, 0.011823817052502072, -0.0013225894819717716,
	-0.011471027274047127, -0.0035941955682399873, 0.00795201731863244, 0.0057876495919874284, -0.003400002594569198, -0.0046592979639329965, 0.0002998529273729419, 0.0014729654667895548, -0.00007901302089504693, 0.0015052777924539301,
	0.0024205294866188245, -0.002339503275470328, -0.0055711055081935894, 0.0004934715301202309, 0.007449536460130379, 0.002990194549285108, -0.006839359417492538, -0.006200775938870265, 0.003992855117806994, 0.007477607522219695,
	-0.0004010381602523958, -0.006338843167347792, -0.002143772286197777, 0.003654885829314124, 0.0025678854769133706, -0.0010578083944437914, -0.0010512919194413438, -0.000015116280357111925, -0.0011546924818686593, -0.0008739045588965111,
	0.002524964970093736, 0.0030029051849081827, -0.0021362973650225197, -0.004983212007292624, 0.00014242678166029782, 0.005587382444676615, 0.0023781907591731728, -0.0044473696801485794, -0.004108213864284433, 0.0021932087332278624,
	0.004247060351054372, -0.00003923071426521127, -0.0029303733259454195, -0.0009418053763149555, 0.0010973240457947447, 0.00042462395545289237, 0.00010132548858608131, 0.0010472220633574577, 0.00003759603702705271, -0.00241142865927784,
	-0.001393304300506093, 0.002732507979365003, 0.0031441463885070794, -0.0017216486628049753, -0.004275809408563589, -0.00014312170015328787, 0.0041577843530587804, 0.001921559161627858, -0.0028850341956466073, -0.002778904046875478,
	0.0011661328775996423, 0.0024343968436937903, 0.00010859533627237019, -0.001278431694707578, -0.0003755197175695685, 0.00010456438831586011, -0.0003104377397048547, 0.0003746401268217804, 0.0013663672927468175, 0.00008542056897488325,
	-0.00204855009083113, -0.0011522641080418641, 0.001902510147331796, 0.0021635244595125205, -0.000993819164841841, -0.0025313724868107432, -0.00016322625554340134, 0.0020792764245218132, 0.0009266463396891097, -0.0011274083918726687,
	-0.000935395563452344, 0.0002751911618193786, 0.00027174830997308923, -0.00004281513953790099, 0.0005657420083707418, 0.0005574964481650076, -0.0009979058848191866, -0.0015125455786355033, 0.0006791597135050718, 0.002342386308112988,
	0.0003145817946935235, -0.002530829215458603, -0.0015007134485464705, 0.0019661323295239, 0.0023869658735495244, -0.0008068958794424991, -0.002485159435458123, -0.000270887639715945, 0.002048622083391921, 0.00113368338293867,
	-0.0009594522556201064, -0.000956768070512709, 0.0005280708198815686, 0.0010436699740755866, 0.0005146661530813266, 0.000420774478514697, 0.0008300048915853866, 0.0009548680892419512, 0.0011853741115800448, 0.0022285247760404935,
	0.003429473494516816, 0.003983026474459068, -0.006452105894518418
}){
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
