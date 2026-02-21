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

	//체력 회복
	UFUNCTION(BlueprintCallable, Category = "Health")
	void Heal(float Amount, AActor* InstigatorActor = nullptr);

	//컴포넌트에 직접 데미지 적용
	UFUNCTION(BlueprintCallable, Category = "Health")
	void ApplyDirectDamage(float Damage, AActor* InstigatorActor = nullptr);


	// ===== Events =====
	UPROPERTY(BlueprintAssignable, Category = "Health|Event")
	FOnHellunaHealthChanged OnHealthChanged;

	UPROPERTY(BlueprintAssignable, Category = "Health|Event")
	FOnHellunaDeath OnDeath;

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