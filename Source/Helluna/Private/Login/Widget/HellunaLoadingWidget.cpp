// File: Source/Helluna/Private/Login/Widget/HellunaLoadingWidget.cpp
#include "Login/Widget/HellunaLoadingWidget.h"
#include "Components/TextBlock.h"

void UHellunaLoadingWidget::SetLoadingMessage(const FString& Message)
{
	if (Text_LoadingMessage)
	{
		Text_LoadingMessage->SetText(FText::FromString(Message));
	}
}
