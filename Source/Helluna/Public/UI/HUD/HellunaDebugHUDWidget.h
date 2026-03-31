// ════════════════════════════════════════════════════════════════════════════════
// HellunaDebugHUDWidget.h
// ════════════════════════════════════════════════════════════════════════════════
//
// 디버그 전용 HUD 오버레이 — FPS / 플레이어 ID / 캐릭터 클래스 / Ping / NetRole 표시.
//
// 📌 표시 항목:
//    - FPS: 현재 프레임 레이트 (NativeTick 기반, 0.25초 간격 갱신)
//    - Player ID: HellunaPlayerState::PlayerUniqueId (로비 로그인 GUID)
//    - Hero Class: HellunaPlayerState::SelectedHeroType (Lui / Luna / Liam / None)
//    - Ping: PlayerState::GetPingInMilliseconds()
//    - Net Role: HasAuthority() → Server(Listen) / Client
//
// 📌 위치: 화면 좌하단 (7시 방향) — Anchor (0.0, 1.0)
//
// 📌 토글 방법:
//    - F5 키 (Enhanced Input: IA_DebugHUD → HellunaHeroController::OnDebugHUDToggle)
//    - 콘솔 변수: Helluna.Debug.ShowHUD 1/0
//
// 📌 사용법:
//    1. WBP_HellunaDebugHUD 생성 (부모: UHellunaDebugHUDWidget)
//    2. BP Designer에서 Text_FPS, Text_PlayerId, Text_HeroClass 등 이름 지정
//    3. BP_HellunaHeroController에서 DebugHUDWidgetClass 지정
//    4. F5 키 또는 콘솔로 토글
//
// 📌 빌드 가드:
//    - #if UE_BUILD_SHIPPING → 위젯 생성 자체를 차단 (HeroController 측)
//    - NativeTick 내에서도 Shipping 체크 → 이중 안전
//
// 작성자: Gihyeon (Claude 보조)
// ════════════════════════════════════════════════════════════════════════════════

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "HellunaDebugHUDWidget.generated.h"

class UTextBlock;

UCLASS()
class HELLUNA_API UHellunaDebugHUDWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	// ════════════════════════════════════════════════════════════════
	// 외부 호출
	// ════════════════════════════════════════════════════════════════

	/** 외부에서 강제 갱신 */
	UFUNCTION(BlueprintCallable, Category = "Debug")
	void ForceRefreshInfo();

	/** 위젯 표시/숨김 토글 (F5 키 핸들러에서 호출) */
	UFUNCTION(BlueprintCallable, Category = "Debug")
	void ToggleVisibility();

	/** 현재 표시 중인지 */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Debug")
	bool IsDebugVisible() const;

protected:
	virtual void NativeConstruct() override;
	virtual void NativeTick(const FGeometry& MyGeometry, float InDeltaTime) override;

	// ════════════════════════════════════════════════════════════════
	// BindWidgetOptional (WBP에서 이름 매칭)
	// ════════════════════════════════════════════════════════════════

	/** FPS 표시 텍스트 */
	UPROPERTY(BlueprintReadWrite, meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> Text_FPS = nullptr;

	/** 플레이어 ID 표시 */
	UPROPERTY(BlueprintReadWrite, meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> Text_PlayerId = nullptr;

	/** 캐릭터 클래스 표시 */
	UPROPERTY(BlueprintReadWrite, meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> Text_HeroClass = nullptr;

	/** 핑 표시 */
	UPROPERTY(BlueprintReadWrite, meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> Text_Ping = nullptr;

	/** 서버/클라이언트 역할 표시 */
	UPROPERTY(BlueprintReadWrite, meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> Text_NetRole = nullptr;

private:
	// ════════════════════════════════════════════════════════════════
	// FPS 계산
	// ════════════════════════════════════════════════════════════════

	/** 델타 타임 누적 */
	float FPSAccumulator = 0.f;

	/** 프레임 카운트 */
	int32 FrameCount = 0;

	/** FPS 갱신 주기 (초) — 0.25초마다 갱신하여 깜빡임 방지 */
	float FPSUpdateInterval = 0.25f;

	/** 마지막 표시된 FPS 값 */
	float LastDisplayedFPS = 0.f;

	// ════════════════════════════════════════════════════════════════
	// 내부 갱신
	// ════════════════════════════════════════════════════════════════

	/** PlayerState에서 정보를 읽어 텍스트 갱신 */
	void UpdatePlayerInfo();

	/** FPS 계산 및 텍스트 갱신 */
	void UpdateFPS(float DeltaTime);

	/** EHellunaHeroType → 표시 문자열 변환 */
	static FString HeroTypeToDisplayString(EHellunaHeroType HeroType);
};
