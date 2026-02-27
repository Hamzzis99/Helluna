// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Character/HellunaBaseCharacter.h"
#include "NiagaraSystem.h"
#include "NiagaraComponent.h"
#include "NiagaraFunctionLibrary.h"
#include "HellunaEnemyCharacter.generated.h"

class UEnemyCombatComponent;
class UHellunaHealthComponent;
class UMassAgentComponent;	
/**
 * 
 */
UCLASS()
class HELLUNA_API AHellunaEnemyCharacter : public AHellunaBaseCharacter
{
	GENERATED_BODY()
	
public:
	AHellunaEnemyCharacter();

protected:
	virtual void BeginPlay() override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
	virtual void PossessedBy(AController* NewController) override;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Combat")
	UEnemyCombatComponent* EnemyCombatComponent;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Component")
	UHellunaHealthComponent* HealthComponent;

	UFUNCTION()
	void OnMonsterHealthChanged(UActorComponent* MonsterHealthComponent, float OldHealth, float NewHealth, AActor* InstigatorActor);

	UFUNCTION()
	void OnMonsterDeath(AActor* DeadActor, AActor* KillerActor);

private:
	void InitEnemyStartUpData();

public:
	FORCEINLINE UEnemyCombatComponent* GetEnemyCombatComponent() const { return EnemyCombatComponent; }

	/** 테스트용: 이 몬스터가 플레이어에게 데미지를 혔을때 서버에 출력 */
	UFUNCTION(BlueprintCallable, Category = "Debug")
	void TestDamage(AActor* DamagedActor, float DamageAmount);

	// =========================================================
	// 피격 / 사망 애니메이션
	// =========================================================

	/** 피격 시 재생할 몽타주 (에디터에서 설정) */
	UPROPERTY(EditDefaultsOnly, Category = "Animation|Combat",
		meta = (DisplayName = "피격 몽타주",
			ToolTip = "데미지를 받았을 때 재생할 Hit React 애니메이션 몽타주입니다."))
	TObjectPtr<UAnimMontage> HitReactMontage = nullptr;

	/** 사망 시 재생할 몽타주 (에디터에서 설정) */
	UPROPERTY(EditDefaultsOnly, Category = "Animation|Combat",
		meta = (DisplayName = "사망 몽타주",
			ToolTip = "HP가 0이 되어 사망할 때 재생할 Death 애니메이션 몽타주입니다."))
	TObjectPtr<UAnimMontage> DeathMontage = nullptr;

	/** 공격 시 재생할 몽타주 (에디터에서 설정) */
	UPROPERTY(EditDefaultsOnly, Category = "Animation|Combat",
		meta = (DisplayName = "공격 몽타주",
			ToolTip = "공격 시 재생할 Attack 애니메이션 몽타주입니다.\n데미지 판정은 몽타주 종료 시점에 이루어집니다."))
	TObjectPtr<UAnimMontage> AttackMontage = nullptr;

	/**
	 * 광폭화 진입 몽타주.
	 * STTask_Enrage에서 EnterEnraged() 호출 시 서버+클라이언트에서 재생한다.
	 */
	UPROPERTY(EditDefaultsOnly, Category = "Animation|Combat",
		meta = (DisplayName = "광폭화 몽타주",
			ToolTip = "광폭화 상태 진입 시 재생하는 애니메이션 몽타주입니다."))
	TObjectPtr<UAnimMontage> EnrageMontage = nullptr;

	// =========================================================
	// 광폭화 스탯 배율 (에디터에서 설정, 서버에서만 적용)
	// =========================================================

	/** 광폭화 시 이동속도 배율 (기본 1.5배) */
	UPROPERTY(EditDefaultsOnly, Category = "Combat|Enrage",
		meta = (DisplayName = "광폭화 이동속도 배율", ClampMin = "1.0", ClampMax = "5.0"))
	float EnrageMoveSpeedMultiplier = 1.5f;

	/** 광폭화 시 공격력 배율 → EnemyGameplayAbility_Attack 참조 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Combat|Enrage",
		meta = (DisplayName = "광폭화 공격력 배율", ClampMin = "1.0", ClampMax = "10.0"))
	float EnrageDamageMultiplier = 2.0f;

	/** 광폭화 시 공격쿨다운 배율 (0.5 = 2배 빠름) → STTask_AttackTarget 참조 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Combat|Enrage",
		meta = (DisplayName = "광폭화 쿨다운 배율", ClampMin = "0.1", ClampMax = "1.0"))
	float EnrageCooldownMultiplier = 0.5f;

	/**
	 * 광폭화 시 공격 애니메이션 재생 속도 배율.
	 * 1.0 = 기본 속도, 1.5 = 1.5배 빠름.
	 * EnemyGameplayAbility_Attack에서 bEnraged == true이면 이 배율로 공격 몽타주를 재생한다.
	 * EnrageCooldownMultiplier와 함께 조정하면 전체 DPS를 제어할 수 있다.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Combat|Enrage",
		meta = (DisplayName = "광폭화 공격 모션 재생 속도", ClampMin = "0.5", ClampMax = "5.0"))
	float EnrageAttackMontagePlayRate = 1.5f;

	/** 현재 광폭화 상태인지 (서버 → 클라 복제) */
	UPROPERTY(Replicated, BlueprintReadOnly, Category = "Combat|Enrage")
	bool bEnraged = false;

	/**
	 * 광폭화 진입.
	 * STTask_Enrage의 EnterState에서 서버 측으로만 호출한다.
	 * 서버에서 이동속도 증가 + 몽타주 + VFX 멀티캐스트 실행.
	 */
	UFUNCTION(BlueprintCallable, Category = "Combat|Enrage")
	void EnterEnraged();

	/** 광폭화 몽타주 멀티캐스트 재생 */
	UFUNCTION(NetMulticast, Reliable)
	void Multicast_PlayEnrage();

	/**
	 * 광폭화 VFX 종료 멀티캐스트.
	 * 서버의 OnEnrageMontageEnded 에서 호출해서 모든 클라이언트의
	 * ActiveEnrageVFXComp 를 DeactivateImmediate 로 끈다.
	 * (VFX 는 Multicast_PlayEnrage 에서 모든 클라이언트에 스폰되기 때문에
	 *  종료도 반드시 Multicast 로 전파해야 한다.)
	 */
	UFUNCTION(NetMulticast, Reliable)
	void Multicast_StopEnrageVFX();

	/**
	 * 광폭화 몽타주 완료 델리게이트.
	 * 몽타주가 끝나면 STTask_Enrage에게 알려 Succeeded를 반환하게 한다.
	 */
	DECLARE_DELEGATE(FOnEnrageMontageFinished)
	FOnEnrageMontageFinished OnEnrageMontageFinished;


public:
	/** 피격 몽타주 재생 (서버 → 멀티캐스트) */
	UFUNCTION(NetMulticast, Reliable)
	void Multicast_PlayHitReact();

	/** 사망 몽타주 재생 (서버 → 멀티캐스트) */
	UFUNCTION(NetMulticast, Reliable)
	void Multicast_PlayDeath();

	/** 사망 몽타주가 끝났을 때 DeathTask에게 알리는 델리게이트 */
	DECLARE_DELEGATE(FOnDeathMontageFinished)
	FOnDeathMontageFinished OnDeathMontageFinished;

	// 내부: AnimInstance OnMontageEnded 바인딩용
	UFUNCTION()
	void OnDeathMontageEnded(UAnimMontage* Montage, bool bInterrupted);

	UFUNCTION()
	void OnEnrageMontageEnded(UAnimMontage* Montage, bool bInterrupted);

	// =========================================================
	// 공격 히트 이펙트 (블루프린트 에디터에서 설정)
	// =========================================================

	/** 히트 시 재생할 나이아가라 이펙트 */
	UPROPERTY(EditDefaultsOnly, Category = "Combat|Effect",
		meta = (DisplayName = "히트 나이아가라 이펙트",
			ToolTip = "이 몬스터가 플레이어 또는 우주선을 타격했을 때 재생할 나이아가라 파티클 이펙트입니다.\n비워두면 이펙트가 재생되지 않습니다."))
	TObjectPtr<UNiagaraSystem> HitNiagaraEffect = nullptr;

	/** 히트 이펙트 크기 배율 */
	UPROPERTY(EditDefaultsOnly, Category = "Combat|Effect",
		meta = (DisplayName = "히트 이펙트 크기", ClampMin = "0.01", ClampMax = "10.0"))
	float HitEffectScale = 1.f;

	/** 광폭화 진입 시 재생할 나이아가라 이펙트 */
	UPROPERTY(EditDefaultsOnly, Category = "Combat|Effect",
		meta = (DisplayName = "광폭화 나이아가라 이펙트",
			ToolTip = "광폭화 진입 시 모든 클라이언트에서 재생할 나이아가라 파티클 이펙트입니다.\n비워두면 이펙트가 재생되지 않습니다."))
	TObjectPtr<UNiagaraSystem> EnrageNiagaraEffect = nullptr;

	/** 광폭화 이펙트 크기 배율 */
	UPROPERTY(EditDefaultsOnly, Category = "Combat|Effect",
		meta = (DisplayName = "광폭화 이펙트 크기", ClampMin = "0.01", ClampMax = "10.0"))
	float EnrageEffectScale = 1.f;

	/**
	 * 광폭화 VFX 컴포넌트 (캐싱용).
	 * Multicast_PlayEnrage_Implementation에서 SpawnSystemAttached로 생성해 저장.
	 * 몽타주 완료(OnEnrageMontageEnded) 시 DeactivateImmediate로 끈다.
	 */
	UPROPERTY(Transient)
	TObjectPtr<UNiagaraComponent> ActiveEnrageVFXComp = nullptr;

	/** 히트 시 재생할 사운드 */
	UPROPERTY(EditDefaultsOnly, Category = "Combat|Effect",
		meta = (DisplayName = "히트 사운드",
			ToolTip = "이 몬스터가 플레이어 또는 우주선을 타격했을 때 재생할 사운드입니다.\n비워두면 사운드가 재생되지 않습니다."))
	TObjectPtr<USoundBase> HitSound = nullptr;

	UPROPERTY(EditDefaultsOnly, Category = "Combat|Effect",
		meta = (DisplayName = "히트 사운드 감쇠 설정"))
	TObjectPtr<USoundAttenuation> HitSoundAttenuation = nullptr;

	// =========================================================
	// 공격 트레이스 시스템 (타이머 기반 - 성능 최적화)
	// =========================================================

	/**
	 * 공격 트레이스 시작
	 * AnimNotify_AttackCollisionStart에서 호출합니다.
	 * 
	 * 타이머 기반 SphereTrace를 이용하여 매 트레이스 물리 충돌 체크 없이
	 * 지정된 간격(기본 50ms)으로만 감지합니다.
	 * 
	 * 성능 비교 (10마리 동시 공격 시):
	 * - 물리 충돌: 5.0ms CPU, 16.8KB/s 네트워크
	 * - Trace 방식: 0.4ms CPU, 0.3KB/s 네트워크 (약 12배 가벼움)
	 * 
	 * @param SocketName - 트레이스 시작 소켓 이름 (예: Hand_R)
	 * @param Radius - 트레이스 구체 반경 (cm)
	 * @param Interval - 트레이스 실행 주기 (초)
	 * @param DamageAmount - 적중 시 데미지량
	 * @param bDebugDraw - 디버그 드로우 활성화 여부
	 * 
	 * @author 김민우
	 */
	void StartAttackTrace(FName SocketName, float Radius, float Interval, 
		float DamageAmount, bool bDebugDraw = false);
	
	/**
	 * 공격 트레이스 중단
	 * AnimNotify_AttackCollisionEnd에서 호출하거나
	 * 첫 히트 성공 시 자동으로 호출합니다.
	 */
	void StopAttackTrace();

private:
	// === 공격 트레이스 관련 변수 ===
	
	/** 타이머 핸들 */
	FTimerHandle AttackTraceTimerHandle;
	
	/** 현재 트레이스 소켓 이름 */
	FName CurrentTraceSocketName;
	
	/** 현재 트레이스 반경 */
	float CurrentTraceRadius;
	
	/** 현재 데미지량 */
	float CurrentDamageAmount;
	
	/** 디버그 드로우 활성화 여부 */
	bool bDrawDebugTrace;
	
	/**
	 * 이번 공격에서 이미 맞춘 액터 목록 (중복 히트 방지)
	 * StartAttackTrace()에서 초기화
	 * PerformAttackTrace()에서 체크 및 추가
	 * 첫 히트 성공 후 즉시 트레이스 종료
	 */
	UPROPERTY()
	TSet<TObjectPtr<AActor>> HitActorsThisAttack;
	
	/**
	 * 타이머 콜백: 매 Interval마다 SphereTrace 실행
	 * (AHellunaHeroCharacter) 감지 시 서버에게 데미지 요청
	 */
	void PerformAttackTrace();
	
	/**
	 * 서버 RPC: 데미지 적용 요청
	 * AI는 서버에서만 실행되므로 직접 호출 가능
	 * 
	 * @param Target - 데미지를 받을 플레이어
	 * @param DamageAmount - 데미지량
	 * @param HitLocation - 충돌 위치 (이펙트 재생)
	 */
	UFUNCTION(Server, Reliable, WithValidation)
	void ServerApplyDamage(AActor* Target, float DamageAmount, const FVector& HitLocation);
	void ServerApplyDamage_Implementation(AActor* Target, float DamageAmount, const FVector& HitLocation);
	bool ServerApplyDamage_Validate(AActor* Target, float DamageAmount, const FVector& HitLocation);
	
	/**
	 * Multicast RPC: 이펙트 재생 (히트/광폭화 공용)
	 * 모든 클라이언트에서 나이아가라 이펙트를 재생한다.
	 * 사운드는 서버에서만 재생 (HitSound 프로퍼티 참조).
	 *
	 * @param SpawnLocation - 이펙트 재생 위치
	 * @param Effect        - 재생할 나이아가라 에셋 (nullptr이면 이펙트 생략)
	 * @param EffectScale   - 이펙트 크기 배율
	 * @param bPlaySound    - true이면 HitSound 재생 (히트에서만 true)
	 */

public:
	UFUNCTION(NetMulticast, Reliable)
	void MulticastPlayEffect(const FVector& SpawnLocation, UNiagaraSystem* Effect,
		float EffectScale, bool bPlaySound);
	void MulticastPlayEffect_Implementation(const FVector& SpawnLocation, UNiagaraSystem* Effect,
		float EffectScale, bool bPlaySound);

	UFUNCTION(BlueprintCallable)
	void SetServerAttackPoseTickEnabled(bool bEnable);

	void LockMovementAndFaceTarget(AActor* TargetActor);

	/** 이동 잠금 해제 (EndAbility에서 호출) */
	void UnlockMovement();

private:
	float SavedMaxWalkSpeed = 300.f;
	bool bMovementLocked = false;

	EVisibilityBasedAnimTickOption SavedAnimTickOption = EVisibilityBasedAnimTickOption::OnlyTickPoseWhenRendered;

	UPROPERTY(Transient)
	bool bSavedURO = true;

	UPROPERTY(Transient)
	bool bPoseTickSaved = false;
	
// ECS 관련 함수
public:
	// 사망 시 서버에서 호출: Mass 엔티티 자체를 제거해서 재생성을 방지
	void DespawnMassEntityOnServer(const TCHAR* Where);

public:
	/**
	 * 거리 기반 애니메이션 그림자 품질 조절.
	 * 카메라 거리 기준으로 가까울수록 거칠게 LOD를 적용한다.
	 * Processor의 UpdateActorTickRate에서 주기적으로 호출.
	 *
	 * @param DistanceToCamera  카메라(또는 플레이어)까지의 거리 (cm 단위)
	 * @author 김기현
	 */
	
	public:
	void UpdateAnimationLOD(float DistanceToCamera);

protected:
	// MassAgent가 이미 붙어있다면 캐싱(없으면 FindComponentByClass로 찾아씀)
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Mass")
	TObjectPtr<UMassAgentComponent> MassAgentComp = nullptr;
};
	
