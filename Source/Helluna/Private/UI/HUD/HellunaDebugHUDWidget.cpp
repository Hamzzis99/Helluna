// ════════════════════════════════════════════════════════════════════════════════
// HellunaDebugHUDWidget.cpp
// ════════════════════════════════════════════════════════════════════════════════

#include "UI/HUD/HellunaDebugHUDWidget.h"
#include "Components/TextBlock.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/PlayerState.h"
#include "Player/HellunaPlayerState.h"
#include "HellunaTypes.h"

// ═══════════════ 콘솔 변수 ═══════════════
static TAutoConsoleVariable<int32> CVarShowDebugHUD(
	TEXT("Helluna.Debug.ShowHUD"),
	0,
	TEXT("디버그 HUD 표시 (1=표시, 0=숨김). Shipping 빌드에서는 무시됨."),
	ECVF_Default
);

DEFINE_LOG_CATEGORY_STATIC(LogHellunaDebugHUD, Log, All);

// ════════════════════════════════════════════════════════════════════════════════
// NativeConstruct
// ════════════════════════════════════════════════════════════════════════════════

void UHellunaDebugHUDWidget::NativeConstruct()
{
	Super::NativeConstruct();

	// 초기 상태: 콘솔 변수에 따라 표시/숨김
	const bool bShouldShow = (CVarShowDebugHUD.GetValueOnGameThread() != 0);
	SetVisibility(bShouldShow ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Collapsed);

	// 초기 정보 갱신
	UpdatePlayerInfo();

	UE_LOG(LogHellunaDebugHUD, Log, TEXT("DebugHUD NativeConstruct — 초기 Visibility=%s"),
		bShouldShow ? TEXT("Visible") : TEXT("Collapsed"));
}

// ════════════════════════════════════════════════════════════════════════════════
// NativeTick
// ════════════════════════════════════════════════════════════════════════════════

void UHellunaDebugHUDWidget::NativeTick(const FGeometry& MyGeometry, float InDeltaTime)
{
	Super::NativeTick(MyGeometry, InDeltaTime);

#if UE_BUILD_SHIPPING
	// Shipping 빌드에서는 항상 숨김 (이중 안전)
	if (GetVisibility() != ESlateVisibility::Collapsed)
	{
		SetVisibility(ESlateVisibility::Collapsed);
	}
	return;
#else
	// 콘솔 변수 동기화
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

	// FPS 갱신 (갱신 주기 내에서 플레이어 정보도 함께 갱신)
	UpdateFPS(InDeltaTime);
#endif
}

// ════════════════════════════════════════════════════════════════════════════════
// FPS 계산
// ════════════════════════════════════════════════════════════════════════════════

void UHellunaDebugHUDWidget::UpdateFPS(float DeltaTime)
{
	FPSAccumulator += DeltaTime;
	FrameCount++;

	if (FPSAccumulator < FPSUpdateInterval)
	{
		return;
	}

	// FPS 계산
	LastDisplayedFPS = (FPSAccumulator > 0.f) ? (static_cast<float>(FrameCount) / FPSAccumulator) : 0.f;
	FPSAccumulator = 0.f;
	FrameCount = 0;

	// FPS 텍스트 갱신
	if (IsValid(Text_FPS))
	{
		const FString FPSStr = FString::Printf(TEXT("FPS: %.1f"), LastDisplayedFPS);
		Text_FPS->SetText(FText::FromString(FPSStr));

		// FPS에 따라 색상 변경 (60+: 초록, 30~59: 노랑, <30: 빨강)
		FSlateColor Color;
		if (LastDisplayedFPS >= 60.f)
		{
			Color = FSlateColor(FLinearColor::Green);
		}
		else if (LastDisplayedFPS >= 30.f)
		{
			Color = FSlateColor(FLinearColor::Yellow);
		}
		else
		{
			Color = FSlateColor(FLinearColor::Red);
		}
		Text_FPS->SetColorAndOpacity(Color);
	}

	// 같은 주기에 플레이어 정보도 갱신
	UpdatePlayerInfo();
}

// ════════════════════════════════════════════════════════════════════════════════
// 플레이어 정보 갱신
// ════════════════════════════════════════════════════════════════════════════════

void UHellunaDebugHUDWidget::UpdatePlayerInfo()
{
	APlayerController* PC = GetOwningPlayer();
	if (!IsValid(PC))
	{
		return;
	}

	AHellunaPlayerState* PS = PC->GetPlayerState<AHellunaPlayerState>();

	// ── Player ID ──
	if (IsValid(Text_PlayerId))
	{
		if (IsValid(PS))
		{
			const FString& PlayerId = PS->GetPlayerUniqueId();
			const FString DisplayId = PlayerId.IsEmpty() ? TEXT("(미로그인)") : PlayerId;
			Text_PlayerId->SetText(FText::FromString(FString::Printf(TEXT("ID: %s"), *DisplayId)));
		}
		else
		{
			Text_PlayerId->SetText(FText::FromString(TEXT("ID: (대기중)")));
		}
	}

	// ── Hero Class ──
	if (IsValid(Text_HeroClass))
	{
		if (IsValid(PS))
		{
			const EHellunaHeroType HeroType = PS->GetSelectedHeroType();
			const FString HeroStr = HeroTypeToDisplayString(HeroType);
			Text_HeroClass->SetText(FText::FromString(FString::Printf(TEXT("Hero: %s"), *HeroStr)));
		}
		else
		{
			Text_HeroClass->SetText(FText::FromString(TEXT("Hero: (미선택)")));
		}
	}

	// ── Ping ──
	if (IsValid(Text_Ping))
	{
		float PingMs = 0.f;
		if (IsValid(PS))
		{
			// UE5: APlayerState::GetPingInMilliseconds() 사용
			PingMs = PS->GetPingInMilliseconds();
		}
		Text_Ping->SetText(FText::FromString(FString::Printf(TEXT("Ping: %.0fms"), PingMs)));
	}

	// ── Net Role ──
	if (IsValid(Text_NetRole))
	{
		const bool bIsServer = PC->HasAuthority();
		const FString RoleStr = bIsServer ? TEXT("Server (Listen)") : TEXT("Client");
		Text_NetRole->SetText(FText::FromString(FString::Printf(TEXT("Role: %s"), *RoleStr)));
	}
}

// ════════════════════════════════════════════════════════════════════════════════
// 외부 호출
// ════════════════════════════════════════════════════════════════════════════════

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

// ════════════════════════════════════════════════════════════════════════════════
// HeroType → 문자열 변환
// ════════════════════════════════════════════════════════════════════════════════

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
