/*
 * Copyright 2026, Haiku.
 * Distributed under the terms of the MIT License.
 */
#ifndef GAUSSIAN_BLUR_H
#define GAUSSIAN_BLUR_H

#include <SupportDefs.h>

#include <vector>


struct GaussianKernel {
						GaussianKernel();

	int32				radius;
	int32				weightShift;
	std::vector<uint16>	weights;
};


class GaussianBlurLibrary {
public:
	static const GaussianKernel&	KernelForRadius(int32 radius);

	static void				BlurRGBA32(const uint32* input, uint32* output,
									int32 width, int32 height,
									const GaussianKernel& kernel,
									std::vector<uint32>& temp);

private:
	static GaussianKernel			_BuildKernel(int32 radius);
};


#endif // GAUSSIAN_BLUR_H
