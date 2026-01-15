// Capstone Project Helluna

#pragma once

#include "CoreMinimal.h"
#include "AbilitySystem/HellunaHeroGameplayAbility.h"
#include "HeroGameplayAbility_Farming.generated.h"

/**
 * 
 */
UCLASS()
class HELLUNA_API UHeroGameplayAbility_Farming : public UHellunaHeroGameplayAbility
{
	GENERATED_BODY()

public:
	UHeroGameplayAbility_Farming();

protected:

	virtual void ActivateAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo, const FGameplayAbilityActivationInfo ActivationInfo, const FGameplayEventData* TriggerEventData) override;
	virtual void EndAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo, const FGameplayAbilityActivationInfo ActivationInfo, bool bReplicateEndAbility, bool bWasCancelled) override;

private:
	// 주변에 광물(혹은 채집 대상)이 있는지 찾기 위한 트레이스 설정
	UPROPERTY(EditDefaultsOnly, Category = "Farming|Trace")
	float TraceRadius = 250.f;

	UPROPERTY(EditDefaultsOnly, Category = "Farming|Trace")
	float TraceDistance = 300.f;

	// 채집 대상(광물) Actor Tag (예: "Ore")
	UPROPERTY(EditDefaultsOnly, Category = "Farming|Target")
	FName FarmTargetTag = TEXT("Ore");
	
	UPROPERTY(EditDefaultsOnly, Category = "Farming|Damage")
	float FarmingDamage = 30.f;

	UPROPERTY(EditDefaultsOnly, Category = "Farming|Debug")
	bool bDrawDebug = false; 

	// 빠르게 회전하는 시간(초) - 0.08~0.15 추천
	UPROPERTY(EditDefaultsOnly, Category = "Farming|Look")
	float LookInterpDuration = 0.10f;

	// 타겟을 선택할 때 “시야 콘” 반각(도) - 8~15 추천
	UPROPERTY(EditDefaultsOnly, Category = "Farming|Look")
	float ViewConeHalfAngleDeg = 12.f;

	bool FindFarmingTarget(const FGameplayAbilityActorInfo* ActorInfo, AActor*& OutTarget, FHitResult& OutHit) const;
	void FaceToTarget_LocalOnly(const FGameplayAbilityActorInfo* ActorInfo, const FVector& TargetLocation) const;

	FTimerHandle LookInterpTimerHandle;
	TWeakObjectPtr<APlayerController> CachedPC;

	float LookElapsed = 0.f;
	float LookStartYaw = 0.f;
	float LookTargetYaw = 0.f;

	void StartSmoothLookAt(APlayerController* PC, float TargetYaw);
	void TickSmoothLook();
};
	