// ════════════════════════════════════════════════════════════════════════════════
// File: Source/Helluna/Private/Settings/Widget/HellunaGraphicsSettingsWidget.cpp
// ════════════════════════════════════════════════════════════════════════════════

#include "Settings/Widget/HellunaGraphicsSettingsWidget.h"

#include "Components/ComboBoxString.h"
#include "Components/CheckBox.h"
#include "Components/Button.h"
#include "Components/TextBlock.h"
#include "Components/Slider.h"
#include "GameFramework/GameUserSettings.h"
#include "Kismet/KismetSystemLibrary.h"
#include "RHI.h"

DEFINE_LOG_CATEGORY_STATIC(LogHellunaGraphics, Log, All);

// ════════════════════════════════════════════════════════════════════════════════
//  CVar 이름 상수
// ════════════════════════════════════════════════════════════════════════════════
namespace GraphicsCVars
{
	// DLSS
	static const TCHAR* DLSSEnable          = TEXT("r.NGX.DLSS.Enable");
	static const TCHAR* DLSSQuality         = TEXT("r.NGX.DLSS.Quality.Setting");

	// FSR
	static const TCHAR* FSREnable           = TEXT("r.FidelityFX.FSR3.Enabled");
	static const TCHAR* FSRQuality          = TEXT("r.FidelityFX.FSR3.QualityMode");
	static const TCHAR* FSRFrameInterp      = TEXT("r.FidelityFX.FI.Enabled");

	// Streamline
	static const TCHAR* DLSSGEnable         = TEXT("r.Streamline.DLSSG.Enable");
	static const TCHAR* ReflexEnable        = TEXT("r.Streamline.Reflex.Enable");

	// TSR / 공통
	static const TCHAR* ScreenPercentage    = TEXT("r.ScreenPercentage");
	static const TCHAR* AntiAliasingMethod  = TEXT("r.AntiAliasingMethod");

	// Scalability
	static const TCHAR* ShadowQuality       = TEXT("sg.ShadowQuality");
	static const TCHAR* TextureQuality      = TEXT("sg.TextureQuality");
	static const TCHAR* EffectsQuality      = TEXT("sg.EffectsQuality");
	static const TCHAR* ViewDistanceQuality = TEXT("sg.ViewDistanceQuality");
	static const TCHAR* AntiAliasingQuality = TEXT("sg.AntiAliasingQuality");
	static const TCHAR* PostProcessQuality  = TEXT("sg.PostProcessQuality");
}

// ════════════════════════════════════════════════════════════════════════════════
//  품질 레벨 <-> 이름 변환
// ════════════════════════════════════════════════════════════════════════════════
static const TArray<FString> QualityNames = { TEXT("Low"), TEXT("Medium"), TEXT("High"), TEXT("Epic") };

int32 UHellunaGraphicsSettingsWidget::QualityNameToLevel(const FString& Name)
{
	const int32 Idx = QualityNames.IndexOfByKey(Name);
	return (Idx != INDEX_NONE) ? Idx : 2; // 기본 High
}

FString UHellunaGraphicsSettingsWidget::QualityLevelToName(int32 Level)
{
	return QualityNames.IsValidIndex(Level) ? QualityNames[Level] : TEXT("High");
}

// ════════════════════════════════════════════════════════════════════════════════
//  CVar 헬퍼
// ════════════════════════════════════════════════════════════════════════════════
void UHellunaGraphicsSettingsWidget::SetCVar(const TCHAR* Name, int32 Value)
{
	if (IConsoleVariable* CVar = IConsoleManager::Get().FindConsoleVariable(Name))
	{
		CVar->Set(Value, ECVF_SetByCode);
	}
	else
	{
		UE_LOG(LogHellunaGraphics, Warning, TEXT("CVar not found: %s"), Name);
	}
}

void UHellunaGraphicsSettingsWidget::SetCVar(const TCHAR* Name, float Value)
{
	if (IConsoleVariable* CVar = IConsoleManager::Get().FindConsoleVariable(Name))
	{
		CVar->Set(Value, ECVF_SetByCode);
	}
	else
	{
		UE_LOG(LogHellunaGraphics, Warning, TEXT("CVar not found: %s"), Name);
	}
}

int32 UHellunaGraphicsSettingsWidget::GetCVarInt(const TCHAR* Name) const
{
	if (const IConsoleVariable* CVar = IConsoleManager::Get().FindConsoleVariable(Name))
	{
		return CVar->GetInt();
	}
	return 0;
}

float UHellunaGraphicsSettingsWidget::GetCVarFloat(const TCHAR* Name) const
{
	if (const IConsoleVariable* CVar = IConsoleManager::Get().FindConsoleVariable(Name))
	{
		return CVar->GetFloat();
	}
	return 100.f;
}

// ════════════════════════════════════════════════════════════════════════════════
//  초기화
// ════════════════════════════════════════════════════════════════════════════════
void UHellunaGraphicsSettingsWidget::NativeOnInitialized()
{
	Super::NativeOnInitialized();

	DetectGPUCapabilities();

	// ── 업스케일러 ComboBox ──
	if (ComboBox_Upscaler)
	{
		ComboBox_Upscaler->ClearOptions();
		ComboBox_Upscaler->AddOption(TEXT("DLSS"));
		ComboBox_Upscaler->AddOption(TEXT("FSR"));
		ComboBox_Upscaler->AddOption(TEXT("TSR"));
		ComboBox_Upscaler->AddOption(TEXT("OFF"));
		ComboBox_Upscaler->OnSelectionChanged.AddUniqueDynamic(this, &ThisClass::OnUpscalerChanged);
	}

	// ── 업스케일러 품질 ComboBox ──
	if (ComboBox_UpscalerQuality)
	{
		ComboBox_UpscalerQuality->OnSelectionChanged.AddUniqueDynamic(this, &ThisClass::OnUpscalerQualityChanged);
	}

	// ── TSR 슬라이더 ──
	if (Slider_ScreenPercentage)
	{
		Slider_ScreenPercentage->SetMinValue(50.f);
		Slider_ScreenPercentage->SetMaxValue(100.f);
		Slider_ScreenPercentage->SetStepSize(1.f);
		Slider_ScreenPercentage->OnValueChanged.AddUniqueDynamic(this, &ThisClass::OnScreenPercentageChanged);
	}

	// ── 프레임 생성 / 리플렉스 체크박스 ──
	if (CheckBox_FrameGen)
	{
		CheckBox_FrameGen->OnCheckStateChanged.AddUniqueDynamic(this, &ThisClass::OnFrameGenToggled);
	}
	if (CheckBox_Reflex)
	{
		CheckBox_Reflex->OnCheckStateChanged.AddUniqueDynamic(this, &ThisClass::OnReflexToggled);
	}

	// ── 전체 품질 프리셋 ──
	if (ComboBox_QualityPreset)
	{
		ComboBox_QualityPreset->ClearOptions();
		for (const FString& Name : QualityNames)
		{
			ComboBox_QualityPreset->AddOption(Name);
		}
		ComboBox_QualityPreset->AddOption(TEXT("Custom"));
		ComboBox_QualityPreset->OnSelectionChanged.AddUniqueDynamic(this, &ThisClass::OnQualityPresetChanged);
	}

	// ── 개별 Scalability ComboBox 6개 ──
	auto InitScalabilityCombo = [](UComboBoxString* Combo)
	{
		if (!Combo) return;
		Combo->ClearOptions();
		for (const FString& Name : QualityNames)
		{
			Combo->AddOption(Name);
		}
	};

	InitScalabilityCombo(ComboBox_ShadowQuality);
	InitScalabilityCombo(ComboBox_TextureQuality);
	InitScalabilityCombo(ComboBox_EffectsQuality);
	InitScalabilityCombo(ComboBox_ViewDistance);
	InitScalabilityCombo(ComboBox_AntiAliasing);
	InitScalabilityCombo(ComboBox_PostProcess);

	// 개별 ComboBox 콜백 바인딩
	if (ComboBox_ShadowQuality)  ComboBox_ShadowQuality->OnSelectionChanged.AddUniqueDynamic(this, &ThisClass::OnShadowQualityChanged);
	if (ComboBox_TextureQuality) ComboBox_TextureQuality->OnSelectionChanged.AddUniqueDynamic(this, &ThisClass::OnTextureQualityChanged);
	if (ComboBox_EffectsQuality) ComboBox_EffectsQuality->OnSelectionChanged.AddUniqueDynamic(this, &ThisClass::OnEffectsQualityChanged);
	if (ComboBox_ViewDistance)   ComboBox_ViewDistance->OnSelectionChanged.AddUniqueDynamic(this, &ThisClass::OnViewDistanceChanged);
	if (ComboBox_AntiAliasing)   ComboBox_AntiAliasing->OnSelectionChanged.AddUniqueDynamic(this, &ThisClass::OnAntiAliasingChanged);
	if (ComboBox_PostProcess)    ComboBox_PostProcess->OnSelectionChanged.AddUniqueDynamic(this, &ThisClass::OnPostProcessChanged);

	// ── 화면모드 ──
	if (ComboBox_WindowMode)
	{
		ComboBox_WindowMode->ClearOptions();
		ComboBox_WindowMode->AddOption(TEXT("Fullscreen"));
		ComboBox_WindowMode->AddOption(TEXT("Windowed"));
		ComboBox_WindowMode->AddOption(TEXT("Borderless"));
		ComboBox_WindowMode->OnSelectionChanged.AddUniqueDynamic(this, &ThisClass::OnWindowModeChanged);
	}

	// ── 해상도 ──
	if (ComboBox_Resolution)
	{
		ComboBox_Resolution->OnSelectionChanged.AddUniqueDynamic(this, &ThisClass::OnResolutionChanged);
	}
	PopulateResolutionOptions();

	// ── 버튼 ──
	if (Button_Apply)        Button_Apply->OnClicked.AddUniqueDynamic(this, &ThisClass::OnApplyClicked);
	if (Button_Cancel)       Button_Cancel->OnClicked.AddUniqueDynamic(this, &ThisClass::OnCancelClicked);
	if (Button_ResetDefault) Button_ResetDefault->OnClicked.AddUniqueDynamic(this, &ThisClass::OnResetDefaultClicked);

	// ── 현재 설정 로드 ──
	LoadCurrentSettings();
}

// ════════════════════════════════════════════════════════════════════════════════
//  GPU 감지
// ════════════════════════════════════════════════════════════════════════════════
void UHellunaGraphicsSettingsWidget::DetectGPUCapabilities()
{
	// 0x10DE = NVIDIA
	bIsNvidiaGPU = (GRHIVendorId == 0x10DE);
	UE_LOG(LogHellunaGraphics, Log, TEXT("GPU Vendor: 0x%04X, IsNvidia: %s"),
		GRHIVendorId, bIsNvidiaGPU ? TEXT("Yes") : TEXT("No"));
}

// ════════════════════════════════════════════════════════════════════════════════
//  현재 설정 로드 → UI 반영
// ════════════════════════════════════════════════════════════════════════════════
void UHellunaGraphicsSettingsWidget::LoadCurrentSettings()
{
	bSuppressCallbacks = true;

	// ── 업스케일러 판별 ──
	const bool bDLSS = (GetCVarInt(GraphicsCVars::DLSSEnable) != 0);
	const bool bFSR  = (GetCVarInt(GraphicsCVars::FSREnable) != 0);

	if (bDLSS)        CurrentUpscaler = EHellunaUpscalerType::DLSS;
	else if (bFSR)    CurrentUpscaler = EHellunaUpscalerType::FSR;
	else
	{
		// DLSS/FSR 둘 다 꺼져 있으면 AntiAliasingMethod=4(TSR) 체크
		const int32 AAMethod = GetCVarInt(GraphicsCVars::AntiAliasingMethod);
		CurrentUpscaler = (AAMethod == 4) ? EHellunaUpscalerType::TSR : EHellunaUpscalerType::Off;
	}

	if (ComboBox_Upscaler)
	{
		switch (CurrentUpscaler)
		{
		case EHellunaUpscalerType::DLSS: ComboBox_Upscaler->SetSelectedOption(TEXT("DLSS")); break;
		case EHellunaUpscalerType::FSR:  ComboBox_Upscaler->SetSelectedOption(TEXT("FSR"));  break;
		case EHellunaUpscalerType::TSR:  ComboBox_Upscaler->SetSelectedOption(TEXT("TSR"));  break;
		default:                         ComboBox_Upscaler->SetSelectedOption(TEXT("OFF"));   break;
		}
	}

	PopulateUpscalerQualityOptions();

	// ── 프레임 생성 상태 ──
	const bool bDLSSG = (GetCVarInt(GraphicsCVars::DLSSGEnable) != 0);
	const bool bFSRFI = (GetCVarInt(GraphicsCVars::FSRFrameInterp) != 0);
	bFrameGenEnabled = bDLSSG || bFSRFI;
	if (CheckBox_FrameGen) CheckBox_FrameGen->SetIsChecked(bFrameGenEnabled);

	// ── 리플렉스 상태 ──
	bReflexEnabled = (GetCVarInt(GraphicsCVars::ReflexEnable) != 0);
	if (CheckBox_Reflex) CheckBox_Reflex->SetIsChecked(bReflexEnabled);

	// ── TSR 슬라이더 ──
	const float ScreenPct = GetCVarFloat(GraphicsCVars::ScreenPercentage);
	if (Slider_ScreenPercentage) Slider_ScreenPercentage->SetValue(ScreenPct);
	if (Text_ScreenPercentage)   Text_ScreenPercentage->SetText(FText::FromString(FString::Printf(TEXT("%d%%"), FMath::RoundToInt(ScreenPct))));

	// ── Scalability 개별 설정 ──
	UGameUserSettings* Settings = UGameUserSettings::GetGameUserSettings();
	if (Settings)
	{
		if (ComboBox_ShadowQuality)  ComboBox_ShadowQuality->SetSelectedOption(QualityLevelToName(Settings->GetShadowQuality()));
		if (ComboBox_TextureQuality) ComboBox_TextureQuality->SetSelectedOption(QualityLevelToName(Settings->GetTextureQuality()));
		if (ComboBox_EffectsQuality) ComboBox_EffectsQuality->SetSelectedOption(QualityLevelToName(Settings->GetVisualEffectQuality()));
		if (ComboBox_ViewDistance)   ComboBox_ViewDistance->SetSelectedOption(QualityLevelToName(Settings->GetViewDistanceQuality()));
		if (ComboBox_AntiAliasing)   ComboBox_AntiAliasing->SetSelectedOption(QualityLevelToName(Settings->GetAntiAliasingQuality()));
		if (ComboBox_PostProcess)    ComboBox_PostProcess->SetSelectedOption(QualityLevelToName(Settings->GetPostProcessingQuality()));

		// 프리셋 동기화
		const int32 OverallLevel = Settings->GetOverallScalabilityLevel();
		if (ComboBox_QualityPreset)
		{
			if (OverallLevel >= 0 && OverallLevel <= 3)
				ComboBox_QualityPreset->SetSelectedOption(QualityLevelToName(OverallLevel));
			else
				ComboBox_QualityPreset->SetSelectedOption(TEXT("Custom"));
		}

		// ── 해상도 ──
		const FIntPoint Res = Settings->GetScreenResolution();
		if (ComboBox_Resolution)
		{
			const FString ResStr = FString::Printf(TEXT("%dx%d"), Res.X, Res.Y);
			ComboBox_Resolution->SetSelectedOption(ResStr);
		}

		// ── 화면모드 ──
		if (ComboBox_WindowMode)
		{
			switch (Settings->GetFullscreenMode())
			{
			case EWindowMode::Fullscreen:           ComboBox_WindowMode->SetSelectedOption(TEXT("Fullscreen")); break;
			case EWindowMode::Windowed:             ComboBox_WindowMode->SetSelectedOption(TEXT("Windowed"));   break;
			case EWindowMode::WindowedFullscreen:    ComboBox_WindowMode->SetSelectedOption(TEXT("Borderless")); break;
			default:                                ComboBox_WindowMode->SetSelectedOption(TEXT("Fullscreen")); break;
			}
		}
	}

	UpdateUpscalerDependentUI();

	bSuppressCallbacks = false;
}

// ════════════════════════════════════════════════════════════════════════════════
//  해상도 목록 채우기
// ════════════════════════════════════════════════════════════════════════════════
void UHellunaGraphicsSettingsWidget::PopulateResolutionOptions()
{
	if (!ComboBox_Resolution) return;

	ComboBox_Resolution->ClearOptions();

	TArray<FIntPoint> Resolutions;
	// 화면 해상도 목록 조회
	if (UKismetSystemLibrary::GetSupportedFullscreenResolutions(Resolutions))
	{
		for (const FIntPoint& Res : Resolutions)
		{
			ComboBox_Resolution->AddOption(FString::Printf(TEXT("%dx%d"), Res.X, Res.Y));
		}
	}

	// 폴백: 현재 해상도가 목록에 없으면 추가
	if (const UGameUserSettings* Settings = UGameUserSettings::GetGameUserSettings())
	{
		const FIntPoint Current = Settings->GetScreenResolution();
		const FString CurrentStr = FString::Printf(TEXT("%dx%d"), Current.X, Current.Y);
		if (ComboBox_Resolution->FindOptionIndex(CurrentStr) == -1)
		{
			ComboBox_Resolution->AddOption(CurrentStr);
		}
	}
}

// ════════════════════════════════════════════════════════════════════════════════
//  업스케일러 품질 옵션 채우기
// ════════════════════════════════════════════════════════════════════════════════
void UHellunaGraphicsSettingsWidget::PopulateUpscalerQualityOptions()
{
	if (!ComboBox_UpscalerQuality) return;

	ComboBox_UpscalerQuality->ClearOptions();

	switch (CurrentUpscaler)
	{
	case EHellunaUpscalerType::DLSS:
		ComboBox_UpscalerQuality->AddOption(TEXT("Ultra Performance"));
		ComboBox_UpscalerQuality->AddOption(TEXT("Performance"));
		ComboBox_UpscalerQuality->AddOption(TEXT("Balanced"));
		ComboBox_UpscalerQuality->AddOption(TEXT("Quality"));
		ComboBox_UpscalerQuality->AddOption(TEXT("DLAA"));
		// 현재 설정 반영
		{
			const int32 Q = GetCVarInt(GraphicsCVars::DLSSQuality);
			switch (Q)
			{
			case 0: ComboBox_UpscalerQuality->SetSelectedOption(TEXT("Ultra Performance")); break;
			case 1: ComboBox_UpscalerQuality->SetSelectedOption(TEXT("Performance")); break;
			case 2: ComboBox_UpscalerQuality->SetSelectedOption(TEXT("Balanced")); break;
			case 3: ComboBox_UpscalerQuality->SetSelectedOption(TEXT("Quality")); break;
			case 5: ComboBox_UpscalerQuality->SetSelectedOption(TEXT("DLAA")); break;
			default: ComboBox_UpscalerQuality->SetSelectedOption(TEXT("Balanced")); break;
			}
		}
		break;

	case EHellunaUpscalerType::FSR:
		ComboBox_UpscalerQuality->AddOption(TEXT("Performance"));
		ComboBox_UpscalerQuality->AddOption(TEXT("Balanced"));
		ComboBox_UpscalerQuality->AddOption(TEXT("Quality"));
		ComboBox_UpscalerQuality->AddOption(TEXT("Ultra Quality"));
		{
			const int32 Q = GetCVarInt(GraphicsCVars::FSRQuality);
			switch (Q)
			{
			case 0: ComboBox_UpscalerQuality->SetSelectedOption(TEXT("Performance")); break;
			case 1: ComboBox_UpscalerQuality->SetSelectedOption(TEXT("Balanced")); break;
			case 2: ComboBox_UpscalerQuality->SetSelectedOption(TEXT("Quality")); break;
			case 3: ComboBox_UpscalerQuality->SetSelectedOption(TEXT("Ultra Quality")); break;
			default: ComboBox_UpscalerQuality->SetSelectedOption(TEXT("Balanced")); break;
			}
		}
		break;

	default:
		// TSR/OFF: ComboBox 비움
		break;
	}
}

// ════════════════════════════════════════════════════════════════════════════════
//  업스케일러 변경
// ════════════════════════════════════════════════════════════════════════════════
void UHellunaGraphicsSettingsWidget::OnUpscalerChanged(FString SelectedItem, ESelectInfo::Type SelectionType)
{
	if (bSuppressCallbacks) return;

	// 이전 업스케일러 끄기
	SetCVar(GraphicsCVars::DLSSEnable, 0);
	SetCVar(GraphicsCVars::FSREnable, 0);

	// 새 업스케일러 설정
	if (SelectedItem == TEXT("DLSS"))
	{
		CurrentUpscaler = EHellunaUpscalerType::DLSS;
		SetCVar(GraphicsCVars::DLSSEnable, 1);
	}
	else if (SelectedItem == TEXT("FSR"))
	{
		CurrentUpscaler = EHellunaUpscalerType::FSR;
		SetCVar(GraphicsCVars::FSREnable, 1);
	}
	else if (SelectedItem == TEXT("TSR"))
	{
		CurrentUpscaler = EHellunaUpscalerType::TSR;
		SetCVar(GraphicsCVars::AntiAliasingMethod, 4);
	}
	else
	{
		CurrentUpscaler = EHellunaUpscalerType::Off;
	}

	// 프레임 생성 리셋
	bFrameGenEnabled = false;
	SetCVar(GraphicsCVars::DLSSGEnable, 0);
	SetCVar(GraphicsCVars::FSRFrameInterp, 0);
	if (CheckBox_FrameGen) CheckBox_FrameGen->SetIsChecked(false);

	PopulateUpscalerQualityOptions();
	UpdateUpscalerDependentUI();

	UE_LOG(LogHellunaGraphics, Log, TEXT("Upscaler changed to: %s"), *SelectedItem);
}

// ════════════════════════════════════════════════════════════════════════════════
//  업스케일러 품질 변경
// ════════════════════════════════════════════════════════════════════════════════
void UHellunaGraphicsSettingsWidget::OnUpscalerQualityChanged(FString SelectedItem, ESelectInfo::Type SelectionType)
{
	if (bSuppressCallbacks) return;

	if (CurrentUpscaler == EHellunaUpscalerType::DLSS)
	{
		int32 QualitySetting = 2; // Balanced
		if      (SelectedItem == TEXT("Ultra Performance")) QualitySetting = 0;
		else if (SelectedItem == TEXT("Performance"))       QualitySetting = 1;
		else if (SelectedItem == TEXT("Balanced"))          QualitySetting = 2;
		else if (SelectedItem == TEXT("Quality"))           QualitySetting = 3;
		else if (SelectedItem == TEXT("DLAA"))              QualitySetting = 5;

		SetCVar(GraphicsCVars::DLSSQuality, QualitySetting);
	}
	else if (CurrentUpscaler == EHellunaUpscalerType::FSR)
	{
		int32 QualityMode = 1; // Balanced
		if      (SelectedItem == TEXT("Performance"))    QualityMode = 0;
		else if (SelectedItem == TEXT("Balanced"))       QualityMode = 1;
		else if (SelectedItem == TEXT("Quality"))        QualityMode = 2;
		else if (SelectedItem == TEXT("Ultra Quality"))  QualityMode = 3;

		SetCVar(GraphicsCVars::FSRQuality, QualityMode);
	}
}

// ════════════════════════════════════════════════════════════════════════════════
//  TSR ScreenPercentage 슬라이더
// ════════════════════════════════════════════════════════════════════════════════
void UHellunaGraphicsSettingsWidget::OnScreenPercentageChanged(float Value)
{
	if (bSuppressCallbacks) return;

	const float Clamped = FMath::Clamp(Value, 50.f, 100.f);
	SetCVar(GraphicsCVars::ScreenPercentage, Clamped);

	if (Text_ScreenPercentage)
	{
		Text_ScreenPercentage->SetText(FText::FromString(FString::Printf(TEXT("%d%%"), FMath::RoundToInt(Clamped))));
	}
}

// ════════════════════════════════════════════════════════════════════════════════
//  프레임 생성 토글
// ════════════════════════════════════════════════════════════════════════════════
void UHellunaGraphicsSettingsWidget::OnFrameGenToggled(bool bIsChecked)
{
	if (bSuppressCallbacks) return;

	bFrameGenEnabled = bIsChecked;

	if (CurrentUpscaler == EHellunaUpscalerType::DLSS)
	{
		SetCVar(GraphicsCVars::DLSSGEnable, bIsChecked ? 1 : 0);
	}
	else if (CurrentUpscaler == EHellunaUpscalerType::FSR)
	{
		SetCVar(GraphicsCVars::FSRFrameInterp, bIsChecked ? 1 : 0);
	}

	UpdateReflexAvailability();

	UE_LOG(LogHellunaGraphics, Log, TEXT("Frame Generation: %s"), bIsChecked ? TEXT("ON") : TEXT("OFF"));
}

// ════════════════════════════════════════════════════════════════════════════════
//  리플렉스 토글
// ════════════════════════════════════════════════════════════════════════════════
void UHellunaGraphicsSettingsWidget::OnReflexToggled(bool bIsChecked)
{
	if (bSuppressCallbacks) return;

	bReflexEnabled = bIsChecked;
	SetCVar(GraphicsCVars::ReflexEnable, bIsChecked ? 1 : 0);
}

// ════════════════════════════════════════════════════════════════════════════════
//  UI 상태 갱신: 업스케일러 하위 섹션
// ════════════════════════════════════════════════════════════════════════════════
void UHellunaGraphicsSettingsWidget::UpdateUpscalerDependentUI()
{
	const bool bIsDLSSOrFSR = (CurrentUpscaler == EHellunaUpscalerType::DLSS || CurrentUpscaler == EHellunaUpscalerType::FSR);
	const bool bIsTSR       = (CurrentUpscaler == EHellunaUpscalerType::TSR);
	const bool bIsOff       = (CurrentUpscaler == EHellunaUpscalerType::Off);

	// 품질 ComboBox: DLSS/FSR에서만 표시
	if (ComboBox_UpscalerQuality) ComboBox_UpscalerQuality->SetVisibility(bIsDLSSOrFSR ? ESlateVisibility::Visible : ESlateVisibility::Collapsed);

	// TSR 슬라이더: TSR에서만 표시
	if (Slider_ScreenPercentage) Slider_ScreenPercentage->SetVisibility(bIsTSR ? ESlateVisibility::Visible : ESlateVisibility::Collapsed);
	if (Text_ScreenPercentage)   Text_ScreenPercentage->SetVisibility(bIsTSR ? ESlateVisibility::Visible : ESlateVisibility::Collapsed);

	// 프레임 생성: DLSS/FSR에서만 활성화
	if (CheckBox_FrameGen)
	{
		CheckBox_FrameGen->SetIsEnabled(bIsDLSSOrFSR);
		if (!bIsDLSSOrFSR)
		{
			CheckBox_FrameGen->SetIsChecked(false);
			bFrameGenEnabled = false;
		}
	}
	if (Text_FrameGenLabel)
	{
		if (bIsOff || bIsTSR)
			Text_FrameGenLabel->SetText(FText::FromString(TEXT("Frame Generation (DLSS/FSR 선택 필요)")));
		else
			Text_FrameGenLabel->SetText(FText::FromString(TEXT("Frame Generation")));
	}

	UpdateReflexAvailability();
}

// ════════════════════════════════════════════════════════════════════════════════
//  리플렉스 활성/비활성 갱신
//  조건: 프레임 생성 ON + DLSS 선택
// ════════════════════════════════════════════════════════════════════════════════
void UHellunaGraphicsSettingsWidget::UpdateReflexAvailability()
{
	const bool bCanUseReflex = bFrameGenEnabled && (CurrentUpscaler == EHellunaUpscalerType::DLSS);

	if (CheckBox_Reflex)
	{
		CheckBox_Reflex->SetIsEnabled(bCanUseReflex);
		if (!bCanUseReflex)
		{
			CheckBox_Reflex->SetIsChecked(false);
			bReflexEnabled = false;
			SetCVar(GraphicsCVars::ReflexEnable, 0);
		}
	}

	if (Text_ReflexLabel)
	{
		if (CurrentUpscaler != EHellunaUpscalerType::DLSS)
			Text_ReflexLabel->SetText(FText::FromString(TEXT("Reflex (DLSS 선택 필요)")));
		else if (!bFrameGenEnabled)
			Text_ReflexLabel->SetText(FText::FromString(TEXT("Reflex (Frame Generation 활성화 필요)")));
		else
			Text_ReflexLabel->SetText(FText::FromString(TEXT("NVIDIA Reflex Low Latency")));
	}
}

// ════════════════════════════════════════════════════════════════════════════════
//  전체 품질 프리셋
// ════════════════════════════════════════════════════════════════════════════════
void UHellunaGraphicsSettingsWidget::OnQualityPresetChanged(FString SelectedItem, ESelectInfo::Type SelectionType)
{
	if (bSuppressCallbacks) return;
	if (SelectedItem == TEXT("Custom")) return;

	const int32 Level = QualityNameToLevel(SelectedItem);

	UGameUserSettings* Settings = UGameUserSettings::GetGameUserSettings();
	if (!Settings) return;

	Settings->SetOverallScalabilityLevel(Level);

	// 개별 ComboBox 동기화
	bSuppressCallbacks = true;
	const FString LevelName = QualityLevelToName(Level);
	if (ComboBox_ShadowQuality)  ComboBox_ShadowQuality->SetSelectedOption(LevelName);
	if (ComboBox_TextureQuality) ComboBox_TextureQuality->SetSelectedOption(LevelName);
	if (ComboBox_EffectsQuality) ComboBox_EffectsQuality->SetSelectedOption(LevelName);
	if (ComboBox_ViewDistance)   ComboBox_ViewDistance->SetSelectedOption(LevelName);
	if (ComboBox_AntiAliasing)   ComboBox_AntiAliasing->SetSelectedOption(LevelName);
	if (ComboBox_PostProcess)    ComboBox_PostProcess->SetSelectedOption(LevelName);
	bSuppressCallbacks = false;
}

// ════════════════════════════════════════════════════════════════════════════════
//  개별 Scalability 변경 핸들러
// ════════════════════════════════════════════════════════════════════════════════
void UHellunaGraphicsSettingsWidget::OnShadowQualityChanged(FString SelectedItem, ESelectInfo::Type SelectionType)
{
	if (bSuppressCallbacks) return;
	SetCVar(GraphicsCVars::ShadowQuality, QualityNameToLevel(SelectedItem));
	CheckAndUpdatePresetFromIndividual();
}

void UHellunaGraphicsSettingsWidget::OnTextureQualityChanged(FString SelectedItem, ESelectInfo::Type SelectionType)
{
	if (bSuppressCallbacks) return;
	SetCVar(GraphicsCVars::TextureQuality, QualityNameToLevel(SelectedItem));
	CheckAndUpdatePresetFromIndividual();
}

void UHellunaGraphicsSettingsWidget::OnEffectsQualityChanged(FString SelectedItem, ESelectInfo::Type SelectionType)
{
	if (bSuppressCallbacks) return;
	SetCVar(GraphicsCVars::EffectsQuality, QualityNameToLevel(SelectedItem));
	CheckAndUpdatePresetFromIndividual();
}

void UHellunaGraphicsSettingsWidget::OnViewDistanceChanged(FString SelectedItem, ESelectInfo::Type SelectionType)
{
	if (bSuppressCallbacks) return;
	SetCVar(GraphicsCVars::ViewDistanceQuality, QualityNameToLevel(SelectedItem));
	CheckAndUpdatePresetFromIndividual();
}

void UHellunaGraphicsSettingsWidget::OnAntiAliasingChanged(FString SelectedItem, ESelectInfo::Type SelectionType)
{
	if (bSuppressCallbacks) return;
	SetCVar(GraphicsCVars::AntiAliasingQuality, QualityNameToLevel(SelectedItem));
	CheckAndUpdatePresetFromIndividual();
}

void UHellunaGraphicsSettingsWidget::OnPostProcessChanged(FString SelectedItem, ESelectInfo::Type SelectionType)
{
	if (bSuppressCallbacks) return;
	SetCVar(GraphicsCVars::PostProcessQuality, QualityNameToLevel(SelectedItem));
	CheckAndUpdatePresetFromIndividual();
}

void UHellunaGraphicsSettingsWidget::CheckAndUpdatePresetFromIndividual()
{
	if (!ComboBox_QualityPreset) return;

	// 6개 ComboBox에서 선택된 인덱스 수집
	TArray<int32> Levels;
	auto GetLevel = [&](const UComboBoxString* Combo) -> int32
	{
		return Combo ? QualityNameToLevel(Combo->GetSelectedOption()) : -1;
	};

	Levels.Add(GetLevel(ComboBox_ShadowQuality));
	Levels.Add(GetLevel(ComboBox_TextureQuality));
	Levels.Add(GetLevel(ComboBox_EffectsQuality));
	Levels.Add(GetLevel(ComboBox_ViewDistance));
	Levels.Add(GetLevel(ComboBox_AntiAliasing));
	Levels.Add(GetLevel(ComboBox_PostProcess));

	// 전부 같으면 프리셋, 아니면 Custom
	const int32 First = Levels[0];
	bool bAllSame = true;
	for (int32 L : Levels)
	{
		if (L != First) { bAllSame = false; break; }
	}

	bSuppressCallbacks = true;
	if (bAllSame && First >= 0 && First <= 3)
		ComboBox_QualityPreset->SetSelectedOption(QualityLevelToName(First));
	else
		ComboBox_QualityPreset->SetSelectedOption(TEXT("Custom"));
	bSuppressCallbacks = false;
}

// ════════════════════════════════════════════════════════════════════════════════
//  해상도 / 화면모드 변경
// ════════════════════════════════════════════════════════════════════════════════
void UHellunaGraphicsSettingsWidget::OnResolutionChanged(FString SelectedItem, ESelectInfo::Type SelectionType)
{
	if (bSuppressCallbacks) return;
	// "1920x1080" 파싱
	FString WidthStr, HeightStr;
	if (SelectedItem.Split(TEXT("x"), &WidthStr, &HeightStr))
	{
		const int32 W = FCString::Atoi(*WidthStr);
		const int32 H = FCString::Atoi(*HeightStr);
		if (UGameUserSettings* Settings = UGameUserSettings::GetGameUserSettings())
		{
			Settings->SetScreenResolution(FIntPoint(W, H));
		}
	}
}

void UHellunaGraphicsSettingsWidget::OnWindowModeChanged(FString SelectedItem, ESelectInfo::Type SelectionType)
{
	if (bSuppressCallbacks) return;

	UGameUserSettings* Settings = UGameUserSettings::GetGameUserSettings();
	if (!Settings) return;

	if (SelectedItem == TEXT("Fullscreen"))
		Settings->SetFullscreenMode(EWindowMode::Fullscreen);
	else if (SelectedItem == TEXT("Windowed"))
		Settings->SetFullscreenMode(EWindowMode::Windowed);
	else
		Settings->SetFullscreenMode(EWindowMode::WindowedFullscreen);
}

// ════════════════════════════════════════════════════════════════════════════════
//  적용 / 취소 / 기본값
// ════════════════════════════════════════════════════════════════════════════════
void UHellunaGraphicsSettingsWidget::OnApplyClicked()
{
	UGameUserSettings* Settings = UGameUserSettings::GetGameUserSettings();
	if (Settings)
	{
		Settings->ApplySettings(false);
		Settings->SaveSettings();
	}
	UE_LOG(LogHellunaGraphics, Log, TEXT("Graphics settings applied and saved."));
}

void UHellunaGraphicsSettingsWidget::OnCancelClicked()
{
	LoadCurrentSettings();
}

void UHellunaGraphicsSettingsWidget::OnResetDefaultClicked()
{
	UGameUserSettings* Settings = UGameUserSettings::GetGameUserSettings();
	if (Settings)
	{
		Settings->SetToDefaults();
		Settings->ApplySettings(false);
	}

	// 업스케일러 기본값: DLSS
	SetCVar(GraphicsCVars::DLSSEnable, 1);
	SetCVar(GraphicsCVars::FSREnable, 0);
	SetCVar(GraphicsCVars::DLSSQuality, 2); // Balanced
	SetCVar(GraphicsCVars::DLSSGEnable, 0);
	SetCVar(GraphicsCVars::FSRFrameInterp, 0);
	SetCVar(GraphicsCVars::ReflexEnable, 0);
	SetCVar(GraphicsCVars::ScreenPercentage, 100.f);

	LoadCurrentSettings();
	UE_LOG(LogHellunaGraphics, Log, TEXT("Graphics settings reset to defaults."));
}

// ════════════════════════════════════════════════════════════════════════════════
//  성능 표시 (NativeTick)
// ════════════════════════════════════════════════════════════════════════════════
void UHellunaGraphicsSettingsWidget::NativeTick(const FGeometry& MyGeometry, float InDeltaTime)
{
	Super::NativeTick(MyGeometry, InDeltaTime);

	PerfDisplayAccumulator += InDeltaTime;
	if (PerfDisplayAccumulator >= PerfDisplayInterval)
	{
		PerfDisplayAccumulator = 0.f;
		UpdatePerformanceDisplay();
	}
}

void UHellunaGraphicsSettingsWidget::UpdatePerformanceDisplay()
{
	const float DeltaSeconds = FApp::GetDeltaTime();
	const float FPS = (DeltaSeconds > 0.f) ? (1.f / DeltaSeconds) : 0.f;

	if (Text_FPS)
	{
		Text_FPS->SetText(FText::FromString(FString::Printf(TEXT("FPS: %d"), FMath::RoundToInt(FPS))));
	}

	// GPU/CPU 시간은 stat unit 데이터에서 가져오기
	if (Text_GPUTime)
	{
		// UE 5.7.2: GGPUFrameTime은 RHI_API uint32 (마이크로초 단위)
		extern RHI_API uint32 GGPUFrameTime;
		const float GPUTimeMs = static_cast<float>(GGPUFrameTime) / 1000.f;
		Text_GPUTime->SetText(FText::FromString(FString::Printf(TEXT("GPU: %.1fms"), GPUTimeMs)));
	}

	if (Text_CPUTime)
	{
		Text_CPUTime->SetText(FText::FromString(FString::Printf(TEXT("CPU: %.1fms"), DeltaSeconds * 1000.f)));
	}
}
