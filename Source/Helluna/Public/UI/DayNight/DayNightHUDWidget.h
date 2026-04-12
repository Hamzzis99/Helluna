// File: Source/Helluna/Public/UI/DayNight/DayNightHUDWidget.h
#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "GameMode/HellunaDefenseGameState.h"
#include "DayNightHUDWidget.generated.h"

class USizeBox;
class UImage;
class UTextBlock;
class UProgressBar;
class UOverlay;
class UCanvasPanel;
class UCanvasPanelSlot;
class UMaterialInstanceDynamic;
class UMaterialInterface;

UCLASS()
class HELLUNA_API UDayNightHUDWidget : public UUserWidget
{
    GENERATED_BODY()

protected:
    virtual void NativeConstruct() override;
    virtual void NativeDestruct() override;
    virtual void NativeTick(const FGeometry& MyGeometry, float InDeltaTime) override;

    // ── 미니맵 (좌상단) ──
    UPROPERTY(BlueprintReadWrite, meta = (BindWidget))
    TObjectPtr<USizeBox> MinimapSizeBox = nullptr;

    UPROPERTY(BlueprintReadWrite, meta = (BindWidgetOptional))
    TObjectPtr<UOverlay> MinimapOverlay = nullptr;

    UPROPERTY(BlueprintReadWrite, meta = (BindWidget))
    TObjectPtr<UImage> MinimapImage = nullptr;

    UPROPERTY(BlueprintReadWrite, meta = (BindWidgetOptional))
    TObjectPtr<UCanvasPanel> MinimapIconCanvas = nullptr;

    /** 미니맵 우주선(BASE) 마커 이미지 (시안 점, WBP에서 추가) */
    UPROPERTY(BlueprintReadWrite, meta = (BindWidgetOptional))
    TObjectPtr<UImage> MinimapBaseMarker = nullptr;

    /** 미니맵 핑 마커 이미지 (노란 다이아몬드, WBP에서 추가) */
    UPROPERTY(BlueprintReadWrite, meta = (BindWidgetOptional))
    TObjectPtr<UImage> MinimapPingMarker = nullptr;

    /** 미니맵 본인 마커 (녹색 화살표) */
    UPROPERTY(BlueprintReadWrite, meta = (BindWidgetOptional))
    TObjectPtr<UImage> MinimapLocalPlayerMarker = nullptr;

    /** 미니맵 다른 플레이어 마커 1 */
    UPROPERTY(BlueprintReadWrite, meta = (BindWidgetOptional))
    TObjectPtr<UImage> MinimapTeamMarker1 = nullptr;

    /** 미니맵 다른 플레이어 마커 2 */
    UPROPERTY(BlueprintReadWrite, meta = (BindWidgetOptional))
    TObjectPtr<UImage> MinimapTeamMarker2 = nullptr;

    // ── 웨이브 정보 (좌상단, 미니맵 아래) ──
    UPROPERTY(BlueprintReadWrite, meta = (BindWidget))
    TObjectPtr<UOverlay> WaveInfoPanel = nullptr;

    UPROPERTY(BlueprintReadWrite, meta = (BindWidget))
    TObjectPtr<UTextBlock> WaveLabelText = nullptr;

    UPROPERTY(BlueprintReadWrite, meta = (BindWidget))
    TObjectPtr<UTextBlock> WaveCountText = nullptr;

    UPROPERTY(BlueprintReadWrite, meta = (BindWidget))
    TObjectPtr<UTextBlock> WaveSubText = nullptr;

    UPROPERTY(BlueprintReadWrite, meta = (BindWidget))
    TObjectPtr<UProgressBar> WaveProgressBar = nullptr;

    // ── 낮 타이머 (상단 중앙, 낮에만 표시) ──
    UPROPERTY(BlueprintReadWrite, meta = (BindWidget))
    TObjectPtr<UOverlay> DayTimerPanel = nullptr;

    UPROPERTY(BlueprintReadWrite, meta = (BindWidget))
    TObjectPtr<UTextBlock> DayLabelText = nullptr;

    UPROPERTY(BlueprintReadWrite, meta = (BindWidget))
    TObjectPtr<UTextBlock> DayTimeText = nullptr;

    UPROPERTY(BlueprintReadWrite, meta = (BindWidget))
    TObjectPtr<UTextBlock> DayTimeUnitText = nullptr;

    UPROPERTY(BlueprintReadWrite, meta = (BindWidget))
    TObjectPtr<UProgressBar> DayTimerBar = nullptr;

    // ── 미션 알림 (상단 중앙, 타이머 아래) ──
    UPROPERTY(BlueprintReadWrite, meta = (BindWidgetOptional))
    TObjectPtr<UOverlay> NotifyPanel = nullptr;

    UPROPERTY(BlueprintReadWrite, meta = (BindWidgetOptional))
    TObjectPtr<UTextBlock> NotifyPhaseText = nullptr;

    UPROPERTY(BlueprintReadWrite, meta = (BindWidgetOptional))
    TObjectPtr<UTextBlock> NotifyTitleText = nullptr;

    UPROPERTY(BlueprintReadWrite, meta = (BindWidgetOptional))
    TObjectPtr<UTextBlock> NotifyDescText = nullptr;

    // ── 미니맵 설정 (에디터에서 조정 가능) ──

    /** 미니맵 머티리얼 (M_Minimap) — BP에서 지정 */
    UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Minimap",
        meta = (DisplayName = "Minimap Material (미니맵 머티리얼)"))
    TObjectPtr<UMaterialInterface> MinimapMaterial = nullptr;

    /** 미니맵이 표시하는 영역 비율 (0.15 = 전체 맵의 15%) */
    UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Minimap",
        meta = (DisplayName = "Zoom Scale (줌 스케일)", ClampMin = "0.01", ClampMax = "1.0"))
    float MinimapZoomScale = 0.15f;

    /** 플레이어 아이콘 크기 (px) */
    UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Minimap",
        meta = (DisplayName = "Player Icon Size (아이콘 크기)"))
    float PlayerIconSize = 8.f;

    /** 로컬 플레이어 아이콘 색상 */
    UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Minimap",
        meta = (DisplayName = "Local Player Color (로컬 색상)"))
    FLinearColor LocalPlayerColor = FLinearColor(0.f, 1.f, 0.f, 1.f);

    /** 다른 플레이어 아이콘 색상 */
    UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Minimap",
        meta = (DisplayName = "Team Player Color (팀 색상)"))
    FLinearColor TeamPlayerColor = FLinearColor(0.2f, 0.6f, 1.f, 1.f);

private:
    // ── 낮/밤 캐시 ──
    EDefensePhase LastPhase = EDefensePhase::Day;
    int32 LastDay = -1;
    int32 LastTimeRemaining = -1;
    int32 LastAliveMonsters = -1;
    int32 LastTotalMonsters = -1;
    float CachedTotalDayTime = 0.f;

    void UpdateDayMode(AHellunaDefenseGameState* GS);
    void UpdateNightMode(AHellunaDefenseGameState* GS);

    // ── 미션 알림 ──
    FTimerHandle NotifyHideTimer;
    bool bWarningShown = false;

    // 애니메이션 상태 (0 = 숨김, 1 = 페이드인 중, 2 = 표시 유지, 3 = 페이드아웃 중)
    uint8 NotifyAnimState = 0;
    float NotifyAnimTime = 0.f;
    static constexpr float NotifyFadeInDuration = 0.35f;
    static constexpr float NotifyFadeOutDuration = 0.45f;
    static constexpr float NotifySlideOffset = 12.f;

    /** 알림 표시 (3초 후 자동 숨김) */
    void ShowNotification(const FString& PhaseLabel, const FString& Title, const FString& Desc, const FLinearColor& Color);

    /** 알림 숨기기 */
    void HideNotification();

    // ── 미니맵 ──

    /** 맵 월드 좌표 범위 (SceneCapture 직교 캡처 기준) */
    static constexpr float MapCenterX = 0.f;
    static constexpr float MapCenterY = 0.f;
    static constexpr float MapHalfSize = 201600.f;  // OrthoWidth/2

    UPROPERTY()
    TObjectPtr<UMaterialInstanceDynamic> MinimapMID = nullptr;

    /** 플레이어 아이콘 위젯 풀 (PlayerState → Icon Image) */
    UPROPERTY()
    TMap<TObjectPtr<APlayerState>, TObjectPtr<UImage>> PlayerIconMap;

    /** 미니맵 초기화 (Dynamic Material + 아이콘 준비) */
    void InitializeMinimap();

    /** 미니맵 매 틱 갱신 (UV 오프셋 + 아이콘 위치) */
    void UpdateMinimap();

    /** 월드 좌표 → 미니맵 UV (0~1) 변환 */
    FVector2D WorldToMinimapUV(const FVector& WorldLocation) const;

    /** 아이콘 생성/제거/위치 갱신 */
    UImage* CreatePlayerIcon(const FLinearColor& Color);
    void CleanupPlayerIcons();
};
