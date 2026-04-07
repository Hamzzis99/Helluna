// File: Source/Helluna/Public/UI/WorldMap/HellunaWorldMapWidget.h
#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "HellunaWorldMapWidget.generated.h"

class UCanvasPanel;
class UCanvasPanelSlot;
class UImage;
class UTextBlock;

/**
 * @brief   풀스크린 전술 맵 위젯
 * @details M키로 토글, 클릭 시 핑(1개) 생성/이동, 우클릭 삭제.
 *          핑은 클라이언트 사이드 전용 (RPC 없음).
 *          WBP_HellunaWorldMap의 부모 클래스로 reparent하여 사용.
 */
UCLASS()
class HELLUNA_API UHellunaWorldMapWidget : public UUserWidget
{
    GENERATED_BODY()

protected:
    virtual void NativeConstruct() override;
    virtual void NativeDestruct() override;
    virtual void NativeTick(const FGeometry& MyGeometry, float InDeltaTime) override;
    virtual FReply NativeOnMouseButtonDown(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent) override;

    // ── BindWidget (WBP 위젯 트리와 매핑) ──

    UPROPERTY(BlueprintReadWrite, meta = (BindWidget))
    TObjectPtr<UCanvasPanel> RootCanvas = nullptr;

    UPROPERTY(BlueprintReadWrite, meta = (BindWidgetOptional))
    TObjectPtr<UImage> MapImage = nullptr;

    UPROPERTY(BlueprintReadWrite, meta = (BindWidgetOptional))
    TObjectPtr<UImage> BaseMarker = nullptr;

    UPROPERTY(BlueprintReadWrite, meta = (BindWidgetOptional))
    TObjectPtr<UImage> LocalPlayerMarker = nullptr;

    UPROPERTY(BlueprintReadWrite, meta = (BindWidgetOptional))
    TObjectPtr<UCanvasPanel> TeamMarkerCanvas = nullptr;

    UPROPERTY(BlueprintReadWrite, meta = (BindWidgetOptional))
    TObjectPtr<UImage> PingMarker = nullptr;

    UPROPERTY(BlueprintReadWrite, meta = (BindWidgetOptional))
    TObjectPtr<UTextBlock> PingDistanceText = nullptr;

public:
    /** 풀맵 열기 (페이드인 + 슬라이드) */
    UFUNCTION(BlueprintCallable, Category = "WorldMap (월드맵)")
    void OpenMap();

    /** 풀맵 닫기 (페이드아웃) */
    UFUNCTION(BlueprintCallable, Category = "WorldMap (월드맵)")
    void CloseMap();

    /** 현재 열림 상태 */
    UFUNCTION(BlueprintPure, Category = "WorldMap (월드맵)")
    bool IsMapOpen() const { return bIsOpen; }

private:
    // ── 좌표 변환 (DayNightHUDWidget과 동일한 값 사용) ──
    static constexpr float MapCenterX = 5679.f;
    static constexpr float MapCenterY = -4864.f;
    static constexpr float MapHalfSize = 54035.f;

    /** 월드 좌표 -> 맵 위젯 픽셀 좌표 */
    FVector2D WorldToMapPixel(const FVector& WorldLocation) const;

    /** 맵 위젯 픽셀 좌표 -> 월드 좌표 */
    FVector MapPixelToWorld(const FVector2D& MapPixel) const;

    /** 핑 마커 위치 갱신 */
    void UpdatePingMarker();

    /** 플레이어/팀원 마커 위치 갱신 */
    void UpdatePlayerMarkers();

    // ── 애니메이션 상태 (NotifyPanel과 동일 패턴) ──
    uint8 AnimState = 0; // 0=숨김, 1=페이드인, 2=표시, 3=페이드아웃
    float AnimTime = 0.f;
    bool bIsOpen = false;
    static constexpr float FadeInDuration = 0.6f;
    static constexpr float FadeOutDuration = 0.6f;
    static constexpr float SlideOffset = 4.f;

    /** 맵 영역 사이즈 (Designer에서 설정한 MapImage 크기) */
    FVector2D MapWidgetSize = FVector2D(900.f, 900.f);
    FVector2D MapWidgetTopLeft = FVector2D(510.f, 90.f);
};
