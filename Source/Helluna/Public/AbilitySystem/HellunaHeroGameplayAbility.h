// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "AbilitySystem/HellunaGameplayAbility.h"
#include "HellunaHeroGameplayAbility.generated.h"

UENUM(BlueprintType)
enum class EHellunaInputActionPolicy : uint8
{
	Trigger UMETA(DisplayName = "Trigger"),
	Toggle  UMETA(DisplayName = "Toggle"),
	Hold  UMETA(DisplayName = "Hold")
};

class AHellunaHeroCharacter;
class AHellunaHeroController;
/**
 * 
 */
UCLASS()
class HELLUNA_API UHellunaHeroGameplayAbility : public UHellunaGameplayAbility
{
	GENERATED_BODY()

public:
	UFUNCTION(BlueprintPure, Category = "Helluna|Ability")
	AHellunaHeroCharacter* GetHeroCharacterFromActorInfo();

	UFUNCTION(BlueprintPure, Category = "Helluna|Ability")
	AHellunaHeroController* GetHeroControllerFromActorInfo();

	UFUNCTION(BlueprintPure, Category = "Helluna|Ability")
	UHeroCombatComponent* GetHeroCombatComponentFromActorInfo();

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Helluna|Input")
	EHellunaInputActionPolicy InputActionPolicy = EHellunaInputActionPolicy::Trigger;

protected:
	virtual bool CanActivateAbility(
		const FGameplayAbilitySpecHandle Handle,
		const FGameplayAbilityActorInfo* ActorInfo,
		const FGameplayTagContainer* SourceTags = nullptr,
		const FGameplayTagContainer* TargetTags = nullptr,
		FGameplayTagContainer* OptionalRelevantTags = nullptr) const override;

	/** true이면 ParryExecution/Kicking 중에도 발동 가능 (GunParry, MeleeKick 전용) */
	UPROPERTY(EditDefaultsOnly, Category = "HellunaAbility")
	bool bIgnoreParryBlock = false;

private:
	TWeakObjectPtr<AHellunaHeroCharacter> CachedHellunaHeroCharacter;
	TWeakObjectPtr<AHellunaHeroController> CachedHellunaHeroController;
	
};
