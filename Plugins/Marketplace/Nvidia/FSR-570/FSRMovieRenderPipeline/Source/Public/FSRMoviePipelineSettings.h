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

#include "MoviePipelineViewFamilySetting.h"
#include "FSRMoviePipelineSettings.generated.h"

class SceneViewFamily;

UENUM(BlueprintType)
enum class EFSRMoviePipelineQuality : uint8
{
    Unused              UMETA(Hidden),
    Native              UMETA(DisplayName = "Native AA"),
    Quality             UMETA(DisplayName = "Quality"),
    Balanced            UMETA(DisplayName = "Balanced"),
    Performance         UMETA(DisplayName = "Performance"),
    UltraPerformance    UMETA(DisplayName = "UltraPerformance")
};

UCLASS(BlueprintType, DisplayName="FSR Upscaling Settings")
class FSRMOVIERENDERPIPELINE_API UFSRMoviePipelineSettings : public UMoviePipelineViewFamilySetting
{
    GENERATED_BODY()
public:
    UFSRMoviePipelineSettings();

    virtual void ValidateStateImpl() override;

    virtual void GetFormatArguments(FMoviePipelineFormatArgs& InOutFormatArgs) const override;

	virtual void SetupViewFamily(FSceneViewFamily& ViewFamily) override;

    UPROPERTY(EditAnywhere, BlueprintReadWRite, Category = "Settings", DisplayName = "Quality Mode")
    EFSRMoviePipelineQuality FSRQuality;
};