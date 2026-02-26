// File: Source/Helluna/Public/GameMode/Widget/HellunaGameResultWidget.h
// ════════════════════════════════════════════════════════════════════════════════
// Phase 7: 게임 결과 UI 위젯
// ════════════════════════════════════════════════════════════════════════════════
//
// 게임 종료 후 표시되는 결과 화면
// - 생존 여부 표시
// - 보존된 아이템 목록 (생존자만)
// - "로비로 돌아가기" 버튼
//
// BP에서 WBP_HellunaGameResultWidget으로 상속하여 UI 구성
//
// ════════════════════════════════════════════════════════════════════════════════

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "Player/Inv_PlayerController.h"  // FInv_SavedItemData
#include "HellunaGameResultWidget.generated.h"

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
		meta = (DisplayName = "결과 데이터 설정"))
	void SetResultData(const TArray<FInv_SavedItemData>& InResultItems, bool bInSurvived, const FString& InReason);

	/**
	 * "로비로 돌아가기" 버튼 클릭 시 호출
	 * BP에서 버튼 OnClicked에 바인딩
	 */
	UFUNCTION(BlueprintCallable, Category = "GameResult(게임결과)",
		meta = (DisplayName = "로비로 돌아가기"))
	void ReturnToLobby();

	/** 로비 서버 URL (C++에서 설정, BP에서 읽기) */
	UPROPERTY(BlueprintReadOnly, Category = "GameResult(게임결과)",
		meta = (DisplayName = "로비 서버 URL"))
	FString LobbyURL;

protected:
	/**
	 * BP에서 구현: 결과 데이터를 UI에 반영
	 * SetResultData() 호출 후 자동 실행
	 */
	UFUNCTION(BlueprintImplementableEvent, Category = "GameResult(게임결과)",
		meta = (DisplayName = "결과 UI 갱신"))
	void OnResultDataSet();

	/** 보존된 아이템 목록 */
	UPROPERTY(BlueprintReadOnly, Category = "GameResult(게임결과)",
		meta = (DisplayName = "결과 아이템"))
	TArray<FInv_SavedItemData> ResultItems;

	/** 생존 여부 */
	UPROPERTY(BlueprintReadOnly, Category = "GameResult(게임결과)",
		meta = (DisplayName = "생존 여부"))
	bool bSurvived = false;

	/** 종료 사유 문자열 */
	UPROPERTY(BlueprintReadOnly, Category = "GameResult(게임결과)",
		meta = (DisplayName = "종료 사유"))
	FString EndReason;
};
