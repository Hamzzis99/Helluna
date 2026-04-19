/**
 * EnemyGameplayAbility_DashAttack.h
 *
 * 보스 패턴 3: 빠른 돌진 + 근접 공격.
 *
 * 처리 순서 ─────────────────────────────────────────────────────────────────
 *  1. 타겟 방향 스냅 + 준비 몬타지 재생 (선택, WindupMontage)
 *  2. 돌진: DashDuration 동안 타겟 방향으로 고속 이동
 *       - LaunchCharacter 를 DashTickInterval 간격으로 재호출해 속도 유지
 *       - 이동 중 TArray<AActor*> HitActors 에 타격한 적을 기록해 중복 데미지 방지
 *       - 충돌 반경 DashHitRadius 내 액터에 DashHitDamage 즉시 적용
 *  3. 돌진 종료 후 근접 공격 몬타지 재생 (AttackMontage == GA의 GetEffectiveAttackMontage())
 *  4. 몬타지 완료 후 AttackRecoveryDelay 대기 → 어빌리티 종료
 *
 * 네트워크 ──────────────────────────────────────────────────────────────────
 *  ServerOnly / InstancedPerActor
 * ────────────────────────────────────────────────────────────────────────────
 *
 * @author 김민우
 */

#pragma once

#include "CoreMinimal.h"
#include "AbilitySystem/HellunaEnemyGameplayAbility.h"
#include "EnemyGameplayAbility_DashAttack.generated.h"

class UAnimMontage;
class UNiagaraSystem;
class UCameraShakeBase;

/** 돌진 종료 조건. MaxDashDistance는 모든 모드에서 공통 안전 상한으로 동작. */
UENUM()
enum class EDashStopMode : uint8
{
	MaxDistance   UMETA(DisplayName = "최대 거리 도달 (기본)"),
	ReachTarget   UMETA(DisplayName = "타겟 근접"),
	HitTarget     UMETA(DisplayName = "타겟 물리 충돌"),
};

/** 돌진 방향 결정 방식 */
UENUM()
enum class EDashDirectionMode : uint8
{
	Locked   UMETA(DisplayName = "시작 방향 고정"),
	Homing   UMETA(DisplayName = "매 틱 타겟 추적"),
};

UCLASS()
class HELLUNA_API UEnemyGameplayAbility_DashAttack : public UHellunaEnemyGameplayAbility
{
	GENERATED_BODY()

public:
	UEnemyGameplayAbility_DashAttack();

	/** StateTree에서 주입하는 공격 대상 */
	UPROPERTY(BlueprintReadWrite, Category = "Attack")
	TWeakObjectPtr<AActor> CurrentTarget = nullptr;

	virtual void SetCurrentTarget(AActor* InTarget) override { CurrentTarget = InTarget; }

	// =========================================================
	// 준비 (Windup)
	// =========================================================

	/** 돌진 직전 재생할 준비 몬타지 (선택). 짧은 애니메이션 권장. */
	UPROPERTY(EditDefaultsOnly, Category = "돌진 공격|준비",
		meta = (DisplayName = "준비 몬타지",
			ToolTip = "돌진 직전 재생할 예비 동작. 비워두면 바로 돌진 시작."))
	TObjectPtr<UAnimMontage> WindupMontage = nullptr;

	/** 준비 몬타지 재생 속도 배율. */
	UPROPERTY(EditDefaultsOnly, Category = "돌진 공격|준비",
		meta = (DisplayName = "준비 몬타지 재생 속도",
			ClampMin = "0.1", ClampMax = "3.0"))
	float WindupPlayRate = 1.f;

	// =========================================================
	// 돌진 (Dash)
	// =========================================================

	/** 돌진 수평 속도 (cm/s). */
	UPROPERTY(EditDefaultsOnly, Category = "돌진 공격|돌진",
		meta = (DisplayName = "돌진 속도 (cm/s)",
			ClampMin = "500.0", ClampMax = "6000.0"))
	float DashSpeed = 2500.f;

	/** 돌진 중 LaunchCharacter 재호출 간격. */
	UPROPERTY(EditDefaultsOnly, Category = "돌진 공격|돌진",
		meta = (DisplayName = "돌진 속도 유지 간격 (초)",
			ToolTip = "이 간격으로 LaunchCharacter를 반복 호출해 감속 없이 돌진 유지.",
			ClampMin = "0.02", ClampMax = "0.5"))
	float DashTickInterval = 0.05f;

	/** 돌진 중 적 충돌 판정 반경 (cm). */
	UPROPERTY(EditDefaultsOnly, Category = "돌진 공격|돌진",
		meta = (DisplayName = "돌진 충돌 반경 (cm)",
			ClampMin = "30.0", ClampMax = "500.0"))
	float DashHitRadius = 120.f;

	/** 돌진으로 타격한 적에게 주는 데미지. */
	UPROPERTY(EditDefaultsOnly, Category = "돌진 공격|돌진",
		meta = (DisplayName = "돌진 데미지",
			ClampMin = "0.0"))
	float DashHitDamage = 15.f;

	/** 돌진 타격 시 넉백 힘. 0이면 넉백 없음. */
	UPROPERTY(EditDefaultsOnly, Category = "돌진 공격|돌진",
		meta = (DisplayName = "돌진 넉백 힘",
			ClampMin = "0.0", ClampMax = "3000.0"))
	float DashKnockbackForce = 800.f;

	/** 돌진 중 스폰할 트레일 VFX (선택). */
	UPROPERTY(EditDefaultsOnly, Category = "돌진 공격|돌진",
		meta = (DisplayName = "돌진 트레일 VFX"))
	TObjectPtr<UNiagaraSystem> DashTrailVFX = nullptr;

	/**
	 * 돌진 중에 재생할 몬타지 (선택).
	 * 비워두면 AnimBP의 locomotion(보통 idle)이 노출되어 "멈춘 채 미끄러지는" 느낌.
	 * 짧은 루프 또는 DashDuration에 맞춘 길이의 돌진 포즈/달리기 애니메이션 권장.
	 * 돌진 종료 시 자동으로 중단되고 마무리 몬타지로 교체됨.
	 */
	UPROPERTY(EditDefaultsOnly, Category = "돌진 공격|돌진",
		meta = (DisplayName = "돌진 중 몬타지",
			ToolTip = "돌진하는 동안 재생할 애니메이션 몬타지. 없으면 기본 locomotion 사용."))
	TObjectPtr<UAnimMontage> DashLoopMontage = nullptr;

	/** 돌진 몬타지 재생 속도 배율. */
	UPROPERTY(EditDefaultsOnly, Category = "돌진 공격|돌진",
		meta = (DisplayName = "돌진 몬타지 재생 속도",
			ClampMin = "0.1", ClampMax = "3.0"))
	float DashLoopPlayRate = 1.f;

	// =========================================================
	// 종료 조건 — 어떻게 돌진을 멈출지
	// =========================================================

	/**
	 * 돌진 종료 기준:
	 *  - MaxDistance   : MaxDashDistance 도달 시 종료 (가장 단순, 예측 가능)
	 *  - ReachTarget   : 타겟과의 거리가 StopAtTargetRange 이하가 되면 종료
	 *  - HitTarget     : 돌진 충돌 판정에 타겟이 걸리면 종료
	 *
	 * MaxDashDistance는 모든 모드에서 공용 안전 상한으로도 동작 —
	 * ReachTarget/HitTarget 모드에서 플레이어가 끝없이 회피해도 MaxDashDistance 도달 시 강제 종료.
	 */
	UPROPERTY(EditDefaultsOnly, Category = "돌진 공격|종료 조건",
		meta = (DisplayName = "종료 조건"))
	EDashStopMode DashStopMode = EDashStopMode::MaxDistance;

	/**
	 * 시작 지점 기준 최대 이동 거리 (cm).
	 * MaxDistance 모드의 기본 종료 조건이자, 다른 모드에서의 안전 상한.
	 */
	UPROPERTY(EditDefaultsOnly, Category = "돌진 공격|종료 조건",
		meta = (DisplayName = "최대 돌진 거리 (cm)",
			ClampMin = "100.0"))
	float MaxDashDistance = 1000.f;

	/** ReachTarget 모드에서 이 거리 이내로 들어오면 돌진 종료 (cm). */
	UPROPERTY(EditDefaultsOnly, Category = "돌진 공격|종료 조건",
		meta = (DisplayName = "타겟 근접 정지 거리 (cm)",
			EditCondition = "DashStopMode==EDashStopMode::ReachTarget",
			EditConditionHides,
			ClampMin = "50.0"))
	float StopAtTargetRange = 200.f;

	// =========================================================
	// 방향 모드 — 돌진 방향을 어떻게 유지할지
	// =========================================================

	/**
	 * 돌진 방향 결정 방식:
	 *  - Locked : ActivateAbility 시점의 타겟 방향을 끝까지 유지 (플레이어가 회피 가능)
	 *  - Homing : 매 틱 타겟 위치로 방향 갱신 (HomingInterpSpeed 로 부드럽게 추적)
	 */
	UPROPERTY(EditDefaultsOnly, Category = "돌진 공격|방향",
		meta = (DisplayName = "방향 모드"))
	EDashDirectionMode DashDirectionMode = EDashDirectionMode::Locked;

	/**
	 * Homing 모드에서 방향 보간 속도.
	 * 값이 클수록 방향을 빠르게 고쳐서 따라감 (회피 어려움).
	 * 값이 작을수록 부드러운 호를 그리며 따라감 (회피 여지 있음).
	 */
	UPROPERTY(EditDefaultsOnly, Category = "돌진 공격|방향",
		meta = (DisplayName = "호밍 보간 속도",
			EditCondition = "DashDirectionMode==EDashDirectionMode::Homing",
			EditConditionHides,
			ClampMin = "0.5", ClampMax = "30.0"))
	float HomingInterpSpeed = 8.f;

	// =========================================================
	// [DashFollowupV1] 돌진 직후 후속 GA 체이닝
	// =========================================================

	/**
	 * 돌진 종료 후 발동할 후속 GA. 마무리 근접 공격은 이 후속 GA 가 전담.
	 * 비워두면 후속 동작 없이 즉시 종료 (대쉬만 수행).
	 *
	 * StateTree 관점:
	 *  - DashAttack Task 는 후속 GA 가 끝날 때까지 살아있음 → StateTree 가 다른 패턴으로
	 *    빠지는 것을 방지. 후속 GA 종료 시 DashAttack 도 EndAbility.
	 *
	 * 태그 처리:
	 *  - State.Enemy.Attacking 은 reference-counted → DashAttack + 후속 GA 가 둘 다
	 *    Add/Remove 해도 StateTree EnterCondition 이 한순간도 끊기지 않음.
	 */
	UPROPERTY(EditDefaultsOnly, Category = "돌진 공격|후속 체이닝",
		meta = (DisplayName = "후속 어빌리티 클래스",
			ToolTip = "돌진 종료 직후 자동 활성화될 GA. 예: 내려찍기 GA. 비워두면 후속 동작 없이 종료."))
	TSubclassOf<UHellunaEnemyGameplayAbility> FollowupAbilityClass;

	/** 후속 GA 가 이 시간 안에 EndAbility 신호를 안 보내면 강제 종료. */
	UPROPERTY(EditDefaultsOnly, Category = "돌진 공격|후속 체이닝",
		meta = (DisplayName = "후속 GA 페일세이프 (초)",
			ToolTip = "후속 GA 가 비정상 행동(무한 루프/응답 없음)일 때 강제 종료까지 대기.",
			ClampMin = "1.0", ClampMax = "30.0"))
	float FollowupFailsafeTime = 8.f;

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
	// 돌진 파이프라인
	void StartWindup();
	void StartDash();
	void TickDash();
	void FinishDash();
	void HandleAttackFinished();

	// [DashFollowupV1] 후속 GA 체이닝
	void StartFollowupAbility();
	void OnFollowupAbilityEnded(const struct FAbilityEndedData& EndData);
	void OnFollowupFailsafeTimeout();
	void CleanupFollowupBindings();

	UFUNCTION()
	void OnWindupCompleted();

	UFUNCTION()
	void OnWindupCancelled();

	// 런타임 상태
	FTimerHandle DashTickTimerHandle;
	FTimerHandle DashEndTimerHandle;

	// [DashFollowupV1] 후속 GA 체이닝 런타임 상태
	FTimerHandle FollowupFailsafeHandle;
	FDelegateHandle FollowupOnAbilityEndedHandle;
	bool bWaitingForFollowupGA = false;

	/** 돌진 단일 발동 중 이미 타격한 액터 (중복 데미지 방지). */
	UPROPERTY()
	TSet<TObjectPtr<AActor>> DashHitActors;

	/** 돌진 시작 시점에 확정된 돌진 방향 (지면 평면 기준). Homing 모드에서는 매 틱 갱신. */
	FVector DashDirection = FVector::ForwardVector;

	/** 돌진 시작 지점. MaxDistance 모드에서 경과 거리 계산용. */
	FVector DashStartLocation = FVector::ZeroVector;
};
