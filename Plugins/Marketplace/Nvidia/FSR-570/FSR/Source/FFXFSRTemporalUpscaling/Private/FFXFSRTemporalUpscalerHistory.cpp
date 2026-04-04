// This file is part of the FSR Upscaling Unreal Engine Plugin.
//
// Copyright (c) 2023-2025 Advanced Micro Devices, Inc. All rights reserved.
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
// The above copyright notice and this permission notice shall be included in
// all copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
// THE SOFTWARE.

#include "FFXFSRTemporalUpscalerHistory.h"
#include "FFXFSRTemporalUpscaler.h"
#include "FFXFSRTemporalUpscaling.h"

const TCHAR* FFXFSRTemporalUpscalerHistory::FfxFsrDebugName = TEXT("FFXFSRTemporalUpscaler");

TCHAR const* FFXFSRTemporalUpscalerHistory::GetUpscalerName()
{
	return FfxFsrDebugName;
}

uint64 FFXFSRTemporalUpscalerHistory::GetFsrHistoryIdFromDebugName()
{
	return *(uint32*)FFXFSRTemporalUpscalerHistory::FfxFsrDebugName;
}

FFXFSRTemporalUpscalerHistory::FFXFSRTemporalUpscalerHistory(FSRStateRef NewState, FFXFSRTemporalUpscaler* _Upscaler, TRefCountPtr<IPooledRenderTarget> InMotionVectors)
{
	FsrHistoryId = FFXFSRTemporalUpscalerHistory::GetFsrHistoryIdFromDebugName();
	MotionVectors = InMotionVectors;
	Upscaler = _Upscaler;
	SetState(NewState);
}

FFXFSRTemporalUpscalerHistory::~FFXFSRTemporalUpscalerHistory()
{
	if (FFXFSRTemporalUpscalingModule::IsInitialized())
	{
		Upscaler->ReleaseState(Fsr);
	}
}

bool FFXFSRTemporalUpscalerHistory::HasFsrHistoryId() const {
	return FsrHistoryId == FFXFSRTemporalUpscalerHistory::GetFsrHistoryIdFromDebugName();
}

#if UE_VERSION_AT_LEAST(5, 3, 0)
const TCHAR* FFXFSRTemporalUpscalerHistory::GetDebugName() const {
	// this has to match FFXFSRTemporalUpscalerHistory::GetDebugName()
	return FfxFsrDebugName;
}

uint64 FFXFSRTemporalUpscalerHistory::GetGPUSizeBytes() const {
	// 5.3 not done
	return 0;
}
#endif

void FFXFSRTemporalUpscalerHistory::SetState(FSRStateRef NewState)
{
	Upscaler->ReleaseState(Fsr);
	Fsr = NewState;
}

ffxContext* FFXFSRTemporalUpscalerHistory::GetFSRContext() const
{
	return Fsr.IsValid() ? &Fsr->Fsr : nullptr;
}

ffxCreateContextDescUpscale* FFXFSRTemporalUpscalerHistory::GetFSRContextDesc() const
{
	return Fsr.IsValid() ? &Fsr->Params : nullptr;
}

TRefCountPtr<IPooledRenderTarget> FFXFSRTemporalUpscalerHistory::GetMotionVectors() const
{
	return Fsr.IsValid() ? MotionVectors : nullptr;
}