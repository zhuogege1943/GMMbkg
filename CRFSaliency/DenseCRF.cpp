﻿/*
    Copyright (c) 2011, Philipp Krähenbühl
    All rights reserved.

    Redistribution and use in source and binary forms, with or without
    modification, are permitted provided that the following conditions are met:
        * Redistributions of source code must retain the above copyright
        notice, this list of conditions and the following disclaimer.
        * Redistributions in binary form must reproduce the above copyright
        notice, this list of conditions and the following disclaimer in the
        documentation and/or other materials provided with the distribution.
        * Neither the name of the Stanford University nor the
        names of its contributors may be used to endorse or promote products
        derived from this software without specific prior written permission.

    THIS SOFTWARE IS PROVIDED BY Philipp Krähenbühl ''AS IS'' AND ANY
    EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
    WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
    DISCLAIMED. IN NO EVENT SHALL Philipp Krähenbühl BE LIABLE FOR ANY
    DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
    (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
    LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
    ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
    (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
    SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*/

//#include "stdafx.h"
#include "DenseCRF.h"
#include <cmath>
#include <cstring>


float* allocate(size_t N) {
	float * r = NULL;
	if (N>0)
		r = new float[N];
	memset( r, 0, sizeof(float)*N);
	return r;
}
void deallocate(float*& ptr) {
	if (ptr)
		delete[] ptr;
	ptr = NULL;
}

PairwisePotential::~PairwisePotential() {
}
SemiMetricFunction::~SemiMetricFunction() {
}
class PottsPotential: public PairwisePotential{
protected:
	Permutohedral lattice_;
	PottsPotential( const PottsPotential&o ){}
	int N_;
	float w_;
	float *norm_;
public:
	~PottsPotential(){
		deallocate( norm_ );
	}
	PottsPotential(const float* features, int D, int N, float w, bool per_pixel_normalization=true) :N_(N), w_(w) {
		lattice_.init( features, D, N );
		norm_ = allocate( N );
		for ( int i=0; i<N; i++ )
			norm_[i] = 1;
		// Compute the normalization factor
		lattice_.compute( norm_, norm_, 1 );
		if ( per_pixel_normalization ) {
			// use a per pixel normalization
			for ( int i=0; i<N; i++ )
				norm_[i] = 1.f / (norm_[i]+1e-20f);
		}
		else {
			float mean_norm = 0;
			for ( int i=0; i<N; i++ )
				mean_norm += norm_[i];
			mean_norm = N / mean_norm;
			// use a per pixel normalization
			for ( int i=0; i<N; i++ )
				norm_[i] = mean_norm;
		}
	}
	void apply(float* out_values, const float* in_values, float* tmp, int value_size) const {
		lattice_.compute( tmp, in_values, value_size );
		for ( int i=0,k=0; i<N_; i++ )
			for ( int j=0; j<value_size; j++, k++ )
				out_values[k] += w_*norm_[i]*tmp[k];
	}
};
class SemiMetricPotential: public PottsPotential{
protected:
	const SemiMetricFunction * function_;
public:
	void apply(float* out_values, const float* in_values, float* tmp, int value_size) const {
		lattice_.compute( tmp, in_values, value_size );

		// To the metric transform
		float * tmp2 = new float[value_size];
		for ( int i=0; i<N_; i++ ) {
			float * out = out_values + i*value_size;
			float * t1  = tmp  + i*value_size;
			function_->apply( tmp2, t1, value_size );
			for ( int j=0; j<value_size; j++ )
				out[j] -= w_*norm_[i]*tmp2[j];
		}
		delete[] tmp2;
	}
	SemiMetricPotential(const float* features, int D, int N, float w, const SemiMetricFunction* function, bool per_pixel_normalization=true) :PottsPotential( features, D, N, w, per_pixel_normalization ),function_(function) {
	}
};



/////////////////////////////
/////  Alloc / Dealloc  /////
/////////////////////////////
DenseCRF::DenseCRF(int N, int M) : N_(N), M_(M) {
	unary_ = allocate( N_*M_ );
	additional_unary_ = allocate( N_*M_ );
	current_ = allocate( N_*M_ );
	next_ = allocate( N_*M_ );
	tmp_ = allocate( 2*N_*M_ );
	// Set the additional_unary_ to zero
	memset( additional_unary_, 0, sizeof(float)*N_*M_ );
}
DenseCRF::~DenseCRF() {
	deallocate( unary_ );
	deallocate( additional_unary_ );
	deallocate( current_ );
	deallocate( next_ );
	deallocate( tmp_ );
	for( unsigned int i=0; i<pairwise_.size(); i++ )
		delete pairwise_[i];
}
DenseCRF2D::DenseCRF2D(int N, int M) : DenseCRF(N,M), N_(N) {
}
DenseCRF2D::~DenseCRF2D() {
}
/////////////////////////////////
/////  Pairwise Potentials  /////
/////////////////////////////////
void DenseCRF::addPairwiseEnergy (const float* features, int D, float w, const SemiMetricFunction * function) {
	if (function)
		addPairwiseEnergy( new SemiMetricPotential( features, D, N_, w, function ) );
	else
		addPairwiseEnergy( new PottsPotential( features, D, N_, w ) );
}
void DenseCRF::addPairwiseEnergy ( PairwisePotential* potential ){
	pairwise_.push_back( potential );
}

//spsfea: [L a b sx sy pixelNumber isborder]
void DenseCRF2D::addPairwiseGaussian(float w, float sx, float sy,
	const InitValue& initVal, bool usePixel, const SemiMetricFunction * function) {
	if (usePixel)
	{
		int W = initVal.m_info.width_;
		int H = initVal.m_info.height_;
		int tM = max(W, H);
		assert(N_ == W*H);
		float * feature = new float[N_ * 2];
		for (int j = 0; j < H; j++)
			for (int i = 0; i < W; i++){
				feature[(j*W + i) * 2 + 0] = i / (tM * sx);
				feature[(j*W + i) * 2 + 1] = j / (tM * sy);
			}
		addPairwiseEnergy(feature, 2, w, function);
		delete[] feature;
	}
	else
	{
		assert(N_ == initVal.m_info.numlabels_);
		float * feature = new float[N_ * 2];
		for (int j = 0; j < N_; j++){
			//position [0 1]
			feature[j * 2 + 0] = initVal.m_info.features_[j].mean_position_[0] / sx;
			feature[j * 2 + 1] = initVal.m_info.features_[j].mean_position_[1] / sy;
		}
		addPairwiseEnergy(feature, 2, w, function);
		delete[] feature;
	}
	
}
void DenseCRF2D::addPairwiseBilateral(float w, float sx, float sy, float sr, float sg, float sb,
	const InitValue& initVal, bool usePixel, const SemiMetricFunction * function) {
	if (usePixel)
	{
		int W = initVal.m_info.width_;
		int H = initVal.m_info.height_;
		int tM = max(W, H);
		assert(N_ == W*H);

		float * feature = new float[N_ * 5];
		//const unsigned char* im = (unsigned char*)initVal.m_info.imBgr_.data;
		const float* im = (float*)initVal.m_info.imNormLab_.data;
		for (int j = 0; j < H; j++)
			for (int i = 0; i < W; i++){
				feature[(j*W + i) * 5 + 0] = i / (tM * sx);
				feature[(j*W + i) * 5 + 1] = j / (tM * sy);
				feature[(j*W + i) * 5 + 2] = im[(i + j*W) * 3 + 0] / sb;
				feature[(j*W + i) * 5 + 3] = im[(i + j*W) * 3 + 1] / sg;
				feature[(j*W + i) * 5 + 4] = im[(i + j*W) * 3 + 2] / sr;
			}
		addPairwiseEnergy(feature, 5, w, function);
		delete[] feature;
	}
	else
	{
		assert(N_ == initVal.m_info.numlabels_);
		float * feature = new float[N_ * 5];
		for (int j = 0; j < N_; j++){
			feature[j * 5 + 0] = initVal.m_info.features_[j].mean_position_[0] / sx;
			feature[j * 5 + 1] = initVal.m_info.features_[j].mean_position_[1] / sy;
			//feature[j * 5 + 2] = initVal.m_info.features_[j].mean_bgr_[2] / sr;//L
			//feature[j * 5 + 3] = initVal.m_info.features_[j].mean_bgr_[1] / sg;//a
			//feature[j * 5 + 4] = initVal.m_info.features_[j].mean_bgr_[0] / sb;//b
		}

		addPairwiseEnergy(feature, 5, w, function);
		delete[] feature;
	}
	
}

void DenseCRF2D::addPairwiseColorGaussian(float w, float sr, float sg, float sb,
	const InitValue& initVal, bool usePixel, const SemiMetricFunction * function)
{
	if (usePixel)
	{
		int W = initVal.m_info.width_;
		int H = initVal.m_info.height_;
		assert(N_ == W*H);
		
		float * feature = new float[N_ * 3];
		//const unsigned char* im = (unsigned char*)initVal.m_info.imBgr_.data;
		const float* im = (float*)initVal.m_info.imNormLab_.data;
		for (int j = 0; j < H; j++)
			for (int i = 0; i < W; i++){
				feature[(j*W + i) * 3 + 0] = im[(i + j*W) * 3 + 0] / sb;
				feature[(j*W + i) * 3 + 1] = im[(i + j*W) * 3 + 1] / sg;
				feature[(j*W + i) * 3 + 2] = im[(i + j*W) * 3 + 2] / sr;
			}
		addPairwiseEnergy(feature, 3, w, function);
		delete[] feature;
	}
	else
	{
		assert(N_ == initVal.m_info.numlabels_);
		float * feature = new float[N_ * 3];
		for (int j = 0; j < N_; j++){
			//feature[j * 3 + 0] = initVal.m_info.features_[j].mean_bgr_[2] / sr;
			//feature[j * 3 + 1] = initVal.m_info.features_[j].mean_bgr_[1] / sg;
			//feature[j * 3 + 2] = initVal.m_info.features_[j].mean_bgr_[0] / sb;
		}
		addPairwiseEnergy(feature, 3, w, function);
		delete[] feature;
	}
	

}

//////////////////////////////
/////  Unary Potentials  /////
//////////////////////////////
void DenseCRF::setUnaryEnergy ( const float* unary ) {
	memcpy( unary_, unary, N_*M_*sizeof(float) );
}
void DenseCRF::setUnaryEnergy ( int n, const float* unary ) {
	memcpy( unary_+n*M_, unary, M_*sizeof(float) );
}
void DenseCRF2D::setUnaryEnergy ( int x, int y, const float* unary ) {
	//memcpy( unary_+(x+y*W_)*M_, unary, M_*sizeof(float) );
}

void DenseCRF2D::zeroBorder(double ratio /* = 0.01 */){
	//int bW = cvCeil(W_*ratio), bH = cvCeil(H_*ratio);
	//assert(M_ == 2);
	//for (int r = 0; r < bH; r++){
	//	float* unaryTop = unary_ + 2 * W_ * r;
	//	float* unaryBot = unary_ + 2 * W_ * (H_-1 - r);
	//	for (int c = 0; c < W_; c+=2){
	//		unaryTop[0] = 0; unaryTop[1] = 1;
	//		unaryBot[0] = 0; unaryBot[1] = 1;
	//	}
	//}
}

///////////////////////
/////  Inference  /////
///////////////////////

float* DenseCRF2D::binarySeg(int n_iterations, float relax){
	startInference();
	for( int it=0; it<n_iterations; it++ ){		
		//zeroBorder();

		//CmShow::mulChannelMat(Mat(H_, W_, CV_32FC2, unary_), "C%d", 2);
		//waitKey(0);
		stepInference(relax);
	}
	return current_;
}

void DenseCRF::inference ( int n_iterations, float* result, float relax ) {
	// Run inference
	float * prob = runInference( n_iterations, relax );
	// Copy the result over
	for( int i=0; i<N_; i++ )
		memcpy( result+i*M_, prob+i*M_, M_*sizeof(float) );
}

void DenseCRF::map ( int n_iterations, short* result, float relax ) {
	// Run inference
	float * prob = runInference( n_iterations, relax );
	
	// Find the map
	for( int i=0; i<N_; i++ ){
		const float * p = prob + i*M_;
		// Find the max and subtract it so that the exp doesn't explode
		float mx = p[0];
		int imx = 0;
		for( int j=1; j<M_; j++ )
			if( mx < p[j] ){
				mx = p[j];
				imx = j;
			}
		result[i] = imx;
	}
}

float* DenseCRF::runInference( int n_iterations, float relax ) {
	startInference();
	for( int it=0; it<n_iterations; it++ )		
		stepInference(relax);
	return current_;
}

void DenseCRF::expAndNormalize ( float* out, const float* in, float scale, float relax ) {
	float *V = new float[ M_ ];
	for( int i=0; i<N_; i++ ){
		const float * b = in + i*M_;
		// Find the max and subtract it so that the exp doesn't explode
		float mx = scale*b[0];
		for( int j=1; j<M_; j++ )
			if( mx < scale*b[j] )
				mx = scale*b[j];
		float tt = 0;
		for( int j=0; j<M_; j++ ){
			V[j] = fast_exp( scale*b[j]-mx );
			tt += V[j];
		}
		// Make it a probability
		for( int j=0; j<M_; j++ )
			V[j] /= tt;
		
		float * a = out + i*M_;
		for( int j=0; j<M_; j++ )
			if (relax == 1)
				a[j] = V[j];
			else
				a[j] = (1-relax)*a[j] + relax*V[j];
	}
	delete[] V;
}
///////////////////
/////  Debug  /////
///////////////////

void DenseCRF::unaryEnergy(const short* ass, float* result) {
	for( int i=0; i<N_; i++ )
		if ( 0 <= ass[i] && ass[i] < M_ )
			result[i] = unary_[ M_*i + ass[i] ];
		else
			result[i] = 0;
}
void DenseCRF::pairwiseEnergy(const short* ass, float* result, int term) {
	float * current = allocate( N_*M_ );
	// Build the current belief [binary assignment]
	for( int i=0,k=0; i<N_; i++ )
		for( int j=0; j<M_; j++, k++ )
			current[k] = (ass[i] == j);
	
	for( int i=0; i<N_*M_; i++ )
		next_[i] = 0;
	if (term == -1)
		for( unsigned int i=0; i<pairwise_.size(); i++ )
			pairwise_[i]->apply( next_, current, tmp_, M_ );
	else
		pairwise_[ term ]->apply( next_, current, tmp_, M_ );
	for( int i=0; i<N_; i++ )
		if ( 0 <= ass[i] && ass[i] < M_ )
			result[i] =-next_[ i*M_ + ass[i] ];
		else
			result[i] = 0;
	deallocate( current );
}
void DenseCRF::startInference(){
	// Initialize using the unary energies
	expAndNormalize( current_, unary_, -1 );
}
void DenseCRF::stepInference( float relax ){
#ifdef SSE_DENSE_CRF
	__m128 * sse_next_ = (__m128*)next_;
	__m128 * sse_unary_ = (__m128*)unary_;
	__m128 * sse_additional_unary_ = (__m128*)additional_unary_;
#endif
	// Set the unary potential
#ifdef SSE_DENSE_CRF
	for( int i=0; i<(N_*M_-1)/4+1; i++ )
		sse_next_[i] = - sse_unary_[i] - sse_additional_unary_[i];
#else
	for( int i=0; i<N_*M_; i++ )
		next_[i] = -unary_[i] - additional_unary_[i];
#endif
	
	// Add up all pairwise potentials
	for( unsigned int i=0; i<pairwise_.size(); i++ )
		pairwise_[i]->apply( next_, current_, tmp_, M_ );
	
	// Exponentiate and normalize
	expAndNormalize( current_, next_, 1.0, relax );
}
void DenseCRF::currentMap( short * result ){
	// Find the map
	for( int i=0; i<N_; i++ ){
		const float * p = current_ + i*M_;
		// Find the max and subtract it so that the exp doesn't explode
		float mx = p[0];
		int imx = 0;
		for( int j=1; j<M_; j++ )
			if( mx < p[j] ){
				mx = p[j];
				imx = j;
			}
		result[i] = imx;
	}
}
