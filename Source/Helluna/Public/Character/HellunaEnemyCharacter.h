// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Character/HellunaBaseCharacter.h"
#include "NiagaraSystem.h"
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

	/** 테스트용: 이 몬스터가 플레이어에게 데미지를 입혔을 때 디버그 출력 */
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
			ToolTip = "공격 시 재생할 Attack 애니메이션 몽타주입니다.\n데미지는 몽타주 종료 시점에 적용됩니다."))
	TObjectPtr<UAnimMontage> AttackMontage = nullptr;

	/** 피격 몽타주 재생 (서버 → 멀티캐스트) */
	UFUNCTION(NetMulticast, Reliable)
	void Multicast_PlayHitReact();

	/** 사망 몽타주 재생 (서버 → 멀티캐스트) */
	UFUNCTION(NetMulticast, Reliable)
	void Multicast_PlayDeath();

	/** 사망 몽타주가 끝났을 때 DeathTask에 알리는 델리게이트 */
	DECLARE_DELEGATE(FOnDeathMontageFinished)
	FOnDeathMontageFinished OnDeathMontageFinished;

	// 내부: AnimInstance OnMontageEnded 바인딩용
	UFUNCTION()
	void OnDeathMontageEnded(UAnimMontage* Montage, bool bInterrupted);

	// =========================================================
	// 공격 히트 이펙트 (블루프린트 에디터에서 설정)
	// =========================================================

	/** 타격 시 재생할 나이아가라 이펙트 (모든 클라이언트에서 재생) */
	UPROPERTY(EditDefaultsOnly, Category = "Combat|Effect",
		meta = (DisplayName = "타격 나이아가라 이펙트",
			ToolTip = "적이 플레이어 또는 우주선을 타격했을 때 재생할 나이아가라 파티클 이펙트입니다.\n비워두면 이펙트가 재생되지 않습니다."))
	TObjectPtr<UNiagaraSystem> HitNiagaraEffect = nullptr;

	/** 타격 이펙트 크기 배율 */
	UPROPERTY(EditDefaultsOnly, Category = "Combat|Effect",
		meta = (DisplayName = "타격 이펙트 크기",
			ToolTip = "타격 나이아가라 이펙트의 크기 배율입니다.\n1.0 = 기본 크기, 0.5 = 절반 크기, 2.0 = 두 배 크기",
			ClampMin = "0.01", ClampMax = "10.0"))
	float HitEffectScale = 1.f;

	/** 타격 시 재생할 사운드 (모든 클라이언트에서 재생) */
	UPROPERTY(EditDefaultsOnly, Category = "Combat|Effect",
		meta = (DisplayName = "타격 사운드",
			ToolTip = "적이 플레이어 또는 우주선을 타격했을 때 재생할 사운드입니다.\n비워두면 사운드가 재생되지 않습니다."))
	TObjectPtr<USoundBase> HitSound = nullptr;

	// =========================================================
	// 공격 트레이스 시스템 (타이머 기반 - 성능 최적화)
	// =========================================================

	/**
	 * 공격 트레이스 시작
	 * AnimNotify_AttackCollisionStart에서 호출됩니다.
	 * 
	 * 타이머 기반 SphereTrace를 사용하여 매 프레임 물리 충돌 체크 대신
	 * 지정된 간격(기본 50ms)으로만 적을 감지합니다.
	 * 
	 * 성능 비교 (10명 동시 공격 시):
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
	 * 공격 트레이스 정지
	 * AnimNotify_AttackCollisionEnd에서 호출되거나,
	 * 첫 히트 성공 시 자동으로 호출됩니다.
	 */
	void StopAttackTrace();

private:
	// === 공격 트레이스 내부 변수 ===
	
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
	 * 이번 공격에서 이미 맞춘 액터 목록 (중복 타격 방지)
	 * StartAttackTrace()에서 초기화
	 * PerformAttackTrace()에서 체크 및 추가
	 * 첫 히트 성공 시 즉시 트레이스 종료
	 */
	UPROPERTY()
	TSet<TObjectPtr<AActor>> HitActorsThisAttack;
	
	/**
	 * 타이머 콜백: 매 Interval마다 SphereTrace 수행
	 * 적(AHellunaHeroCharacter) 감지 시 서버에 데미지 요청
	 */
	void PerformAttackTrace();
	
	/**
	 * 서버 RPC: 데미지 적용 요청
	 * AI는 서버에서만 실행되므로 직접 호출 가능
	 * 
	 * @param Target - 데미지를 받을 플레이어
	 * @param DamageAmount - 데미지량
	 * @param HitLocation - 충돌 위치 (이펙트 재생용)
	 */
	UFUNCTION(Server, Reliable, WithValidation)
	void ServerApplyDamage(AActor* Target, float DamageAmount, const FVector& HitLocation);
	void ServerApplyDamage_Implementation(AActor* Target, float DamageAmount, const FVector& HitLocation);
	bool ServerApplyDamage_Validate(AActor* Target, float DamageAmount, const FVector& HitLocation);
	
	/**
	 * Multicast RPC: 히트 이펙트 재생
	 * 모든 클라이언트에서 타격 이펙트/사운드 재생
	 * 
	 * @param HitLocation - 타격 위치
	 */
	UFUNCTION(NetMulticast, Reliable)
	void MulticastPlayHitEffect(const FVector& HitLocation, float EffectScale);
	void MulticastPlayHitEffect_Implementation(const FVector& HitLocation, float EffectScale);

public:
	// ✅ 서버에서만: 공격 중에만 포즈/본 갱신을 강제하기 위한 토글
	UFUNCTION(BlueprintCallable)
	void SetServerAttackPoseTickEnabled(bool bEnable);
	// 원복용 저장값
	EVisibilityBasedAnimTickOption SavedAnimTickOption = EVisibilityBasedAnimTickOption::OnlyTickPoseWhenRendered;

	UPROPERTY(Transient)
	bool bSavedURO = true;

	UPROPERTY(Transient)
	bool bPoseTickSaved = false;
	
// ECS 관련 함수
public:
	// ✅ 사망 시 서버에서 호출: Mass 엔티티 자체를 제거해서 “재생성” 방지
	void DespawnMassEntityOnServer(const TCHAR* Where);

public:
	/**
	 * 거리 기반 애니메이션/그림자 품질 조절.
	 * 카메라 거리 기준으로 근/중/원거리 LOD를 적용한다.
	 * Processor의 UpdateActorTickRate에서 매 틱 호출.
	 *
	 * @param DistanceToCamera  카메라(또는 플레이어)와의 거리 (cm 단위)
	 * @author 김기현
	 */
	
	public:
	void UpdateAnimationLOD(float DistanceToCamera);

protected:
	// MassAgent가 이미 달려있다고 했으니 캐싱(없으면 FindComponentByClass로 찾아도 됨)
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Mass")
	TObjectPtr<UMassAgentComponent> MassAgentComp = nullptr;
};
	