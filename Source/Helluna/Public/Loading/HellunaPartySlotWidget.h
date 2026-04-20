// ════════════════════════════════════════════════════════════════════════════════
// HellunaPartySlotWidget.h
// ════════════════════════════════════════════════════════════════════════════════
//
// 파티 슬롯 위젯 C++ 베이스 (Reedme/loading/08-final-design-decisions.md §핵심결정 7).
// BP에서 WBP_PartySlot로 상속해 이미지/텍스트 위젯을 바인딩한다.
//
// 📌 작성자: Gihyeon (Loading Barrier Stage F)
// ════════════════════════════════════════════════════════════════════════════════

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "HellunaPartySlotWidget.generated.h"

UCLASS(Abstract)
class HELLUNA_API UHellunaPartySlotWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	/** 슬롯 초기 세팅. PlayerId + bIsMe. */
	UFUNCTION(BlueprintCallable, Category = "Loading Barrier|PartySlot")
	void InitSlot(const FString& InPlayerId, bool bIsMe);

	/** Ready 상태 업데이트. */
	UFUNCTION(BlueprintCallable, Category = "Loading Barrier|PartySlot")
	void SetReady(bool bInReady);

	/** BP에서 재정의 가능한 시각 갱신 이벤트. */
	UFUNCTION(BlueprintImplementableEvent, Category = "Loading Barrier|PartySlot")
	void OnSlotVisualUpdated(const FString& InPlayerId, bool bInReady, bool bInIsMe);

	UFUNCTION(BlueprintPure, Category = "Loading Barrier|PartySlot")
	const FString& GetPlayerId() const { return PlayerId; }

	UFUNCTION(BlueprintPure, Category = "Loading Barrier|PartySlot")
	bool IsReady() const { return bReady; }

	UFUNCTION(BlueprintPure, Category = "Loading Barrier|PartySlot")
	bool IsMe() const { return bIsMe; }

protected:
	UPROPERTY(BlueprintReadOnly, Category = "Loading Barrier|PartySlot")
	FString PlayerId;

	UPROPERTY(BlueprintReadOnly, Category = "Loading Barrier|PartySlot")
	bool bIsMe = false;

	UPROPERTY(BlueprintReadOnly, Category = "Loading Barrier|PartySlot")
	bool bReady = false;
};
