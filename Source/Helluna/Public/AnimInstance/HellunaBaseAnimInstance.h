// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Animation/AnimInstance.h"
#include "GameplayTagContainer.h"
#include "HellunaBaseAnimInstance.generated.h"

/**
 * 
 */
UCLASS()
class HELLUNA_API UHellunaBaseAnimInstance : public UAnimInstance
{
	GENERATED_BODY()
	
protected:
	UFUNCTION(BlueprintPure, meta = (BlueprintThreadSafe, DisplayName = "소유자가 태그를 가지고 있는지"))
	bool DoesOwnerHaveTag(FGameplayTag TagToCheck) const;
};
