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

FIRFilter chromaSeparationFilter({
	-0.0008084198921017675, 0.0006187579867392353, -0.0042764826290806, 0.00023552803823015075, 0.0029350689942645334, 0.0018303194259556947, -0.0006665164815438609, -0.0009775065344609743, 0.0011472693670836079, 0.0021899359358202902,
	0.00047397888432248867, -0.0011936947905478367, -0.0003340858817935444, 0.0012929857873576248, 0.0010762065214391275, -0.00020921686048868077, -0.00041918642561435414, 0.00020086896643058938, 0.0002496876991831115, 0.00007633423118796786,
	0.0005087909502194233, 0.0005814257143186315, -0.0005016761116956633, -0.0010295793807538305, 0.00040605353540659446, 0.0016860059596370063, 0.00036029461853548747, -0.0016962546454071812, -0.0010219271244939839, 0.001417844174052741,
	0.001634194754788557, -0.0006422354812487633, -0.0016406170525134556, -0.000024449788647273482, 0.0012642723198736472, 0.0004486152195372795, -0.0005485613460414528, -0.0002672786431669205, 0.000022819634740385123, -0.00031708973449485426,
	0.00008820263646881974, 0.0010834338863132385, 0.00042754396930485004, -0.0015327156147460028, -0.0013262923263252155, 0.0014160782864150265, 0.002255738717898832, -0.0006557358667519558, -0.002734155920042715, -0.00042955302201752425,
	0.002540889460237056, 0.0013945619314224913, -0.0017283176550015485, -0.0017824255460039597, 0.0007064317199275275, 0.0014445133763883755, 0.00001824481698249103, -0.0005679010846329329, -0.00003627500616496595, -0.0003398382009982229,
	-0.0006943509792201608, 0.0007411907342408252, 0.001844463013137552, -0.00030110171360244484, -0.002820372342003177, -0.0008906073849091523, 0.003081076466026201, 0.002361186314183753, -0.002397867802422487, -0.0034544752770188578,
	0.0010082865026793313, 0.0036641654049757843, 0.00048257059544390407, -0.002899435340268885, -0.0013858596492442555, 0.0015600199390735177, 0.0012756772145747434, -0.00036409644709556245, -0.00022813963719119066, 4.2792417022280905e-8,
	-0.0011824011900674783, -0.0007659313596485364, 0.002148741835910928, 0.002393006509006156, -0.002026607711212397, -0.004139726359195634, 0.0006796526339337505, 0.005140826447766138, 0.0014138917383190119, -0.004844332544962665,
	-0.0033672286237908275, 0.003309476330712637, 0.004302126362403214, -0.0012302826831179624, -0.0038135816114454593, -0.00039205116552638966, 0.0021961300227112297, 0.0007290979537405085, -0.000350259844379635, 0.0004288162986091685,
	-0.0006397066727917901, -0.0025186349915811692, 0.000055642222988915534, 0.004444245417868276, 0.0020642342803674847, -0.005091207458519579, -0.0048810441819254966, 0.0039050448082875704, 0.00712855744406191, -0.0012155714773423765,
	-0.007729784912090011, -0.0018680535027260864, 0.00635902182366749, 0.003960522781864317, -0.003672201469987563, -0.004097072249581145, 0.0010411260459047508, 0.002259204061210176, 0.00009778020814124743, 0.0005421841023644833,
	0.0010287042057747287, -0.0026952357856696277, -0.0040675978276551615, 0.002772849134323076, 0.007627414581032347, -0.00028355751732421454, -0.009905263558679541, -0.00400153951043806, 0.009566362719045613, 0.00831984783732327,
	-0.006478402295842504, -0.010735085217749104, 0.0018880578611164885, 0.010119624767300852, 0.002093096002322051, -0.006804999913632329, -0.003490006685947415, 0.002554281099106314, 0.0014899008198413174, 0.00018604531723056324,
	0.0030080763549514473, 0.0005194418716754934, -0.007662808949380242, -0.005043235779656341, 0.009733311393689046, 0.011822506238487763, -0.007427557140776094, -0.017925718411881956, 0.0009325264937686848, 0.020373652083520617,
	0.007430327157484658, -0.01765211795787576, -0.014097191809787028, 0.010697449506474374, 0.015914288664094233, -0.0027770050299042543, -0.011810384961427504, -0.0017915573807276816, 0.003706806161428886, -0.0003630438026126603,
	0.00389774608388887, 0.009818918398266388, -0.005633679785762584, -0.02351667119577497, -0.002371602580222098, 0.03545943724012464, 0.020443148235339083, -0.03884909832620022, -0.04451738542374165, 0.028810052646692286,
	0.06712385756245634, -0.0047293969359982575, -0.07982134540340874, -0.02892169830408917, 0.07632798122385262, 0.06366228734827452, -0.05506042210309779, -0.089718817337857, 0.0200692964748796, 0.09937676787037238,
	0.0200692964748796, -0.089718817337857, -0.05506042210309779, 0.06366228734827452, 0.07632798122385262, -0.02892169830408917, -0.07982134540340874, -0.0047293969359982575, 0.06712385756245634, 0.028810052646692286,
	-0.04451738542374165, -0.03884909832620022, 0.020443148235339083, 0.03545943724012464, -0.002371602580222098, -0.02351667119577497, -0.005633679785762584, 0.009818918398266388, 0.00389774608388887, -0.0003630438026126603,
	0.003706806161428886, -0.0017915573807276816, -0.011810384961427504, -0.0027770050299042543, 0.015914288664094233, 0.010697449506474374, -0.014097191809787028, -0.01765211795787576, 0.007430327157484658, 0.020373652083520617,
	0.0009325264937686848, -0.017925718411881956, -0.007427557140776094, 0.011822506238487763, 0.009733311393689046, -0.005043235779656341, -0.007662808949380242, 0.0005194418716754934, 0.0030080763549514473, 0.00018604531723056324,
	0.0014899008198413174, 0.002554281099106314, -0.003490006685947415, -0.006804999913632329, 0.002093096002322051, 0.010119624767300852, 0.0018880578611164885, -0.010735085217749104, -0.006478402295842504, 0.00831984783732327,
	0.009566362719045613, -0.00400153951043806, -0.009905263558679541, -0.00028355751732421454, 0.007627414581032347, 0.002772849134323076, -0.0040675978276551615, -0.0026952357856696277, 0.0010287042057747287, 0.0005421841023644833,
	0.00009778020814124743, 0.002259204061210176, 0.0010411260459047508, -0.004097072249581145, -0.003672201469987563, 0.003960522781864317, 0.00635902182366749, -0.0018680535027260864, -0.007729784912090011, -0.0012155714773423765,
	0.00712855744406191, 0.0039050448082875704, -0.0048810441819254966, -0.005091207458519579, 0.0020642342803674847, 0.004444245417868276, 0.000055642222988915534, -0.0025186349915811692, -0.0006397066727917901, 0.0004288162986091685,
	-0.000350259844379635, 0.0007290979537405085, 0.0021961300227112297, -0.00039205116552638966, -0.0038135816114454593, -0.0012302826831179624, 0.004302126362403214, 0.003309476330712637, -0.0033672286237908275, -0.004844332544962665,
	0.0014138917383190119, 0.005140826447766138, 0.0006796526339337505, -0.004139726359195634, -0.002026607711212397, 0.002393006509006156, 0.002148741835910928, -0.0007659313596485364, -0.0011824011900674783, 4.2792417022280905e-8,
	-0.00022813963719119066, -0.00036409644709556245, 0.0012756772145747434, 0.0015600199390735177, -0.0013858596492442555, -0.002899435340268885, 0.00048257059544390407, 0.0036641654049757843, 0.0010082865026793313, -0.0034544752770188578,
	-0.002397867802422487, 0.002361186314183753, 0.003081076466026201, -0.0008906073849091523, -0.002820372342003177, -0.00030110171360244484, 0.001844463013137552, 0.0007411907342408252, -0.0006943509792201608, -0.0003398382009982229,
	-0.00003627500616496595, -0.0005679010846329329, 0.00001824481698249103, 0.0014445133763883755, 0.0007064317199275275, -0.0017824255460039597, -0.0017283176550015485, 0.0013945619314224913, 0.002540889460237056, -0.00042955302201752425,
	-0.002734155920042715, -0.0006557358667519558, 0.002255738717898832, 0.0014160782864150265, -0.0013262923263252155, -0.0015327156147460028, 0.00042754396930485004, 0.0010834338863132385, 0.00008820263646881974, -0.00031708973449485426,
	0.000022819634740385123, -0.0002672786431669205, -0.0005485613460414528, 0.0004486152195372795, 0.0012642723198736472, -0.000024449788647273482, -0.0016406170525134556, -0.0006422354812487633, 0.001634194754788557, 0.001417844174052741,
	-0.0010219271244939839, -0.0016962546454071812, 0.00036029461853548747, 0.0016860059596370063, 0.00040605353540659446, -0.0010295793807538305, -0.0005016761116956633, 0.0005814257143186315, 0.0005087909502194233, 0.00007633423118796786,
	0.0002496876991831115, 0.00020086896643058938, -0.00041918642561435414, -0.00020921686048868077, 0.0010762065214391275, 0.0012929857873576248, -0.0003340858817935444, -0.0011936947905478367, 0.00047397888432248867,
	0.0021899359358202902, 0.0011472693670836079, -0.0009775065344609743, -0.0006665164815438609, 0.0018303194259556947, 0.0029350689942645334, 0.00023552803823015075, -0.0042764826290806, 0.0006187579867392353,
	-0.0008084198921017675
});

FIRFilter chromaLowpass({
	-0.008876307100104262, -0.013556919317715915, -0.018988399308695385, -0.020386891649879245, -0.014318422301337229, 0.001699592554332439, 0.028093762653337663, 0.06252109896793875, 0.1000136681614268,
	0.13393481216209052, 0.15760935386262073, 0.16610808639225502, 0.15760935386262073, 0.13393481216209052, 0.1000136681614268, 0.06252109896793875, 0.028093762653337663, 0.001699592554332439,
	-0.014318422301337229, -0.020386891649879245, -0.018988399308695385, -0.013556919317715915, -0.008876307100104262
});

#pragma mark -


Decoder::Decoder(void* bitmapData, unsigned int bitmapWidth, unsigned int bitmapHeight, unsigned int bitmapStride, CFRunLoopSourceRef runLoopSource) :bitmapData(bitmapData), bitmapWidth(bitmapWidth), bitmapHeight(bitmapHeight), bitmapStride(bitmapStride/4), decoderThread(std::bind(&Decoder::runDecoderThread, this)), runLoopSource(runLoopSource){
	
	chromaDeemphasisFilter=new BiquadFilter(-1.09184352, 0.12286846, 0.58102424, -1.09184352, 0.54184422);
	chromaLowpassFilter=new BiquadFilter(-1.47543442, 0.58687001, 0.02785890, 0.05571779, 0.02785890);
	
	currentOutputBuffer=outputBufferPool.Get();
	mainThreadRunLoop=CFRunLoopGetMain();

	decoderThread.SetName("Decoder");
	decoderThread.Start();
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
	
	const int filterSize=chromaSeparationFilter.getSize();
	const int filterDelay=chromaSeparationFilter.getDelay();
	float *samples=(float*)calloc(BUFFER_SIZE+filterSize*2, sizeof(float));
	prevLineChroma=(float*)calloc(DEFAULT_LINE_DURATION, sizeof(float));
	
	vector<SyncPulse> currentFieldSyncPulses;
	vector<SyncPulse> syncPulseLocations;
	vector<SyncPulse> fixedSyncPulseLocations;
	
	vector<VideoLine> lineBuffer;
	
	float *subcarrier=(float*)calloc(BUFFER_SIZE+filterSize, sizeof(float));
	
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
	
	int bufferCount=0;
	//float fieldSyncThreshold=0.2f;
	
	int lastLongSyncPulseLocation=0;
	int earliestNextLongSyncPulseLocation=-1;
	bool nextFieldIsBottom=false;
	
	while(running){
		//syncPulseLocations.clear();
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
		
		memcpy(raw, samples+filterDelay, BUFFER_SIZE*sizeof(float));
		
		memcpy(samples, samples+BUFFER_SIZE, filterSize*sizeof(float));
		for(int i=0;i<BUFFER_SIZE;i++){
			int32_t sampleInt=(int32_t)buf[i];
			float sample=sampleInt/255.0;
			samples[i+filterSize]=sample;
		}
		
		chromaSeparationFilter.process(samples, subcarrier, BUFFER_SIZE+filterDelay);
		for(int i=0;i<BUFFER_SIZE;i++){
			nextLuminance[i]=samples[i+filterDelay]-subcarrier[i];
		}
		syncLowpass.process(nextLuminance, nextLuminanceForSync, BUFFER_SIZE);
		
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
	
	free(samples);
	free(subcarrier);
	free(luminance);
	free(chrominance);
	free(nextLuminance);
	free(nextChrominance);
	free(prevLuminance);
	free(prevChrominance);
	free(luminanceForSync);
	free(nextLuminanceForSync);
	free(prevLineChroma);
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
	for(int i=trailingAlign-15;i<numSamples;i++){
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
