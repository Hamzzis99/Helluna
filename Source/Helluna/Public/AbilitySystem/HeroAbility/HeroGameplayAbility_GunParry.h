// Capstone Project Helluna — Gun Parry System

#pragma once

#include "CoreMinimal.h"
#include "AbilitySystem/HellunaHeroGameplayAbility.h"
#include "HeroGameplayAbility_GunParry.generated.h"

class AHellunaEnemyCharacter;
class UMaterialInterface;
class AHeroWeapon_GunBase;
class UHellunaAbilitySystemComponent;
class UAbilityTask_PlayMontageAndWait;
class UAbilityTask_WaitGameplayEvent;
class UCameraShakeBase;
class UCurveFloat;

HELLUNA_API DECLARE_LOG_CATEGORY_EXTERN(LogGunParry, Log, All);

/**
 * 건패링 GA — 바이오하자드 RE 스타일 카운터 처형
 *
 * 사격 버튼(LMB) → Shoot GA에서 TryParryInstead() 호출
 * → CanActivateAbility 통과 시 이 GA 활성화 → 처형 연출 → 적 사망
 *
 * 팀원 코드 간접 참조용 static 헬퍼 4개 제공:
 *   ShouldBlockDamage()    — 무적 상태 체크
 *   ShouldBlockHitReact()  — 피격 모션 차단 체크
 *   ShouldDeferDeath()     — 지연 사망 체크
 *   TryParryInstead()      — Shoot GA에서 패링 분기
 */
UCLASS()
class HELLUNA_API UHeroGameplayAbility_GunParry : public UHellunaHeroGameplayAbility
{
	GENERATED_BODY()

public:
	UHeroGameplayAbility_GunParry();

	// ═══════════════════════════════════════════════════════════
	// Static 헬퍼 — 팀원 코드에서 1줄로 호출
	// ═══════════════════════════════════════════════════════════

	/** 무적 상태 체크 (Invincible 또는 PostParryInvincible) */
	static bool ShouldBlockDamage(const AActor* Target);

	/** 피격 모션 차단 체크 (적: AnimLocked / 플레이어: Invincible) */
	static bool ShouldBlockHitReact(const AActor* Target);

	/** 지연 사망 체크 (AnimLocked 상태인 적) */
	static bool ShouldDeferDeath(const AActor* Enemy);

	/** Shoot GA에서 호출 — FullAuto 체크 + 패링 GA 발동 시도 */
	static bool TryParryInstead(UHellunaAbilitySystemComponent* ASC, const AHeroWeapon_GunBase* Weapon);

protected:
	// ═══════════════════════════════════════════════════════════
	// 에디터 설정
	// ═══════════════════════════════════════════════════════════

	/** 패링 감지 범위 (cm) */
	UPROPERTY(EditDefaultsOnly, Category = "GunParry|Detection",
		meta = (DisplayName = "감지 범위", ClampMin = "100.0", ClampMax = "1000.0",
			ToolTip = "패링 감지 범위(cm). 이 범위 안의 적만 패링 대상."))
	float ParryDetectionRange = 300.f;

	/** 전방 반각 (도) — 이 각도 안에 있는 적만 패링 대상 */
	UPROPERTY(EditDefaultsOnly, Category = "GunParry|Detection",
		meta = (DisplayName = "전방 반각(도)", ClampMin = "10.0", ClampMax = "180.0",
			ToolTip = "패링 감지 반각(도). 전방 이 각도 안의 적만 감지. 60=전방 120도."))
	float ParryDetectionHalfAngle = 60.f;

	/** Motion Warping 타겟 이름 */
	UPROPERTY(EditDefaultsOnly, Category = "GunParry|Execution",
		meta = (ToolTip = "MotionWarping에서 사용하는 타겟 이름. 몽타주의 MotionWarping Notify와 일치해야 함."))
	FName WarpTargetName = TEXT("ParryTarget");

	/** 워프 시 히어로 원래 Z 높이 유지 */
	UPROPERTY(EditDefaultsOnly, Category = "GunParry|Execution",
		meta = (DisplayName = "워프 시 원래 높이 유지",
			ToolTip = "워프 시 히어로 Z 좌표 유지. true면 높이 변화 없이 수평 이동만."))
	bool bKeepHeroZOnWarp = true;

	/** 워프 위치 Z 오프셋 (cm) */
	UPROPERTY(EditDefaultsOnly, Category = "GunParry|Execution",
		meta = (DisplayName = "워프 Z 오프셋", ClampMin = "-200.0", ClampMax = "200.0",
			ToolTip = "워프 시 Z 오프셋(cm). bKeepHeroZOnWarp=true일 때 추가 높이 조절."))
	float WarpZOffset = 0.f;

	/** 처형 후 넉백 강도 */
	UPROPERTY(EditDefaultsOnly, Category = "GunParry|PostExecution",
		meta = (DisplayName = "넉백 강도", ClampMin = "0.0", ClampMax = "2000.0",
			ToolTip = "처형 후 히어로 넉백 강도. 뒤로 밀리는 힘."))
	float PostParryKnockbackStrength = 600.f;

	/** 사후 무적 시간 (초) */
	UPROPERTY(EditDefaultsOnly, Category = "GunParry|PostExecution",
		meta = (DisplayName = "사후 무적 시간(초)", ClampMin = "0.0", ClampMax = "5.0",
			ToolTip = "처형 후 히어로 무적 시간(초). 이 동안 데미지 면역."))
	float PostParryInvincibleDuration = 1.0f;

	/** 적이 나를 타겟할 때만 패링 가능 */
	UPROPERTY(EditDefaultsOnly, Category = "GunParry|Detection",
		meta = (ToolTip = "true면 적이 나(Hero)를 AI 타겟으로 할 때만 패링 가능. false면 기존처럼 범위+각도만 체크."))
	bool bRequireEnemyTargetingMe = false;

	// ═══════════════════════════════════════════════════════════
	// 스태거 시스템 — 패링 후 주변 적 넉백 + Staggered 태그
	// ═══════════════════════════════════════════════════════════

	/** 패링 처형 후 주변 적 스태거 범위(cm). 0이면 비활성. */
	UPROPERTY(EditDefaultsOnly, Category = "GunParry|Stagger",
		meta = (ToolTip = "패링 처형 후 주변 적 스태거 범위(cm). 0이면 비활성."))
	float ParryStaggerRadius = 300.f;

	/** 스태거 넉백 강도 */
	UPROPERTY(EditDefaultsOnly, Category = "GunParry|Stagger",
		meta = (ToolTip = "스태거 넉백 강도."))
	float ParryStaggerKnockback = 800.f;

	/** 스태거 유지 시간(초). 이 시간 내에 발차기 가능. */
	UPROPERTY(EditDefaultsOnly, Category = "GunParry|Stagger",
		meta = (ToolTip = "스태거 유지 시간(초). 이 시간 내에 발차기 가능."))
	float ParryStaggerDuration = 4.0f;

	/** 스태거 시각 효과 오버레이 머티리얼 (Staggered 동안 적 메시에 적용) */
	UPROPERTY(EditDefaultsOnly, Category = "GunParry|Stagger",
		meta = (ToolTip = "Staggered 상태 동안 적 메시에 적용할 오버레이 머티리얼. 없으면 시각 효과 없음."))
	TObjectPtr<UMaterialInterface> StaggerOverlayMaterial = nullptr;

	/** Staggered 적에게 재생할 몽타주 (비틀거림) */
	UPROPERTY(EditDefaultsOnly, Category = "GunParry|Stagger",
		meta = (ToolTip = "Staggered 적에게 재생할 아이들 몽타주."))
	TObjectPtr<UAnimMontage> StaggerMontage = nullptr;

	/** 스태거 AOE 데미지. 0이면 넉백만. */
	UPROPERTY(EditDefaultsOnly, Category = "GunParry|Stagger",
		meta = (ToolTip = "스태거 AOE 데미지. 0이면 넉백만."))
	float ParryStaggerDamage = 0.f;

	// ═══════════════════════════════════════════════════════════
	// GAS 오버라이드
	// ═══════════════════════════════════════════════════════════

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
	// ═══════════════════════════════════════════════════════════
	// 내부 로직
	// ═══════════════════════════════════════════════════════════

	/** 범위+전방각+태그 기반 패링 대상 검색 (인스턴스 메서드) */
	AHellunaEnemyCharacter* FindParryableEnemy(const AHellunaHeroCharacter* Hero) const; 

	/** 패링 가능한 적이 근처에 있는지 경량 체크 (static — TryParryInstead용) */
	static AHellunaEnemyCharacter* FindParryableEnemyStatic(const AHellunaHeroCharacter* Hero, bool bSkipAnimLocked = false);

	/** 몽타주 완료 콜백 */
	UFUNCTION()
	void OnExecutionMontageCompleted();

	/** 몽타주 중단 콜백 */
	UFUNCTION()
	void OnExecutionMontageInterrupted();

	/** 총 발사 AnimNotify가 전송한 GameplayEvent 수신 */
	UFUNCTION()
	void OnParryExecutionFireEvent(FGameplayEventData Payload);

	/** 처형 종료 공통 처리 (사망+넉백+태그 정리) */
	void HandleExecutionFinished(bool bWasCancelled);

	/** 총 발사 프레임에 서버 킬 처리 (Notify/폴백 공용) */
	void ProcessExecutionKill(bool bIsFallback);

	/** 총 발사 프레임 카메라 셰이크 */
	void PlayParryExecutionCameraShake(AHellunaHeroCharacter* Hero) const;

	/** 카메라 연출 시작 (로컬만) */
	void BeginCameraEffect(AHellunaHeroCharacter* Hero);

	/** 카메라 연출 종료 (로컬만) */
	void EndCameraEffect(AHellunaHeroCharacter* Hero);

	/** 워프 등장 딜레이 완료 — 메시 보이기 + 몽타주 시작 */
	void OnWarpAppearTimerComplete();

	/** 몽타주 생성 및 시작 (딜레이 후 또는 즉시) */
	void BeginExecutionMontage();

	// ═══════════════════════════════════════════════════════════
	// 다이나믹 VFX 헬퍼 (로컬 전용)
	// ═══════════════════════════════════════════════════════════

	/** 워프 시작 VFX — FOV burst + ChromaticAberration + CameraLag */
	void BeginWarpVFX(AHellunaHeroCharacter* Hero);

	/** 워프 도착 VFX — FOV를 처형용으로 전환 */
	void OnWarpArrivalVFX(AHellunaHeroCharacter* Hero);

	/** 킬 순간 VFX — FOV punch + Vignette + Desaturation */
	void BeginKillVFX(AHellunaHeroCharacter* Hero);

	/** 킬 VFX 종료 — PostProcess override 리셋 */
	void EndKillVFX(AHellunaHeroCharacter* Hero);

	/** 모든 PostProcess + CameraLag를 안전 원복 (EndAbility 방어선) */
	void ResetAllDynamicVFX(AHellunaHeroCharacter* Hero);

	// ═══════════════════════════════════════════════════════════
	// 런타임 상태
	// ═══════════════════════════════════════════════════════════

	/** 처형 대상 적 — TObjectPtr로 GC 방지 (처형 중 소멸 방지) */
	UPROPERTY()
	TObjectPtr<AHellunaEnemyCharacter> ParryTarget;

	UPROPERTY()
	TObjectPtr<UAbilityTask_PlayMontageAndWait> ExecutionMontageTask;

	UPROPERTY()
	TObjectPtr<UAbilityTask_WaitGameplayEvent> ParryFireEventTask;

	FTimerHandle PostInvincibleTimerHandle;

	/** HandleExecutionFinished가 이미 호출되었는지 (EndAbility에서 서버 킬 로직 보장용) */
	bool bHandleExecutionFinishedCalled = false;

	/** 총 발사 프레임 킬/셰이크 처리 완료 여부 */
	bool bKillProcessed = false;

	/** ProcessExecutionKill에서 적 정리(래그돌/AnimLocked/LifeSpan) 완료 여부 */
	bool bEnemyCleanedUp = false;

	/** 카메라 연출 전 원래 값 저장 */
	float SavedArmLength = 0.f;
	float SavedFOV = 0.f;
	FVector SavedSocketOffset = FVector::ZeroVector;
	bool bCameraEffectActive = false;
	bool bSavedDoCollisionTest = true;

	/** 카메라 복귀 보간 타이머 — GA GC 후에도 안전하게 동작하도록 TSharedPtr */
	TSharedPtr<FTimerHandle> CameraReturnTimerHandle;

	// ═══════════════════════════════════════════════════════════
	// 무기에서 캐싱한 카메라/워프 설정 (ActivateAbility에서 읽음)
	// ═══════════════════════════════════════════════════════════
	float CachedWarpAngleOffset = 180.f;
	float CachedExecutionDistance = 100.f;
	bool bCachedFaceEnemyAfterWarp = true;
	float CachedArmLengthMul = 0.6f;
	float CachedFOVMul = 0.85f;
	float CachedYawOffset = 0.0f;
	FVector CachedCameraTargetOffset = FVector::ZeroVector;
	float CachedReturnSpeed = 3.0f;
	float CachedReturnDelay = 0.0f;
	TSubclassOf<UCameraShakeBase> CachedParryExecutionCameraShake = nullptr;
	float CachedParryExecutionShakeScale = 1.0f;

	/** ControlRotation/bUseControllerRotationYaw 저장 (카메라 정면 배치용) */
	float SavedControlRotationYaw = 0.f;
	bool bSavedUseControllerRotationYaw = true;
	bool bSavedOrientRotationToMovement = true;

	/** 패링 전 히어로 충돌 상태 저장 */
	ECollisionEnabled::Type SavedHeroCapsuleCollision = ECollisionEnabled::QueryAndPhysics;
	FName SavedHeroCapsuleProfile = NAME_None;

	// ═══════════════════════════════════════════════════════════
	// 카메라 스무스 진입 (InterpTo)
	// ═══════════════════════════════════════════════════════════
	FTimerHandle CameraEntryTimerHandle;
	FRotator TargetCameraEntryRotation = FRotator::ZeroRotator;
	FRotator StartCameraEntryRotation = FRotator::ZeroRotator;
	float CachedCameraEntryDuration = 0.3f;
	float CameraEntryElapsedTime = 0.f;
	int32 CameraEntryTickCount = 0;

	// ═══════════════════════════════════════════════════════════
	// 워프 등장 딜레이
	// ═══════════════════════════════════════════════════════════
	FTimerHandle WarpAppearTimerHandle;
	float CachedWarpAppearDelay = 0.f;
	bool bMeshHiddenForWarp = false;

	/** 캐싱한 처형 몽타주 (BeginExecutionMontage에서 사용) */
	UPROPERTY()
	TObjectPtr<UAnimMontage> CachedExecutionMontage = nullptr;

	// ═══════════════════════════════════════════════════════════
	// 다이나믹 VFX — 타이머 핸들 + 저장/캐시
	// ═══════════════════════════════════════════════════════════
	FTimerHandle WarpPostProcessFadeTimerHandle;
	FTimerHandle WarpCameraLagRestoreTimerHandle;
	FTimerHandle KillFOVPunchRestoreTimerHandle;
	FTimerHandle KillPostProcessFadeTimerHandle;

	// CameraLag 저장값
	bool bSavedEnableCameraLag = false;
	float SavedCameraLagSpeed = 10.f;
	float SavedPostProcessBlendWeight = 1.0f;

	// 페이드 경과 시간
	float WarpPPFadeElapsed = 0.f;
	float KillPPFadeElapsed = 0.f;

	// 처형 FOV 값 (ExecutionFOV > 0이면 사용, 아니면 SavedFOV * CachedFOVMul)
	float CachedExecutionFOVValue = 90.f;

	// 무기에서 캐싱
	float CachedWarpFOVBurst = 140.f;
	float CachedWarpChromaticAberration = 5.0f;
	float CachedWarpPPFadeDuration = 0.3f;
	float CachedWarpCameraLagSpeed = 3.0f;
	float CachedWarpCameraLagDuration = 0.2f;
	float CachedKillFOVPunch = 70.f;
	float CachedKillFOVPunchDuration = 0.2f;
	float CachedKillVignetteIntensity = 1.5f;
	float CachedKillDesaturation = 0.3f;
	float CachedKillPPFadeDuration = 0.5f;

	/** 다이나믹 VFX가 현재 활성 상태인지 (중복 호출/원복 추적) */
	bool bDynamicVFXActive = false;

	// Phase 2 VFX 캐시
	float CachedKillMotionBlurAmount = 0.f;
	float CachedKillMotionBlurDuration = 0.2f;
	TSubclassOf<UCameraShakeBase> CachedWarpShakeClass = nullptr;
	float CachedWarpShakeScale = 1.0f;

	// CurveFloat 카메라
	UPROPERTY()
	TObjectPtr<UCurveFloat> CachedArmCurve = nullptr;
	UPROPERTY()
	TObjectPtr<UCurveFloat> CachedFOVCurve = nullptr;
	UPROPERTY()
	TObjectPtr<UCurveFloat> CachedYawCurve = nullptr;
	float CurveStartTime = 0.f;
	float CurveDuration = 0.f;
	FTimerHandle CameraCurveTimerHandle;

	// ═══════════════════════════════════════════════════════════
	// 시네마틱 카메라 — DOF + 오빗
	// ═══════════════════════════════════════════════════════════
	FTimerHandle DOFTransitionTimerHandle;
	FTimerHandle OrbitTimerHandle;

	// DOF
	float CachedDOFFstop = 1.4f;
	float CachedDOFTransitionDuration = 0.3f;
	float DOFTransitionElapsed = 0.f;
	bool bDOFActive = false;
	bool bDOFFadingOut = false;
	float DOFStartFstop = 0.f;   // 페이드 시작 시점의 Fstop (fade-in: 큰값→목표, fade-out: 목표→큰값)

	// 오빗
	float CachedOrbitSpeed = 10.f;
	float CachedOrbitTotalAngle = 15.f;
	float OrbitElapsed = 0.f;
	float OrbitBaseYaw = 0.f;    // 오빗 시작 시점의 Yaw
	bool bOrbitActive = false;

	// ═══════════════════════════════════════════════════════════
	// 래그돌 사망 — 캐싱
	// ═══════════════════════════════════════════════════════════
	bool bCachedParryRagdollDeath = true;
	float CachedParryRagdollImpulse = 5000.f;
	float CachedParryRagdollUpwardRatio = 0.3f;
	float CachedParryRagdollLifeSpan = 3.0f;
};
