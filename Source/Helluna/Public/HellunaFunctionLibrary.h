// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "HellunaType/HellunaEnumTypes.h"
#include "HellunaFunctionLibrary.generated.h"

class UHellunaAbilitySystemComponent;
class UPawnCombatComponent;
struct FScalableFloat;
class UHellunaGameInstance;
/**
 * 
 */
UCLASS()
class HELLUNA_API UHellunaFunctionLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	static UHellunaAbilitySystemComponent* NativeGetHellunaASCFromActor(AActor* InActor);

	UFUNCTION(BlueprintCallable, Category = "Helluna|FunctionLibrary")
	static void AddGameplayTagToActorIfNone(AActor* InActor, FGameplayTag TagToAdd);

	UFUNCTION(BlueprintCallable, Category = "Helluna|FunctionLibrary")
	static void RemoveGameplayTagFromActorIfFound(AActor* InActor, FGameplayTag TagToRemove);

	static bool NativeDoesActorHaveTag(AActor* InActor, FGameplayTag TagToCheck);

	UFUNCTION(BlueprintCallable, Category = "Helluna|FunctionLibrary", meta = (DisplayName = "Does Actor Have Tag", ExpandEnumAsExecs = "OutConfirmType"))
	static void BP_DoesActorHaveTag(AActor* InActor, FGameplayTag TagToCheck, EHellunaConfirmType& OutConfirmType);

};
