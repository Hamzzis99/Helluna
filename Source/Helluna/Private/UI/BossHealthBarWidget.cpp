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

namespace
{
	/** [Phase2ExtraBarV1] Boss 의 OldMax, NewMax, CurrentHP 를 한꺼번에 읽어 main/extra 바 percent 를 계산.
	 *  - bHasExtra=true 이면 추가 바 활성. main 은 OldMax 까지만 차고, extra 는 OldMax → NewMax 구간.
	 *  - bHasExtra=false (페이즈2 fill 시작 전) 이면 기존처럼 main 만 사용. */
	void ComputeBossHpPercents(AActor* BossActor, float& OutMainPercent, float& OutExtraPercent, bool& bHasExtra)
	{
		OutMainPercent = 0.f;
		OutExtraPercent = 0.f;
		bHasExtra = false;

		if (!IsValid(BossActor)) return;
		UHellunaHealthComponent* HC = BossActor->FindComponentByClass<UHellunaHealthComponent>();
		if (!HC) return;

		const float CurHP = HC->GetHealth();
		const float MaxHP = FMath::Max(HC->GetMaxHealth(), KINDA_SMALL_NUMBER);

		const AHellunaEnemyCharacter_Boss* Boss = Cast<AHellunaEnemyCharacter_Boss>(BossActor);
		const float OldMax = Boss ? Boss->Phase2HealthFillOldMax : 0.f;
		const float NewMax = Boss ? Boss->Phase2HealthFillNewMax : 0.f;

		// [Phase2VisualSimplifyV3] 5/15 — 추가 바 안 쓰므로 main 도 OldMax cap 제거.
		//   이전: bHasExtra 시 OutMainPercent = CurHP/OldMax (cap 1.0) — CurHP > OldMax 동안 fill 안 줄어들어
		//   OldMax 경계 통과 시 한꺼번에 깎이는 느낌. 단순 CurHP/MaxHP 로 연속 감소.
		OutMainPercent = FMath::Clamp(CurHP / MaxHP, 0.f, 1.f);
		(void)OldMax; (void)NewMax;  // 변수 unused 경고 회피
	}
}

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
		// [Phase2HealthFillV3] break-through 시점에 회색 바 확장 — Boss Tick 의 Stage 2 진입 시 broadcast.
		Boss->OnBossPhase2BreakThroughStart.AddUniqueDynamic(this,
			&UBossHealthBarWidget::HandleBossPhase2BreakThroughStart);

		UE_LOG(LogTemp, Warning,
			TEXT("[BossHealthBar_ColorDiag] NativeConstruct — Boss=%s, AlreadyPhase2=%d, OnPhase2 binding OK"),
			*GetNameSafe(Boss), IsBossInPhase2() ? 1 : 0);

		// 이미 페이즈2 인 상태로 위젯 생성되는 경우 (late join 등) 즉시 페이즈2 시각 상태로
		if (IsBossInPhase2())
		{
			DisplayedPhase = 2;
			PhaseColorBlendAlpha = 1.f;
			Phase2WidthAlpha = 1.f;
		}
	}
	else
	{
		UE_LOG(LogTemp, Warning,
			TEXT("[BossHealthBar_ColorDiag] NativeConstruct — BossActor cast FAILED. BossActor=%s (class=%s)"),
			*GetNameSafe(BossActor),
			BossActor ? *BossActor->GetClass()->GetName() : TEXT("NULL"));
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

	const double Now = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0;

	// [Phase2ExtraBarV1] main / extra 두 바의 target percent 동시 계산.
	float MainTarget = 0.f;
	float ExtraTarget = 0.f;
	bool bHasExtra = false;
	ComputeBossHpPercents(BossActor, MainTarget, ExtraTarget, bHasExtra);

	// 전체 HP 비율 (피격 감지용 — main+extra 합쳐 한 번의 피격으로 인식).
	const float Target = bHasExtra ? (0.5f * (MainTarget + ExtraTarget)) : MainTarget;

	// [Phase2VisualSimplifyV2] 5/15 사용자 요청 — 추가 바는 끄되 가로 확장(최대치 시각화)은 활성.
	//   메인 바 폭이 페이즈2 진입 시 우측으로 확장되어 "늘어난 최대 체력" 표현.
	//   ↳ ProgressBar_Health 의 BG alpha=0 으로 회색 잔여 시각 제거 (Border 어두운 네이비가 트랙 역할).
	const bool bUseLegacyWidthExpand = true;
	if (bUseLegacyWidthExpand)
	{
		if (!bPhase2WidthExpanding && Phase2WidthAlpha < 1.f)
		{
			if (AHellunaEnemyCharacter_Boss* Boss = Cast<AHellunaEnemyCharacter_Boss>(BossActor))
			{
				if (Boss->Phase2HealthFillStage >= 3)
				{
					bPhase2WidthExpanding = true;
					Phase2WidthExpandStartTime = Now;
				}
			}
		}
		if (bPhase2WidthExpanding)
		{
			const float ExpandDur = FMath::Max(Phase2WidthExpandDuration, 0.05f);
			const float Elapsed = static_cast<float>(Now - Phase2WidthExpandStartTime);
			Phase2WidthAlpha = FMath::Clamp(Elapsed / ExpandDur, 0.f, 1.f);
			if (Phase2WidthAlpha >= 1.f)
			{
				bPhase2WidthExpanding = false;
			}
		}
	}
	else
	{
		// extra 바가 visible 이면 legacy 가로 확장 강제 0 — 메인 바 폭 그대로 유지.
		Phase2WidthAlpha = 0.f;
		bPhase2WidthExpanding = false;
	}

	// ----- HP 표시값 갱신 — 항상 정상 흐름 (Boss 코드의 SetHealth/SetMaxHealth 가 percent driving) -----
	bPhase2FillAnimationActive = false;  // V3 에서 더 이상 사용 안 함, 안전 reset
	{
		// 피격 시 잔상 hold + 색 플래시 트리거 (main+extra 합산 기준).
		if (Target < PreviousTargetPercent - KINDA_SMALL_NUMBER)
		{
			LastDamageTime = Now;
			FlashTimeRemaining = DamageFlashDuration;
		}
		PreviousTargetPercent = Target;

		DisplayMainPercent = FMath::FInterpTo(DisplayMainPercent, MainTarget, InDeltaTime, MainInterpSpeed);

		const bool bHolding = (Now - LastDamageTime) < DelayedHoldDuration;
		if (!bHolding)
		{
			DisplayDelayedPercent = FMath::FInterpTo(DisplayDelayedPercent, MainTarget,
				InDeltaTime, DelayedInterpSpeed);
		}
		DisplayDelayedPercent = FMath::Max(DisplayDelayedPercent, DisplayMainPercent);

		// [Phase2ExtraBarV1] extra 바는 회색 잔상 없이 직접 lerp.
		DisplayExtraPercent = FMath::FInterpTo(DisplayExtraPercent, ExtraTarget, InDeltaTime, MainInterpSpeed);

		// width expand 끝났을 때 확장 상태 유지 (legacy 폴백)
		if (bUseLegacyWidthExpand && DisplayedPhase == 2 && !bPhase2WidthExpanding && Phase2WidthAlpha >= 1.f)
		{
			Phase2WidthAlpha = 1.f;
		}
	}

	// ----- 페이즈 색 블렌드 -----
	//   [Phase2ColorFillSyncV2] 페이즈2 진입 + Phase2HealthFillStage>=1 (fill 시작) 부터만 단조 증가.
	//     - 페이즈1: alpha = 0 (빨강)
	//     - 페이즈2 진입 직후 fill 시작 전: alpha = 0 강제 유지 (빨강) — Phase2HealthFillDelay 동안.
	//     - fill 시작 후: alpha = max(이전, DisplayMainPercent) — fill 진행 percent 따라 보라.
	//   ↳ HandleBossEnterPhase2 에서 alpha + percent 를 0 으로 reset 했으니 fill 시작 시점부터 깨끗하게 lerp.
	const float PrevAlpha = PhaseColorBlendAlpha;
	bool bFillStarted = false;
	if (const AHellunaEnemyCharacter_Boss* BossCb = Cast<AHellunaEnemyCharacter_Boss>(BossActor))
	{
		bFillStarted = (BossCb->Phase2HealthFillStage >= 1);
	}

	if (DisplayedPhase == 2 && bFillStarted)
	{
		PhaseColorBlendAlpha = FMath::Max(PhaseColorBlendAlpha, DisplayMainPercent);
	}
	else if (DisplayedPhase != 2)
	{
		PhaseColorBlendAlpha = 0.f;
	}
	// (DisplayedPhase==2 && !bFillStarted) — alpha 변경 안 함 (HandleBossEnterPhase2 에서 0 reset 됨).

	// [BossHealthBar_ColorDiag] 1초마다 + alpha 변화 ≥0.05 시 로그 (스팸 방지).
	{
		const double NowS = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0;
		const bool bAlphaJump = FMath::Abs(PhaseColorBlendAlpha - PrevAlpha) > 0.05f;
		if (bAlphaJump || NowS - LastColorDiagLogTime >= 1.0)
		{
			LastColorDiagLogTime = NowS;
			int32 FillStage = -1;
			if (const AHellunaEnemyCharacter_Boss* BossCb2 = Cast<AHellunaEnemyCharacter_Boss>(BossActor))
			{
				FillStage = BossCb2->Phase2HealthFillStage;
			}
			UE_LOG(LogTemp, Warning,
				TEXT("[BossHealthBar_ColorDiag] DisplayedPhase=%d, BlendAlpha=%.3f, MainPct=%.3f, Target=%.3f, FillStage=%d, IsPhase2=%d"),
				DisplayedPhase, PhaseColorBlendAlpha, DisplayMainPercent, Target,
				FillStage, IsBossInPhase2() ? 1 : 0);
		}
	}

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

	// [Phase2VisualSimplifyV1] 추가 바 비활성 — 항상 Collapsed.
	if (ProgressBar_Extra)
	{
		ProgressBar_Extra->SetVisibility(ESlateVisibility::Collapsed);
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
	// [Phase2HealthFillV3] Phase2 진입 시점엔 색 전환 + flash 만 트리거.
	//   HP fill 자체는 Boss Tick 의 Phase2HealthFillV3 가 SetHealth/SetMaxHealth lerp 로 driving.
	//   회색 바 width 확장은 OnBossPhase2BreakThroughStart 받을 때 시작 (HandleBossPhase2BreakThroughStart).
	DisplayedPhase = 2;
	Phase2WidthAlpha = 0.f;
	bPhase2WidthExpanding = false;

	// [Phase2ColorFillSyncV2] 진입 시점에 색 alpha 강제 reset.
	//   페이즈1 끝에 HP=full → DisplayMainPercent=1.0 → 단조 증가 alpha 가 잘못된 값 (0.7~0.8) 으로 stuck 되는 버그 fix.
	PhaseColorBlendAlpha = 0.f;
	// 진입 직후 한 frame 의 lerp 잔존 percent 를 alpha 가 캐치하지 않도록 percent 도 reset.
	DisplayMainPercent = 0.f;

	// 진입 순간 강한 색 플래시 한 번
	FlashTimeRemaining = DamageFlashDuration * 2.f;

	UE_LOG(LogTemp, Warning,
		TEXT("[BossHealthBar_ColorDiag] HandleBossEnterPhase2 FIRED — DisplayedPhase=2 set, BlendAlpha+MainPercent reset to 0, BossActor=%s"),
		*GetNameSafe(BossActor));
}

void UBossHealthBarWidget::HandleBossPhase2BreakThroughStart()
{
	// [Phase2HealthFillV3] Boss Tick 의 Stage 2 시작 broadcast — 회색 바 width 확장 시작.
	bPhase2WidthExpanding = true;
	Phase2WidthExpandStartTime = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0;
	Phase2WidthAlpha = 0.f;

	UE_LOG(LogTemp, Warning,
		TEXT("[BossHealthBar V3] Break-through started — width expand %.2fs (+%.0fpx)"),
		Phase2WidthExpandDuration, Phase2BarWidthAddDesignPx);
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
