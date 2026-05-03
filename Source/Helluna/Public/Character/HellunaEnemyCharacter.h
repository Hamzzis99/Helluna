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
class UMaterialInstanceDynamic;

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
	 * 메시 슬롯 0 DMI 의 "TeamColor" 벡터 파라미터를 지정 색으로 설정.
	 * 서버 호출이면 ReplicatedTeamColor 가 세팅되어 OnRep 으로 모든 클라가 같은 색을 적용.
	 * 클라 호출(OnRep 경로)이면 로컬 DMI 만 갱신.
	 */
	void ApplyTeamColor(const FLinearColor& Color);

	/**
	 * TeamColorOptions 에서 랜덤 픽해 ApplyTeamColor 호출. Mass 미경유 (레벨 배치) 액터용.
	 * Pool 경로는 Mass entity 가 색을 결정해 ActivateActor 로 전달하므로 이 함수를 안 거친다.
	 */
	void ApplyRandomTeamColor();

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
	// 팀 컬러 RGB 기반 색감 다양화
	//   각 Mass entity 가 FLinearColor 를 보유 → 액터에 replicate → 각 클라가 자신의
	//   DMI 의 "TeamColor" 벡터 파라미터에 set. DMI 자체는 머신 로컬, 색만 동기화.
	//
	//   TeamColorOptions 는 Mass 첫 스폰 시 CDO 에서 읽어 랜덤 픽하는 풀.
	//   임시로 /Game/Enemy/MaterialVariants/Super 4개 색을 BP 기본값으로 사용.
	// =========================================================

	/**
	 * Mass 첫 스폰 시 랜덤 픽되는 RGB 풀. 비어 있으면 색 미적용 (기본 메시 머티리얼 유지).
	 * BP CDO 에서 등록 — 같은 클래스 모든 인스턴스가 동일한 풀을 공유.
	 */
	UPROPERTY(EditDefaultsOnly, Category = "Visual|TeamColor",
		meta = (DisplayName = "팀 컬러 RGB 풀",
			ToolTip = "Mass entity 첫 스폰 시 이 배열에서 랜덤 픽한 색을 자기 RGB 로 보존.\n임시 기본값은 /Game/Enemy/MaterialVariants/Super 4종 색."))
	TArray<FLinearColor> TeamColorOptions;

	/**
	 * 마스터 머티리얼에서 "팀 컬러" 를 받는 벡터 파라미터 이름. 기본 "TeamColor".
	 * 변경 필요 시 BP CDO 에서 오버라이드.
	 */
	UPROPERTY(EditDefaultsOnly, Category = "Visual|TeamColor",
		meta = (DisplayName = "팀 컬러 파라미터 이름"))
	FName TeamColorParameterName = FName(TEXT("TeamColor"));

	/**
	 * 서버에서 결정한 RGB. 클라엔 OnRep 으로 DMI 파라미터 set.
	 * 풀 재활성화로 값이 바뀌면 OnRep 재발화 → 모든 클라가 같은 색.
	 * 초기값 Transparent (A=0) = "아직 결정 안 됨".
	 *
	 * Why: DMI/OverrideMaterials 는 replicate 안 됨. RGB 만 복제해 클라가
	 * 자기 머신의 DMI 에 SetVectorParameterValue 하도록 한다.
	 */
	UPROPERTY(ReplicatedUsing = OnRep_TeamColor, Transient)
	FLinearColor ReplicatedTeamColor = FLinearColor::Transparent;

	/** 클라 측에서 ReplicatedTeamColor 변경 시 DMI 파라미터 적용. */
	UFUNCTION()
	void OnRep_TeamColor();

private:
	/** 슬롯 0 DMI 캐시. 첫 ApplyTeamColor 시 생성, 이후 재사용. */
	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> TeamColorMID;

public:

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
	// 보스 전용 hooks — 일반 몬스터에서는 no-op, AHellunaEnemyCharacter_Boss 가 override.
	//   OnMonsterHealthChanged 가 EnemyGrade==Boss/SemiBoss 분기에서 호출.
	//   가상 호출이지만 일반 몹은 default body 가 즉시 false/return → 비용 거의 0.
	// =========================================================

	/** HP 0 도달 시 페이즈2 가로채기. 가로채면 true 반환해 사망 처리 스킵. 일반 몬스터: 항상 false. */
	virtual bool TryInterceptDeathForPhase2(float OldHealth, float NewHealth) { return false; }

	/** 피격 시 보스 전용 히트 스톱 (시간 일시 정지). 일반 몬스터: no-op. */
	virtual void TriggerHitStop() {}

	/** PossessedBy 후 NextTick StartLogic 을 건너뛸지. 보스 소환 시네마틱 동안 true. 일반: false. */
	virtual bool ShouldSuppressBrainRestartAfterPossess() const { return false; }

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

	// =========================================================
	// Block 관련 (GunParry와 분리)
	// =========================================================

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Combat|Block",
		meta = (DisplayName = "일반 Block 데미지 감쇠 적용"))
	bool bMeleeDamageCanBeBlocked = false;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Combat|Block",
		meta = (DisplayName = "Block 성공 시 데미지 배율", ClampMin = "0.0", ClampMax = "1.0"))
	float BlockedMeleeDamageMultiplier = 0.3f;

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

	// =========================================================
	// 사망 디졸브 / 먼지화
	// =========================================================

	/**
	 * 사망 몽타주/GA 완료 후 액터를 즉시 Destroy하지 않고, 메시 머티리얼을 서서히 디졸브한다.
	 * 서버는 충돌/Mass entity를 즉시 제거하고, 클라이언트 시각 효과 완료까지 액터 Destroy만 지연한다.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Combat|Effect|DeathDissolve",
		meta = (DisplayName = "사망 디졸브 사용"))
	bool bEnableDeathDissolve = true;

	/**
	 * Warrior의 DissolveAmount와 Helluna Paragon Minion의 FadeOut/Ash를 함께 지원한다.
	 * 현재 머티리얼에 존재하지 않는 파라미터는 SetScalarParameterValue 호출 시 무시된다.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Combat|Effect|DeathDissolve",
		meta = (DisplayName = "디졸브 진행 스칼라 파라미터"))
	TArray<FName> DeathDissolveScalarParameterNames;

	/** Warrior의 DissolveEdgeColor와 Helluna Paragon Minion의 BurnColor를 함께 지원한다. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Combat|Effect|DeathDissolve",
		meta = (DisplayName = "디졸브 컬러 벡터 파라미터"))
	TArray<FName> DeathDissolveVectorParameterNames;

	/** Warrior식 디졸브 가장자리/Niagara 컬러. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Combat|Effect|DeathDissolve",
		meta = (DisplayName = "Warrior 디졸브 엣지 컬러"))
	FLinearColor DeathDissolveEdgeColor = FLinearColor(2.5f, 1.05f, 0.25f, 1.0f);

	/** Helluna Paragon Minion BurnColor용 과장 컬러. 기본 BurnColor 계열보다 강하게 태운다. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Combat|Effect|DeathDissolve",
		meta = (DisplayName = "Helluna 먼지화 Burn 컬러"))
	FLinearColor DeathDissolveBurnColor = FLinearColor(65.0f, 23.0f, 1.6f, 1.0f);

	/** Warrior보다 짧고 강한 기본값. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Combat|Effect|DeathDissolve",
		meta = (DisplayName = "디졸브 시간", ClampMin = "0.1", ClampMax = "30.0", Units = "s"))
	float DeathDissolveDuration = 4.5f;

	/** 머티리얼 진행값 갱신 주기. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Combat|Effect|DeathDissolve",
		meta = (DisplayName = "디졸브 Tick 간격", ClampMin = "0.016", ClampMax = "0.25", Units = "s"))
	float DeathDissolveTickInterval = 0.033f;

	/**
	 * 사망 디졸브 중 메시 기준으로 붙일 Niagara.
	 * 기본값은 Helluna의 기존 Dissolve VFX이며, BP_Helluna_Enemy_1 기본값에서 교체 가능하다.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Combat|Effect|DeathDissolve",
		meta = (DisplayName = "사망 디졸브 나이아가라"))
	TSoftObjectPtr<UNiagaraSystem> DeathDissolveNiagaraEffect;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Combat|Effect|DeathDissolve",
		meta = (DisplayName = "사망 디졸브 이펙트 크기", ClampMin = "0.01", ClampMax = "10.0"))
	float DeathDissolveEffectScale = 1.65f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Combat|Effect|DeathDissolve",
		meta = (DisplayName = "나이아가라 컬러 변수"))
	FName DeathDissolveNiagaraColorVariableName = FName(TEXT("User.DissolveEdgeColor"));

	UFUNCTION(NetMulticast, Reliable)
	void Multicast_StartDeathDissolve();

	/**
	 * [SpawnDissolveV1] Reverse dissolve — 몬스터가 디졸브 머티리얼로 점점 등장 (보스 포탈 emerge 용).
	 *   Death dissolve 와 같은 머티리얼/파라미터를 공유하지만 알파를 1→0 으로 역방향 재생.
	 *   완료 시 메시 visibility 변경하지 않음 (보스가 솔리드 상태로 유지).
	 *   @param Duration 페이드 인 지속시간 (초). 0 이하면 즉시 솔리드.
	 *   @param StartAmount 시작 dissolve 양 (0=솔리드, 1=완전히 사라짐). 보통 1.0.
	 */
	UFUNCTION(NetMulticast, Reliable)
	void Multicast_StartSpawnDissolve(float Duration, float StartAmount);

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
	void StartDeathDissolveVisuals();
	void TickDeathDissolveVisuals();
	void ApplyDeathDissolveAmount(float Amount);

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

public:
	/**
	 * 서버 전용 데미지 적용 (일반 함수).
	 * AI는 서버에서만 실행되므로 Server RPC가 불필요 — 직접 호출로 오버헤드 제거.
	 * Block 검사/Cue 호출이 이 진입점에 묶여 있어 외부 GA(DashAttack 등)에서도 호출 가능하게 public 노출.
	 *
	 * @param Target - 데미지를 받을 플레이어
	 * @param DamageAmount - 데미지량
	 * @param HitLocation - 충돌 위치 (이펙트 재생)
	 */
	void ServerApplyDamage(AActor* Target, float DamageAmount, const FVector& HitLocation);

private:
	
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

	/** 특정 몬타지를 모든 머신에서 블렌드 아웃으로 중단 (DashLoopMontage 종료 등). */
	UFUNCTION(NetMulticast, Reliable)
	void Multicast_StopMontage(UAnimMontage* Montage, float BlendOutTime);

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

	/** [KnockbackV1] 현재 공격의 넉백 활성 여부 / 힘 (GA 가 ActivateAbility 에서 주입). */
	bool bCachedKnockbackEnabled = false;
	float CachedKnockbackForce = 0.f;

	/** [KnockbackV1] 현재 공격의 넉백 설정 캐싱 (기본 OFF). */
	void SetCachedKnockback(bool bEnable, float Force)
	{
		bCachedKnockbackEnabled = bEnable;
		CachedKnockbackForce = FMath::Max(0.f, Force);
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

	UPROPERTY(Transient)
	TArray<TObjectPtr<UMaterialInstanceDynamic>> DeathDissolveMIDs;

	FTimerHandle DeathDissolveTimerHandle;
	FTimerHandle DeathDissolveDestroyTimerHandle;
	double DeathDissolveStartTime = 0.0;

	UPROPERTY(Transient)
	bool bDeathDissolveVisualsStarted = false;

	// [SpawnDissolveV1] reverse dissolve (1→0) 상태 관리
	FTimerHandle SpawnDissolveTimerHandle;
	double SpawnDissolveStartTime = 0.0;
	float  SpawnDissolveDurationCached = 1.f;
	float  SpawnDissolveStartAmount = 1.f;
	UPROPERTY(Transient)
	bool bSpawnDissolveActive = false;

	void TickSpawnDissolveVisuals();

public:
	/** [SpawnDissolveV1] 외부에서 직접 호출 가능 — Multicast 거치지 않고 바로 페이드 인. */
	void StartSpawnDissolveVisuals(float Duration, float StartAmount);

	/**
	 * [PortalClipV1] 보스 등장 시네마틱 — MF_PortalClipPlane 머티리얼 함수의 파라미터 set.
	 *   평면 뒤쪽 픽셀이 PixelDepthOffset 으로 화면 밖 깊이로 밀려서 invisible.
	 *   각 클라가 로컬에서 호출 (각 머신의 MID 에 적용).
	 *   @param PlanePosWS 평면 위 한 점 (월드)
	 *   @param PlaneNormalWS 평면 노멀 — 이 방향이 visible 쪽
	 */
	void StartPortalClipPlaneVisuals(const FVector& PlanePosWS, const FVector& PlaneNormalWS);

	/** [PortalClipV1] EnableClipPlane=0 으로 모든 픽셀 visible. */
	void StopPortalClipPlaneVisuals();

	/**
	 * [BerserkGlowV1] MF_BerserkGlow 머티리얼 함수의 파라미터 set — 보스 광폭화 emissive 발광.
	 *   각 클라가 로컬에서 호출 (각 머신의 MID 에 적용).
	 *   @param Color 베르세르크 발광 색 (R/G/B 1.0 기준, HDR 가능)
	 *   @param Boost 발광 강도 multiplier
	 */
	void StartBerserkVisuals(const FLinearColor& Color, float Boost);

	/** [BerserkGlowV1] EnableBerserk=0 으로 정상 emissive 복원. */
	void StopBerserkVisuals();

private:

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
	
