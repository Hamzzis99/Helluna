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

#include "FFXFSRViewExtension.h"
#include "FFXFSRTemporalUpscaler.h"
#include "FFXFSRTemporalUpscalerProxy.h"
#include "FFXFSRTemporalUpscaling.h"
#include "FFXFSRSettings.h"
#include "PostProcess/PostProcessing.h"
#include "Materials/Material.h"

#include "ScenePrivate.h"
#include "EngineUtils.h"
#include "Engine/StaticMesh.h"
#include "LandscapeProxy.h"
#include "Components/HierarchicalInstancedStaticMeshComponent.h"

static void ForceLandscapeHISMMobility(FSceneViewFamily& InViewFamily, ALandscapeProxy* Landscape)
{
	for (FCachedLandscapeFoliage::TGrassSet::TIterator Iter(Landscape->FoliageCache.CachedGrassComps); Iter; ++Iter)
	{
		ULandscapeComponent* Component = (*Iter).Key.BasedOn.Get();
		if (Component)
		{
			UHierarchicalInstancedStaticMeshComponent* Used = (*Iter).Foliage.Get();
			if (Used && Used->Mobility == EComponentMobility::Static)
			{
				if (CVarFSRForceLandscapeHISMMobility.GetValueOnGameThread() == 2)
				{
#if UE_VERSION_AT_LEAST(4, 27, 0)
					TArray<FStaticMaterial> const& Materials = Used->GetStaticMesh()->GetStaticMaterials();
#else
					TArray<FStaticMaterial> const& Materials = Used->GetStaticMesh()->StaticMaterials;
#endif
					for (auto MaterialInfo : Materials)
					{
						const UMaterial* Material = MaterialInfo.MaterialInterface->GetMaterial_Concurrent();
#if UE_VERSION_AT_LEAST(5, 7, 0)
						if (const FMaterialResource* MaterialResource = Material->GetMaterialResource(InViewFamily.GetShaderPlatform()))
#else
						if (const FMaterialResource* MaterialResource = Material->GetMaterialResource(InViewFamily.GetFeatureLevel()))
#endif
						{
							check(IsInGameThread());
							bool bAlwaysHasVelocity = MaterialResource->MaterialModifiesMeshPosition_GameThread();
							if (bAlwaysHasVelocity)
							{
								Used->Mobility = EComponentMobility::Stationary;
								break;
							}
						}
					}
				}
				else
				{
					Used->Mobility = EComponentMobility::Stationary;
				}
			}
		}
	}
}

FFXFSRViewExtension::FFXFSRViewExtension(const FAutoRegister& AutoRegister) : FSceneViewExtensionBase(AutoRegister)
, bFSRSupported(false)
{
	static IConsoleVariable* CVarMinAutomaticViewMipBiasMin = IConsoleManager::Get().FindConsoleVariable(TEXT("r.ViewTextureMipBias.Min"));
	static IConsoleVariable* CVarMinAutomaticViewMipBiasOffset = IConsoleManager::Get().FindConsoleVariable(TEXT("r.ViewTextureMipBias.Offset"));
	static IConsoleVariable* CVarVertexDeformationOutputsVelocity = IConsoleManager::Get().FindConsoleVariable(TEXT("r.Velocity.EnableVertexDeformation"));
	static IConsoleVariable* CVarVelocityEnableLandscapeGrass = IConsoleManager::Get().FindConsoleVariable(TEXT("r.Velocity.EnableLandscapeGrass"));
	static IConsoleVariable* CVarSeparateTranslucency = IConsoleManager::Get().FindConsoleVariable(TEXT("r.SeparateTranslucency"));
	static IConsoleVariable* CVarSSRExperimentalDenoiser = IConsoleManager::Get().FindConsoleVariable(TEXT("r.SSR.ExperimentalDenoiser"));

#if FFX_ENABLE_DX12
	FString RHIName = GDynamicRHI->GetName();
	if (RHIName == FFXFSRStrings::D3D12)
	{
		bFSRSupported = true;
	}
#endif

	PreviousFSRState = CVarEnableFSR.GetValueOnAnyThread();
	PreviousFSRStateRT = CVarEnableFSR.GetValueOnAnyThread();
	CurrentFSRStateRT = CVarEnableFSR.GetValueOnAnyThread();
	SSRExperimentalDenoiser = 0;
	VertexDeformationOutputsVelocity = CVarVertexDeformationOutputsVelocity ? CVarVertexDeformationOutputsVelocity->GetInt() : 0;
	VelocityEnableLandscapeGrass = CVarVelocityEnableLandscapeGrass ? CVarVelocityEnableLandscapeGrass->GetInt() : 0;
	MinAutomaticViewMipBiasMin = CVarMinAutomaticViewMipBiasMin ? CVarMinAutomaticViewMipBiasMin->GetFloat() : 0;
	MinAutomaticViewMipBiasOffset = CVarMinAutomaticViewMipBiasOffset ? CVarMinAutomaticViewMipBiasOffset->GetFloat() : 0;
	SeparateTranslucency = CVarSeparateTranslucency ? CVarSeparateTranslucency->GetInt() : 0;
	SSRExperimentalDenoiser = CVarSSRExperimentalDenoiser ? CVarSSRExperimentalDenoiser->GetInt() : 0;

	IFFXFSRTemporalUpscalingModule& FSRModuleInterface = FModuleManager::GetModuleChecked<IFFXFSRTemporalUpscalingModule>(TEXT("FFXFSRTemporalUpscaling"));
	if (FSRModuleInterface.GetTemporalUpscaler() == nullptr)
	{
		FFXFSRTemporalUpscalingModule& FSRModule = (FFXFSRTemporalUpscalingModule&)FSRModuleInterface;
		TSharedPtr<FFXFSRTemporalUpscaler, ESPMode::ThreadSafe> FSRTemporalUpscaler = MakeShared<FFXFSRTemporalUpscaler, ESPMode::ThreadSafe>();
		FSRModule.SetTemporalUpscaler(FSRTemporalUpscaler);
	}
}

void FFXFSRViewExtension::SetupViewFamily(FSceneViewFamily& InViewFamily)
{
	if (InViewFamily.GetFeatureLevel() > ERHIFeatureLevel::SM5 || (bFSRSupported && InViewFamily.GetFeatureLevel() == ERHIFeatureLevel::SM5))
	{
		static IConsoleVariable* CVarMinAutomaticViewMipBiasMin = IConsoleManager::Get().FindConsoleVariable(TEXT("r.ViewTextureMipBias.Min"));
		static IConsoleVariable* CVarMinAutomaticViewMipBiasOffset = IConsoleManager::Get().FindConsoleVariable(TEXT("r.ViewTextureMipBias.Offset"));
		static IConsoleVariable* CVarVertexDeformationOutputsVelocity = IConsoleManager::Get().FindConsoleVariable(TEXT("r.Velocity.EnableVertexDeformation"));
		static IConsoleVariable* CVarVelocityEnableLandscapeGrass = IConsoleManager::Get().FindConsoleVariable(TEXT("r.Velocity.EnableLandscapeGrass"));
		static IConsoleVariable* CVarSeparateTranslucency = IConsoleManager::Get().FindConsoleVariable(TEXT("r.SeparateTranslucency"));
		static IConsoleVariable* CVarSSRExperimentalDenoiser = IConsoleManager::Get().FindConsoleVariable(TEXT("r.SSR.ExperimentalDenoiser"));
		IFFXFSRTemporalUpscalingModule& FSRModuleInterface = FModuleManager::GetModuleChecked<IFFXFSRTemporalUpscalingModule>(TEXT("FFXFSRTemporalUpscaling"));
		check(FSRModuleInterface.GetFSRUpscaler());

		if (CVarEnableFSR.GetValueOnAnyThread() && !FSRModuleInterface.GetFSRUpscaler()->IsApiSupported())
		{
			FSRModuleInterface.GetFSRUpscaler()->Initialize();

			if (CVarEnableFSR.GetValueOnAnyThread() && FSRModuleInterface.GetFSRUpscaler()->IsApiSupported())
			{
				// Initialize by default for game, but not the editor unless we intend to use FSR in the viewport by default
				if (!GIsEditor || CVarEnableFSRInEditor.GetValueOnAnyThread())
				{
					// Set this at startup so that it will apply consistently
					if (CVarFSRAdjustMipBias.GetValueOnGameThread())
					{
						if (CVarMinAutomaticViewMipBiasMin != nullptr)
						{
							CVarMinAutomaticViewMipBiasMin->Set(float(0.f + log2(1.f / 3.0f) - 1.0f + FLT_EPSILON), EConsoleVariableFlags::ECVF_SetByCode);
						}
						if (CVarMinAutomaticViewMipBiasOffset != nullptr)
						{
							CVarMinAutomaticViewMipBiasOffset->Set(float(-1.0f + FLT_EPSILON), EConsoleVariableFlags::ECVF_SetByCode);
						}
					}

					if (CVarFSRForceVertexDeformationOutputsVelocity.GetValueOnGameThread())
					{
						if (CVarVertexDeformationOutputsVelocity != nullptr)
						{
							CVarVertexDeformationOutputsVelocity->Set(1, EConsoleVariableFlags::ECVF_SetByCode);
						}
						if (CVarVelocityEnableLandscapeGrass != nullptr)
						{
							CVarVelocityEnableLandscapeGrass->Set(1, EConsoleVariableFlags::ECVF_SetByCode);
						}
					}

					if (CVarFSRCreateReactiveMask->GetInt())
					{
						if (CVarSeparateTranslucency != nullptr)
						{
							CVarSeparateTranslucency->Set(1, EConsoleVariableFlags::ECVF_SetByCode);
						}

						if (CVarSSRExperimentalDenoiser != nullptr)
						{
							CVarFSRUseExperimentalSSRDenoiser->Set(SSRExperimentalDenoiser, EConsoleVariableFlags::ECVF_SetByCode);
							CVarSSRExperimentalDenoiser->Set(1, EConsoleVariableFlags::ECVF_SetByCode);
						}
					}
				}
				else
				{
					// Pretend it is disabled so that when the Editor does enable FSR the state change is picked up properly.
					PreviousFSRState = false;
					PreviousFSRStateRT = false;
					CurrentFSRStateRT = false;
				}
			}
			else
			{
				// Disable FSR as it could not be initialised, this avoids errors if it is enabled later.
				PreviousFSRState = false;
				PreviousFSRStateRT = false;
				CurrentFSRStateRT = false;
#if UE_VERSION_AT_LEAST(5, 0, 0)
				CVarEnableFSR->Set(0, EConsoleVariableFlags::ECVF_SetByGameOverride);
#else
				CVarEnableFSR->Set(0, EConsoleVariableFlags::ECVF_SetByCode);
#endif
			}
		}

		int32 EnableFSR = CVarEnableFSR.GetValueOnAnyThread();

		if (EnableFSR)
		{
			if (CVarFSRForceVertexDeformationOutputsVelocity.GetValueOnGameThread() && CVarVertexDeformationOutputsVelocity != nullptr && VertexDeformationOutputsVelocity == 0 && CVarVertexDeformationOutputsVelocity->GetInt() == 0)
			{
				VertexDeformationOutputsVelocity = CVarVertexDeformationOutputsVelocity->GetInt();
				CVarVertexDeformationOutputsVelocity->Set(1, EConsoleVariableFlags::ECVF_SetByCode);
			}

			if (CVarFSRForceVertexDeformationOutputsVelocity.GetValueOnGameThread() && CVarVelocityEnableLandscapeGrass != nullptr && VelocityEnableLandscapeGrass == 0 && CVarVelocityEnableLandscapeGrass->GetInt() == 0)
			{
				VelocityEnableLandscapeGrass = CVarVelocityEnableLandscapeGrass->GetInt();
				CVarVelocityEnableLandscapeGrass->Set(1, EConsoleVariableFlags::ECVF_SetByCode);
			}

			if (CVarFSRForceLandscapeHISMMobility.GetValueOnGameThread())
			{
				// Landscape Hierarchical Instanced Static Mesh components are usually foliage and thus might use WPO.
				// To make it generate motion vectors it can't be Static which is hard-coded into the Engine.
#if UE_VERSION_AT_LEAST(5, 0, 0)
				for (ALandscapeProxy* Landscape : TObjectRange<ALandscapeProxy>(RF_ClassDefaultObject | RF_ArchetypeObject, true, EInternalObjectFlags::Garbage))
#else
				for (ALandscapeProxy* Landscape : TObjectRange<ALandscapeProxy>(RF_ClassDefaultObject | RF_ArchetypeObject, true, EInternalObjectFlags::PendingKill))
#endif
				{
					ForceLandscapeHISMMobility(InViewFamily, Landscape);
				}
			}
		}

		if (PreviousFSRState != EnableFSR)
		{
			// Update tracking of the FSR state when it is changed
			PreviousFSRState = EnableFSR;

			if (EnableFSR)
			{
				// When toggling reapply the settings that FSR wants to override
				if (CVarFSRAdjustMipBias.GetValueOnGameThread())
				{
					if (CVarMinAutomaticViewMipBiasMin != nullptr)
					{
						MinAutomaticViewMipBiasMin = CVarMinAutomaticViewMipBiasMin->GetFloat();
						CVarMinAutomaticViewMipBiasMin->Set(float(0.f + log2(1.f / 3.0f) - 1.0f + FLT_EPSILON), EConsoleVariableFlags::ECVF_SetByCode);
					}
					if (CVarMinAutomaticViewMipBiasOffset != nullptr)
					{
						MinAutomaticViewMipBiasOffset = CVarMinAutomaticViewMipBiasOffset->GetFloat();
						CVarMinAutomaticViewMipBiasOffset->Set(float(-1.0f + FLT_EPSILON), EConsoleVariableFlags::ECVF_SetByCode);
					}
				}

				if (CVarFSRForceVertexDeformationOutputsVelocity.GetValueOnGameThread())
				{
					if (CVarVertexDeformationOutputsVelocity != nullptr)
					{
						CVarVertexDeformationOutputsVelocity->Set(1, EConsoleVariableFlags::ECVF_SetByCode);
					}
					if (CVarVelocityEnableLandscapeGrass != nullptr)
					{
						CVarVelocityEnableLandscapeGrass->Set(1, EConsoleVariableFlags::ECVF_SetByCode);
					}
				}

				if (CVarFSRCreateReactiveMask->GetInt())
				{
					if (CVarSeparateTranslucency != nullptr)
					{
						SeparateTranslucency = CVarSeparateTranslucency->GetInt();
						CVarSeparateTranslucency->Set(1, EConsoleVariableFlags::ECVF_SetByCode);
					}
					if (CVarSSRExperimentalDenoiser != nullptr)
					{
						SSRExperimentalDenoiser = CVarSSRExperimentalDenoiser->GetInt();
						CVarFSRUseExperimentalSSRDenoiser->Set(SSRExperimentalDenoiser, EConsoleVariableFlags::ECVF_SetByCode);
						CVarSSRExperimentalDenoiser->Set(1, EConsoleVariableFlags::ECVF_SetByCode);
					}
				}
			}
			// Put the variables FSR modifies back to the way they were when FSR was toggled on.
			else
			{
				if (CVarFSRAdjustMipBias.GetValueOnGameThread())
				{
					if (CVarMinAutomaticViewMipBiasMin != nullptr)
					{
						CVarMinAutomaticViewMipBiasMin->Set(MinAutomaticViewMipBiasMin, EConsoleVariableFlags::ECVF_SetByCode);
					}
					if (CVarMinAutomaticViewMipBiasOffset != nullptr)
					{
						CVarMinAutomaticViewMipBiasOffset->Set(MinAutomaticViewMipBiasOffset, EConsoleVariableFlags::ECVF_SetByCode);
					}
				}

				if (CVarFSRForceVertexDeformationOutputsVelocity.GetValueOnGameThread())
				{
					if (CVarVertexDeformationOutputsVelocity != nullptr)
					{
						CVarVertexDeformationOutputsVelocity->Set(VertexDeformationOutputsVelocity, EConsoleVariableFlags::ECVF_SetByCode);
					}
					if (CVarVelocityEnableLandscapeGrass != nullptr)
					{
						CVarVelocityEnableLandscapeGrass->Set(VelocityEnableLandscapeGrass, EConsoleVariableFlags::ECVF_SetByCode);
					}
				}

				if (CVarFSRCreateReactiveMask->GetInt())
				{
					if (CVarSeparateTranslucency != nullptr)
					{
						CVarSeparateTranslucency->Set(SeparateTranslucency, EConsoleVariableFlags::ECVF_SetByCode);
					}
					if (CVarSSRExperimentalDenoiser != nullptr)
					{
						CVarSSRExperimentalDenoiser->Set(SSRExperimentalDenoiser, EConsoleVariableFlags::ECVF_SetByCode);
					}
				}
			}
		}
	}
}

void FFXFSRViewExtension::BeginRenderViewFamily(FSceneViewFamily& InViewFamily)
{
	if (InViewFamily.GetFeatureLevel() > ERHIFeatureLevel::SM5 || (bFSRSupported && InViewFamily.GetFeatureLevel() == ERHIFeatureLevel::SM5))
	{
		IFFXFSRTemporalUpscalingModule& FSRModuleInterface = FModuleManager::GetModuleChecked<IFFXFSRTemporalUpscalingModule>(TEXT("FFXFSRTemporalUpscaling"));
		FFXFSRTemporalUpscaler* Upscaler = FSRModuleInterface.GetFSRUpscaler();
		bool IsTemporalUpscalingRequested = false;
		bool bIsGameView = !WITH_EDITOR;
		for (int i = 0; i < InViewFamily.Views.Num(); i++)
		{
			const FSceneView* InView = InViewFamily.Views[i];
			if (ensure(InView))
			{
				if (Upscaler)
				{
					FGlobalShaderMap* GlobalMap = GetGlobalShaderMap(InViewFamily.GetFeatureLevel());
					Upscaler->SetSSRShader(GlobalMap);
				}

				bIsGameView |= InView->bIsGameView;

				// Don't run FSR if Temporal Upscaling is unused.
				IsTemporalUpscalingRequested |= (InView->PrimaryScreenPercentageMethod == EPrimaryScreenPercentageMethod::TemporalUpscale);
			}
		}

#if WITH_EDITOR
		IsTemporalUpscalingRequested &= Upscaler->IsEnabledInEditor();
#endif

		if (IsTemporalUpscalingRequested && CVarEnableFSR.GetValueOnAnyThread() && (InViewFamily.GetTemporalUpscalerInterface() == nullptr))
		{
			if (!WITH_EDITOR || (CVarEnableFSRInEditor.GetValueOnGameThread() == 1) || bIsGameView)
			{
				Upscaler->UpdateDynamicResolutionState();
#if UE_VERSION_AT_LEAST(5, 1, 0)
				InViewFamily.SetTemporalUpscalerInterface(new FFXFSRTemporalUpscalerProxy(Upscaler));
#else
				InViewFamily.SetTemporalUpscalerInterface(Upscaler);
#endif
			}
		}
	}
}

#if UE_VERSION_AT_LEAST(5, 0, 0)
void FFXFSRViewExtension::PreRenderViewFamily_RenderThread(FRenderGraphType& GraphBuilder, FSceneViewFamily& InViewFamily)
#else
void FFXFSRViewExtension::PreRenderViewFamily_RenderThread(FRHICommandListImmediate& RHICmdList, FSceneViewFamily& InViewFamily)
#endif
{
	if (InViewFamily.GetFeatureLevel() > ERHIFeatureLevel::SM5 || (bFSRSupported && InViewFamily.GetFeatureLevel() == ERHIFeatureLevel::SM5))
	{
		// When the FSR plugin is enabled/disabled dispose of any previous history as it will be invalid if it comes from another upscaler
		CurrentFSRStateRT = CVarEnableFSR.GetValueOnRenderThread();
		if (PreviousFSRStateRT != CurrentFSRStateRT)
		{
			// This also requires updating our tracking of the FSR state
			PreviousFSRStateRT = CurrentFSRStateRT;
#if UE_VERSION_OLDER_THAN(5, 3, 0)
			for (auto* SceneView : InViewFamily.Views)
			{
				if (SceneView->bIsViewInfo)
				{
					FViewInfo* View = (FViewInfo*)SceneView;
					View->PrevViewInfo.CustomTemporalAAHistory.SafeRelease();
					if (!View->bStatePrevViewInfoIsReadOnly && View->ViewState)
					{
						View->ViewState->PrevFrameViewInfo.CustomTemporalAAHistory.SafeRelease();
					}
				}
			}
#endif
		}
	}
}

#if UE_VERSION_AT_LEAST(5, 0, 0)
void FFXFSRViewExtension::PreRenderView_RenderThread(FRenderGraphType& GraphBuilder, FSceneView& InView)
#else
void FFXFSRViewExtension::PreRenderView_RenderThread(FRHICommandListImmediate& RHICmdList, FSceneView& InView)
#endif
{
#if UE_VERSION_AT_LEAST(5, 0, 0)
	// FSR can access the previous frame of Lumen data at this point, but not later where it will be replaced with the current frame's which won't be accessible when FSR runs.
	if (InView.GetFeatureLevel() > ERHIFeatureLevel::SM5 || (bFSRSupported && InView.GetFeatureLevel() == ERHIFeatureLevel::SM5))
	{
		if (CVarEnableFSR.GetValueOnAnyThread())
		{
			IFFXFSRTemporalUpscalingModule& FSRModuleInterface = FModuleManager::GetModuleChecked<IFFXFSRTemporalUpscalingModule>(TEXT("FFXFSRTemporalUpscaling"));
			FSRModuleInterface.GetFSRUpscaler()->SetLumenReflections(InView);
		}
	}
#endif
}

void FFXFSRViewExtension::PrePostProcessPass_RenderThread(FRDGBuilder& GraphBuilder, const FSceneView& View, const FPostProcessingInputs& Inputs)
{
	// FSR requires the separate translucency data which is only available through the post-inputs so bind them to the upscaler now.
	if (View.GetFeatureLevel() > ERHIFeatureLevel::SM5 || (bFSRSupported && View.GetFeatureLevel() == ERHIFeatureLevel::SM5))
	{
		if (CVarEnableFSR.GetValueOnAnyThread())
		{
			IFFXFSRTemporalUpscalingModule& FSRModuleInterface = FModuleManager::GetModuleChecked<IFFXFSRTemporalUpscalingModule>(TEXT("FFXFSRTemporalUpscaling"));
			FSRModuleInterface.GetFSRUpscaler()->SetPostProcessingInputs(Inputs);
		}
	}
}

#if UE_VERSION_AT_LEAST(5, 0, 0)
void FFXFSRViewExtension::PostRenderViewFamily_RenderThread(FRenderGraphType& GraphBuilder, FSceneViewFamily& InViewFamily)
#else
void FFXFSRViewExtension::PostRenderViewFamily_RenderThread(FRHICommandListImmediate& RHICmdList, FSceneViewFamily& InViewFamily)
#endif
{
	// As FSR retains pointers/references to objects the engine is not expecting clear them out now to prevent leaks or accessing dangling pointers.
	if (InViewFamily.GetFeatureLevel() > ERHIFeatureLevel::SM5 || (bFSRSupported && InViewFamily.GetFeatureLevel() == ERHIFeatureLevel::SM5))
	{
		if (CVarEnableFSR.GetValueOnAnyThread())
		{
			IFFXFSRTemporalUpscalingModule& FSRModuleInterface = FModuleManager::GetModuleChecked<IFFXFSRTemporalUpscalingModule>(TEXT("FFXFSRTemporalUpscaling"));
			FSRModuleInterface.GetFSRUpscaler()->EndOfFrame();
		}
	}
}
