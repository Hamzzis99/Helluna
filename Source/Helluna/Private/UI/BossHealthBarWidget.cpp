// Capstone Project Helluna

#include "UI/BossHealthBarWidget.h"

#include "Character/HellunaEnemyCharacter.h"
#include "Character/HellunaEnemyCharacter_Boss.h"
#include "Character/EnemyComponent/HellunaHealthComponent.h"
#include "Components/ProgressBar.h"
#include "Components/TextBlock.h"
#include "GameFramework/Actor.h"
#include "Engine/World.h"

void UBossHealthBarWidget::NativeConstruct()
{
	Super::NativeConstruct();

	const float InitialPercent = GetBossHealthNormalized();
	DisplayMainPercent = InitialPercent;
	DisplayDelayedPercent = InitialPercent;
	PreviousTargetPercent = InitialPercent;

	// 보스 2페이즈 델리게이트 바인딩 (보스 서브클래스 전용)
	if (AHellunaEnemyCharacter_Boss* Boss = Cast<AHellunaEnemyCharacter_Boss>(BossActor))
	{
		Boss->OnBossEnterPhase2.AddUniqueDynamic(this, &UBossHealthBarWidget::HandleBossEnterPhase2);

		// 이미 2페이즈 상태로 생성되는 경우 (late join 등) 즉시 반영
		if (IsBossInPhase2())
		{
			DisplayedPhase = 2;
			PhaseColorBlendAlpha = 1.f;
		}
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
		TEXT("[BossHealthBarV1] Construct — Boss=%s, InitialPercent=%.2f, Phase=%d"),
		*GetNameSafe(BossActor), InitialPercent, DisplayedPhase);
}

void UBossHealthBarWidget::NativeTick(const FGeometry& MyGeometry, float InDeltaTime)
{
	Super::NativeTick(MyGeometry, InDeltaTime);

	const float Target = GetBossHealthNormalized();

	// HP 감소 감지 — 잔상 바 hold 타이밍 + 플래시 트리거
	if (Target < PreviousTargetPercent - KINDA_SMALL_NUMBER)
	{
		LastDamageTime = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0;
		FlashTimeRemaining = DamageFlashDuration;
		FlashElapsed = 0.f;
	}
	PreviousTargetPercent = Target;

	// 주 바: 즉시 반응
	DisplayMainPercent = FMath::FInterpTo(DisplayMainPercent, Target, InDeltaTime, MainInterpSpeed);

	// 잔상 바: 피격 직후 잠깐 홀드 후 천천히 따라감
	const double Now = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0;
	const bool bHolding = (Now - LastDamageTime) < DelayedHoldDuration;
	if (!bHolding)
	{
		DisplayDelayedPercent = FMath::FInterpTo(DisplayDelayedPercent, Target,
			InDeltaTime, DelayedInterpSpeed);
	}
	DisplayDelayedPercent = FMath::Max(DisplayDelayedPercent, DisplayMainPercent);

	// 페이즈 색 전환 보간
	const float TargetBlendAlpha = (DisplayedPhase == 2) ? 1.f : 0.f;
	const float BlendSpeed = 1.f / FMath::Max(PhaseColorBlendDuration, 0.05f);
	PhaseColorBlendAlpha = FMath::FInterpTo(PhaseColorBlendAlpha, TargetBlendAlpha,
		InDeltaTime, BlendSpeed);

	const FLinearColor MainColor = FMath::Lerp(Phase1MainColor, Phase2MainColor, PhaseColorBlendAlpha);
	const FLinearColor DelayedColor = FMath::Lerp(Phase1DelayedColor, Phase2DelayedColor, PhaseColorBlendAlpha);

	// [HPFlashV1] 피격 플래시 — 남은 시간 감쇠 + 짧은 한 번의 사인 펄스
	float FlashAlpha = 0.f;
	float FlashScaleMul = 1.f;
	if (FlashTimeRemaining > 0.f)
	{
		FlashTimeRemaining = FMath::Max(0.f, FlashTimeRemaining - InDeltaTime);
		FlashElapsed += InDeltaTime;

		// Flash 강도(0~1, linear decay — 피크 → 감쇠)
		const float NormT = FlashTimeRemaining / FMath::Max(DamageFlashDuration, 0.05f);
		FlashAlpha = NormT; // 0(끝) ~ 1(시작)

		// 짧은 단일 펄스: sin(pi * t/duration) 형태 — 시작·끝 0, 중간 피크 1
		const float PulseT = FlashElapsed / FMath::Max(DamageFlashDuration, 0.05f);
		const float Pulse01 = FMath::Sin(FMath::Clamp(PulseT, 0.f, 1.f) * PI);
		FlashScaleMul = 1.f + Pulse01 * FlashScaleAmplitude;
	}

	// Flash 색 섞임 (Flash 진행도만큼 하이라이트 색 mix)
	const float MixAmount = FlashAlpha * FlashColorMixMax;
	const FLinearColor MainColorFlashed = FMath::Lerp(MainColor, FlashHighlightColor, MixAmount);
	const FLinearColor DelayedColorFlashed = FMath::Lerp(DelayedColor, FlashHighlightColor, MixAmount * 0.6f);

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

	// 위젯 전체 RenderScale — 피격 순간 살짝 커졌다 돌아옴 (평시엔 1.0)
	SetRenderScale(FVector2D(FlashScaleMul, FlashScaleMul));
}

void UBossHealthBarWidget::HandleBossEnterPhase2()
{
	DisplayedPhase = 2;
	// 2페이즈 진입 순간은 HP가 증가(0→풀)하므로 Tick의 감소 감지가 트리거되지 않음.
	// 수동으로 플래시를 강하게 한 번 줘서 전환 순간을 강조.
	FlashTimeRemaining = DamageFlashDuration * 2.f;
	FlashElapsed = 0.f;
	UE_LOG(LogTemp, Warning,
		TEXT("[BossHealthBarV1] Phase 2 triggered — color blend + flash"));
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
