// ════════════════════════════════════════════════════════════════════════════════
// HellunaLobbyCharSelectWidget.h
// ════════════════════════════════════════════════════════════════════════════════
//
// 로비 캐릭터 선택 위젯 — 캐릭터 3명 버튼 + V2 3D 프리뷰
//
// 📌 레이아웃:
//    ┌───────────────────────────────────────┐
//    │         [PreviewImage_V2]             │
//    │   (3캐릭터 3D 프리뷰 RenderTarget)    │
//    ├───────────────────────────────────────┤
//    │  [LuiButton] [LunaButton] [LiamButton]│
//    ├───────────────────────────────────────┤
//    │           [MessageText]               │
//    └───────────────────────────────────────┘
//
// 📌 흐름:
//    1. LobbyController가 가용 캐릭터 정보와 함께 이 위젯 초기화
//    2. 버튼 클릭 → LobbyController->Server_SelectLobbyCharacter(Index)
//    3. 서버 응답 → OnSelectionResult() → 성공 시 FOnLobbyCharacterSelected broadcast
//
// 작성자: Gihyeon (Claude Code 보조)
// ════════════════════════════════════════════════════════════════════════════════

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "HellunaTypes.h"
#include "HellunaLobbyCharSelectWidget.generated.h"

class UButton;
class UTextBlock;
class AHellunaCharacterSelectSceneV2;
class AHellunaLobbyController;

// 캐릭터 선택 완료 델리게이트
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnLobbyCharacterSelected, EHellunaHeroType, SelectedHero);

UCLASS()
class HELLUNA_API UHellunaLobbyCharSelectWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	virtual void NativeOnInitialized() override;
	virtual void NativeDestruct() override;

	// ════════════════════════════════════════════════════════════════
	// 초기화
	// ════════════════════════════════════════════════════════════════

	/**
	 * 가용 캐릭터 설정 — 비활성화된 캐릭터 버튼을 Disable 처리
	 *
	 * @param InUsedCharacters  3개 bool (true=사용중=비활성화)
	 */
	UFUNCTION(BlueprintCallable, Category = "로비|캐릭터선택",
		meta = (DisplayName = "가용 캐릭터 설정"))
	void SetAvailableCharacters(const TArray<bool>& InUsedCharacters);

	/**
	 * V2 프리뷰 씬 캐시 (직접 뷰포트 모드 — RT 불필요)
	 *
	 * @param InScene  V2 씬 액터
	 */
	UFUNCTION(BlueprintCallable, Category = "로비|캐릭터선택",
		meta = (DisplayName = "V2 프리뷰 설정"))
	void SetupPreviewV2(AHellunaCharacterSelectSceneV2* InScene);

	/**
	 * 서버 응답 처리 — 성공 시 델리게이트 broadcast
	 *
	 * @param bSuccess  선택 성공 여부
	 * @param Message   결과 메시지
	 */
	UFUNCTION(BlueprintCallable, Category = "로비|캐릭터선택",
		meta = (DisplayName = "선택 결과 처리"))
	void OnSelectionResult(bool bSuccess, const FString& Message);

	// ════════════════════════════════════════════════════════════════
	// 델리게이트
	// ════════════════════════════════════════════════════════════════

	/** 캐릭터 선택 성공 시 broadcast */
	UPROPERTY(BlueprintAssignable, Category = "로비|캐릭터선택")
	FOnLobbyCharacterSelected OnCharacterSelected;

protected:
	// ════════════════════════════════════════════════════════════════
	// BindWidget — BP에서 연결
	// ════════════════════════════════════════════════════════════════

	/** 루이 선택 버튼 */
	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UButton> LuiButton;

	/** 루나 선택 버튼 */
	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UButton> LunaButton;

	/** 리암 선택 버튼 */
	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UButton> LiamButton;

	/** 상태 메시지 텍스트 — 없어도 동작 */
	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> MessageText;

	/** 루이 이름 라벨 — 투명 버튼 하단에 표시 */
	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> LuiNameText;

	/** 루나 이름 라벨 — 투명 버튼 하단에 표시 */
	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> LunaNameText;

	/** 리암 이름 라벨 — 투명 버튼 하단에 표시 */
	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> LiamNameText;

private:
	// ════════════════════════════════════════════════════════════════
	// 버튼 콜백
	// ════════════════════════════════════════════════════════════════

	UFUNCTION() void OnLuiClicked();
	UFUNCTION() void OnLunaClicked();
	UFUNCTION() void OnLiamClicked();

	UFUNCTION() void OnLuiHovered();
	UFUNCTION() void OnLunaHovered();
	UFUNCTION() void OnLiamHovered();

	UFUNCTION() void OnLuiUnhovered();
	UFUNCTION() void OnLunaUnhovered();
	UFUNCTION() void OnLiamUnhovered();

	/** 캐릭터 인덱스로 선택 요청 */
	void RequestCharacterSelection(int32 CharacterIndex);

	/** LobbyController 가져오기 */
	AHellunaLobbyController* GetLobbyController() const;

	// ════════════════════════════════════════════════════════════════
	// 내부 상태
	// ════════════════════════════════════════════════════════════════

	/** V2 씬 액터 참조 */
	UPROPERTY()
	TObjectPtr<AHellunaCharacterSelectSceneV2> CachedPreviewScene;

	/** 마지막으로 선택 요청한 캐릭터 인덱스 (서버 응답 대기용) */
	int32 PendingSelectionIndex = -1;
};
