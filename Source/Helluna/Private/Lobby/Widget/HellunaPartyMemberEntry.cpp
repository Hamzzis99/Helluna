// File: Source/Helluna/Private/Lobby/Widget/HellunaPartyMemberEntry.cpp
// ════════════════════════════════════════════════════════════════════════════════
//
// [Phase 12g] 파티 멤버 엔트리 위젯 구현
//
// ════════════════════════════════════════════════════════════════════════════════

#include "Lobby/Widget/HellunaPartyMemberEntry.h"
#include "Components/TextBlock.h"
#include "Components/Image.h"
#include "Components/Button.h"

void UHellunaPartyMemberEntry::SetMemberInfo(
	const FHellunaPartyMemberInfo& MemberInfo,
	bool bIsLocalLeader,
	bool bIsLocalPlayer)
{
	CachedPlayerId = MemberInfo.PlayerId;

	// 플레이어 이름
	if (Text_PlayerName)
	{
		FString NameStr = MemberInfo.DisplayName;
		if (MemberInfo.Role == EHellunaPartyRole::Leader)
		{
			NameStr += TEXT(" (Leader)");
		}
		Text_PlayerName->SetText(FText::FromString(NameStr));
	}

	// 영웅 타입
	if (Text_HeroType)
	{
		FString HeroStr;
		switch (MemberInfo.SelectedHeroType)
		{
		case 0: HeroStr = TEXT("Warrior"); break;
		case 1: HeroStr = TEXT("Mage"); break;
		case 2: HeroStr = TEXT("Ranger"); break;
		default: HeroStr = TEXT("None"); break;
		}
		Text_HeroType->SetText(FText::FromString(HeroStr));
	}

	// 준비 상태
	if (Text_ReadyStatus)
	{
		if (MemberInfo.Role == EHellunaPartyRole::Leader)
		{
			Text_ReadyStatus->SetText(FText::FromString(TEXT("Leader")));
		}
		else
		{
			Text_ReadyStatus->SetText(FText::FromString(MemberInfo.bIsReady ? TEXT("Ready") : TEXT("---")));
		}
	}

	// 리더 왕관 아이콘
	if (Image_LeaderCrown)
	{
		Image_LeaderCrown->SetVisibility(
			MemberInfo.Role == EHellunaPartyRole::Leader
			? ESlateVisibility::Visible
			: ESlateVisibility::Collapsed);
	}

	// 강퇴 버튼 (리더가 다른 멤버를 볼 때만 표시)
	if (Button_Kick)
	{
		const bool bShowKick = bIsLocalLeader && !bIsLocalPlayer;
		Button_Kick->SetVisibility(bShowKick ? ESlateVisibility::Visible : ESlateVisibility::Collapsed);

		if (bShowKick)
		{
			Button_Kick->OnClicked.AddUniqueDynamic(this, &ThisClass::OnKickClicked);
		}
	}
}

void UHellunaPartyMemberEntry::OnKickClicked()
{
	if (!CachedPlayerId.IsEmpty())
	{
		OnKickRequested.Broadcast(CachedPlayerId);
	}
}
