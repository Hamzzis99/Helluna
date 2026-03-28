// File: Source/Helluna/Public/AbilitySystem/HeroAbility/HeroGameplayAbility_Aim.h
#pragma once

#include "CoreMinimal.h"
#include "AbilitySystem/HellunaHeroGameplayAbility.h"
#include "HeroGameplayAbility_Aim.generated.h"

class UAT_AimCameraInterp;

// ═══════════════════════════════════════════════════════════
// [Aim System] 조준 GA — 3페이즈 AbilityTask 방식
//
// Phase 1: 줌인 + 이동감속 (AbilityTask가 카메라 보간)
// Phase 2: 유지 + 사격가능 (Hold 대기)
// Phase 3: 줌아웃 + 속도복원 (AbilityTask가 카메라 복귀)
// ═══════════════════════════════════════════════════════════

UCLASS()
class HELLUNA_API UHeroGameplayAbility_Aim : public UHellunaHeroGameplayAbility
{
	GENERATED_BODY()

public:
	UHeroGameplayAbility_Aim();

protected:
	virtual void ActivateAbility(const FGameplayAbilitySpecHandle Handle,
		const FGameplayAbilityActorInfo* ActorInfo,
		const FGameplayAbilityActivationInfo ActivationInfo,
		const FGameplayEventData* TriggerEventData) override;

	virtual void EndAbility(const FGameplayAbilitySpecHandle Handle,
		const FGameplayAbilityActorInfo* ActorInfo,
		const FGameplayAbilityActivationInfo ActivationInfo,
		bool bReplicateEndAbility, bool bWasCancelled) override;

	virtual void InputReleased(const FGameplayAbilitySpecHandle Handle,
		const FGameplayAbilityActorInfo* ActorInfo,
		const FGameplayAbilityActivationInfo ActivationInfo) override;

private:
	/** Phase 1 완료 → Phase 2 진입 */
	UFUNCTION()
	void OnZoomInCompleted();

	/** Phase 3 완료 → EndAbility */
	UFUNCTION()
	void OnZoomOutCompleted();

	/** Phase 3 시작 (줌아웃) */
	void StartZoomOut();

	// ── 설정값 ──
	UPROPERTY(EditDefaultsOnly, Category = "Aim",
		meta = (DisplayName = "Aim Walk Speed (조준 시 이동 속도)"))
	float AimMaxWalkSpeed = 150.f;

	// ── 캐싱 ──
	float CachedDefaultMaxWalkSpeed = 0.f;
	float CachedDefaultFOV = 0.f;
	float CachedDefaultArmLength = 0.f;
	FVector CachedDefaultSocketOffset = FVector::ZeroVector;

	/** 캐싱: 조준 전 회전 방식 */
	bool CachedOrientRotationToMovement = true;
	bool CachedUseControllerDesiredRotation = false;
	FRotator CachedRotationRate = FRotator::ZeroRotator;

	/** 현재 페이즈 (디버그용) */
	int32 CurrentPhase = 0;

	/** 입력 해제 플래그 (Phase 1 중 해제 시 바로 Phase 3 전환용) */
	bool bInputReleased = false;

	/** 줌아웃 태스크 참조 (GA 취소 시 정리용) */
	UPROPERTY()
	TObjectPtr<UAT_AimCameraInterp> ZoomOutTask = nullptr;
};
