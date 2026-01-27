//
//  Filters.cpp
//  AVDecoder
//
//  Created by Grishka on 09.01.2026.
//

#include "Filters.hpp"
#include <stdlib.h>
#include <assert.h>
#ifdef __ARM_NEON__
#include <arm_neon.h>
#endif

FIRFilter::FIRFilter(std::vector<float> taps){
	this->taps=(float*)malloc(taps.size()*sizeof(float));
	memcpy(this->taps, taps.data(), taps.size()*sizeof(float));
	numTaps=(int)taps.size();
	delay=(numTaps-1)/2;
}

FIRFilter::~FIRFilter(){
	free(taps);
}

void FIRFilter::process(const float *input, float *output, int count){
#ifdef USE_ACCELERATE
	vDSP_desamp(input, 1, taps, output, count, numTaps);
#else
	const int numTaps=this->numTaps;
	for(int i=0;i<count;i++){
		float sum=0;
		for(int t=0;t<numTaps;t++){
			sum+=input[i+t]*taps[t];
		}
		output[i]=sum;
	}
#endif
}

BiquadFilter::BiquadFilter(float a1, float a2, float b0, float b1, float b2):a1(a1), a2(a2), b0(b0), b1(b1), b2(b2){
#ifdef USE_ACCELERATE
	double coefficients[]={b0, b1, b2, a1, a2};
	vdspSetup=vDSP_biquad_CreateSetup(coefficients, 1);
#endif
}

BiquadFilter::~BiquadFilter(){
#ifdef USE_ACCELERATE
	vDSP_biquad_DestroySetup(vdspSetup);
#endif
}

void BiquadFilter::process(const float *input, float *output, int count){
#ifdef USE_ACCELERATE
	vDSP_biquad(vdspSetup, vdspState, input, 1, output, 1, count);
#else
	float x1=0, x2=0, y1=0, y2=0;
	for(int i=0;i<count;i++){
		float x=input[i];
		float y=b0*x+b1*x1+b2*x2-a1*y1-a2*y2;
		output[i]=y;
		x2=x1;
		x1=x;
		y2=y1;
		y1=y;
	}
#endif
}
