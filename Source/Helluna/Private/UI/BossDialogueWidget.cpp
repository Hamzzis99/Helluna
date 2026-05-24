// Capstone Project Helluna

#include "UI/BossDialogueWidget.h"

#include "Components/TextBlock.h"
#include "Components/Border.h"
#include "GameFramework/GameStateBase.h"
#include "Engine/World.h"

void UBossDialogueWidget::NativeConstruct()
{
	Super::NativeConstruct();

	// 기본 상태: 완전히 숨김
	SetRenderOpacity(0.f);
	if (DialogueText) DialogueText->SetText(FText::GetEmpty());
	if (SpeakerNameText) SpeakerNameText->SetText(FText::GetEmpty());

	// [CinematicSkipVoteV1] 스킵 카운터 초기 표시 [0/전체]. PlayerArray 는 클라에도 복제됨.
	if (Text_SkipCount)
	{
		int32 Total = 1;
		if (const UWorld* W = GetWorld())
		{
			if (const AGameStateBase* GS = W->GetGameState())
			{
				Total = FMath::Max(GS->PlayerArray.Num(), 1);
			}
		}
		SetSkipCount(0, Total);
	}

	Phase = EBossDialoguePhase::Idle;
}

void UBossDialogueWidget::SetSkipCount(int32 Voted, int32 Total)
{
	if (!Text_SkipCount)
	{
		return;
	}
	Total = FMath::Max(Total, 1);
	Voted = FMath::Clamp(Voted, 0, Total);
	Text_SkipCount->SetText(FText::FromString(FString::Printf(TEXT("[%d/%d]"), Voted, Total)));
}

void UBossDialogueWidget::CompleteTyping()
{
	// 페이드 인/타이핑 중이면 즉시 전체 표시 + Holding 으로.
	if (Phase == EBossDialoguePhase::FadingIn || Phase == EBossDialoguePhase::Typing)
	{
		SetRenderOpacity(1.f);
		if (DialogueText)
		{
			DialogueText->SetText(FText::FromString(FullDialogue));
		}
		LastRevealedChars = FullDialogue.Len();
		PhaseElapsed = 0.f;
		Phase = EBossDialoguePhase::Holding;
	}
}

bool UBossDialogueWidget::IsTyping() const
{
	return Phase == EBossDialoguePhase::FadingIn || Phase == EBossDialoguePhase::Typing;
}

void UBossDialogueWidget::SetPromptSkipMode(bool bSkipNext)
{
	// [CinematicSkipFlowV2] 다음 스페이스 동작 표시: 대화 넘김="TO CONTINUE", 스킵="TO SKIP".
	if (Text_Continue)
	{
		Text_Continue->SetText(FText::FromString(bSkipNext ? TEXT("TO SKIP") : TEXT("TO CONTINUE")));
	}
}

void UBossDialogueWidget::NativeDestruct()
{
	Phase = EBossDialoguePhase::Idle;
	Super::NativeDestruct();
}

void UBossDialogueWidget::PlayDialogue(const FText& InSpeakerName, const FText& InDialogueLine)
{
	UE_LOG(LogTemp, Warning,
		TEXT("[BossDialogueV1] PlayDialogue — Speaker='%s' Line len=%d"),
		*InSpeakerName.ToString(), InDialogueLine.ToString().Len());

	if (SpeakerNameText)
	{
		SpeakerNameText->SetText(InSpeakerName);
	}

	FullDialogue = InDialogueLine.ToString();
	LastRevealedChars = 0;
	PhaseElapsed = 0.f;

	if (DialogueText)
	{
		DialogueText->SetText(FText::GetEmpty());
	}

	// 페이드 인 시작 — 이미 보이는 경우에도 강제 리셋
	SetRenderOpacity(0.f);
	Phase = EBossDialoguePhase::FadingIn;
}

void UBossDialogueWidget::HideDialogue()
{
	if (Phase == EBossDialoguePhase::Idle)
	{
		return;
	}

	UE_LOG(LogTemp, Warning, TEXT("[BossDialogueV1] HideDialogue — fading out"));
	PhaseElapsed = 0.f;
	Phase = EBossDialoguePhase::FadingOut;
}

void UBossDialogueWidget::NativeTick(const FGeometry& MyGeometry, float InDeltaTime)
{
	Super::NativeTick(MyGeometry, InDeltaTime);

	if (Phase == EBossDialoguePhase::Idle)
	{
		return;
	}

	PhaseElapsed += InDeltaTime;

	switch (Phase)
	{
	case EBossDialoguePhase::FadingIn:
	{
		const float Alpha = FadeInDuration > KINDA_SMALL_NUMBER
			? FMath::Clamp(PhaseElapsed / FadeInDuration, 0.f, 1.f)
			: 1.f;
		SetRenderOpacity(Alpha);

		if (Alpha >= 1.f)
		{
			PhaseElapsed = 0.f;
			LastRevealedChars = 0;
			Phase = (TypingSpeed > 0.f && !FullDialogue.IsEmpty())
				? EBossDialoguePhase::Typing
				: EBossDialoguePhase::Holding;

			// 즉시 표시 모드면 전체 대사를 한 번에 넣기
			if (Phase == EBossDialoguePhase::Holding && DialogueText)
			{
				DialogueText->SetText(FText::FromString(FullDialogue));
			}
		}
		break;
	}

	case EBossDialoguePhase::Typing:
	{
		const int32 TargetChars = FMath::FloorToInt(PhaseElapsed * TypingSpeed);
		const int32 ClampedChars = FMath::Min(TargetChars, FullDialogue.Len());

		if (ClampedChars > LastRevealedChars)
		{
			LastRevealedChars = ClampedChars;
			if (DialogueText)
			{
				DialogueText->SetText(FText::FromString(FullDialogue.Left(ClampedChars)));
			}
		}

		if (ClampedChars >= FullDialogue.Len())
		{
			PhaseElapsed = 0.f;
			Phase = EBossDialoguePhase::Holding;
		}
		break;
	}

	case EBossDialoguePhase::Holding:
		// 외부에서 HideDialogue 호출까지 대기. Tick 유지만.
		break;

	case EBossDialoguePhase::FadingOut:
	{
		const float Alpha = FadeOutDuration > KINDA_SMALL_NUMBER
			? 1.f - FMath::Clamp(PhaseElapsed / FadeOutDuration, 0.f, 1.f)
			: 0.f;
		SetRenderOpacity(Alpha);

		if (Alpha <= 0.f)
		{
			Phase = EBossDialoguePhase::Idle;
			RemoveFromParent();
		}
		break;
	}

	default:
		break;
	}
}
