// ════════════════════════════════════════════════════════════════════════════════
// HellunaLoadingHUDWidget.h
// ════════════════════════════════════════════════════════════════════════════════
//
// 로딩 배리어 HUD 위젯 C++ 베이스. 2-Phase (Reedme/loading/08-final-design-decisions §2).
// BP에서 WBP_SelfLoadingHUD로 상속해 UMG 애니메이션/서브타이틀을 바인딩한다.
//
// 📌 작성자: Gihyeon (Loading Barrier Stage F/G)
// ════════════════════════════════════════════════════════════════════════════════

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "HellunaLoadingHUDWidget.generated.h"

class UHellunaPartySlotWidget;

UCLASS(Abstract)
class HELLUNA_API UHellunaLoadingHUDWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	/** 초기 세팅: 기대 파티원 + 로컬 PlayerId. Phase 1에서 슬롯을 눈속임으로 채운다. */
	UFUNCTION(BlueprintCallable, Category = "Loading Barrier|HUD")
	void InitializeHUD(const TArray<FString>& ExpectedIds, const FString& InLocalPlayerId, int32 PartyId);

	/** 내 로딩 진행률(0~1) 갱신. */
	UFUNCTION(BlueprintCallable, Category = "Loading Barrier|HUD")
	void UpdateMyProgress(float Normalized01);

	/** Phase 1 → Phase 2 전환 (내가 Ready 된 직후). */
	UFUNCTION(BlueprintCallable, Category = "Loading Barrier|HUD")
	void TransitionToPhase2();

	/** 특정 파티원 슬롯 상태 갱신 (서버 브로드캐스트에서 호출). */
	UFUNCTION(BlueprintCallable, Category = "Loading Barrier|HUD")
	void UpdateSlot(const FString& PlayerId, bool bReady);

	/** 페이드 아웃 애니메이션 재생 — BP에서 WidgetAnimation을 돌려라. */
	UFUNCTION(BlueprintImplementableEvent, Category = "Loading Barrier|HUD")
	void PlayFadeOutAnimation();

	/** Phase 전환 애니메이션 — BP 구현. */
	UFUNCTION(BlueprintImplementableEvent, Category = "Loading Barrier|HUD")
	void PlayTransitionAnimation();

	/** 슬롯 생성 — BP 구현 (VerticalBox에 자식 추가). 실패 시 nullptr 반환. */
	UFUNCTION(BlueprintImplementableEvent, BlueprintCallable, Category = "Loading Barrier|HUD")
	UHellunaPartySlotWidget* SpawnPartySlot(const FString& PlayerId, bool bIsMe);

protected:
	UPROPERTY(BlueprintReadOnly, Category = "Loading Barrier|HUD")
	TArray<FString> ExpectedPlayerIds;

	UPROPERTY(BlueprintReadOnly, Category = "Loading Barrier|HUD")
	FString LocalPlayerId;

	UPROPERTY(BlueprintReadOnly, Category = "Loading Barrier|HUD")
	int32 PartyId = 0;

	UPROPERTY(BlueprintReadOnly, Category = "Loading Barrier|HUD")
	bool bInPhase2 = false;

	UPROPERTY(BlueprintReadOnly, Category = "Loading Barrier|HUD")
	float MyProgress = 0.f;

	UPROPERTY(BlueprintReadOnly, Category = "Loading Barrier|HUD")
	TMap<FString, TObjectPtr<UHellunaPartySlotWidget>> PlayerIdToSlot;
};
