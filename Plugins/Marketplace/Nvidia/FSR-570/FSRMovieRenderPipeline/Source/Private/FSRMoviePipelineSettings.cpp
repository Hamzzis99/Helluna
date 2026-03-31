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

#include "FSRMoviePipelineSettings.h"

#include "FFXFSRTemporalUpscaling.h"

#include "MovieRenderPipelineDataTypes.h"
#include "SceneView.h"
#include "Templates/SharedPointer.h"

#define LOCTEXT_NAMESPACE "FSRMoviePipelineSettings"

UFSRMoviePipelineSettings::UFSRMoviePipelineSettings()
: FSRQuality(EFSRMoviePipelineQuality::Quality)
{

}

void UFSRMoviePipelineSettings::ValidateStateImpl()
{
    Super::ValidateStateImpl();

	IFFXFSRTemporalUpscalingModule& FSRModuleInterface = FModuleManager::GetModuleChecked<IFFXFSRTemporalUpscalingModule>(TEXT("FFXFSRTemporalUpscaling"));
	if (!FSRModuleInterface.IsPlatformSupported(GMaxRHIShaderPlatform))
	{	
		ValidationResults.Add(FText::FromString(TEXT("FSR Upscaling is unsupported on this platform.")));
		ValidationState = EMoviePipelineValidationState::Warnings;
	}
}

void UFSRMoviePipelineSettings::GetFormatArguments(FMoviePipelineFormatArgs& InOutFormatArgs) const
{
    Super::GetFormatArguments(InOutFormatArgs);

	IFFXFSRTemporalUpscalingModule& FSRModuleInterface = FModuleManager::GetModuleChecked<IFFXFSRTemporalUpscalingModule>(TEXT("FFXFSRTemporalUpscaling"));
	if (FSRModuleInterface.IsPlatformSupported(GMaxRHIShaderPlatform))
	{
		InOutFormatArgs.FileMetadata.Add(TEXT("amd/fidelityFxFSRQualityMode"), StaticEnum<EFSRMoviePipelineQuality>()->GetDisplayNameTextByIndex((int32)FSRQuality).ToString());
		InOutFormatArgs.FilenameArguments.Add(TEXT("fidelityFxFSRQualityMode"), StaticEnum<EFSRMoviePipelineQuality>()->GetDisplayNameTextByIndex((int32)FSRQuality).ToString());
	}
	else
	{
		InOutFormatArgs.FileMetadata.Add(TEXT("amd/fidelityFxFSRQualityMode"), TEXT("Unsupported"));
		InOutFormatArgs.FilenameArguments.Add(TEXT("fidelityFxFSRQualityMode"), TEXT("Unsupported"));
	}
}

void UFSRMoviePipelineSettings::SetupViewFamily(FSceneViewFamily& ViewFamily)
{
	static IConsoleVariable* CVarScreenPercentage = IConsoleManager::Get().FindConsoleVariable(TEXT("r.ScreenPercentage"));
	static IConsoleVariable* CVarFSREnabled = IConsoleManager::Get().FindConsoleVariable(TEXT("r.FidelityFX.FSR.Enabled"));
	IFFXFSRTemporalUpscalingModule& FSRModuleInterface = FModuleManager::GetModuleChecked<IFFXFSRTemporalUpscalingModule>(TEXT("FFXFSRTemporalUpscaling"));
	if (ViewFamily.ViewMode == EViewModeIndex::VMI_Lit && CVarFSREnabled->GetInt())
	{
		float ScreenPercentage = FSRQuality == EFSRMoviePipelineQuality::Native ? 100.f : FSRModuleInterface.GetResolutionFraction((uint32)FSRQuality) * 100.0f;
		if (CVarScreenPercentage)
		{
			CVarScreenPercentage->Set(ScreenPercentage, ECVF_SetByCode);
		}
	}
}
