#include "Widgets/HUD/Inv_InfoMessage.h"

#include "Components/TextBlock.h"

void UInv_InfoMessage::NativeOnInitialized()
{
	Super::NativeOnInitialized();

	Text_Message->SetText(FText::GetEmpty());
	MessageHide();
}

void UInv_InfoMessage::SetMessage(const FText& Message)
{
	Text_Message->SetText(Message);

	if (!bIsMessageActive)
	{
		MessageShow();
	}
	bIsMessageActive = true;

	UWorld* World = GetWorld();
	if (!World) return;
	TWeakObjectPtr<UInv_InfoMessage> WeakThis = this;
	World->GetTimerManager().SetTimer(MessageTimer, [WeakThis]()
		{
			if (!WeakThis.IsValid()) return;
			WeakThis->MessageHide();
			WeakThis->bIsMessageActive = false;
		}, MessageLifetime, false);
}