// Capstone Project Helluna

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "HellunaHealthComponent.generated.h"


class AActor;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_FourParams(
	FOnHellunaHealthChanged,
	UActorComponent*, HealthComponent,
	float, OldHealth,
	float, NewHealth,
	AActor*, InstigatorActor
);

DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(
	FOnHellunaDeath,
	AActor*, DeadActor,
	AActor*, KillerActor
);

DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(
	FOnHellunaDowned,
	AActor*, DownedActor,
	AActor*, InstigatorActor
);

UCLASS(ClassGroup = (Custom), meta = (BlueprintSpawnableComponent))
class HELLUNA_API UHellunaHealthComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UHellunaHealthComponent();

	// ===== Read =====
	UFUNCTION(BlueprintPure, Category = "Health")
	float GetHealth() const { return Health; }

	UFUNCTION(BlueprintPure, Category = "Health")
	float GetMaxHealth() const { return MaxHealth; }

	UFUNCTION(BlueprintPure, Category = "Health")
	bool IsDead() const { return bDead; }

	//체력 백분율(0~1)
	UFUNCTION(BlueprintPure, Category = "Health")
	float GetHealthNormalized() const { return (MaxHealth <= 0.f) ? 0.f : (Health / MaxHealth); }

	// ===== 당장은 사용하지 않지만 후에 사용할 가능성 대비 =====
	//체력 강제 설정
	UFUNCTION(BlueprintCallable, Category = "Health")
	void SetHealth(float NewHealth);

	/**
	 * [BossPhase2_MaxHpV1] 최대 체력 강제 변경 — 광폭화/페이즈2 등에서 MaxHP 자체 확장.
	 *   bRefillHealth=true 면 NewMaxHealth 로 풀 회복 (HP 바 시각적으로 max 까지 채워짐).
	 *   서버 권한 전용 — 클라에서 호출하면 무시.
	 */
	UFUNCTION(BlueprintCallable, Category = "Health")
	void SetMaxHealth(float NewMaxHealth, bool bRefillHealth = false);

	//체력 회복
	UFUNCTION(BlueprintCallable, Category = "Health")
	void Heal(float Amount, AActor* InstigatorActor = nullptr);

	//컴포넌트에 직접 데미지 적용
	UFUNCTION(BlueprintCallable, Category = "Health")
	void ApplyDirectDamage(float Damage, AActor* InstigatorActor = nullptr);

	/** [Cheat] 무조건 즉사 — Shield/Parry/Downed 모든 필터 우회. 서버에서만 호출. */
	UFUNCTION(BlueprintCallable, Category = "Health|Cheat")
	void Cheat_ForceKill(AActor* InstigatorActor = nullptr);


	// ===== Downed System (다운/부활) =====
	UFUNCTION(BlueprintPure, Category = "Health|Downed")
	bool IsDowned() const { return bDowned; }

	UFUNCTION(BlueprintPure, Category = "Health|Downed")
	bool IsAliveAndNotDowned() const { return !bDead && !bDowned; }

	UFUNCTION(BlueprintPure, Category = "Health|Downed")
	float GetBleedoutTimeRemaining() const { return BleedoutTimeRemaining; }

	/** [Phase21-C] 출혈 총 시간 (getter) */
	UFUNCTION(BlueprintPure, Category = "Health|Downed")
	float GetBleedoutDuration() const { return BleedoutDuration; }

	/** 서버: 다운 상태에서 강제 사망 (생존 팀원 없을 때) */
	void ForceKillFromDowned();

	/** 서버: 부활 */
	void Revive(float InReviveHealthPercent);

	/** [HIGH-FIX] 서버: 풀에서 재사용되는 액터를 생존 상태로 리셋(사망/다운 플래그+타이머 해제 후 HP 충전).
	 *  NewHealth<=0 이면 MaxHealth 로 풀 충전. SetHealth는 bDead면 무시되므로 풀 재사용엔 이걸 사용. */
	void ResetForPoolReuse(float NewHealth = -1.f);

	/** 서버: 다운 중 피격 → 출혈 가속 */
	void ApplyBleedoutDamage(float Damage);

	// ===== Events =====
	UPROPERTY(BlueprintAssignable, Category = "Health|Event")
	FOnHellunaHealthChanged OnHealthChanged;

	UPROPERTY(BlueprintAssignable, Category = "Health|Event")
	FOnHellunaDeath OnDeath;

	UPROPERTY(BlueprintAssignable, Category = "Health|Event")
	FOnHellunaDowned OnDowned;

	// [ShipHP/FriendlyFire] true 면 AHellunaHeroCharacter(플레이어)가 유발한 데미지를 무시한다.
	//   - 우주선/터렛 등 "아군 구조물"이 플레이어 오사(誤射)로 파괴되지 않도록 보호하는 opt-in 필터.
	//   - 기본 false → 적/캐릭터의 기존 거동은 전혀 바뀌지 않음(하위호환).
	//   - 플레이어는 단일 클래스이므로 태그 없이 Cast 한 번으로 모든 플레이어를 판정(신규 적 추가 시 자동 데미지 허용).
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Health|Damage Filter",
		meta = (DisplayName = "플레이어 데미지 무시(아군 오사 방지)"))
	bool bIgnoreDamageFromHeroes = false;

protected:
	virtual void BeginPlay() override;

	UPROPERTY(EditDefaultsOnly, Category = "Health|Death", meta = (DisplayName = "사망 시 자동 제거"))
	bool bAutoDestroyOwnerOnDeath = false;

	UPROPERTY(EditDefaultsOnly, Category = "Health|Death", meta = (ClampMin = "0.0", DisplayName = "사망 후 제거까지 지연 시간(초)"))
	float DestroyDelayOnDeath = 1.0f;

	UPROPERTY(EditDefaultsOnly, ReplicatedUsing = OnRep_MaxHealth, Category = "Health", meta = (DisplayName = "최대 체력"))
	float MaxHealth = 100.f;

	UPROPERTY(ReplicatedUsing = OnRep_Health, BlueprintReadOnly, Category = "Health")
	float Health = 100.f;

	UPROPERTY(Replicated, BlueprintReadOnly, Category = "Health")
	bool bDead = false;

	// ✅ 사망 처리(Notify/파괴예약) 1회 보장용 (복제 X)
	bool bDeathProcessed = false;

	// ===== Downed System =====
	UPROPERTY(ReplicatedUsing = OnRep_Downed, BlueprintReadOnly, Category = "Health|Downed")
	bool bDowned = false;

	UPROPERTY(EditDefaultsOnly, Category = "Health|Downed",
		meta = (DisplayName = "Bleedout Duration (출혈 시간, 초)", ClampMin = "1.0"))
	float BleedoutDuration = 100.f;

	UPROPERTY(Replicated, BlueprintReadOnly, Category = "Health|Downed")
	float BleedoutTimeRemaining = 0.f;

	UFUNCTION()
	void OnRep_Downed();

	FTimerHandle BleedoutTimerHandle;

	/** 매 1초 출혈 카운트다운 */
	void TickBleedout();


	// 클라에서 체력이 복제되어 바꿔질 때 호출
	UFUNCTION()
	void OnRep_MaxHealth(float OldMaxHealth);

	UFUNCTION()
	void OnRep_Health(float OldHealth);

	// Owner OnTakeAnyDamage 바인딩용
	UFUNCTION()
	void HandleOwnerAnyDamage(
		AActor* DamagedActor,
		float Damage,
		const UDamageType* DamageType,
		AController* InstigatedBy,
		AActor* DamageCauser
	);

	// 체력값이 바뀔 때 이 함수를 통해서 바꾸게 함
	void Internal_SetHealth(float NewHealth, AActor* InstigatorActor);
	// 죽을 때 호출할 함수들
	void HandleDeath(AActor* KillerActor);

	FTimerHandle TimerHandle_Destroy;

	void DestroyOwner();

public:
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
};