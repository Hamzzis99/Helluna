// File: Source/Helluna/Public/Lobby/Widget/HellunaPartyMemberEntry.h
// ════════════════════════════════════════════════════════════════════════════════
//
// [Phase 12g] 파티 멤버 엔트리 위젯
//
// 레이아웃:
//   ┌───────────────────────────────────────────┐
//   │  [Crown]  PlayerName  | HeroType | Ready  [Kick] │
//   └───────────────────────────────────────────┘
//
// ════════════════════════════════════════════════════════════════════════════════

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "Lobby/Party/HellunaPartyTypes.h"
#include "HellunaPartyMemberEntry.generated.h"

class UTextBlock;
class UImage;
class UButton;

UCLASS()
class HELLUNA_API UHellunaPartyMemberEntry : public UUserWidget
{
	GENERATED_BODY()

public:
	/**
	 * 멤버 정보 설정
	 *
	 * @param MemberInfo      멤버 데이터
	 * @param bIsLocalLeader  로컬 플레이어가 리더인가 (Kick 버튼 표시용)
	 * @param bIsLocalPlayer  이 엔트리가 로컬 플레이어인가 (Kick 숨김용)
	 */
	UFUNCTION(BlueprintCallable, Category = "Party (파티)",
		meta = (DisplayName = "Set Member Info (멤버 정보 설정)"))
	void SetMemberInfo(const FHellunaPartyMemberInfo& MemberInfo, bool bIsLocalLeader, bool bIsLocalPlayer);

	/** 이 엔트리의 PlayerId */
	FString GetPlayerId() const { return CachedPlayerId; }

	/** Kick 버튼 클릭 시 호출되는 델리게이트 */
	DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnKickRequested, const FString&, PlayerId);

	UPROPERTY(BlueprintAssignable, Category = "Party (파티)")
	FOnKickRequested OnKickRequested;

protected:
	// ════════════════════════════════════════════════════════════════
	// BindWidget — BP에서 연결
	// ════════════════════════════════════════════════════════════════

	/** 플레이어 이름 */
	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UTextBlock> Text_PlayerName;

	/** 영웅 타입 */
	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UTextBlock> Text_HeroType;

	/** 준비 상태 */
	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UTextBlock> Text_ReadyStatus;

	/** 리더 왕관 아이콘 */
	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UImage> Image_LeaderCrown;

	/** 강퇴 버튼 (리더만 보임) */
	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UButton> Button_Kick;

private:
	UFUNCTION()
	void OnKickClicked();

	FString CachedPlayerId;
};
