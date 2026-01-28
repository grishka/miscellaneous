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
#define BUFFER_SIZE_THRESHOLD (DEFAULT_LINE_DURATION*625*2)

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
	tgvoip::Buffer buf=bufferPool.Get();
	buf.CopyFrom(samples, 0, count);
	blockingSemaphore.Release();
	newlyAcquiredDataBuffers.Put(std::move(buf));
}

void Decoder::runDecoderThread(){
	blackLevel=1;
	
	const int filterSize=chromaSeparationFilter.getSize();
	const int filterDelay=chromaSeparationFilter.getDelay();
	float *samples=(float*)calloc(BUFFER_SIZE+filterSize*2, sizeof(float));
	for(int i=0;i<DEFAULT_LINE_DURATION;i++)
		prevLineChroma.push_back(0);
	
	vector<float> currentFieldLuminance, currentFieldChrominance, currentFieldRaw, currentFieldLuminanceForSync;
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
				
				vector<float> nextFieldLuminance, nextFieldChrominance, nextFieldLuminanceForSync, nextFieldRaw;
				if(lastLongSyncPulseLocation+fieldDuration<0){
					int samplesToMove=-(lastLongSyncPulseLocation+fieldDuration);
					//printf("moving %d samples\n", samplesToMove);
					nextFieldLuminance=vector<float>(currentFieldLuminance.end()-samplesToMove, currentFieldLuminance.end());
					nextFieldChrominance=vector<float>(currentFieldChrominance.end()-samplesToMove, currentFieldChrominance.end());
					nextFieldLuminanceForSync=vector<float>(currentFieldLuminanceForSync.end()-samplesToMove, currentFieldLuminanceForSync.end());
					nextFieldRaw=vector<float>(currentFieldRaw.end()-samplesToMove, currentFieldRaw.end());
					currentFieldLuminance.erase(currentFieldLuminance.end()-samplesToMove, currentFieldLuminance.end());
					currentFieldChrominance.erase(currentFieldChrominance.end()-samplesToMove, currentFieldChrominance.end());
					currentFieldLuminanceForSync.erase(currentFieldLuminanceForSync.end()-samplesToMove, currentFieldLuminanceForSync.end());
					currentFieldRaw.erase(currentFieldRaw.end()-samplesToMove, currentFieldRaw.end());
				}else{
					currentFieldLuminance.insert(currentFieldLuminance.end(), luminance+std::max(0, lastLongSyncPulseLocation), luminance+lastLongSyncPulseLocation+fieldDuration);
					currentFieldChrominance.insert(currentFieldChrominance.end(), chrominance+std::max(0, lastLongSyncPulseLocation), chrominance+lastLongSyncPulseLocation+fieldDuration);
					currentFieldLuminanceForSync.insert(currentFieldLuminanceForSync.end(), luminanceForSync+std::max(0, lastLongSyncPulseLocation), luminanceForSync+lastLongSyncPulseLocation+fieldDuration);
					currentFieldRaw.insert(currentFieldRaw.end(), raw+std::max(0, lastLongSyncPulseLocation), raw+lastLongSyncPulseLocation+fieldDuration);
				}
				assert(currentFieldLuminance.size()==fieldDuration);
				for(int j=10;j<i;j++){
					if(syncPulseLocations[j].location>=lastLongSyncPulseLocation)
						currentFieldSyncPulses.push_back(syncPulseLocations[j].offset(-lastLongSyncPulseLocation));
				}
				vector<VideoLine> field=processField(currentFieldLuminance, currentFieldChrominance, currentFieldLuminanceForSync, currentFieldRaw, currentFieldSyncPulses, syncLevel, blackLevel, visibleBrightnessRange, nextFieldIsBottom);
				currentFieldLuminance=nextFieldLuminance;
				currentFieldChrominance=nextFieldChrominance;
				currentFieldLuminanceForSync=nextFieldLuminanceForSync;
				currentFieldRaw=nextFieldRaw;
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
			currentFieldLuminance.insert(currentFieldLuminance.end(), luminance+std::max(0, lastLongSyncPulseLocation), luminance+lastLongSyncPulseLocation+nextFieldDuration);
			currentFieldChrominance.insert(currentFieldChrominance.end(), chrominance+std::max(0, lastLongSyncPulseLocation), chrominance+lastLongSyncPulseLocation+nextFieldDuration);
			currentFieldLuminanceForSync.insert(currentFieldLuminanceForSync.end(), luminanceForSync+std::max(0, lastLongSyncPulseLocation), luminanceForSync+lastLongSyncPulseLocation+nextFieldDuration);
			currentFieldRaw.insert(currentFieldRaw.end(), raw+std::max(0, lastLongSyncPulseLocation), raw+lastLongSyncPulseLocation+nextFieldDuration);
			assert(currentFieldLuminance.size()==nextFieldDuration);
			for(int j=10;j<syncPulseLocations.size();j++){
				if(syncPulseLocations[j].location>=std::min(BUFFER_SIZE, lastLongSyncPulseLocation+nextFieldDuration))
					break;
				if(syncPulseLocations[j].location>=lastLongSyncPulseLocation)
					currentFieldSyncPulses.push_back(syncPulseLocations[j].offset(-lastLongSyncPulseLocation));
			}
			vector<VideoLine> field=processField(currentFieldLuminance, currentFieldChrominance, currentFieldLuminanceForSync, currentFieldRaw, currentFieldSyncPulses, syncLevel, blackLevel, visibleBrightnessRange, nextFieldIsBottom);
			currentFieldLuminance.clear();
			currentFieldChrominance.clear();
			currentFieldLuminanceForSync.clear();
			currentFieldRaw.clear();
			currentFieldSyncPulses.clear();

			lastLongSyncPulseLocation+=nextFieldDuration;
			earliestNextLongSyncPulseLocation=lastLongSyncPulseLocation+DEFAULT_LINE_DURATION*10;
			unprocessedSamplesRemain-=nextFieldDuration;
			printf("desync, adding field, last location %d, remain %d, %s field\n", lastLongSyncPulseLocation, unprocessedSamplesRemain, nextFieldIsBottom ? "bottom" : "top");
			nextFieldIsBottom=!nextFieldIsBottom;
			if(nextFieldIsBottom)
				nextFieldDuration=DEFAULT_LINE_DURATION*313;
			else
				nextFieldDuration=DEFAULT_LINE_DURATION*312;
		}
		if(unprocessedSamplesRemain){
			currentFieldLuminance.insert(currentFieldLuminance.end(), luminance+(BUFFER_SIZE-unprocessedSamplesRemain), luminance+BUFFER_SIZE);
			currentFieldChrominance.insert(currentFieldChrominance.end(), chrominance+(BUFFER_SIZE-unprocessedSamplesRemain), chrominance+BUFFER_SIZE);
			currentFieldLuminanceForSync.insert(currentFieldLuminanceForSync.end(), luminanceForSync+(BUFFER_SIZE-unprocessedSamplesRemain), luminanceForSync+BUFFER_SIZE);
			currentFieldRaw.insert(currentFieldRaw.end(), raw+(BUFFER_SIZE-unprocessedSamplesRemain), raw+BUFFER_SIZE);
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
}

Decoder::VideoLine Decoder::joinLines(VideoLine a, VideoLine b){
	vector<float> luminance=a.luminance;
	luminance.insert(luminance.end(), b.luminance.begin(), b.luminance.end());
	vector<float> chrominance=a.chrominance;
	chrominance.insert(chrominance.end(), b.chrominance.begin(), b.chrominance.end());
	vector<float> filteredLuminance=a.filteredLuminance;
	filteredLuminance.insert(filteredLuminance.end(), b.filteredLuminance.begin(), b.filteredLuminance.end());
	vector<float> raw=a.raw;
	raw.insert(raw.end(), b.raw.begin(), b.raw.end());
	vector<SyncPulse> sync=a.sync;
	for(SyncPulse sp:b.sync){
		sync.push_back(sp.offset((int)a.luminance.size()));
	}
	//printf("join %d + %d -> %d\n", (int)a.luminance.size(), (int)b.luminance.size(), (int)luminance.size());
	return VideoLine{
		.luminance=luminance,
		.chrominance=chrominance,
		.filteredLuminance=filteredLuminance,
		.raw=raw,
		.sync=sync,
	};
}

std::vector<Decoder::VideoLine> Decoder::splitLine(VideoLine line, int numParts){
	float partLength=(float)line.luminance.size()/numParts;
	//printf("split into %d parts %f samples each\n", numParts, partLength);
	vector<VideoLine> parts;
	for(int i=0;i<numParts;i++){
		int offset=(int)(partLength*i);
		int length=i==numParts-1 ? (int)line.chrominance.size()-offset : (int)partLength;
		assert(length>0);
		vector<SyncPulse> sync;
		for(SyncPulse sp:line.sync){
			if(sp.location>=offset && sp.location<offset+length)
				sync.push_back(sp);
		}
		VideoLine part={
			.luminance=vector<float>(line.luminance.begin()+offset, line.luminance.begin()+offset+length),
			.chrominance=vector<float>(line.chrominance.begin()+offset, line.chrominance.begin()+offset+length),
			.filteredLuminance=vector<float>(line.filteredLuminance.begin()+offset, line.filteredLuminance.begin()+offset+length),
			.raw=vector<float>(line.raw.begin()+offset, line.raw.begin()+offset+length),
			.sync=sync,
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

vector<Decoder::VideoLine> Decoder::processField(std::vector<float> luminance, std::vector<float> chrominance, std::vector<float> filteredLuminance, std::vector<float> raw, std::vector<SyncPulse> sync, float syncLevel, float blackLevel, float visibleBrightnessRange, bool isBottom){
	assert(luminance.size()==chrominance.size());
	assert(luminance.size()==filteredLuminance.size());
	SyncPulse nextSyncPulse=sync.size() ? sync[0] : SyncPulse{0, 0};
	int syncPulseIndex=0;
	VideoLine currentLine;
	vector<VideoLine> lines;
	int nextLineStartEarliestLocation=MIN_LINE_DURATION;
	float syncLevelSum=0, blackLevelSum=0;
	int syncLevelSampleCount=0, syncSamplesToAdd=0, blackLevelSampleCount=0, blackSamplesToAdd=0, blackSampleDelay=0;
	for(int i=0;i<luminance.size();i++){
		currentLine.luminance.push_back(luminance[i]);
		currentLine.chrominance.push_back(chrominance[i]);
		currentLine.filteredLuminance.push_back(filteredLuminance[i]);
		currentLine.raw.push_back(raw[i]);
		if(i==nextSyncPulse.location){
			if(nextSyncPulse.location>=nextLineStartEarliestLocation){
				lines.push_back(currentLine);
				currentLine=VideoLine();
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
			syncLevelSum+=filteredLuminance[i];
			syncSamplesToAdd--;
			syncLevelSampleCount++;
		}
		if(blackSamplesToAdd){
			if(blackSampleDelay){
				blackSampleDelay--;
			}else{
				blackLevelSum+=filteredLuminance[i];
				blackSamplesToAdd--;
				blackLevelSampleCount++;
			}
		}
	}
	if(currentLine.luminance.size()>=MIN_LINE_DURATION){
		lines.push_back(currentLine);
	}
	
	int j=0;
	for(auto line=lines.begin();line!=lines.end() && j<313;line++, j++){
		if(line->luminance.size()>/*MIN_LINE_DURATION*2*/MAX_LINE_DURATION){
			//printf("line of length %d offset %d\n", (int)line->luminance.size(), line->offsetInBuffer);
			VideoLine joined=*line;
			vector<VideoLine>::iterator replaceBegin=line, replaceEnd=line+1;
			if(line!=lines.begin()){
				VideoLine prev=*(line-1);
				if(prev.luminance.size()>MAX_LINE_DURATION){
					joined=joinLines(prev, joined);
					replaceBegin=line-1;
				}
			}
			int numParts=(int)roundf(joined.luminance.size()/(float)DEFAULT_LINE_DURATION);
			if(numParts>1){
				vector<VideoLine> parts=splitLine(joined, numParts);
				line=lines.erase(replaceBegin, replaceEnd);
				line=lines.insert(line, parts.begin(), parts.end());
			}
		}
	}
	
	if(lines.size()>313){
		printf("field too long with %d lines\n", (int)lines.size());
		//lines.erase(lines.begin(), lines.begin()+(lines.size()-313));
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
				for(int k=120;k<line.filteredLuminance.size();k++){
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
				int lineLength=std::min((int)line.raw.size(), DEFAULT_LINE_DURATION);
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
	int numSamples=(int)line.luminance.size();
	/*if(numSamples<MIN_LINE_DURATION){
		printf("line %d is too short (%d samples)\n", lineIndex, numSamples);
		return;
	}*/

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
	for(float& chromaSample:line.chrominance){
		chromaSample=std::clamp((chromaSample-centerFreq)/maxDeviation, -1.0f, 1.0f);
	}
	chromaLowpassFilter->process(line.chrominance.data(), line.chrominance.data(), (int)line.chrominance.size());

	float offset=0;
	float endOffset=0;
	int leftAlign;
	int rightAlign;
	if((lineIndex>4 && lineIndex<310) || (lineIndex>317 && lineIndex<623)){
		leftAlign=LINE_LONG_SYNC_DURATION-LINE_SYNC_WINDOW+15;
		rightAlign=LINE_SYNC_WINDOW-15;
	}else{
		leftAlign=LINE_SYNC_DURATION-LINE_SYNC_WINDOW+15;
		rightAlign=LINE_SYNC_WINDOW-15;
	}
	float threshold=syncLevel+(blackLevel-syncLevel)*0.7f;
	if(line.filteredLuminance[0]<threshold){
		for(int i=leftAlign-15;i<leftAlign+15;i++){
			if(line.filteredLuminance[i]>threshold && line.filteredLuminance[i+1]>threshold && line.filteredLuminance[i+2]>threshold){
				float prevSample=line.filteredLuminance[i-1];
				offset=i-leftAlign+(threshold-prevSample)/(line.filteredLuminance[i]-prevSample);
				break;
			}
		}
		int size=(int)line.luminance.size();
		for(int i=2;i<100;i++){
			if(line.filteredLuminance[size-i]>threshold && line.filteredLuminance[size-(i+1)]>threshold && line.filteredLuminance[size-(i+2)]>threshold){
				float prevSample=line.filteredLuminance[size-(i-1)];
				float curSample=line.filteredLuminance[size-i];
				if(prevSample==curSample) // TODO
					continue;
				endOffset=rightAlign-i+(threshold-curSample)/(prevSample-curSample);
				break;
			}
		}
	}
	
	
	vector<float> interpolatedLuminance(DEFAULT_LINE_DURATION);
	vector<float> interpolatedChrominance(DEFAULT_LINE_DURATION);
	
	for(int x=0;x<DEFAULT_LINE_DURATION;x++){
		float sampleOffsetK=x/(float)DEFAULT_LINE_DURATION;
		float sampleOffset=endOffset*sampleOffsetK+offset*(1.0f-sampleOffsetK);
		float sampleIndex=std::clamp(x/(float)DEFAULT_LINE_DURATION*numSamples+sampleOffset, 0.0f, (float)numSamples-2);
		float k=sampleIndex-(int)sampleIndex;

		float sample1Y=line.luminance[((int)sampleIndex)];
		float sample2Y=line.luminance[(int)sampleIndex+1];
		interpolatedLuminance[x]=sample2Y*k+sample1Y*(1.0f-k);
		
		float sample1C=line.chrominance[((int)sampleIndex)];
		float sample2C=line.chrominance[(int)sampleIndex+1];
		interpolatedChrominance[x]=sample2C*k+sample1C*(1.0f-k);
	}
	
	if(colorMode==ColorDisplayMode::Raw && line.raw.size()<DEFAULT_LINE_DURATION){
		for(int i=0;i<DEFAULT_LINE_DURATION-line.raw.size();i++){
			line.raw.push_back(0);
		}
	}

	if(lineIndex==scopeLineIndex){
		scopeData1.clear();
		for(float v:line.luminance){
			scopeData1.push_back((float)v);
		}
		scopeData2.clear();
		for(float v:line.chrominance){
			scopeData2.push_back((float)v/2+0.5);
		}
		scopeLines.clear();
		scopeLines.push_back(syncLevel);
		scopeLines.push_back(blackLevel);
		scopeLines.push_back(whiteLevel);
	}

	vector<float>* _samplesForDisplay;
	switch(colorMode){
		case ColorDisplayMode::Full:
			_samplesForDisplay=&interpolatedLuminance;
			break;
		case ColorDisplayMode::Raw:
			_samplesForDisplay=&line.raw;
			break;
		case ColorDisplayMode::Y:
			_samplesForDisplay=&interpolatedLuminance;
			break;
		case ColorDisplayMode::Db:
			_samplesForDisplay=isRedLine ? &prevLineChroma : &interpolatedChrominance;
			break;
		case ColorDisplayMode::Dr:
			_samplesForDisplay=isRedLine ? &interpolatedChrominance : &prevLineChroma;
			break;
	}
	vector<float> samplesForDisplay=*_samplesForDisplay;
	vector<float>& crSamples(isRedLine ? interpolatedChrominance : prevLineChroma);
	vector<float>& cbSamples(isRedLine ? prevLineChroma : interpolatedChrominance);

	for(int x=0;x<bitmapWidth;x++){
		float sampleIndex=std::clamp(x/(float)bitmapWidth*samplesForDisplay.size(), 0.0f, (float)samplesForDisplay.size()-2);
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
	
	prevLineChroma=interpolatedChrominance;
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
