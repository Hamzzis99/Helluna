// File: Source/Helluna/Public/AbilitySystem/HeroAbility/HeroGameplayAbility_Block.h
#pragma once

#include "CoreMinimal.h"
#include "AbilitySystem/HellunaHeroGameplayAbility.h"
#include "HeroGameplayAbility_Block.generated.h"

class AHellunaHeroCharacter;
class UAnimMontage;
class UNiagaraComponent;
class UNiagaraSystem;

UCLASS()
class HELLUNA_API UHeroGameplayAbility_Block : public UHellunaHeroGameplayAbility
{
	GENERATED_BODY()

public:
	UHeroGameplayAbility_Block();

	static bool IsBlocking(const AActor* Blocker);
	static bool IsPerfectBlocking(const AActor* Blocker);
	static bool EvaluateBlock(AActor* Blocker, AActor* SourceActor, bool& bOutPerfectBlock);
	static void ExecuteBlockCue(AActor* Blocker, AActor* SourceActor, bool bPerfectBlock);

protected:
	virtual void ActivateAbility(
		const FGameplayAbilitySpecHandle Handle,
		const FGameplayAbilityActorInfo* ActorInfo,
		const FGameplayAbilityActivationInfo ActivationInfo,
		const FGameplayEventData* TriggerEventData) override;

	virtual void EndAbility(
		const FGameplayAbilitySpecHandle Handle,
		const FGameplayAbilityActorInfo* ActorInfo,
		const FGameplayAbilityActivationInfo ActivationInfo,
		bool bReplicateEndAbility,
		bool bWasCancelled) override;

private:
	UAnimMontage* ResolveBlockMontage();
	UNiagaraSystem* ResolveBlockShieldVFX();

	void StartBlockShieldVFX(AHellunaHeroCharacter* Hero);
	void StopBlockShieldVFX();

	void ClearPerfectBlockWindow();

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Block|PerfectTiming",
		meta = (DisplayName = "퍼펙트 Block 시간", ClampMin = "0.0", ClampMax = "2.0", AllowPrivateAccess = "true"))
	float PerfectBlockWindowSeconds = 0.25f;

	UPROPERTY(EditDefaultsOnly, Category = "Block")
	TSoftObjectPtr<UAnimMontage> BlockMontage;

	UPROPERTY(EditDefaultsOnly, Category = "Block", meta = (ClampMin = "0.0", ClampMax = "2.0"))
	float BlockMontagePlayRate = 1.0f;

	UPROPERTY(Transient)
	TObjectPtr<UAnimMontage> LoadedBlockMontage = nullptr;

	UPROPERTY(EditDefaultsOnly, Category = "Block|VFX")
	TSoftObjectPtr<UNiagaraSystem> BlockShieldVFX;

	UPROPERTY(EditDefaultsOnly, Category = "Block|VFX")
	FName BlockShieldAttachSocket = TEXT("MagicShieldSocket");

	UPROPERTY(EditDefaultsOnly, Category = "Block|VFX")
	FVector BlockShieldRelativeLocation = FVector::ZeroVector;

	UPROPERTY(EditDefaultsOnly, Category = "Block|VFX")
	FRotator BlockShieldRelativeRotation = FRotator::ZeroRotator;

	UPROPERTY(EditDefaultsOnly, Category = "Block|VFX", meta = (ClampMin = "0.01"))
	FVector BlockShieldRelativeScale = FVector(1.25f);

	UPROPERTY(Transient)
	TObjectPtr<UNiagaraSystem> LoadedBlockShieldVFX = nullptr;

	UPROPERTY(Transient)
	TObjectPtr<UNiagaraComponent> ActiveBlockShieldVFX = nullptr;

	FTimerHandle PerfectBlockTimerHandle;
};
