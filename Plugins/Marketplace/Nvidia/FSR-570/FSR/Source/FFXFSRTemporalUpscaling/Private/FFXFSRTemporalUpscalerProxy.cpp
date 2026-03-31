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

#include "FFXFSRTemporalUpscalerProxy.h"

#if UE_VERSION_AT_LEAST(5, 1, 0)

#include "FFXFSRTemporalUpscaling.h"
#include "FFXFSRInclude.h"
#include "FFXFSRTemporalUpscalerHistory.h"
#include "SceneTextureParameters.h"
#include "TranslucentRendering.h"
#include "ScenePrivate.h"
#include "LogFFXFSR.h"
#include "LegacyScreenPercentageDriver.h"
#include "PlanarReflectionSceneProxy.h"
#include "ScreenSpaceRayTracing.h"
#include "Serialization/MemoryImage.h"
#include "Serialization/MemoryLayout.h"
#include "FXSystem.h"
#include "PostProcess/SceneRenderTargets.h"


//------------------------------------------------------------------------------------------------------
// FFXFSRTemporalUpscalerProxy implementation.
//------------------------------------------------------------------------------------------------------
FFXFSRTemporalUpscalerProxy::FFXFSRTemporalUpscalerProxy(FFXFSRTemporalUpscaler* TemporalUpscaler)
: TemporalUpscaler(TemporalUpscaler)
{
}

FFXFSRTemporalUpscalerProxy::~FFXFSRTemporalUpscalerProxy()
{
}

const TCHAR* FFXFSRTemporalUpscalerProxy::GetDebugName() const
{
	return TemporalUpscaler->GetDebugName();
}

IFFXFSRTemporalUpscaler::FOutputs FFXFSRTemporalUpscalerProxy::AddPasses(
	FRDGBuilder& GraphBuilder,
	const FFXFSRView& View,
	const FFXFSRPassInput& PassInputs) const
{
	return TemporalUpscaler->AddPasses(GraphBuilder, View, PassInputs);
}

IFFXFSRTemporalUpscaler* FFXFSRTemporalUpscalerProxy::Fork_GameThread(const class FSceneViewFamily& InViewFamily) const
{
	return new FFXFSRTemporalUpscalerProxy(TemporalUpscaler);
}

float FFXFSRTemporalUpscalerProxy::GetMinUpsampleResolutionFraction() const
{
	return TemporalUpscaler->GetMinUpsampleResolutionFraction();
}

float FFXFSRTemporalUpscalerProxy::GetMaxUpsampleResolutionFraction() const
{
	return TemporalUpscaler->GetMaxUpsampleResolutionFraction();
}

IScreenSpaceDenoiser::FReflectionsOutputs FFXFSRTemporalUpscalerProxy::DenoiseReflections(
	FRDGBuilder& GraphBuilder,
	const FViewInfo& View,
	FPreviousViewInfo* PreviousViewInfos,
	const FSceneTextureParameters& SceneTextures,
	const FReflectionsInputs& ReflectionInputs,
	const FReflectionsRayTracingConfig RayTracingConfig) const
{
	return TemporalUpscaler->DenoiseReflections(GraphBuilder, View, PreviousViewInfos, SceneTextures, ReflectionInputs, RayTracingConfig);
}

IScreenSpaceDenoiser::EShadowRequirements FFXFSRTemporalUpscalerProxy::GetShadowRequirements(
	const FViewInfo& View,
	const FLightSceneInfo& LightSceneInfo,
	const FShadowRayTracingConfig& RayTracingConfig) const
{
	return TemporalUpscaler->GetShadowRequirements(View, LightSceneInfo, RayTracingConfig);
}

void FFXFSRTemporalUpscalerProxy::DenoiseShadowVisibilityMasks(
	FRDGBuilder& GraphBuilder,
	const FViewInfo& View,
	FPreviousViewInfo* PreviousViewInfos,
	const FSceneTextureParameters& SceneTextures,
	const TStaticArray<FShadowVisibilityParameters, IScreenSpaceDenoiser::kMaxBatchSize>& InputParameters,
	const int32 InputParameterCount,
	TStaticArray<FShadowVisibilityOutputs, IScreenSpaceDenoiser::kMaxBatchSize>& Outputs) const
{

	return TemporalUpscaler->DenoiseShadowVisibilityMasks(GraphBuilder, View, PreviousViewInfos, SceneTextures, InputParameters, InputParameterCount, Outputs);
}

IScreenSpaceDenoiser::FPolychromaticPenumbraOutputs FFXFSRTemporalUpscalerProxy::DenoisePolychromaticPenumbraHarmonics(
	FRDGBuilder& GraphBuilder,
	const FViewInfo& View,
	FPreviousViewInfo* PreviousViewInfos,
	const FSceneTextureParameters& SceneTextures,
	const FPolychromaticPenumbraHarmonics& Inputs) const
{
	return TemporalUpscaler->DenoisePolychromaticPenumbraHarmonics(GraphBuilder, View, PreviousViewInfos, SceneTextures, Inputs);
}

IScreenSpaceDenoiser::FReflectionsOutputs FFXFSRTemporalUpscalerProxy::DenoiseWaterReflections(
	FRDGBuilder& GraphBuilder,
	const FViewInfo& View,
	FPreviousViewInfo* PreviousViewInfos,
	const FSceneTextureParameters& SceneTextures,
	const FReflectionsInputs& ReflectionInputs,
	const FReflectionsRayTracingConfig RayTracingConfig) const
{
	return TemporalUpscaler->DenoiseWaterReflections(GraphBuilder, View, PreviousViewInfos, SceneTextures, ReflectionInputs, RayTracingConfig);
}

IScreenSpaceDenoiser::FAmbientOcclusionOutputs FFXFSRTemporalUpscalerProxy::DenoiseAmbientOcclusion(
	FRDGBuilder& GraphBuilder,
	const FViewInfo& View,
	FPreviousViewInfo* PreviousViewInfos,
	const FSceneTextureParameters& SceneTextures,
	const FAmbientOcclusionInputs& ReflectionInputs,
	const FAmbientOcclusionRayTracingConfig RayTracingConfig) const
{
	return TemporalUpscaler->DenoiseAmbientOcclusion(GraphBuilder, View, PreviousViewInfos, SceneTextures, ReflectionInputs, RayTracingConfig);
}

FSSDSignalTextures FFXFSRTemporalUpscalerProxy::DenoiseDiffuseIndirect(
	FRDGBuilder& GraphBuilder,
	const FViewInfo& View,
	FPreviousViewInfo* PreviousViewInfos,
	const FSceneTextureParameters& SceneTextures,
	const FDiffuseIndirectInputs& Inputs,
	const FAmbientOcclusionRayTracingConfig Config) const
{
	return TemporalUpscaler->DenoiseDiffuseIndirect(GraphBuilder, View, PreviousViewInfos, SceneTextures, Inputs, Config);
}

#if ENGINE_HAS_DENOISE_INDIRECT
FSSDSignalTextures FFXFSRTemporalUpscalerProxy::DenoiseIndirect(
	FRDGBuilder& GraphBuilder,
	const FViewInfo& View,
	FPreviousViewInfo* PreviousViewInfos,
	const FSceneTextureParameters& SceneTextures,
	const FIndirectInputs& Inputs,
	const FAmbientOcclusionRayTracingConfig Config) const
{
	return TemporalUpscaler->DenoiseIndirect(GraphBuilder, View, PreviousViewInfos, SceneTextures, Inputs, Config);
}
#endif

IScreenSpaceDenoiser::FDiffuseIndirectOutputs FFXFSRTemporalUpscalerProxy::DenoiseSkyLight(
	FRDGBuilder& GraphBuilder,
	const FViewInfo& View,
	FPreviousViewInfo* PreviousViewInfos,
	const FSceneTextureParameters& SceneTextures,
	const FDiffuseIndirectInputs& Inputs,
	const FAmbientOcclusionRayTracingConfig Config) const
{
	return TemporalUpscaler->DenoiseSkyLight(GraphBuilder, View, PreviousViewInfos, SceneTextures, Inputs, Config);
}

#if UE_VERSION_OLDER_THAN(5, 4, 0)
IScreenSpaceDenoiser::FDiffuseIndirectOutputs FFXFSRTemporalUpscalerProxy::DenoiseReflectedSkyLight(
	FRDGBuilder& GraphBuilder,
	const FViewInfo& View,
	FPreviousViewInfo* PreviousViewInfos,
	const FSceneTextureParameters& SceneTextures,
	const FDiffuseIndirectInputs& Inputs,
	const FAmbientOcclusionRayTracingConfig Config) const
{
	return TemporalUpscaler->DenoiseReflectedSkyLight(GraphBuilder, View, PreviousViewInfos, SceneTextures, Inputs, Config);
}
#endif

FSSDSignalTextures FFXFSRTemporalUpscalerProxy::DenoiseDiffuseIndirectHarmonic(
	FRDGBuilder& GraphBuilder,
	const FViewInfo& View,
	FPreviousViewInfo* PreviousViewInfos,
	const FSceneTextureParameters& SceneTextures,
	const FDiffuseIndirectHarmonic& Inputs,
	const HybridIndirectLighting::FCommonParameters& CommonDiffuseParameters) const

{
	return TemporalUpscaler->DenoiseDiffuseIndirectHarmonic(GraphBuilder, View, PreviousViewInfos, SceneTextures, Inputs, CommonDiffuseParameters);
}

bool FFXFSRTemporalUpscalerProxy::SupportsScreenSpaceDiffuseIndirectDenoiser(EShaderPlatform Platform) const
{
	return TemporalUpscaler->SupportsScreenSpaceDiffuseIndirectDenoiser(Platform);
}

FSSDSignalTextures FFXFSRTemporalUpscalerProxy::DenoiseScreenSpaceDiffuseIndirect(
	FRDGBuilder& GraphBuilder,
	const FViewInfo& View,
	FPreviousViewInfo* PreviousViewInfos,
	const FSceneTextureParameters& SceneTextures,
	const FDiffuseIndirectInputs& Inputs,
	const FAmbientOcclusionRayTracingConfig Config) const
{
	return TemporalUpscaler->DenoiseScreenSpaceDiffuseIndirect(GraphBuilder, View, PreviousViewInfos, SceneTextures, Inputs, Config);
}

#endif
