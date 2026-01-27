//
//  Filters.hpp
//  AVDecoder
//
//  Created by Grishka on 09.01.2026.
//

#ifndef Filters_hpp
#define Filters_hpp

#define USE_ACCELERATE

#include <vector>
#ifdef USE_ACCELERATE
#include <Accelerate/Accelerate.h>
#endif

#include "Buffers.h"

class BiquadFilter{
public:
	BiquadFilter(float a1, float a2, float b0, float b1, float b2);
	~BiquadFilter();
	void process(const float *input, float *output, int count);
private:
	const float a1, a2, b0, b1, b2;
#ifdef USE_ACCELERATE
	vDSP_biquad_Setup vdspSetup;
	float vdspState[4];
#endif
};

class FIRFilter{
public:
	FIRFilter(std::vector<float> taps);
	~FIRFilter();
	void process(const float *input, float *output, int count);
	int getSize(){
		return numTaps;
	};
	int getDelay(){
		return delay;
	};
private:
	float* taps;
	int numTaps;
	int delay;
};

#endif /* Filters_hpp */
