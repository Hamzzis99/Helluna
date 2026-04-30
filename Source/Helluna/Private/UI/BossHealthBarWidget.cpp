// Capstone Project Helluna

#include "UI/BossHealthBarWidget.h"

#include "Character/HellunaEnemyCharacter.h"
#include "Character/HellunaEnemyCharacter_Boss.h"
#include "Character/EnemyComponent/HellunaHealthComponent.h"
#include "Components/ProgressBar.h"
#include "Components/TextBlock.h"
#include "Components/Border.h"
#include "GameFramework/Actor.h"
#include "Engine/World.h"

void UBossHealthBarWidget::NativeConstruct()
{
	Super::NativeConstruct();

	const float InitialPercent = GetBossHealthNormalized();
	DisplayMainPercent = InitialPercent;
	DisplayDelayedPercent = InitialPercent;
	PreviousTargetPercent = InitialPercent;

	// 보스 페이즈2 진입 델리게이트 바인딩
	if (AHellunaEnemyCharacter_Boss* Boss = Cast<AHellunaEnemyCharacter_Boss>(BossActor))
	{
		Boss->OnBossEnterPhase2.AddUniqueDynamic(this, &UBossHealthBarWidget::HandleBossEnterPhase2);

		// 이미 페이즈2 인 상태로 위젯 생성되는 경우 (late join 등) 즉시 페이즈2 시각 상태로
		if (IsBossInPhase2())
		{
			DisplayedPhase = 2;
			PhaseColorBlendAlpha = 1.f;
			Phase2WidthAlpha = 1.f;
		}
	}

	// Border padding 원본 캐시 — 확장 단계에서 Right 를 음수로 lerp 할 때 시작값
	if (Border_Frame)
	{
		CachedBorderPadding = Border_Frame->GetPadding();
		bCachedBorderPadding = true;
	}

	// 초기 색 적용
	if (ProgressBar_Health)
	{
		ProgressBar_Health->SetFillColorAndOpacity(
			DisplayedPhase == 2 ? Phase2MainColor : Phase1MainColor);
		ProgressBar_Health->SetPercent(DisplayMainPercent);
	}
	if (ProgressBar_Delayed)
	{
		ProgressBar_Delayed->SetFillColorAndOpacity(
			DisplayedPhase == 2 ? Phase2DelayedColor : Phase1DelayedColor);
		ProgressBar_Delayed->SetPercent(DisplayDelayedPercent);
	}

	UE_LOG(LogTemp, Warning,
		TEXT("[BossHealthBar] Construct — Boss=%s, InitialHP=%.2f, Phase=%d, BorderPadding=(L%.0f T%.0f R%.0f B%.0f)"),
		*GetNameSafe(BossActor), InitialPercent, DisplayedPhase,
		CachedBorderPadding.Left, CachedBorderPadding.Top,
		CachedBorderPadding.Right, CachedBorderPadding.Bottom);
}

void UBossHealthBarWidget::NativeTick(const FGeometry& MyGeometry, float InDeltaTime)
{
	Super::NativeTick(MyGeometry, InDeltaTime);

	const float Target = GetBossHealthNormalized();
	const double Now = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0;

	// ----- HP 표시값 갱신 -----
	if (bPhase2FillAnimationActive)
	{
		// 페이즈2 진입 — 두 단계 시퀀스
		const float Elapsed = static_cast<float>(Now - Phase2FillStartTime);
		const float FillDur = FMath::Max(Phase2FillDuration, 0.05f);
		const float ExpandDur = FMath::Max(Phase2WidthExpandDuration, 0.f);

		// 1단계: HP fill 0→Target (선형)
		const float FillRaw = FMath::Clamp(Elapsed / FillDur, 0.f, 1.f);
		DisplayMainPercent = FMath::Lerp(0.f, Target, FillRaw);
		DisplayDelayedPercent = DisplayMainPercent;

		// 2단계: 가로 확장 0→1 (fill 끝나야 시작, 선형)
		float ExpandRaw = 0.f;
		if (ExpandDur > KINDA_SMALL_NUMBER)
		{
			ExpandRaw = FMath::Clamp((Elapsed - FillDur) / ExpandDur, 0.f, 1.f);
		}
		else
		{
			ExpandRaw = (FillRaw >= 1.f) ? 1.f : 0.f;
		}
		Phase2WidthAlpha = ExpandRaw;

		// 시퀀스 진행 동안 정상 흐름의 감소/hold 트리거 차단
		PreviousTargetPercent = Target;
		LastDamageTime = Now;

		if (FillRaw >= 1.f && ExpandRaw >= 1.f)
		{
			bPhase2FillAnimationActive = false;
			Phase2WidthAlpha = 1.f;
		}
	}
	else
	{
		// 정상 흐름 — 피격 시 잔상 hold + 색 플래시 트리거
		if (Target < PreviousTargetPercent - KINDA_SMALL_NUMBER)
		{
			LastDamageTime = Now;
			FlashTimeRemaining = DamageFlashDuration;
		}
		PreviousTargetPercent = Target;

		DisplayMainPercent = FMath::FInterpTo(DisplayMainPercent, Target, InDeltaTime, MainInterpSpeed);

		const bool bHolding = (Now - LastDamageTime) < DelayedHoldDuration;
		if (!bHolding)
		{
			DisplayDelayedPercent = FMath::FInterpTo(DisplayDelayedPercent, Target,
				InDeltaTime, DelayedInterpSpeed);
		}
		DisplayDelayedPercent = FMath::Max(DisplayDelayedPercent, DisplayMainPercent);

		// 페이즈2 도달 후 fill 시퀀스 종료 시 확장 상태 유지
		if (DisplayedPhase == 2)
		{
			Phase2WidthAlpha = 1.f;
		}
	}

	// ----- 페이즈 색 블렌드 -----
	const float TargetBlendAlpha = (DisplayedPhase == 2) ? 1.f : 0.f;
	const float BlendSpeed = 1.f / FMath::Max(PhaseColorBlendDuration, 0.05f);
	PhaseColorBlendAlpha = FMath::FInterpTo(PhaseColorBlendAlpha, TargetBlendAlpha,
		InDeltaTime, BlendSpeed);

	const FLinearColor MainColor = FMath::Lerp(Phase1MainColor, Phase2MainColor, PhaseColorBlendAlpha);
	const FLinearColor DelayedColor = FMath::Lerp(Phase1DelayedColor, Phase2DelayedColor, PhaseColorBlendAlpha);

	// ----- 피격 색 플래시 (스케일 펄스 없음) -----
	float FlashAlpha = 0.f;
	if (FlashTimeRemaining > 0.f)
	{
		FlashTimeRemaining = FMath::Max(0.f, FlashTimeRemaining - InDeltaTime);
		FlashAlpha = FlashTimeRemaining / FMath::Max(DamageFlashDuration, 0.05f);
	}

	const float MixAmount = FlashAlpha * FlashColorMixMax;
	const FLinearColor MainColorFlashed = FMath::Lerp(MainColor, FlashHighlightColor, MixAmount);
	const FLinearColor DelayedColorFlashed = FMath::Lerp(DelayedColor, FlashHighlightColor, MixAmount * 0.6f);

	// ----- 바 색 + percent 적용 -----
	if (ProgressBar_Health)
	{
		ProgressBar_Health->SetPercent(DisplayMainPercent);
		ProgressBar_Health->SetFillColorAndOpacity(MainColorFlashed);
	}
	if (ProgressBar_Delayed)
	{
		ProgressBar_Delayed->SetPercent(DisplayDelayedPercent);
		ProgressBar_Delayed->SetFillColorAndOpacity(DelayedColorFlashed);
	}

	// ----- Border padding.Right 음수 lerp = 바만 우측 확장 -----
	// 핵심 트릭: SizeBox/Border layout 폭은 그대로(= Border 시각 프레임 크기 유지),
	//   Border padding.Right 를 14 → 14 - Add 로 lerp 하면 Border content area 가
	//   Border 우측 밖으로 확장됨. VBox/HealthOverlay/바는 HAlign=Fill 로 자동으로 따라감.
	const float WidthAlphaClamped = FMath::Clamp(Phase2WidthAlpha, 0.f, 1.f);
	const float CurrentExtensionPx = FMath::Max(Phase2BarWidthAddDesignPx, 0.f) * WidthAlphaClamped;

	if (bCachedBorderPadding && Border_Frame)
	{
		FMargin NewPadding = CachedBorderPadding;
		NewPadding.Right = CachedBorderPadding.Right - CurrentExtensionPx;
		Border_Frame->SetPadding(NewPadding);
	}

	// ----- 보스 이름 위치 보정 -----
	// Text_BossName 의 VerticalBoxSlot 이 HAlign_Center 라 VBox 폭이 확장되면 텍스트가
	//   우측으로 재정렬됨 (확장량의 절반만큼 우로 이동). 같은 양만큼 좌측으로 RenderTranslation
	//   해주면 시각적으로 원래 위치 고정.
	if (Text_BossName)
	{
		Text_BossName->SetRenderTranslation(FVector2D(-CurrentExtensionPx * 0.5f, 0.f));
	}
}

void UBossHealthBarWidget::HandleBossEnterPhase2()
{
	DisplayedPhase = 2;

	// HP 즉시 풀 회복되지만 화면엔 0 부터 천천히 차오르도록 강제
	DisplayMainPercent = 0.f;
	DisplayDelayedPercent = 0.f;
	PreviousTargetPercent = 0.f;
	bPhase2FillAnimationActive = true;
	Phase2FillStartTime = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0;
	Phase2WidthAlpha = 0.f;

	// 진입 순간 강한 색 플래시 한 번
	FlashTimeRemaining = DamageFlashDuration * 2.f;

	UE_LOG(LogTemp, Warning,
		TEXT("[BossHealthBar] Phase2 triggered — fill=%.2fs → expand=%.2fs (+%.0fpx)"),
		Phase2FillDuration, Phase2WidthExpandDuration, Phase2BarWidthAddDesignPx);
}

float UBossHealthBarWidget::GetBossHealthNormalized() const
{
	if (!IsValid(BossActor))
	{
		return 0.f;
	}
	if (UHellunaHealthComponent* HC = BossActor->FindComponentByClass<UHellunaHealthComponent>())
	{
		return HC->GetHealthNormalized();
	}
	return 0.f;
}

bool UBossHealthBarWidget::IsBossInPhase2() const
{
	if (const AHellunaEnemyCharacter_Boss* Boss = Cast<AHellunaEnemyCharacter_Boss>(BossActor))
	{
		return Boss->bInPhase2;
	}
	return false;
}
