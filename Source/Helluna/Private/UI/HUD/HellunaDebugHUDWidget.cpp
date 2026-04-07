// ════════════════════════════════════════════════════════════════════════════════
// HellunaDebugHUDWidget.cpp
// ════════════════════════════════════════════════════════════════════════════════

#include "UI/HUD/HellunaDebugHUDWidget.h"
#include "Components/TextBlock.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/PlayerState.h"
#include "Player/HellunaPlayerState.h"
#include "HellunaTypes.h"

static TAutoConsoleVariable<int32> CVarShowDebugHUD(
	TEXT("Helluna.Debug.ShowHUD"),
	1,
	TEXT("디버그 HUD 표시 (1=표시, 0=숨김). Shipping 빌드에서는 무시됨."),
	ECVF_Default
);

DEFINE_LOG_CATEGORY_STATIC(LogHellunaDebugHUD, Log, All);

void UHellunaDebugHUDWidget::NativeConstruct()
{
	Super::NativeConstruct();
	const bool bShouldShow = (CVarShowDebugHUD.GetValueOnGameThread() != 0);
	SetVisibility(bShouldShow ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Collapsed);
	UpdatePlayerInfo();
	UE_LOG(LogHellunaDebugHUD, Log, TEXT("DebugHUD NativeConstruct — Visibility=%s"),
		bShouldShow ? TEXT("Visible") : TEXT("Collapsed"));
}

void UHellunaDebugHUDWidget::NativeTick(const FGeometry& MyGeometry, float InDeltaTime)
{
	Super::NativeTick(MyGeometry, InDeltaTime);

#if UE_BUILD_SHIPPING
	if (GetVisibility() != ESlateVisibility::Collapsed)
	{
		SetVisibility(ESlateVisibility::Collapsed);
	}
	return;
#else
	const bool bShouldShow = (CVarShowDebugHUD.GetValueOnGameThread() != 0);
	const ESlateVisibility DesiredVis = bShouldShow ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Collapsed;
	if (GetVisibility() != DesiredVis)
	{
		SetVisibility(DesiredVis);
	}

	if (!bShouldShow)
	{
		return;
	}

	UpdateFPS(InDeltaTime);
#endif
}

void UHellunaDebugHUDWidget::UpdateFPS(float DeltaTime)
{
	FPSAccumulator += DeltaTime;
	FrameCount++;

	if (FPSAccumulator < FPSUpdateInterval)
	{
		return;
	}

	LastDisplayedFPS = (FPSAccumulator > 0.f) ? (static_cast<float>(FrameCount) / FPSAccumulator) : 0.f;
	FPSAccumulator = 0.f;
	FrameCount = 0;

	if (IsValid(Text_FPS))
	{
		const FString FPSStr = FString::Printf(TEXT("FPS: %.1f"), LastDisplayedFPS);
		Text_FPS->SetText(FText::FromString(FPSStr));

		FSlateColor Color;
		if (LastDisplayedFPS >= 60.f)
		{
			Color = FSlateColor(FLinearColor(0.29f, 0.87f, 0.50f, 1.0f));
		}
		else if (LastDisplayedFPS >= 30.f)
		{
			Color = FSlateColor(FLinearColor(0.98f, 0.75f, 0.14f, 1.0f));
		}
		else
		{
			Color = FSlateColor(FLinearColor(0.97f, 0.44f, 0.44f, 1.0f));
		}
		Text_FPS->SetColorAndOpacity(Color);
	}

	UpdatePlayerInfo();
}

void UHellunaDebugHUDWidget::UpdatePlayerInfo()
{
	APlayerController* PC = GetOwningPlayer();
	if (!IsValid(PC))
	{
		return;
	}

	AHellunaPlayerState* PS = PC->GetPlayerState<AHellunaPlayerState>();

	if (IsValid(Text_PlayerId))
	{
		if (IsValid(PS))
		{
			const FString& PlayerId = PS->GetPlayerUniqueId();
			const FString DisplayId = PlayerId.IsEmpty() ? TEXT("(미로그인)") : PlayerId;
			Text_PlayerId->SetText(FText::FromString(FString::Printf(TEXT("ID: %s"), *DisplayId)));
			Text_PlayerId->SetColorAndOpacity(FSlateColor(FLinearColor(0.38f, 0.65f, 0.98f, 1.0f)));
		}
		else
		{
			Text_PlayerId->SetText(FText::FromString(TEXT("ID: (대기중)")));
		}
	}

	if (IsValid(Text_HeroClass))
	{
		if (IsValid(PS))
		{
			const EHellunaHeroType HeroType = PS->GetSelectedHeroType();
			const FString HeroStr = HeroTypeToDisplayString(HeroType);
			Text_HeroClass->SetText(FText::FromString(FString::Printf(TEXT("Hero: %s"), *HeroStr)));
			Text_HeroClass->SetColorAndOpacity(FSlateColor(FLinearColor(0.75f, 0.52f, 0.99f, 1.0f)));
		}
		else
		{
			Text_HeroClass->SetText(FText::FromString(TEXT("Hero: (미선택)")));
		}
	}

	if (IsValid(Text_Ping))
	{
		float PingMs = 0.f;
		if (IsValid(PS))
		{
			PingMs = PS->GetPingInMilliseconds();
		}
		Text_Ping->SetText(FText::FromString(FString::Printf(TEXT("Ping: %.0fms"), PingMs)));
		Text_Ping->SetColorAndOpacity(FSlateColor(FLinearColor(0.98f, 0.75f, 0.14f, 1.0f)));
	}

	if (IsValid(Text_NetRole))
	{
		const bool bIsServer = PC->HasAuthority();
		const FString RoleStr = bIsServer ? TEXT("Role: Server (Listen)") : TEXT("Role: Client");
		Text_NetRole->SetText(FText::FromString(RoleStr));
		Text_NetRole->SetColorAndOpacity(FSlateColor(FLinearColor(0.9f, 0.9f, 0.9f, 0.85f)));
	}
}

void UHellunaDebugHUDWidget::ForceRefreshInfo()
{
	UpdatePlayerInfo();
}

void UHellunaDebugHUDWidget::ToggleVisibility()
{
	const int32 Current = CVarShowDebugHUD.GetValueOnGameThread();
	const int32 NewVal = (Current == 0) ? 1 : 0;
	CVarShowDebugHUD->Set(NewVal, ECVF_SetByConsole);
	UE_LOG(LogHellunaDebugHUD, Log, TEXT("DebugHUD 토글 — Helluna.Debug.ShowHUD = %d"), NewVal);
}

bool UHellunaDebugHUDWidget::IsDebugVisible() const
{
	return (CVarShowDebugHUD.GetValueOnGameThread() != 0);
}

FString UHellunaDebugHUDWidget::HeroTypeToDisplayString(EHellunaHeroType HeroType)
{
	switch (HeroType)
	{
	case EHellunaHeroType::Lui:  return TEXT("Lui (루이)");
	case EHellunaHeroType::Luna: return TEXT("Luna (루나)");
	case EHellunaHeroType::Liam: return TEXT("Liam (리암)");
	case EHellunaHeroType::None: return TEXT("None (미선택)");
	default:                     return TEXT("Unknown");
	}
}
