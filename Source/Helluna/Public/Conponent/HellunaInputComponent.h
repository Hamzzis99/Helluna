// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "EnhancedInputComponent.h"
#include "DataAsset/DataAsset_InputConfig.h"
#include "HellunaInputComponent.generated.h"

/**
 * 
 */
UCLASS()
class HELLUNA_API UHellunaInputComponent : public UEnhancedInputComponent
{
	GENERATED_BODY()

public:
	template<class UserObject, typename CallbackFunc>
	void BindNativeInputAction(const UDataAsset_InputConfig* InInputConfig, const FGameplayTag& InInputTag, ETriggerEvent TriggerEvent, UserObject* ContextObject, CallbackFunc Func);

	template<class UserObject, typename CallbackFunc>
	void BindAbilityInputAction(const UDataAsset_InputConfig* InInputConfig, UserObject* ContextObject, CallbackFunc InputPressedFunc, CallbackFunc InputRelasedFunc);
};

template<class UserObject, typename CallbackFunc>
inline void UHellunaInputComponent::BindNativeInputAction(const UDataAsset_InputConfig* InInputConfig, const FGameplayTag& InInputTag, ETriggerEvent TriggerEvent, UserObject* ContextObject, CallbackFunc Func)
{
	// [Fix:checkf-removal 2026-05-02] checkf는 Test/Shipping 빌드에서 프로세스 종료.
	// DataAsset 미설정은 에디터 작업 누락으로 발생 가능 — 강제 종료 대신 로그 + 안전 반환.
	if (!InInputConfig)
	{
		UE_LOG(LogTemp, Error, TEXT("[HellunaInputComponent] BindNativeInputAction: InputConfig DataAsset is null — binding skipped"));
		return;
	}

	if (UInputAction* FoundAction = InInputConfig->FindNativeInputActionByTag(InInputTag))
	{
		BindAction(FoundAction, TriggerEvent, ContextObject, Func);
	}
}

template<class UserObject, typename CallbackFunc>
inline void UHellunaInputComponent::BindAbilityInputAction(const UDataAsset_InputConfig* InInputConfig, UserObject* ContextObject, CallbackFunc InputPressedFunc, CallbackFunc InputRelasedFunc)
{
	// [Fix:checkf-removal 2026-05-02] 동일 — checkf 다운그레이드.
	if (!InInputConfig)
	{
		UE_LOG(LogTemp, Error, TEXT("[HellunaInputComponent] BindAbilityInputAction: InputConfig DataAsset is null — binding skipped"));
		return;
	}

	for (const FHellunaInputActionConfig& AbilityInputActionConfig : InInputConfig->AbilityInputActions)
	{
		if (!AbilityInputActionConfig.IsValid()) continue;

		BindAction(AbilityInputActionConfig.InputAction, ETriggerEvent::Started, ContextObject, InputPressedFunc, AbilityInputActionConfig.InputTag);
		BindAction(AbilityInputActionConfig.InputAction, ETriggerEvent::Completed, ContextObject, InputRelasedFunc, AbilityInputActionConfig.InputTag);
	}
}

