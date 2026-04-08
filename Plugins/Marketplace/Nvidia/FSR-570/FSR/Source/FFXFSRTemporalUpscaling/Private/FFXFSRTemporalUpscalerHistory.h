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

#pragma once

#include "CoreMinimal.h"
#include "SceneRendering.h"
#include "FFXFSRInclude.h"
#include "FFXFSRHistory.h"

class FFXFSRTemporalUpscaler;

#define FFX_FSR_UPSCALER_MAX_NUM_BUFFERS 3

//-------------------------------------------------------------------------------------
// The FSR state wrapper, deletion is handled by the RHI so that they aren't removed out from under the GPU.
//-------------------------------------------------------------------------------------
struct FFXFSRState : public FRHIResource
{
	FFXFSRState(IFFXSharedBackend* InBackend)
	: FRHIResource(RRT_None)
	, Backend(InBackend)
	, LastUsedFrame(~0u)
	{
	}
	~FFXFSRState()
	{
		Backend->ffxDestroyContext(&Fsr);
	}

	uint32 AddRef() const
	{
		return FRHIResource::AddRef();
	}

	uint32 Release() const
	{
		return FRHIResource::Release();
	}

	uint32 GetRefCount() const
	{
		return FRHIResource::GetRefCount();
	}

	void PushActivity(FGPUFenceRHIRef Fence)
	{
		// only produce from the Render thread so we don't have to synchronize.
		check(IsInRenderingThread());

		ActiveFences.Enqueue(Fence);
	}

	bool PollActivity()
	{
		// only consume from the Render thread so we don't have to synchronize.
		check(IsInRenderingThread());

		while (!ActiveFences.IsEmpty())
		{
			FGPUFenceRHIRef* ActiveFence = ActiveFences.Peek();
			if (ActiveFence && ActiveFence->IsValid() && (*ActiveFence)->Poll())
			{
				ActiveFences.Pop();
			}
			else
			{
				break;
			}
		}

		return ActiveFences.IsEmpty();
	}

	IFFXSharedBackend* Backend;
	ffxCreateContextDescUpscale Params;
	ffxContext Fsr;
	uint64 LastUsedFrame;
	uint32 ViewID;
	uint32 RequestedFSRProvider;

private:
	TQueue<FGPUFenceRHIRef, EQueueMode::Spsc> ActiveFences;
};
typedef TRefCountPtr<FFXFSRState> FSRStateRef;

#if UE_VERSION_OLDER_THAN(5, 6, 0)
#define FReturnedRefCountValue uint32
#endif

//-------------------------------------------------------------------------------------
// The ICustomTemporalAAHistory for FSR, this retains the FSR state object.
//-------------------------------------------------------------------------------------
class FFXFSRTemporalUpscalerHistory final : public IFFXFSRHistory, public FRefCountBase
{
public:
	FFXFSRTemporalUpscalerHistory(FSRStateRef NewState, FFXFSRTemporalUpscaler* Upscaler, TRefCountPtr<IPooledRenderTarget> InMotionVectors);

	virtual ~FFXFSRTemporalUpscalerHistory();

#if UE_VERSION_AT_LEAST(5, 3, 0)
	virtual const TCHAR* GetDebugName() const override;
	virtual uint64 GetGPUSizeBytes() const override;
#endif

	ffxContext* GetFSRContext() const final;
    ffxCreateContextDescUpscale* GetFSRContextDesc() const final;
	TRefCountPtr<IPooledRenderTarget> GetMotionVectors() const final;

	void SetState(FSRStateRef NewState);

	bool HasFsrHistoryId() const;

	inline FSRStateRef const& GetState() const
	{
		return Fsr;
	}
	
	FReturnedRefCountValue AddRef() const final
	{
		return FRefCountBase::AddRef();
	}

	uint32 Release() const final
	{
		return FRefCountBase::Release();
	}

	uint32 GetRefCount() const final
	{
		return FRefCountBase::GetRefCount();
	}

	static TCHAR const* GetUpscalerName();
	static uint64 GetFsrHistoryIdFromDebugName();

private:
	static TCHAR const* FfxFsrDebugName;
	uint64 FsrHistoryId;
	FSRStateRef Fsr;
	FFXFSRTemporalUpscaler* Upscaler;
	TRefCountPtr<IPooledRenderTarget> MotionVectors;
};
