// File: Source/Helluna/Private/UI/PauseMenu/HellunaConfirmDialogWidget.cpp
// ════════════════════════════════════════════════════════════════════════════════
// 범용 확인 다이얼로그 위젯 구현
// ════════════════════════════════════════════════════════════════════════════════

#include "UI/PauseMenu/HellunaConfirmDialogWidget.h"
#include "Components/Button.h"
#include "Components/TextBlock.h"
#include "Components/Image.h"
#include "Animation/WidgetAnimation.h"

const FLinearColor UHellunaConfirmDialogWidget::RedColor = FLinearColor(1.0f, 0.251f, 0.376f, 1.0f); // #FF4060

void UHellunaConfirmDialogWidget::NativeOnInitialized()
{
	Super::NativeOnInitialized();

	if (IsValid(Button_Cancel))
	{
		Button_Cancel->OnClicked.AddUniqueDynamic(this, &ThisClass::OnCancelClicked);
	}
	if (IsValid(Button_Confirm))
	{
		Button_Confirm->OnClicked.AddUniqueDynamic(this, &ThisClass::OnConfirmClicked);
	}

	UE_LOG(LogTemp, Log, TEXT("[ConfirmDialog] NativeOnInitialized — Anim_DialogShow=%s"),
		IsValid(Anim_DialogShow) ? TEXT("Bound") : TEXT("NotBound"));
}

void UHellunaConfirmDialogWidget::SetDialogContent(
	const FText& InIcon,
	const FText& InTitle,
	const FText& InDescription,
	bool bIsWarning)
{
	if (IsValid(Text_Icon))
	{
		Text_Icon->SetText(InIcon);
	}

	if (IsValid(Text_DialogTitle))
	{
		Text_DialogTitle->SetText(InTitle);
	}

	if (IsValid(Text_DialogDescription))
	{
		Text_DialogDescription->SetText(InDescription);
		if (bIsWarning)
		{
			Text_DialogDescription->SetColorAndOpacity(FSlateColor(RedColor));
		}
	}
}

void UHellunaConfirmDialogWidget::PlayShowAnimation()
{
	if (IsValid(Anim_DialogShow))
	{
		PlayAnimation(Anim_DialogShow);
	}
}

void UHellunaConfirmDialogWidget::OnCancelClicked()
{
	OnCancelled.Broadcast();
}

void UHellunaConfirmDialogWidget::OnConfirmClicked()
{
	OnConfirmed.Broadcast();
}
