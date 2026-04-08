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

#include "Modules/ModuleManager.h"
#include "UObject/ObjectMacros.h"
#include "UObject/Object.h"
#include "Engine/DeveloperSettings.h"
#include "HAL/IConsoleManager.h"
#include "Engine/EngineTypes.h"

#include "FFXFSRSettings.generated.h"

//-------------------------------------------------------------------------------------
// The official FSR Upscaling quality modes.
//-------------------------------------------------------------------------------------
UENUM()
enum class EFFXFSRQualityMode : int32
{
	NativeAA UMETA(DisplayName = "Native AA"),
	Quality UMETA(DisplayName = "Quality"),
	Balanced UMETA(DisplayName = "Balanced"),
	Performance UMETA(DisplayName = "Performance"),
	UltraPerformance UMETA(DisplayName = "Ultra Performance"),
};

//-------------------------------------------------------------------------------------
// The support texture formats for the FSR Upscaling history data.
//-------------------------------------------------------------------------------------
UENUM()
enum class EFFXFSRHistoryFormat : int32
{
	FloatRGBA UMETA(DisplayName = "PF_FloatRGBA"),
	FloatR11G11B10 UMETA(DisplayName = "PF_FloatR11G11B10"),
};

//-------------------------------------------------------------------------------------
// The modes for enabling an extra pass to de-dither rendering before handing over to
// FSR Upscaling to avoid over-thinning.
//-------------------------------------------------------------------------------------
UENUM()
enum class EFFXFSRDeDitherMode : int32
{
	Off UMETA(DisplayName = "Off"),
	Full UMETA(DisplayName = "Full"),
	HairOnly UMETA(DisplayName = "Hair Only"),
};

//-------------------------------------------------------------------------------------
// The modes for forcing Landscape Hierachical Instance Static Model to not be Static.
//-------------------------------------------------------------------------------------
UENUM()
enum class EFFXFSRLandscapeHISMMode : int32
{
	Off UMETA(DisplayName = "Off"),
	AllStatic UMETA(DisplayName = "All Instances"),
	StaticWPO UMETA(DisplayName = "Instances with World-Position-Offset"),
};

//-------------------------------------------------------------------------------------
// The modes for rendering UI when using Frame Generation.
//-------------------------------------------------------------------------------------
UENUM()
enum class EFFXFSRFrameGenUIMode : int32
{
	SlateRedraw UMETA(DisplayName = "Slate Redraw"),
	UIExtraction UMETA(DisplayName = "UI Extraction")
};

//-------------------------------------------------------------------------------------
// The modes for pacing frames when using the RHI backend.
//-------------------------------------------------------------------------------------
UENUM()
enum class EFFXFSRPaceRHIFrameMode : int32
{
	None UMETA(DisplayName = "None"),
	CustomPresentVSync UMETA(DisplayName = "Custom Present VSync")
};

//------------------------------------------------------------------------------------------------------
// Console variables that control how FSR Upscaling operates.
//------------------------------------------------------------------------------------------------------
extern FFXFSRSETTINGS_API TAutoConsoleVariable<int32> CVarEnableFSR;
extern FFXFSRSETTINGS_API TAutoConsoleVariable<int32> CVarEnableFSRInEditor;
extern FFXFSRSETTINGS_API TAutoConsoleVariable<int32> CVarFSRAdjustMipBias;
extern FFXFSRSETTINGS_API TAutoConsoleVariable<int32> CVarFSRForceVertexDeformationOutputsVelocity;
extern FFXFSRSETTINGS_API TAutoConsoleVariable<int32> CVarFSRForceLandscapeHISMMobility;
extern FFXFSRSETTINGS_API TAutoConsoleVariable<float> CVarFSRSharpness;
extern FFXFSRSETTINGS_API TAutoConsoleVariable<int32> CVarFSRAutoExposure;
extern FFXFSRSETTINGS_API TAutoConsoleVariable<int32> CVarFSRHistoryFormat;
extern FFXFSRSETTINGS_API TAutoConsoleVariable<int32> CVarFSRCreateReactiveMask;
extern FFXFSRSETTINGS_API TAutoConsoleVariable<float> CVarFSRReactiveMaskReflectionScale;
extern FFXFSRSETTINGS_API TAutoConsoleVariable<float> CVarFSRReactiveMaskRoughnessScale;
extern FFXFSRSETTINGS_API TAutoConsoleVariable<float> CVarFSRReactiveMaskRoughnessBias;
extern FFXFSRSETTINGS_API TAutoConsoleVariable<float> CVarFSRReactiveMaskRoughnessMaxDistance;
extern FFXFSRSETTINGS_API TAutoConsoleVariable<int32> CVarFSRReactiveMaskRoughnessForceMaxDistance;
extern FFXFSRSETTINGS_API TAutoConsoleVariable<float> CVarFSRReactiveMaskReflectionLumaBias;
extern FFXFSRSETTINGS_API TAutoConsoleVariable<float> CVarFSRReactiveHistoryTranslucencyBias;
extern FFXFSRSETTINGS_API TAutoConsoleVariable<float> CVarFSRReactiveHistoryTranslucencyLumaBias;
extern FFXFSRSETTINGS_API TAutoConsoleVariable<float> CVarFSRReactiveMaskTranslucencyBias;
extern FFXFSRSETTINGS_API TAutoConsoleVariable<float> CVarFSRReactiveMaskTranslucencyLumaBias;
extern FFXFSRSETTINGS_API TAutoConsoleVariable<float> CVarFSRReactiveMaskTranslucencyMaxDistance;
extern FFXFSRSETTINGS_API TAutoConsoleVariable<float> CVarFSRReactiveMaskForceReactiveMaterialValue;
extern FFXFSRSETTINGS_API TAutoConsoleVariable<int32> CVarFSRReactiveMaskReactiveShadingModelID;
extern FFXFSRSETTINGS_API TAutoConsoleVariable<int32> CVarFSRUseExperimentalSSRDenoiser;
extern FFXFSRSETTINGS_API TAutoConsoleVariable<int32> CVarFSRDeDitherMode;
extern FFXFSRSETTINGS_API TAutoConsoleVariable<int32> CVarFSRQualityMode;
extern FFXFSRSETTINGS_API TAutoConsoleVariable<int32> CVarFSRQuantizeInternalTextures;
extern FFXFSRSETTINGS_API TAutoConsoleVariable<float> CVarFSRReactiveMaskPreDOFTranslucencyScale;
extern FFXFSRSETTINGS_API TAutoConsoleVariable<int32> CVarFSRReactiveMaskPreDOFTranslucencyMax;
extern FFXFSRSETTINGS_API TAutoConsoleVariable<float> CVarFSRReactiveMaskCustomStencilScale;
extern FFXFSRSETTINGS_API TAutoConsoleVariable<float> CVarFSRReactiveHistoryCustomStencilScale;
extern FFXFSRSETTINGS_API TAutoConsoleVariable<float> CVarFSRReactiveMaskDeferredDecalScale;
extern FFXFSRSETTINGS_API TAutoConsoleVariable<float> CVarFSRReactiveHistoryDeferredDecalScale;
extern FFXFSRSETTINGS_API TAutoConsoleVariable<int32> CVarFSRCustomStencilMask;
extern FFXFSRSETTINGS_API TAutoConsoleVariable<int32> CVarFSRCustomStencilShift;
extern FFXFSRSETTINGS_API TAutoConsoleVariable<float> CVarFSRReactiveMaskTAAResponsiveValue;
extern FFXFSRSETTINGS_API TAutoConsoleVariable<float> CVarFSRReactiveHistoryTAAResponsiveValue;
extern FFXFSRSETTINGS_API TAutoConsoleVariable<float> CVarFSRVelocityFactor;
extern FFXFSRSETTINGS_API TAutoConsoleVariable<float> CVarFSRReactivenessScale;
extern FFXFSRSETTINGS_API TAutoConsoleVariable<float> CVarFSRShadingChangeScale;
extern FFXFSRSETTINGS_API TAutoConsoleVariable<float> CVarFSRAccumulationAddedPerFrame;
extern FFXFSRSETTINGS_API TAutoConsoleVariable<float> CVarFSRMinDisocclutionAccumulation;
extern FFXFSRSETTINGS_API TAutoConsoleVariable<int32> CVarFSRDeferDelete;
extern FFXFSRSETTINGS_API TAutoConsoleVariable<int32> CVarRequestFSRProvider;

//------------------------------------------------------------------------------------------------------
// Console variables for Frame Interpolation.
//------------------------------------------------------------------------------------------------------
extern FFXFSRSETTINGS_API TAutoConsoleVariable<int32> CVarEnableFFXFI;
extern FFXFSRSETTINGS_API TAutoConsoleVariable<int32> CVarFFXFICaptureDebugUI;
extern FFXFSRSETTINGS_API TAutoConsoleVariable<int32> CVarFFXFIUpdateGlobalFrameTime;
extern FFXFSRSETTINGS_API TAutoConsoleVariable<int32> CVarFFXFIModifySlateDeltaTime;
extern FFXFSRSETTINGS_API TAutoConsoleVariable<int32> CVarFFXFIUIMode;
extern FFXFSRSETTINGS_API TAutoConsoleVariable<int32> CVarFFXFIUseDistortionTexture;
#if (UE_BUILD_DEBUG || UE_BUILD_DEVELOPMENT || UE_BUILD_TEST)
extern FFXFSRSETTINGS_API TAutoConsoleVariable<int32> CVarFFXFIShowDebugTearLines;
extern FFXFSRSETTINGS_API TAutoConsoleVariable<int32> CVarFFXFIShowDebugView;
#endif

//-------------------------------------------------------------------------------------
// Console variables for the D3D12 backend.
//-------------------------------------------------------------------------------------
extern FFXFSRSETTINGS_API TAutoConsoleVariable<int32> CVarFSROverrideSwapChainDX12;
extern FFXFSRSETTINGS_API TAutoConsoleVariable<int32> CVarFSRAllowAsyncWorkloads;

//-------------------------------------------------------------------------------------
// Settings for FSR exposed through the Editor UI.
//-------------------------------------------------------------------------------------
UCLASS(Config = Engine, DefaultConfig, DisplayName = "FSR Upscaling")
class FFXFSRSETTINGS_API UFFXFSRSettings : public UDeveloperSettings
{
	GENERATED_UCLASS_BODY()
public:
	virtual FName GetContainerName() const override;
	virtual FName GetCategoryName() const override;
	virtual FName GetSectionName() const override;

	virtual void PostInitProperties() override;
#if WITH_EDITOR
	virtual void PostEditChangeProperty(struct FPropertyChangedEvent& PropertyChangedEvent) override;
#endif

public:
	UPROPERTY(Config, EditAnywhere, Category = "General Settings", meta = (ConsoleVariable = "r.FidelityFX.FSR.Enabled", DisplayName = "Enabled"))
		bool bEnabled;

	UPROPERTY(Config, EditAnywhere, Category = "General Settings", meta = (ConsoleVariable = "r.FidelityFX.FSR.AutoExposure", DisplayName = "Auto Exposure", ToolTip = "Enable to use FSR's own auto-exposure, otherwise the engine's auto-exposure value is used."))
		bool bAutoExposure;

	UPROPERTY(Config, EditAnywhere, Category = "General Settings", meta = (ConsoleVariable = "r.FidelityFX.FSR.EnabledInEditorViewport", DisplayName = "Enabled in Editor Viewport", ToolTip = "When enabled use FSR Upscaling by default in the Editor viewports."))
		bool bEnabledInEditorViewport;

	UPROPERTY(Config, EditAnywhere, Category = "General Settings", meta = (ConsoleVariable = "r.FidelityFX.FSR.UseSSRExperimentalDenoiser", DisplayName = "Use SSR Experimental Denoiser", ToolTip = "Set to 1 to use r.SSR.ExperimentalDenoiser when FSR is enabled. This is required when r.FidelityFX.FSR.CreateReactiveMask is enabled as the FSR plugin sets r.SSR.ExperimentalDenoiser to 1 in order to capture reflection data to generate the reactive mask."))
		bool bUseSSRExperimentalDenoiser;

	UPROPERTY(Config, EditAnywhere, Category = "Frame Generation Settings", meta = (ConsoleVariable = "r.FidelityFX.FI.Enabled", DisplayName = "Frame Generation Enabled"))
		bool bFrameGenEnabled;

	UPROPERTY(Config, EditAnywhere, Category = "Frame Generation Settings", meta = (ConsoleVariable = "r.FidelityFX.FI.CaptureDebugUI", DisplayName = "Capture Debug UI", ToolTip = "Force FidelityFX Frame Generation to detect and copy any debug UI which only renders on the first invocation of Slate's DrawWindow command."))
		bool bCaptureDebugUI;

	UPROPERTY(Config, EditAnywhere, Category = "Frame Generation Settings", meta = (ConsoleVariable = "r.FidelityFX.FI.UpdateGlobalFrameTime", DisplayName = "Update Global Frame Time", ToolTip = "Update the GAverageMS and GAverageFPS engine globals with the frame time & FPS including frame generation."))
		bool bUpdateGlobalFrameTime;

	UPROPERTY(Config, EditAnywhere, Category = "Frame Generation Settings", meta = (ConsoleVariable = "r.FidelityFX.FI.ModifySlateDeltaTime", DisplayName = "Modify Slate Delta Time", ToolTip = "Set the FSlateApplication delta time to 0.0 when redrawing the UI for the 'Slate Redraw' UI mode to prevent widgets' NativeTick implementations updating incorrectly, ignored when using 'UI Extraction'."))
		bool bModifySlateDeltaTime;

	UPROPERTY(Config, EditAnywhere, Category = "Frame Generation Settings", meta = (ConsoleVariable = "r.FidelityFX.FI.UIMode", DisplayName = "UI Mode", ToolTip = "The method to render the UI when using Frame Generation.\nSlate Redraw (0): will cause Slate to render the UI on to both the real & generation images each frame, this is higher quality but requires UI elements to be able to render multiple times per game frame.\nUI Extraction (1): will compare the pre & post UI frame to extract the UI and copy it on to the generated frame, this might result in lower quality for translucent UI elements but doesn't require re-rendering UI elements."))
		EFFXFSRFrameGenUIMode UIMode;

	UPROPERTY(Config, EditAnywhere, Category = "Frame Generation Settings", meta = (ConsoleVariable = "r.FidelityFX.FI.UseDistortionTexture", DisplayName = "Use Distortion Texture", ToolTip = "Set to 1 to bind the UE distortion texture to the Frame Interpolation context to better interpolate distortion, set to 0 to ignore distortion."))
		bool bUseDistortionTexture;

	UPROPERTY(Config, EditAnywhere, Category = "Frame Generation Settings", meta = (ConsoleVariable = "r.FidelityFX.FI.AllowAsyncWorkloads", DisplayName = "D3D12 Async. Interpolation", ToolTip = "True to use async. execution of Frame Interpolation, false to run Frame Interpolation synchronously with the game."))
		bool bD3D12AsyncInterpolation;

	UPROPERTY(Config, EditAnywhere, Category = "Frame Generation Settings", meta = (ConsoleVariable = "r.FidelityFX.FI.OverrideSwapChainDX12", DisplayName = "D3D12 Async. Present", ToolTip = "True to use FSR's D3D12 swap-chain override that improves frame pacing."))
		bool bD3D12AsyncPresent;

	UPROPERTY(Config, EditAnywhere, Category = "Quality Settings", meta = (ConsoleVariable = "r.FidelityFX.FSR.QualityMode", DisplayName = "Quality Mode", ToolTip = "Selects the default quality mode to be used when upscaling with FSR."))
		EFFXFSRQualityMode QualityMode;

	UPROPERTY(Config, EditAnywhere, Category = "Quality Settings", meta = (ConsoleVariable = "r.FidelityFX.FSR.HistoryFormat", DisplayName = "History Format", ToolTip = "Selects the bit-depth for the FSR history texture format, defaults to PF_FloatRGBA but can be set to PF_FloatR11G11B10 to reduce bandwidth at the expense of quality."))
		EFFXFSRHistoryFormat HistoryFormat;

	UPROPERTY(Config, EditAnywhere, Category = "Quality Settings", meta = (ConsoleVariable = "r.FidelityFX.FSR.DeDither", DisplayName = "De-Dither Rendering", ToolTip = "Enable an extra pass to de-dither rendering before handing over to FSR Upscaling to avoid over-thinning, defaults to Off but can be set to Full for all pixels or to Hair Only for just around Hair (requires Deffered Renderer)."))
		EFFXFSRDeDitherMode DeDither;

	UPROPERTY(Config, EditAnywhere, Category = "Quality Settings", meta = (ConsoleVariable = "r.FidelityFX.FSR.Sharpness", DisplayName = "Sharpness", ClampMin = 0, ClampMax = 1, ToolTip = "When greater than 0.0 this enables Robust Contrast Adaptive Sharpening Filter to sharpen the output image."))
		float Sharpness;

	UPROPERTY(Config, EditAnywhere, Category = "Quality Settings", meta = (ConsoleVariable = "r.FidelityFX.FSR.AdjustMipBias", DisplayName = "Adjust Mip Bias & Offset", ToolTip = "Applies negative MipBias to material textures, improving results."))
		bool bAdjustMipBias;

	UPROPERTY(Config, EditAnywhere, Category = "Quality Settings", meta = (ConsoleVariable = "r.FidelityFX.FSR.ForceVertexDeformationOutputsVelocity", DisplayName = "Force Vertex Deformation To Output Velocity", ToolTip = "Force enables materials with World Position Offset and/or World Displacement to output velocities during velocity pass even when the actor has not moved."))
		bool bForceVertexDeformationOutputsVelocity;

	UPROPERTY(Config, EditAnywhere, Category = "Quality Settings", meta = (ConsoleVariable = "r.FidelityFX.FSR.ForceLandscapeHISMMobility", DisplayName = "Force Landscape HISM Mobility", ToolTip = "Allow FSR to force the mobility of Landscape actors Hierarchical Instance Static Mesh components that use World-Position-Offset materials so they render valid velocities.\nSetting 'All Instances' is faster on the CPU, 'Instances with World-Position-Offset' is faster on the GPU."))
		EFFXFSRLandscapeHISMMode ForceLandscapeHISMMobility;

	UPROPERTY(Config, EditAnywhere, Category = "Quality Settings", meta = (ConsoleVariable = "r.FidelityFX.FSR.VelocityFactor", DisplayName = "Velocity Factor", ClampMin = 0, ClampMax = 1, ToolTip = "Range from 0.0 to 1.0 (Default 1.0), value of 0.0f can improve temporal stability of bright pixels."))
		float VelocityFactor;

	UPROPERTY(Config, EditAnywhere, Category = "Reactive Mask Settings", meta = (ConsoleVariable = "r.FidelityFX.FSR.CreateReactiveMask", DisplayName = "Reactive Mask", ToolTip = "Enable to generate a mask from the SceneColor, GBuffer, SeparateTranslucency & ScreenspaceReflections that determines how reactive each pixel should be."))
		bool bReactiveMask;

	UPROPERTY(Config, EditAnywhere, Category = "Reactive Mask Settings", meta = (ConsoleVariable = "r.FidelityFX.FSR.ReactiveMaskReflectionScale", DisplayName = "Reflection Scale", ClampMin = 0, ClampMax = 1, ToolTip = "Scales the Unreal engine reflection contribution to the reactive mask, which can be used to control the amount of aliasing on reflective surfaces."))
		float ReflectionScale;

	UPROPERTY(Config, EditAnywhere, Category = "Reactive Mask Settings", meta = (ConsoleVariable = "r.FidelityFX.FSR.ReactiveMaskReflectionLumaBias", DisplayName = "Reflection Luminance Bias", ClampMin = 0, ClampMax = 1, ToolTip = "Biases the reactive mask by the luminance of the reflection. Use to balance aliasing against ghosting on brightly lit reflective surfaces."))
		float ReflectionLuminanceBias;

	UPROPERTY(Config, EditAnywhere, Category = "Reactive Mask Settings", meta = (ConsoleVariable = "r.FidelityFX.FSR.ReactiveMaskRoughnessScale", DisplayName = "Roughness Scale", ClampMin = 0, ClampMax = 1, ToolTip = "Scales the GBuffer roughness to provide a fallback value for the reactive mask when screenspace & planar reflections are disabled or don't affect a pixel."))
		float RoughnessScale;

	UPROPERTY(Config, EditAnywhere, Category = "Reactive Mask Settings", meta = (ConsoleVariable = "r.FidelityFX.FSR.ReactiveMaskRoughnessBias", DisplayName = "Roughness Bias", ClampMin = 0, ClampMax = 1, ToolTip = "Biases the reactive mask value when screenspace/planar reflections are weak with the GBuffer roughness to account for reflection environment captures."))
		float RoughnessBias;

	UPROPERTY(Config, EditAnywhere, Category = "Reactive Mask Settings", meta = (ConsoleVariable = "r.FidelityFX.FSR.ReactiveMaskRoughnessMaxDistance", DisplayName = "Roughness Max Distance", ClampMin = 0, ToolTip = "Maximum distance in world units for using material roughness to contribute to the reactive mask, the maximum of this value and View.FurthestReflectionCaptureDistance will be used."))
		float RoughnessMaxDistance;

	UPROPERTY(Config, EditAnywhere, Category = "Reactive Mask Settings", meta = (ConsoleVariable = "r.FidelityFX.FSR.ReactiveMaskRoughnessForceMaxDistance", DisplayName = "Force Roughness Max Distance", ToolTip = "Enable to force the maximum distance in world units for using material roughness to contribute to the reactive mask rather than using View.FurthestReflectionCaptureDistance."))
		bool bReactiveMaskRoughnessForceMaxDistance;

	UPROPERTY(Config, EditAnywhere, Category = "Reactive Mask Settings", meta = (ConsoleVariable = "r.FidelityFX.FSR.ReactiveMaskTranslucencyBias", DisplayName = "Translucency Bias", ClampMin = 0, ClampMax = 1, ToolTip = "Scales how much contribution translucency makes to the reactive mask. Higher values will make translucent materials less reactive which can reduce smearing."))
		float TranslucencyBias;

	UPROPERTY(Config, EditAnywhere, Category = "Reactive Mask Settings", meta = (ConsoleVariable = "r.FidelityFX.FSR.ReactiveMaskTranslucencyLumaBias", DisplayName = "Translucency Luminance Bias", ClampMin = 0, ClampMax = 1, ToolTip = "Biases the translucency contribution to the reactive mask by the luminance of the transparency."))
		float TranslucencyLuminanceBias;

	UPROPERTY(Config, EditAnywhere, Category = "Reactive Mask Settings", meta = (ConsoleVariable = "r.FidelityFX.FSR.ReactiveMaskTranslucencyMaxDistance", DisplayName = "Translucency Max Distance", ClampMin = 0, ToolTip = "Maximum distance in world units for using translucency to contribute to the reactive mask. This is another way to remove sky-boxes and other back-planes from the reactive mask, at the expense of nearer translucency not being reactive."))
		float TranslucencyMaxDistance;

	UPROPERTY(Config, EditAnywhere, Category = "Reactive Mask Settings", meta = (ConsoleVariable = "r.FidelityFX.FSR.ReactiveMaskReactiveShadingModelID", DisplayName = "Reactive Shading Model", ToolTip = "Treat the specified shading model as reactive, taking the CustomData0.x value as the reactive value to write into the mask. Default is MSM_NUM (Off)."))
		TEnumAsByte<enum EMaterialShadingModel> ReactiveShadingModelID;

	UPROPERTY(Config, EditAnywhere, Category = "Reactive Mask Settings", meta = (ConsoleVariable = "r.FidelityFX.FSR.ReactiveMaskForceReactiveMaterialValue", DisplayName = "Force value for Reactive Shading Model", ClampMin = 0, ClampMax = 1, ToolTip = "Force the reactive mask value for Reactive Shading Model materials, when > 0 this value can be used to override the value supplied in the Material Graph."))
		float ForceReactiveMaterialValue;

	UPROPERTY(Config, EditAnywhere, Category = "Reactive Mask Settings", meta = (ConsoleVariable = "r.FidelityFX.FSR.ReactiveHistoryTranslucencyBias", DisplayName = "Translucency Bias", ClampMin = 0, ClampMax = 1, ToolTip = "Scales how much contribution translucency makes to suppress history via the reactive mask. Higher values will make translucent materials less reactive which can reduce smearing."))
		float ReactiveHistoryTranslucencyBias;

	UPROPERTY(Config, EditAnywhere, Category = "Reactive Mask Settings", meta = (ConsoleVariable = "r.FidelityFX.FSR.ReactiveHistoryTranslucencyLumaBias", DisplayName = "Translucency Luminance Bias", ClampMin = 0, ClampMax = 1, ToolTip = "Biases the translucency contribution to suppress history via the reactive mask by the luminance of the transparency."))
		float ReactiveHistoryTranslucencyLumaBias;

	UPROPERTY(Config, EditAnywhere, Category = "Reactive Mask Settings", meta = (ConsoleVariable = "r.FidelityFX.FSR.ReactiveMaskPreDOFTranslucencyScale", DisplayName = "Pre Depth-of-Field Translucency Scale", ClampMin = 0, ClampMax = 1, ToolTip = "Scales how much contribution pre-Depth-of-Field translucency color makes to the reactive mask. Higher values will make translucent materials less reactive which can reduce smearing."))
		float PreDOFTranslucencyScale;

	UPROPERTY(Config, EditAnywhere, Category = "Reactive Mask Settings", meta = (ConsoleVariable = "r.FidelityFX.FSR.ReactiveMaskPreDOFTranslucencyMax", DisplayName = "Pre Depth-of-Field Translucency Max/Average", ToolTip = "Toggle to determine whether to use the max(SceneColorPostDepthOfField - SceneColorPreDepthOfField) or length(SceneColorPostDepthOfField - SceneColorPreDepthOfField) to determine the contribution of Pre-Depth-of-Field translucency."))
		bool bPreDOFTranslucencyMax;

	UPROPERTY(Config, EditAnywhere, Category = "Reactive Mask Settings", meta = (ConsoleVariable = "r.FidelityFX.FSR.ReactiveMaskDeferredDecalScale", DisplayName = "Deferred Decal Reactive Mask Scale", ClampMin = 0, ClampMax = 1, ToolTip = "Range from 0.0 to 1.0 (Default 0.0), scales how much deferred decal values contribute to the reactive mask. Higher values will make translucent materials more reactive which can reduce smearing."))
		float ReactiveMaskDeferredDecalScale;

	UPROPERTY(Config, EditAnywhere, Category = "Reactive Mask Settings", meta = (ConsoleVariable = "r.FidelityFX.FSR.ReactiveHistoryDeferredDecalScale", DisplayName = "Deferred Decal Reactive History Scale", ClampMin = 0, ClampMax = 1, ToolTip = "Range from 0.0 to 1.0 (Default 0.0), scales how much deferred decal values contribute to supressing hitory. Higher values will make translucent materials more reactive which can reduce smearing."))
		float ReactiveHistoryDeferredDecalScale;

	UPROPERTY(Config, EditAnywhere, Category = "Reactive Mask Settings", meta = (ConsoleVariable = "r.FidelityFX.FSR.ReactiveMaskTAAResponsiveValue", DisplayName = "Responsive TAA Reactive Mask Scale", ClampMin = 0, ClampMax = 1, ToolTip = "Range from 0.0 to 1.0 (Default 0.0), value to write to reactive mask when pixels are marked in the stencil buffer as TAA Responsive. Higher values will make translucent materials more reactive which can reduce smearing."))
		float ReactiveMaskTAAResponsiveValue;

	UPROPERTY(Config, EditAnywhere, Category = "Reactive Mask Settings", meta = (ConsoleVariable = "r.FidelityFX.FSR.ReactiveHistoryTAAResponsiveValue", DisplayName = "Responsive TAA Reactive Mask Scale", ClampMin = 0, ClampMax = 1, ToolTip = "Range from 0.0 to 1.0 (Default 0.0), value to write to reactive history when pixels are marked in the stencil buffer as TAA Responsive. Higher values will make translucent materials more reactive which can reduce smearing."))
		float ReactiveHistoryTAAResponsiveValue;

	UPROPERTY(Config, EditAnywhere, Category = "Reactive Mask Settings", meta = (ConsoleVariable = "r.FidelityFX.FSR.ReactiveMaskCustomStencilScale", DisplayName = "Custom Stencil Reactive Mask Scale", ClampMin = 0, ClampMax = 1, ToolTip = "Range from 0.0 to 1.0 (Default 0.0), scales how much customm stencil values contribute to the reactive mask. Higher values will make translucent materials more reactive which can reduce smearing."))
		float ReactiveMaskCustomStencilScale;

	UPROPERTY(Config, EditAnywhere, Category = "Reactive Mask Settings", meta = (ConsoleVariable = "r.FidelityFX.FSR.ReactiveHistoryCustomStencilScale", DisplayName = "Custom Stencil Reactive History Scale", ClampMin = 0, ClampMax = 1, ToolTip = "Range from 0.0 to 1.0 (Default 0.0), scales how much customm stencil values contribute to supressing hitory. Higher values will make translucent materials more reactive which can reduce smearing."))
		float ReactiveHistoryCustomStencilScale;

	UPROPERTY(Config, EditAnywhere, Category = "Reactive Mask Settings", meta = (ConsoleVariable = "r.FidelityFX.FSR.CustomStencilMask", DisplayName = "CustomS tencil Bit Mask", ClampMin = 0, ClampMax = 255, ToolTip = "A bitmask 0-255 (0-0xff) used when accessing the custom stencil to read reactive mask values. Setting to 0 will disable use of the custom-depth/stencil buffer. Default is 0."))
		int32 CustomStencilMask;

	UPROPERTY(Config, EditAnywhere, Category = "Reactive Mask Settings", meta = (ConsoleVariable = "r.FidelityFX.FSR.CustomStencilShift", DisplayName = "Custom Stencil Bit Shift", ClampMin = 0, ClampMax = 31, ToolTip = "Bitshift to apply to the value read from the custom stencil when using it to provide reactive mask values. Default is 0."))
		int32 CustomStencilShift;
};

class FFXFSRSettingsModule final : public IModuleInterface
{
public:
	// IModuleInterface implementation
	void StartupModule() override;
	void ShutdownModule() override;
};