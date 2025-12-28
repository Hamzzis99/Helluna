// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Character.h"
#include "AbilitySystemInterface.h"
#include "HellunaBaseCharacter.generated.h"

class UHellunaAbilitySystemComponent;
class UHellunaAttributeSet;
class UDataAsset_BaseStartUpData;
class UMotionWarpingComponent;

UCLASS()
class HELLUNA_API AHellunaBaseCharacter : public ACharacter, public IAbilitySystemInterface
{
	GENERATED_BODY()

public:
	// Sets default values for this character's properties
	AHellunaBaseCharacter();

	virtual UAbilitySystemComponent* GetAbilitySystemComponent() const override;
	//~ End IAbilitySystemInterface Interface

protected:
	//~ Begin APawn Interface.
	virtual void PossessedBy(AController* NewController) override;
	//~ End APawn Interface

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "AbilitySystem")
	UHellunaAbilitySystemComponent* HellunaAbilitySystemComponent;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "AbilitySystem")
	UHellunaAttributeSet* HellunaAttributeSet;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "CharacterData")
	TSoftObjectPtr<UDataAsset_BaseStartUpData> CharacterStartUpData;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "MotionWarping")
	UMotionWarpingComponent* MotionWarpingComponent;

public:
	FORCEINLINE UHellunaAbilitySystemComponent* GetHellunaAbilitySystemComponent() const { return HellunaAbilitySystemComponent; }

	FORCEINLINE UHellunaAttributeSet* GetHellunaAttributeSet() const { return HellunaAttributeSet; }

};
