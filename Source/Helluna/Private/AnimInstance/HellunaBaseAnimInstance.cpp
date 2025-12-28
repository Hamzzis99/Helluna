// Fill out your copyright notice in the Description page of Project Settings.


#include "AnimInstance/HellunaBaseAnimInstance.h"
#include "HellunaFunctionLibrary.h"

bool UHellunaBaseAnimInstance::DoesOwnerHaveTag(FGameplayTag TagToCheck) const
{
	if (APawn* OwningPawn = TryGetPawnOwner())
	{
		return UHellunaFunctionLibrary::NativeDoesActorHaveTag(OwningPawn, TagToCheck);
	}

	return false;
}

