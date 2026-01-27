    // Fill out your copyright notice in the Description page of Project Settings.


#include "HellunaFunctionLibrary.h"
#include "AbilitySystemBlueprintLibrary.h"
#include "AbilitySystem/HellunaAbilitySystemComponent.h"
#include "HellunaGameplayTags.h"
#include "Kismet/KismetMathLibrary.h"
#include "Kismet/GameplayStatics.h"

UHellunaAbilitySystemComponent* UHellunaFunctionLibrary::NativeGetHellunaASCFromActor(AActor* InActor)
{
    if (!IsValid(InActor))
    {
        return nullptr;
    }

    UAbilitySystemComponent* ASC = UAbilitySystemBlueprintLibrary::GetAbilitySystemComponent(InActor);
    return Cast<UHellunaAbilitySystemComponent>(ASC); // ✅ CastChecked → Cast
}

void UHellunaFunctionLibrary::AddGameplayTagToActorIfNone(AActor* InActor, FGameplayTag TagToAdd)
{
    UHellunaAbilitySystemComponent* ASC = NativeGetHellunaASCFromActor(InActor);

    if (!ASC->HasMatchingGameplayTag(TagToAdd))
    {
        ASC->AddLooseGameplayTag(TagToAdd);
    }
}

void UHellunaFunctionLibrary::RemoveGameplayTagFromActorIfFound(AActor* InActor, FGameplayTag TagToRemove)
{
    UHellunaAbilitySystemComponent* ASC = NativeGetHellunaASCFromActor(InActor);

    if (ASC->HasMatchingGameplayTag(TagToRemove))
    {
        ASC->RemoveLooseGameplayTag(TagToRemove);
    }
}

bool UHellunaFunctionLibrary::NativeDoesActorHaveTag(AActor* InActor, FGameplayTag TagToCheck)
{
    UHellunaAbilitySystemComponent* ASC = NativeGetHellunaASCFromActor(InActor);

    return ASC->HasMatchingGameplayTag(TagToCheck);
}

void UHellunaFunctionLibrary::BP_DoesActorHaveTag(AActor* InActor, FGameplayTag TagToCheck, EHellunaConfirmType& OutConfirmType)
{
    OutConfirmType = NativeDoesActorHaveTag(InActor, TagToCheck) ? EHellunaConfirmType::Yes : EHellunaConfirmType::No;
}



