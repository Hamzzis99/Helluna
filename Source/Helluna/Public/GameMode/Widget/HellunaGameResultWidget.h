// File: Source/Helluna/Public/GameMode/Widget/HellunaGameResultWidget.h
// ════════════════════════════════════════════════════════════════════════════════
// Phase 7: 게임 결과 UI 위젯 (BindWidget 패턴)
// ════════════════════════════════════════════════════════════════════════════════
//
// 게임 종료 후 표시되는 결과 화면
// - 생존 여부 표시
// - 종료 사유 표시
// - 보존된 아이템 목록 (생존자만)
// - "로비로 돌아가기" 버튼
//
// WBP에서 동일한 이름의 위젯을 배치해야 함 (BindWidget)
//
// ════════════════════════════════════════════════════════════════════════════════

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "Player/Inv_PlayerController.h"  // FInv_SavedItemData
#include "HellunaGameResultWidget.generated.h"

class UTextBlock;
class UButton;
class UVerticalBox;

UCLASS(Blueprintable, BlueprintType)
class HELLUNA_API UHellunaGameResultWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	/**
	 * 결과 데이터 설정 (서버 → 클라이언트 RPC에서 호출)
	 *
	 * @param InResultItems  보존된 아이템 목록 (사망자는 빈 배열)
	 * @param bInSurvived    생존 여부
	 * @param InReason       게임 종료 사유 문자열
	 */
	UFUNCTION(BlueprintCallable, Category = "GameResult(게임결과)",
		meta = (DisplayName = "Set Result Data (결과 데이터 설정)"))
	void SetResultData(const TArray<FInv_SavedItemData>& InResultItems, bool bInSurvived, const FString& InReason);

	/**
	 * "로비로 돌아가기" 버튼 클릭 시 호출
	 */
	UFUNCTION(BlueprintCallable, Category = "GameResult(게임결과)",
		meta = (DisplayName = "Return To Lobby (로비로 돌아가기)"))
	void ReturnToLobby();

	/** 로비 서버 URL (C++에서 설정, BP에서 읽기) */
	UPROPERTY(BlueprintReadOnly, Category = "GameResult(게임결과)",
		meta = (DisplayName = "Lobby Server URL (로비 서버 URL)"))
	FString LobbyURL;

protected:
	virtual void NativeConstruct() override;

	// ──────────────────────────────────────────────
	// BindWidget — WBP에서 동일 이름으로 위젯 배치 필수
	// ──────────────────────────────────────────────

	/** 생존/사망 상태 텍스트 (예: "탈출 성공!" / "사망...") */
	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UTextBlock> Text_SurvivalStatus;

	/** 종료 사유 텍스트 (예: "Escaped" / "AllDead") */
	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UTextBlock> Text_EndReason;

	/** 보존된 아이템 개수 텍스트 (예: "보존 아이템: 3개") */
	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UTextBlock> Text_ItemCount;

	/** 로비 복귀 버튼 */
	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UButton> Btn_ReturnToLobby;

	/** 아이템 목록 표시 영역 (선택 — 없어도 됨) */
	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UVerticalBox> VBox_ItemList;

	// ──────────────────────────────────────────────
	// 데이터
	// ──────────────────────────────────────────────

	/** 보존된 아이템 목록 */
	UPROPERTY(BlueprintReadOnly, Category = "GameResult(게임결과)",
		meta = (DisplayName = "Result Items (결과 아이템)"))
	TArray<FInv_SavedItemData> ResultItems;

	/** 생존 여부 */
	UPROPERTY(BlueprintReadOnly, Category = "GameResult(게임결과)",
		meta = (DisplayName = "Survived (생존 여부)"))
	bool bSurvived = false;

	/** 종료 사유 문자열 */
	UPROPERTY(BlueprintReadOnly, Category = "GameResult(게임결과)",
		meta = (DisplayName = "End Reason (종료 사유)"))
	FString EndReason;

private:
	/** 버튼 클릭 콜백 */
	UFUNCTION()
	void OnReturnToLobbyClicked();

	/** UI 텍스트 갱신 */
	void UpdateUI();
};
