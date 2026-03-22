// Capstone Project Helluna — Melee Kick System (Resident Evil Style)

#pragma once

#include "CoreMinimal.h"
#include "AbilitySystem/HellunaHeroGameplayAbility.h"
#include "HeroGameplayAbility_MeleeKick.generated.h"

class AHellunaEnemyCharacter;
class UCameraShakeBase;
class UMaterialInterface;
class UNiagaraSystem;
class UMeleeTraceComponent;

DECLARE_LOG_CATEGORY_EXTERN(LogMeleeKick, Log, All);

/**
 * 발차기 GA — 바이오하자드 스타일 스태거→킥 시스템
 *
 * Staggered 상태의 적에게 접근 후 발차기.
 * 타겟 적에게 큰 데미지 + 주변 AOE 넉백 + Staggered 연쇄.
 */
UCLASS()
class HELLUNA_API UHeroGameplayAbility_MeleeKick : public UHellunaHeroGameplayAbility
{
	GENERATED_BODY()

public:
	UHeroGameplayAbility_MeleeKick();

	// ═══════════════════════════════════════════════════════════
	// 에디터 설정
	// ═══════════════════════════════════════════════════════════

	/** 발차기 몽타주 */
	UPROPERTY(EditDefaultsOnly, Category = "MeleeKick",
		meta = (ToolTip = "발차기 몽타주."))
	TObjectPtr<UAnimMontage> KickMontage = nullptr;

	/** Staggered 적 감지 범위(cm) */
	UPROPERTY(EditDefaultsOnly, Category = "MeleeKick",
		meta = (ToolTip = "Staggered 적 감지 범위(cm)."))
	float KickDetectionRange = 200.f;

	/** Staggered 적 감지 전방 반각(도) */
	UPROPERTY(EditDefaultsOnly, Category = "MeleeKick",
		meta = (ToolTip = "Staggered 적 감지 전방 반각(도)."))
	float KickDetectionHalfAngle = 60.f;

	/** 발차기 워프 거리(cm) */
	UPROPERTY(EditDefaultsOnly, Category = "MeleeKick",
		meta = (ToolTip = "발차기 워프 거리(cm). 적과 이 거리만큼 떨어짐."))
	float KickWarpDistance = 80.f;

	/** 발차기 타겟 데미지 */
	UPROPERTY(EditDefaultsOnly, Category = "MeleeKick",
		meta = (ToolTip = "발차기 타겟 데미지."))
	float KickDamage = 50.f;

	/** 발차기 AOE 넉백 범위(cm) */
	UPROPERTY(EditDefaultsOnly, Category = "MeleeKick",
		meta = (ToolTip = "발차기 AOE 넉백 범위(cm)."))
	float KickAOERadius = 250.f;

	/** 발차기 AOE 넉백 강도 */
	UPROPERTY(EditDefaultsOnly, Category = "MeleeKick",
		meta = (ToolTip = "발차기 AOE 넉백 강도."))
	float KickAOEKnockback = 1200.f;

	/** 발차기 AOE 데미지 */
	UPROPERTY(EditDefaultsOnly, Category = "MeleeKick",
		meta = (ToolTip = "발차기 AOE 데미지."))
	float KickAOEDamage = 20.f;

	/** 발차기 카메라 셰이크 */
	UPROPERTY(EditDefaultsOnly, Category = "MeleeKick",
		meta = (ToolTip = "발차기 카메라 셰이크."))
	TSubclassOf<UCameraShakeBase> KickCameraShake = nullptr;

	/** 발차기 셰이크 강도 */
	UPROPERTY(EditDefaultsOnly, Category = "MeleeKick",
		meta = (ToolTip = "발차기 셰이크 강도."))
	float KickShakeScale = 1.5f;

	// ═══════════════════════════════════════════════════════════
	// 소켓 히트 판정 설정
	// ═══════════════════════════════════════════════════════════

	/** 소켓 히트 임팩트 VFX (ImpactPoint에 스폰) */
	UPROPERTY(EditDefaultsOnly, Category = "MeleeKick|SocketHit",
		meta = (ToolTip = "소켓 히트 시 ImpactPoint에 스폰할 나이아가라 이펙트."))
	TObjectPtr<UNiagaraSystem> KickImpactVFX = nullptr;

	/** 히트스탑 지속 시간(초) — 타격감을 위해 AnimRate를 잠시 0으로 */
	UPROPERTY(EditDefaultsOnly, Category = "MeleeKick|SocketHit",
		meta = (ToolTip = "소켓 히트 시 히트스탑 지속 시간(초). 0이면 히트스탑 없음.",
			ClampMin = "0.0", ClampMax = "0.2"))
	float HitStopDuration = 0.05f;

	/** 발차기 AOE Staggered 연쇄 지속 시간(초). 0이면 연쇄 없음. */
	UPROPERTY(EditDefaultsOnly, Category = "MeleeKick",
		meta = (ToolTip = "발차기 AOE Staggered 연쇄 지속 시간(초). 0이면 연쇄 없음."))
	float KickAOEStaggerDuration = 4.0f;

	/** Staggered 오버레이 머티리얼 (킥 가능 적 반짝임) */
	UPROPERTY(EditDefaultsOnly, Category = "MeleeKick",
		meta = (ToolTip = "Staggered 적에게 적용할 오버레이 머티리얼."))
	TObjectPtr<UMaterialInterface> StaggerOverlayMaterial = nullptr;

	/** Staggered 적에게 재생할 몽타주 */
	UPROPERTY(EditDefaultsOnly, Category = "MeleeKick",
		meta = (ToolTip = "Staggered 적에게 재생할 아이들 몽타주."))
	TObjectPtr<UAnimMontage> StaggerMontage = nullptr;

	// ═══════════════════════════════════════════════════════════
	// 카메라 연출
	// ═══════════════════════════════════════════════════════════

	/** 킥 진입 시 SpringArm 길이 배율 (1.0=변화없음, 0.7=줌인) */
	UPROPERTY(EditDefaultsOnly, Category = "MeleeKick|Camera",
		meta = (ToolTip = "킥 진입 시 SpringArm 길이 배율. 1.0=변화없음, 0.7=줌인.", ClampMin="0.3", ClampMax="2.0"))
	float KickArmLengthMultiplier = 0.8f;

	/** 킥 진입 시 FOV 배율 (1.0=변화없음, 0.85=좁힘) */
	UPROPERTY(EditDefaultsOnly, Category = "MeleeKick|Camera",
		meta = (ToolTip = "킥 진입 시 FOV 배율. 1.0=변화없음, 0.85=좁힘.", ClampMin="0.5", ClampMax="1.5"))
	float KickFOVMultiplier = 0.85f;

	/** 킥 진입 카메라 전환 속도 (InterpTo Speed) */
	UPROPERTY(EditDefaultsOnly, Category = "MeleeKick|Camera",
		meta = (ToolTip = "킥 진입 시 카메라 ArmLength/FOV 전환 속도.", ClampMin="1.0", ClampMax="30.0"))
	float KickCameraInterpSpeed = 10.f;

	/** 킥 종료 후 카메라 복귀 속도 */
	UPROPERTY(EditDefaultsOnly, Category = "MeleeKick|Camera",
		meta = (ToolTip = "킥 종료 후 원래 ArmLength/FOV로 복귀 속도.", ClampMin="1.0", ClampMax="30.0"))
	float KickCameraReturnSpeed = 5.f;

	/** 킥 종료 후 카메라 복귀 딜레이(초) */
	UPROPERTY(EditDefaultsOnly, Category = "MeleeKick|Camera",
		meta = (ToolTip = "킥 종료 후 카메라 복귀 시작 딜레이(초).", ClampMin="0.0", ClampMax="1.0"))
	float KickCameraReturnDelay = 0.1f;

	/** 킥 중 SpringArm SocketOffset 오프셋 (캐릭터 기준 로컬 좌표) */
	UPROPERTY(EditDefaultsOnly, Category = "MeleeKick|Camera",
		meta = (ToolTip = "킥 중 SpringArm SocketOffset 추가값. X=전후, Y=좌우, Z=상하."))
	FVector KickCameraSocketOffset = FVector(0.f, 30.f, 20.f);

protected:
	virtual bool CanActivateAbility(
		const FGameplayAbilitySpecHandle Handle,
		const FGameplayAbilityActorInfo* ActorInfo,
		const FGameplayTagContainer* SourceTags = nullptr,
		const FGameplayTagContainer* TargetTags = nullptr,
		FGameplayTagContainer* OptionalRelevantTags = nullptr) const override;

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
	/** Staggered 적 찾기 */
	AHellunaEnemyCharacter* FindStaggeredEnemy() const;

	/** 킥 임팩트 (노티파이에서 호출) — 소켓 히트 없으면 폴백 */
	UFUNCTION()
	void OnKickImpactEvent(FGameplayEventData Payload);

	/** 소켓 히트 콜백 (MeleeTraceComponent에서 호출) */
	UFUNCTION()
	void OnSocketHit(AActor* HitActor, const FHitResult& HitResult);

	/** 몽타주 완료 */
	UFUNCTION()
	void OnKickMontageCompleted();

	/** 몽타주 중단 */
	UFUNCTION()
	void OnKickMontageInterrupted();

	UPROPERTY()
	TObjectPtr<AHellunaEnemyCharacter> KickTarget = nullptr;

	bool bKickImpactProcessed = false;
	bool bSavedOrientRotation = false;

	/** 소켓 히트가 발생했는가 (폴백 판단용) */
	bool bSocketHitOccurred = false;

	/** 서버 AnimInstance Tick 원래 옵션 (킥 종료 시 복원) */
	EVisibilityBasedAnimTickOption SavedAnimTickOption = EVisibilityBasedAnimTickOption::OnlyTickPoseWhenRendered;

	// 카메라 원본 값 저장
	float SavedKickArmLength = 0.f;
	float SavedKickFOV = 0.f;
	FVector SavedKickSocketOffset = FVector::ZeroVector;

	// 카메라 상태
	bool bKickCameraActive = false;
};
