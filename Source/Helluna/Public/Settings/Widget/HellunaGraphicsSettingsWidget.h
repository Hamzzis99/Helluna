// ════════════════════════════════════════════════════════════════════════════════
// File: Source/Helluna/Public/Settings/Widget/HellunaGraphicsSettingsWidget.h
// ════════════════════════════════════════════════════════════════════════════════
//
// 그래픽 설정 위젯 -- 업스케일러/품질/프레임생성/리플렉스/해상도/화면모드 설정
//
// 핵심 로직:
//    - DLSS/FSR 모두 항상 선택 가능 (GPU 제한 없음)
//    - 프레임 생성: DLSS/FSR 선택 시에만 활성화
//    - 리플렉스: 프레임 생성 ON + DLSS 선택 시에만 활성화
//    - 개별 품질 변경 시 프리셋 -> "Custom" 자동 전환
//
// 작성자: Gihyeon (Claude Code 보조)
// ════════════════════════════════════════════════════════════════════════════════

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "HellunaGraphicsSettingsWidget.generated.h"

// 전방 선언
class UComboBoxString;
class UCheckBox;
class UButton;
class UTextBlock;
class USlider;

// ── 업스케일러 타입 ──
UENUM(BlueprintType)
enum class EHellunaUpscalerType : uint8
{
	DLSS    UMETA(DisplayName = "DLSS"),
	FSR     UMETA(DisplayName = "FSR"),
	TSR     UMETA(DisplayName = "TSR"),
	Off     UMETA(DisplayName = "OFF")
};

UCLASS()
class HELLUNA_API UHellunaGraphicsSettingsWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	virtual void NativeOnInitialized() override;
	virtual void NativeTick(const FGeometry& MyGeometry, float InDeltaTime) override;

protected:
	// ════════════════════════════════════════════════════════════
	//  BindWidgetOptional
	// ════════════════════════════════════════════════════════════

	// ── 제목 ──
	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> Text_Title;

	// ── 업스케일러 선택 ──
	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UComboBoxString> ComboBox_Upscaler;

	// ── 업스케일러 품질 ──
	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UComboBoxString> ComboBox_UpscalerQuality;

	// ── TSR 전용 ScreenPercentage 슬라이더 ──
	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<USlider> Slider_ScreenPercentage;

	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> Text_ScreenPercentage;

	// ── 프레임 생성 ──
	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UCheckBox> CheckBox_FrameGen;

	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> Text_FrameGenLabel;

	// ── 리플렉스 ──
	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UCheckBox> CheckBox_Reflex;

	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> Text_ReflexLabel;

	// ── 전체 품질 프리셋 ──
	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UComboBoxString> ComboBox_QualityPreset;

	// ── 개별 Scalability (6개) ──
	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UComboBoxString> ComboBox_ShadowQuality;

	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UComboBoxString> ComboBox_TextureQuality;

	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UComboBoxString> ComboBox_EffectsQuality;

	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UComboBoxString> ComboBox_ViewDistance;

	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UComboBoxString> ComboBox_AntiAliasing;

	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UComboBoxString> ComboBox_PostProcess;

	// ── 해상도/화면모드 ──
	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UComboBoxString> ComboBox_Resolution;

	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UComboBoxString> ComboBox_WindowMode;

	// ── 성능 표시 ──
	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> Text_FPS;

	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> Text_GPUTime;

	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> Text_CPUTime;

	// ── 버튼 ──
	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UButton> Button_Apply;

	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UButton> Button_Cancel;

	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UButton> Button_ResetDefault;

private:
	// ════════════════════════════════════════════════════════════
	//  초기화
	// ════════════════════════════════════════════════════════════
	void LoadCurrentSettings();
	void PopulateResolutionOptions();
	void PopulateUpscalerQualityOptions();
	void DetectGPUCapabilities();

	// ════════════════════════════════════════════════════════════
	//  업스케일러
	// ════════════════════════════════════════════════════════════
	UFUNCTION()
	void OnUpscalerChanged(FString SelectedItem, ESelectInfo::Type SelectionType);

	UFUNCTION()
	void OnUpscalerQualityChanged(FString SelectedItem, ESelectInfo::Type SelectionType);

	UFUNCTION()
	void OnScreenPercentageChanged(float Value);

	// ════════════════════════════════════════════════════════════
	//  프레임 생성 / 리플렉스
	// ════════════════════════════════════════════════════════════
	UFUNCTION()
	void OnFrameGenToggled(bool bIsChecked);

	UFUNCTION()
	void OnReflexToggled(bool bIsChecked);

	// ════════════════════════════════════════════════════════════
	//  품질 프리셋 / 개별 설정
	// ════════════════════════════════════════════════════════════
	UFUNCTION()
	void OnQualityPresetChanged(FString SelectedItem, ESelectInfo::Type SelectionType);

	UFUNCTION()
	void OnShadowQualityChanged(FString SelectedItem, ESelectInfo::Type SelectionType);
	UFUNCTION()
	void OnTextureQualityChanged(FString SelectedItem, ESelectInfo::Type SelectionType);
	UFUNCTION()
	void OnEffectsQualityChanged(FString SelectedItem, ESelectInfo::Type SelectionType);
	UFUNCTION()
	void OnViewDistanceChanged(FString SelectedItem, ESelectInfo::Type SelectionType);
	UFUNCTION()
	void OnAntiAliasingChanged(FString SelectedItem, ESelectInfo::Type SelectionType);
	UFUNCTION()
	void OnPostProcessChanged(FString SelectedItem, ESelectInfo::Type SelectionType);

	void CheckAndUpdatePresetFromIndividual();

	// ════════════════════════════════════════════════════════════
	//  해상도 / 화면모드
	// ════════════════════════════════════════════════════════════
	UFUNCTION()
	void OnResolutionChanged(FString SelectedItem, ESelectInfo::Type SelectionType);
	UFUNCTION()
	void OnWindowModeChanged(FString SelectedItem, ESelectInfo::Type SelectionType);

	// ════════════════════════════════════════════════════════════
	//  버튼
	// ════════════════════════════════════════════════════════════
	UFUNCTION()
	void OnApplyClicked();
	UFUNCTION()
	void OnCancelClicked();
	UFUNCTION()
	void OnResetDefaultClicked();

	// ════════════════════════════════════════════════════════════
	//  UI 상태 갱신
	// ════════════════════════════════════════════════════════════
	void UpdateUpscalerDependentUI();
	void UpdateReflexAvailability();
	void UpdatePerformanceDisplay();

	// ════════════════════════════════════════════════════════════
	//  CVar 헬퍼
	// ════════════════════════════════════════════════════════════
	void SetCVar(const TCHAR* Name, int32 Value);
	void SetCVar(const TCHAR* Name, float Value);
	int32 GetCVarInt(const TCHAR* Name) const;
	float GetCVarFloat(const TCHAR* Name) const;

	/** ComboBox 선택 인덱스 → Scalability 레벨 (Low=0 ~ Epic=3) */
	static int32 QualityNameToLevel(const FString& Name);
	static FString QualityLevelToName(int32 Level);

	// ════════════════════════════════════════════════════════════
	//  상태 캐시
	// ════════════════════════════════════════════════════════════
	EHellunaUpscalerType CurrentUpscaler = EHellunaUpscalerType::DLSS;
	bool bFrameGenEnabled = false;
	bool bReflexEnabled = false;
	bool bIsNvidiaGPU = false;

	/** FPS 표시 업데이트 주기 제어 */
	float PerfDisplayAccumulator = 0.f;
	static constexpr float PerfDisplayInterval = 0.5f;

	/** 프로그래밍적 ComboBox 변경 시 콜백 무시용 */
	bool bSuppressCallbacks = false;
};
