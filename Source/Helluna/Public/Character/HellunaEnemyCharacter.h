// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Character/HellunaBaseCharacter.h"
#include "HellunaTypes.h"
#include "NiagaraSystem.h"
#include "NiagaraComponent.h"
#include "NiagaraFunctionLibrary.h"
#include "DebugHelper.h"
#include "HellunaEnemyCharacter.generated.h"

class UEnemyCombatComponent;
class UHellunaHealthComponent;
class UMassAgentComponent;
class UNiagaraSystem;
class USoundBase;
class UHellunaEnemyGameplayAbility;
class UCameraShakeBase;
/**
 * 
 */
UCLASS()
class HELLUNA_API AHellunaEnemyCharacter : public AHellunaBaseCharacter
{
	GENERATED_BODY()
	
public:
	AHellunaEnemyCharacter();

	/**
	 * TakeDamage 오버라이드 — PuzzleShieldComponent 보호막 필터링
	 *
	 * [보스전 로드맵]
	 * PuzzleShieldComponent가 부착된 경우 bShieldActive=true면 데미지 0 반환.
	 * 컴포넌트 미부착 시 기존 로직 그대로 통과.
	 */
	virtual float TakeDamage(float DamageAmount, struct FDamageEvent const& DamageEvent,
		class AController* EventInstigator, AActor* DamageCauser) override;

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

	/**
	 * 이 캐릭터의 등급.
	 * 사망 시 GameMode::NotifyMonsterDied 에서 등급에 따라 처리 경로가 분기된다.
	 *
	 *   Normal   → 일반 몬스터 처리 (AliveMonsters 차감, 전멸 시 낮 전환)
	 *   SemiBoss → 세미보스 처리   (NotifyBossDied, TypeLabel = "세미보스")
	 *   Boss     → 정보스 처리     (NotifyBossDied, TypeLabel = "보스")
	 *
	 * 각 캐릭터 BP의 기본값에서 설정하세요.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Enemy|Grade",
		meta = (DisplayName = "적 등급",
			ToolTip = "Normal=일반 몬스터, SemiBoss=세미보스, Boss=정보스.\n각 BP 기본값에서 설정하세요."))
	EEnemyGrade EnemyGrade = EEnemyGrade::Normal;

	/** 테스트용: 이 몬스터가 플레이어에게 데미지를 혔을때 서버에 출력 */
	UFUNCTION(BlueprintCallable, Category = "Debug")
	void TestDamage(AActor* DamagedActor, float DamageAmount);

	/** 보스 소환 진단용 — TrySummonBoss에서 호출 */
	void DebugPrintBossStatus() const
	{
#if HELLUNA_DEBUG_PRINT
		Debug::Print(FString::Printf(TEXT("  HealthComp  : %s"), HealthComponent ? TEXT("✅ 있음") : TEXT("❌ 없음")), FColor::Cyan);
		Debug::Print(FString::Printf(TEXT("  StartUpData : %s"),
			CharacterStartUpData.IsNull() ? TEXT("❌ 없음 — GA_Death 부여 안 됨") : TEXT("✅ 있음")), FColor::Cyan);
#endif
	}

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

	// =========================================================
	// [PrewarmV1] GA 사전 인스턴스화 — 첫 발동 렉 완화
	// =========================================================

	/**
	 * BeginPlay(서버) 시점에 미리 GiveAbility + Primary Instance 생성할 GA 클래스.
	 * 각 몬스터 BP 기본값에서 점프/특수 공격 등 지연이 민감한 GA를 넣으세요.
	 * 일반 근접 공격처럼 이미 자주 발동되는 GA는 넣을 필요 없음.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Combat|Prewarm",
		meta = (DisplayName = "사전 워밍업 GA 목록",
			ToolTip = "BeginPlay에서 미리 인스턴스를 만들어 첫 발동 시 렉을 제거.\n보스/점프/특수 공격 GA 권장."))
	TArray<TSubclassOf<UHellunaEnemyGameplayAbility>> AbilitiesToPrewarm;

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


	// =========================================================
	// 건패링 관련
	// =========================================================

	/** 이 몬스터가 건패링 당할 수 있는지 (보스/특정 몬스터는 해제) */
	UPROPERTY(EditDefaultsOnly, Category = "Combat|Parry",
		meta = (DisplayName = "건패링 가능 여부",
			ToolTip = "체크 해제하면 이 몬스터는 건패링 대상이 되지 않습니다.\n보스, 세미보스 등 특수 몬스터에서 해제하세요."))
	bool bCanBeParried = true;

	/** Stagger 전 원본 머티리얼 저장 */
	UPROPERTY()
	TArray<TObjectPtr<UMaterialInterface>> SavedOriginalMaterials;

	/** 건패링 처형 당할 때 몬스터 측 몽타주 */
	UPROPERTY(EditDefaultsOnly, Category = "Animation|Parry",
		meta = (DisplayName = "패링 피격 몽타주",
			ToolTip = "건패링 처형을 당할 때 이 몬스터가 재생할 애니메이션입니다."))
	TObjectPtr<UAnimMontage> ParryVictimMontage = nullptr;

	/** 처형 중 HP가 0이 돼서 사망이 보류된 상태 (지연 사망) */
	bool bParryDeferredDeath = false;

	/** 처형 몽타주 멀티캐스트 (서버 → 전체 클라) */
	UFUNCTION(NetMulticast, Reliable)
	void Multicast_PlayParryVictim();

	/** 처형 후 래그돌 전환 + 임펄스 (서버 → 전체 클라, 시각적) */
	UFUNCTION(NetMulticast, Unreliable)
	void Multicast_ActivateRagdoll(FVector Impulse, FVector ImpulseLocation);

	/** Stagger 비주얼 ON/OFF (서버→모든 클라이언트) */
	UFUNCTION(NetMulticast, Reliable)
	void Multicast_SetStaggerVisual(UMaterialInterface* StaggerMat, UAnimMontage* StaggerAnim, bool bEnable);

public:
	/** 피격 몽타주 재생 (서버 → 멀티캐스트, Unreliable — 동시 피격 시 네트워크 포화 방지) */
	UFUNCTION(NetMulticast, Unreliable)
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

	// [HitSoundV1] HitSound 는 공격 GA(UHellunaEnemyGameplayAbility) 로 이관됨.
	//   Why: 같은 몬스터가 여러 공격을 가질 때 공격마다 다른 사운드를 쓰기 위함.
	//   Run-time 저장소는 아래 CachedHitSound/CachedHitSoundVolume/CachedHitSoundAttenuation.
	//
	// [HitSoundV1-Compat] 아래 두 필드는 **BP 에셋 역호환성**을 위해서만 유지.
	//   기존 몬스터 BP CDO 가 직렬화한 HitSound/HitSoundAttenuation 포인터를
	//   C++ 필드에서 제거하면 GC 가 dangling UObject* 를 읽다가 크래시나는 현상 방지.
	//   에디터 노출 없음(UPROPERTY() 만) — 런타임은 GA 의 HitSound 를 사용.
	UPROPERTY()
	TObjectPtr<USoundBase> HitSound = nullptr;

	UPROPERTY()
	TObjectPtr<USoundAttenuation> HitSoundAttenuation = nullptr;

	// =========================================================
	// 공격 히트박스 시스템 (Overlap 기반 — Box 컴포넌트 Collision On/Off)
	// =========================================================
	//
	// ■ 최적화 포인트
	//   - 기본 상태: NoCollision → 물리 broad-phase 트리에 미등록 → 비용 0
	//   - AnimNotify Start 시점에만 Collision QueryOnly 활성화
	//   - End 시점에 다시 NoCollision. Tick 없음.
	//   - BeginOverlap Delegate 는 BeginPlay 에서 1회 바인딩 후 재사용.
	//   - 서버만 판정 (HasAuthority 체크로 클라 이벤트 무시).

	/**
	 * 이름으로 찾은 공격 박스 컴포넌트의 Collision 을 켜고, 이번 공격의 데미지/디버그 설정을 캐시.
	 * bEnable=false 면 Collision 끄고 중복 방지용 히트 목록 클리어.
	 *
	 * @param BoxComponentName - BP_Enemy_Melee 에 추가된 UBoxComponent 이름 (예: "Hitbox_MeleeAttack")
	 * @param bEnable          - true=QueryOnly, false=NoCollision
	 * @param Damage           - 이번 공격의 데미지 (GA 캐시가 있으면 그걸 우선)
	 * @param bDebugDraw       - 활성 구간 동안 박스 와이어프레임 Draw
	 */
	void SetAttackBoxActive(FName BoxComponentName, bool bEnable, float Damage, bool bDebugDraw);

private:
	/**
	 * 현재 활성 공격의 데미지 (서버에서만 유효). GA 의 CachedMeleeAttackDamage 가 있으면 그 값.
	 * 여러 박스를 동시에 켜지 않는 전제. 동시 공격 지원 필요하면 per-box map으로 확장.
	 */
	float CurrentAttackDamage = 0.f;

	/** 현재 공격 활성 구간 동안 박스 디버그 Draw 여부 */
	bool bCurrentAttackDrawDebug = false;

	/** 이번 공격에서 이미 맞춘 액터 (중복 히트 방지). SetAttackBoxActive(true) 시 초기화. */
	UPROPERTY()
	TSet<TObjectPtr<AActor>> HitActorsThisAttack;

	/**
	 * BeginPlay 에서 "Hitbox_" 이름으로 시작하는 UBoxComponent 들을 찾아
	 * OnAttackBoxBeginOverlap 델리게이트에 1회 바인딩.
	 */
	void BindAttackHitboxes();

	/** 공격 박스 BeginOverlap 공통 핸들러 (서버 전용 로직) */
	UFUNCTION()
	void OnAttackBoxBeginOverlap(UPrimitiveComponent* OverlappedComp, AActor* OtherActor,
		UPrimitiveComponent* OtherComp, int32 OtherBodyIndex, bool bFromSweep,
		const FHitResult& SweepResult);

	/**
	 * [Block 상대 감지용] BeginOverlap 은 Block 관계에서 발동 안 함.
	 * 우주선/타워의 DynamicMesh(WorldStatic, BlockAll) 는 Overlap 이벤트 불가 → 주기적 Sweep.
	 * Pawn 계열 (Player/Pawn Turret) 은 BeginOverlap 으로 즉시 감지되므로 여기서는 WorldStatic 만.
	 */
	void SweepAttackBoxForBlockers();

	FTimerHandle AttackBlockSweepTimer;
	
	/**
	 * 서버 전용 데미지 적용 (일반 함수).
	 * AI는 서버에서만 실행되므로 Server RPC가 불필요 — 직접 호출로 오버헤드 제거.
	 *
	 * @param Target - 데미지를 받을 플레이어
	 * @param DamageAmount - 데미지량
	 * @param HitLocation - 충돌 위치 (이펙트 재생)
	 */
	void ServerApplyDamage(AActor* Target, float DamageAmount, const FVector& HitLocation);
	
	/**
	 * Multicast RPC: 나이아가라 이펙트 재생 (히트/광폭화/보스 패턴 공용)
	 * 모든 클라이언트에서 나이아가라 이펙트를 재생한다.
	 *
	 * [HitSoundV1] 사운드 재생은 이 RPC 에서 분리됨.
	 *   타격 사운드는 GA(UHellunaEnemyGameplayAbility::HitSound) → CachedHitSound →
	 *   Multicast_PlayHitSound 경로로 재생.
	 *   bPlaySound 파라미터는 레거시 호출자(BossEvent 존 등)를 위해 시그니처만 유지.
	 *
	 * @param SpawnLocation - 이펙트 재생 위치
	 * @param Effect        - 재생할 나이아가라 에셋 (nullptr이면 이펙트 생략)
	 * @param EffectScale   - 이펙트 크기 배율
	 * @param bPlaySound    - [DEPRECATED] 무시됨. 하위 호환용 잔여 파라미터.
	 */

public:
	UFUNCTION(NetMulticast, Unreliable)
	void MulticastPlayEffect(const FVector& SpawnLocation, UNiagaraSystem* Effect,
		float EffectScale, bool bPlaySound);
	void MulticastPlayEffect_Implementation(const FVector& SpawnLocation, UNiagaraSystem* Effect,
		float EffectScale, bool bPlaySound);

	UFUNCTION(BlueprintCallable)
	void SetServerAttackPoseTickEnabled(bool bEnable);

	void SetCachedMeleeAttackDamage(float InDamage) { CachedMeleeAttackDamage = FMath::Max(0.f, InDamage); }
	float GetCachedMeleeAttackDamage() const { return CachedMeleeAttackDamage; }

	void LockMovementAndFaceTarget(AActor* TargetActor);

	/** 이동 잠금 해제 (EndAbility에서 호출) */
	void UnlockMovement();

	/**
	 * 이동 잠금/해제를 클라이언트에도 동기화.
	 * bLock=true: 속도 0 + 회전 모드를 Controller 기반으로 전환 (공격 중 타겟 방향 유지)
	 * bLock=false: 속도 복원 + 회전 모드를 이동 방향 기반으로 복원
	 */
	UFUNCTION(NetMulticast, Reliable)
	void Multicast_SetMovementLocked(bool bLock, float WalkSpeed);

	UFUNCTION(NetMulticast, Reliable)
	void Multicast_PlayAttackMontage(UAnimMontage* Montage, float PlayRate, FRotator FacingRotation);

	/** [AttackAssetsV1] GA 소유 공격 시작 사운드 멀티캐스트. */
	UFUNCTION(NetMulticast, Unreliable)
	void Multicast_PlayAttackSound(USoundBase* Sound, float VolumeMultiplier);

	/** [HitVFXV1] 타격 지점에 VFX 멀티캐스트 스폰. */
	UFUNCTION(NetMulticast, Unreliable)
	void Multicast_SpawnHitVFX(UNiagaraSystem* VFX, FVector HitLocation, float Scale);

	/**
	 * [HitVFXLocalV1] 현재 머신(서버 or 클라)에서 로컬로만 HitVFX 스폰.
	 * 서버가 Sweep 판정 직후 자기 화면에 지연 없이 VFX 를 띄우기 위해 사용.
	 * Multicast_SpawnHitVFX 는 이미 서버가 로컬 스폰한 경우를 감지해 스킵한다.
	 */
	void SpawnHitVFXLocal(UNiagaraSystem* VFX, const FVector& HitLocation, float Scale);

	/**
	 * 일반 VFX를 지정 위치에 모든 클라이언트에 스폰 (attach 없음).
	 * HitVFX와 달리 타격 컨텍스트 없이 쓰는 범용 헬퍼 — 예: DashAttack 마무리 폭발.
	 */
	UFUNCTION(NetMulticast, Unreliable)
	void Multicast_SpawnLocationVFX(UNiagaraSystem* VFX, FVector Location, FRotator Rotation, float Scale);

	/**
	 * 캐릭터 루트에 attach 되는 VFX를 모든 클라이언트에 스폰 (bAutoDestroy=true).
	 * DashTrail 같은 짧은 연출용. 수명은 Niagara System 자체 설정으로 처리.
	 */
	UFUNCTION(NetMulticast, Unreliable)
	void Multicast_SpawnAttachedVFX(UNiagaraSystem* VFX);

	/**
	 * PlayWorldCameraShake를 모든 클라이언트에서 실행.
	 * 서버 전용 GA에서 직접 호출하면 리스너/클라이언트 컨트롤러가 없어 흔들림이 안 전파됨.
	 */
	UFUNCTION(NetMulticast, Unreliable)
	void Multicast_PlayWorldCameraShake(
		TSubclassOf<UCameraShakeBase> ShakeClass,
		FVector Origin,
		float InnerRadius,
		float OuterRadius,
		float Falloff);

	/**
	 * 현재 활성화된 RangedAttack GA를 찾아 투사체를 발사.
	 * AnimNotify_EnemyFireProjectile에서 호출됨.
	 * 서버 권한에서만 실제 스폰 발생 (GA ServerOnly).
	 */
	void FireActiveRangedProjectile();

	/**
	 * 공격 몬타지 중간(예: 발사 Notify 시점)에 클라이언트 Yaw 를 서버 값으로 재스냅.
	 * Multicast_PlayAttackMontage 는 몬타지 시작에만 스냅 → 몬타지 0.5~2초간 클라가
	 * Linear smoothing 으로 따라가는데 플레이어 빠른 이동 시 뒤처짐 + wrap 구간에서
	 * 반대 방향으로 돌아갈 수 있음. 발사 시점에 한 번 더 강제 스냅해 연출 밀림 제거.
	 */
	UFUNCTION(NetMulticast, Unreliable)
	void Multicast_SyncAttackRotation(FRotator NewRotation);

	/** 현재 진행 중인 공격의 HitVFX를 캐싱 (GA가 Activate에서 호출). */
	void SetCachedHitVFX(UNiagaraSystem* InVFX, float InScale)
	{
		CachedHitVFX = InVFX;
		CachedHitVFXScale = InScale;
	}

	/** [HitSoundV1] 타격 지점에 사운드 멀티캐스트 스폰. */
	UFUNCTION(NetMulticast, Unreliable)
	void Multicast_PlayHitSound(USoundBase* Sound, FVector HitLocation, float VolumeMultiplier, USoundAttenuation* Attenuation);

	/** 현재 진행 중인 공격의 HitSound 를 캐싱 (GA Activate 경로에서 자동 호출). */
	void SetCachedHitSound(USoundBase* InSound, float InVolume, USoundAttenuation* InAttenuation)
	{
		CachedHitSound = InSound;
		CachedHitSoundVolume = InVolume;
		CachedHitSoundAttenuation = InAttenuation;
	}

	/** 캐싱된 HitSound 가 있으면 지정 위치에 Multicast 로 재생 (없으면 무시). */
	void TryPlayCachedHitSound(const FVector& HitLocation)
	{
		if (CachedHitSound)
		{
			Multicast_PlayHitSound(CachedHitSound, HitLocation, CachedHitSoundVolume, CachedHitSoundAttenuation);
		}
	}

	// =========================================================
	// 시간 왜곡 — 지속형 VFX 멀티캐스트
	// =========================================================

	/**
	 * 지속형 VFX를 보스에 부착 스폰 (모든 클라이언트).
	 * @param SlotIndex  0=ChargingVFX, 1=SlowAreaVFX
	 */
	UFUNCTION(NetMulticast, Reliable)
	void Multicast_SpawnPersistentVFX(uint8 SlotIndex, UNiagaraSystem* Effect, float EffectScale);

	/** 지속형 VFX 중지 */
	UFUNCTION(NetMulticast, Reliable)
	void Multicast_StopPersistentVFX(uint8 SlotIndex);

	/** 사운드를 모든 클라이언트에서 재생 */
	UFUNCTION(NetMulticast, Unreliable)
	void Multicast_PlaySoundAtLocation(USoundBase* Sound, FVector Location);

	/** 지속형 VFX 슬롯 (0=Charging, 1=SlowArea) */
	UPROPERTY(Transient)
	TObjectPtr<UNiagaraComponent> PersistentVFXSlots[2];

private:
	float SavedMaxWalkSpeed = 300.f;
	bool bMovementLocked = false;

	/**
	 * 공격 중 AIController Focus 백업 (서버).
	 * 공격 진입 시 ClearFocus → AIController가 자동 UpdateControlRotation을
	 * 멈춰 Aim Offset/상체 회전이 고정됨. UnlockMovement에서 복원.
	 */
	UPROPERTY()
	TWeakObjectPtr<AActor> SavedFocusActor;

	UPROPERTY()
	bool bFocusSaved = false;

	/** 공격 전 CMC 네트워크 스무딩 모드 백업 (클라이언트) */
	ENetworkSmoothingMode SavedSmoothingMode = ENetworkSmoothingMode::Exponential;

	/** 공격 락 진입 전 NetUpdateFrequency 백업 (서버). 클라 회전 끊김 완화용 */
	float SavedNetUpdateFrequency = 10.f;
	bool bNetFreqBoosted = false;

	/** 히트 이펙트 RPC 쓰로틀링 — 0.1초 내 중복 호출 생략 */
	double LastEffectRPCTime = 0.0;

	/** 피격 모션 RPC 쓰로틀링 — 0.2초 쿨다운 (#11 최적화) */
	double LastHitReactTime = 0.0;

	/** 애님 노티파이에서 바로 읽는 현재 근접 공격 데미지 캐시 */
	float CachedMeleeAttackDamage = 0.f;

	/** [HitVFXV1] 현재 공격의 HitVFX / 스케일 캐시 (PerformAttackTrace 에서 사용). */
	UPROPERTY()
	TObjectPtr<UNiagaraSystem> CachedHitVFX = nullptr;
	float CachedHitVFXScale = 1.f;

	/** [HitSoundV1] 현재 공격의 HitSound / 볼륨 / 감쇠 캐시. */
	UPROPERTY()
	TObjectPtr<USoundBase> CachedHitSound = nullptr;
	float CachedHitSoundVolume = 1.f;
	UPROPERTY()
	TObjectPtr<USoundAttenuation> CachedHitSoundAttenuation = nullptr;

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

	// DespawnMassEntityOnServer 중복 호출 방지 플래그
	bool bDespawnStarted = false;
};
	
