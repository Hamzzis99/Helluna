// Capstone Project Helluna

#include "UI/BossDialogueWidget.h"

#include "Components/TextBlock.h"
#include "Components/Border.h"

void UBossDialogueWidget::NativeConstruct()
{
	Super::NativeConstruct();

	// 기본 상태: 완전히 숨김
	SetRenderOpacity(0.f);
	if (DialogueText) DialogueText->SetText(FText::GetEmpty());
	if (SpeakerNameText) SpeakerNameText->SetText(FText::GetEmpty());

	Phase = EBossDialoguePhase::Idle;
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
