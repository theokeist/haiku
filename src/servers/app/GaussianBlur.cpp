/*
 * Copyright 2026, Haiku.
 * Distributed under the terms of the MIT License.
 */

#include "GaussianBlur.h"

#include <algorithm>
#include <math.h>


GaussianKernel::GaussianKernel()
	:
	radius(0),
	weightShift(14)
{
}


namespace {

const int32 kMinRadius = 1;
const int32 kMaxRadius = 32;

std::vector<GaussianKernel> sKernels;


static inline int32
_ClampIndex(int32 value, int32 max)
{
	if (value < 0)
		return 0;
	if (value > max)
		return max;
	return value;
}


static inline uint32
_ComposePixel(uint32 b, uint32 g, uint32 r, uint32 a)
{
	if (b > 255)
		b = 255;
	if (g > 255)
		g = 255;
	if (r > 255)
		r = 255;
	if (a > 255)
		a = 255;
	return (a << 24) | (r << 16) | (g << 8) | b;
}

} // namespace


const GaussianKernel&
GaussianBlurLibrary::KernelForRadius(int32 radius)
{
	if (radius < kMinRadius)
		radius = kMinRadius;
	if (radius > kMaxRadius)
		radius = kMaxRadius;

	if ((int32)sKernels.size() != kMaxRadius + 1)
		sKernels.resize(kMaxRadius + 1);

	GaussianKernel& kernel = sKernels[radius];
	if (kernel.weights.empty())
		kernel = _BuildKernel(radius);

	return kernel;
}


void
GaussianBlurLibrary::BlurRGBA32(const uint32* input, uint32* output,
	int32 width, int32 height, const GaussianKernel& kernel,
	std::vector<uint32>& temp)
{
	if (input == NULL || output == NULL || width <= 0 || height <= 0)
		return;

	const int32 radius = kernel.radius;
	const int32 shift = kernel.weightShift;
	if (radius <= 0 || kernel.weights.empty())
		return;

	const int32 pixelCount = width * height;
	if ((int32)temp.size() != pixelCount)
		temp.resize(pixelCount);

	const int32 xMax = width - 1;
	const int32 yMax = height - 1;
	const uint16* weights = &kernel.weights[0];
	const int32 kernelSize = radius * 2 + 1;

	for (int32 y = 0; y < height; y++) {
		for (int32 x = 0; x < width; x++) {
			uint32 sumB = 0;
			uint32 sumG = 0;
			uint32 sumR = 0;
			uint32 sumA = 0;

			for (int32 k = 0; k < kernelSize; k++) {
				int32 sampleX = _ClampIndex(x + k - radius, xMax);
				uint32 pixel = input[y * width + sampleX];
				uint32 weight = weights[k];
				sumB += (pixel & 0xff) * weight;
				sumG += ((pixel >> 8) & 0xff) * weight;
				sumR += ((pixel >> 16) & 0xff) * weight;
				sumA += ((pixel >> 24) & 0xff) * weight;
			}

			temp[y * width + x] = _ComposePixel(sumB >> shift, sumG >> shift,
				sumR >> shift, sumA >> shift);
		}
	}

	for (int32 y = 0; y < height; y++) {
		for (int32 x = 0; x < width; x++) {
			uint32 sumB = 0;
			uint32 sumG = 0;
			uint32 sumR = 0;
			uint32 sumA = 0;

			for (int32 k = 0; k < kernelSize; k++) {
				int32 sampleY = _ClampIndex(y + k - radius, yMax);
				uint32 pixel = temp[sampleY * width + x];
				uint32 weight = weights[k];
				sumB += (pixel & 0xff) * weight;
				sumG += ((pixel >> 8) & 0xff) * weight;
				sumR += ((pixel >> 16) & 0xff) * weight;
				sumA += ((pixel >> 24) & 0xff) * weight;
			}

			output[y * width + x] = _ComposePixel(sumB >> shift, sumG >> shift,
				sumR >> shift, sumA >> shift);
		}
	}
}


GaussianKernel
GaussianBlurLibrary::_BuildKernel(int32 radius)
{
	GaussianKernel kernel;
	kernel.radius = radius;
	kernel.weightShift = 14;

	const int32 size = radius * 2 + 1;
	kernel.weights.resize(size);

	const double sigma = std::max(1.0, radius / 2.0);
	const double twoSigma2 = 2.0 * sigma * sigma;

	double sum = 0.0;
	for (int32 i = -radius; i <= radius; i++) {
		double value = exp(-(double)(i * i) / twoSigma2);
		kernel.weights[i + radius] = (uint16)(value * 65535.0);
		sum += value;
	}

	uint32 fixedSum = 0;
	for (int32 i = 0; i < size; i++) {
		double normalized = (kernel.weights[i] / 65535.0) / sum;
		uint32 fixed = (uint32)(normalized * (1 << kernel.weightShift));
		if (fixed == 0)
			fixed = 1;
		kernel.weights[i] = (uint16)fixed;
		fixedSum += fixed;
	}

	if (fixedSum != (uint32)(1 << kernel.weightShift)) {
		int32 center = radius;
		int32 delta = (1 << kernel.weightShift) - (int32)fixedSum;
		int32 corrected = (int32)kernel.weights[center] + delta;
		if (corrected < 1)
			corrected = 1;
		kernel.weights[center] = (uint16)corrected;
	}

	return kernel;
}
