// Capstone Project Helluna

#include "Character/EnemyComponent/HellunaHealthComponent.h"

#include "GameFramework/Actor.h"
#include "Net/UnrealNetwork.h"
#include "TimerManager.h"
#include "Kismet/GameplayStatics.h"

#include "DebugHelper.h"

UHellunaHealthComponent::UHellunaHealthComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
	SetIsReplicatedByDefault(true);
}

void UHellunaHealthComponent::BeginPlay()
{
	Super::BeginPlay();

	AActor* Owner = GetOwner();
	if (!Owner)
		return;

	if (Owner->HasAuthority())
	{
		MaxHealth = FMath::Max(1.f, MaxHealth);
		Health = FMath::Clamp(Health, 0.f, MaxHealth);

		bDead = (Health <= 0.f);

		Owner->OnTakeAnyDamage.AddDynamic(this, &ThisClass::HandleOwnerAnyDamage);
	}
}

void UHellunaHealthComponent::SetHealth(float NewHealth)
{
	AActor* Owner = GetOwner();
	if (!Owner || !Owner->HasAuthority())
		return;

	if (bDead)
		return;

	Internal_SetHealth(NewHealth, nullptr);
}

void UHellunaHealthComponent::Heal(float Amount, AActor* InstigatorActor)
{
	AActor* Owner = GetOwner();
	if (!Owner || !Owner->HasAuthority())
		return;

	if (bDead)
		return;

	if (Amount <= 0.f)
		return;

	Internal_SetHealth(Health + Amount, InstigatorActor);
}

void UHellunaHealthComponent::ApplyDirectDamage(float Damage, AActor* InstigatorActor)
{
	AActor* Owner = GetOwner();
	if (!Owner || !Owner->HasAuthority())
		return;

	if (bDead)
		return;

	if (Damage <= 0.f)
		return;

	Internal_SetHealth(Health - Damage, InstigatorActor);
}

void UHellunaHealthComponent::HandleOwnerAnyDamage(
	AActor* DamagedActor,
	float Damage,
	const UDamageType* DamageType,
	AController* InstigatedBy,
	AActor* DamageCauser
)
{
	AActor* Owner = GetOwner();
	if (!Owner || !Owner->HasAuthority())
		return;

	if (bDead)
		return;

	if (Damage <= 0.f)
		return;

	AActor* InstigatorActor = DamageCauser;
	if (!InstigatorActor && InstigatedBy)
	{
		InstigatorActor = InstigatedBy->GetPawn();
	}

	Internal_SetHealth(Health - Damage, InstigatorActor);
}

void UHellunaHealthComponent::Internal_SetHealth(float NewHealth, AActor* InstigatorActor)
{
	const float OldHealth = Health;

	Health = FMath::Clamp(NewHealth, 0.f, MaxHealth);

	if (!FMath::IsNearlyEqual(OldHealth, Health))
	{
		OnHealthChanged.Broadcast(this, OldHealth, Health, InstigatorActor);
	}

	// 죽음 판정 - OnDeath 브로드캐스트만 담당
	// GameMode 통지 / DespawnMassEntity 는 StateTree DeathTask가 담당
	if (!bDead && Health <= 0.f)
	{
		bDead = true;
		HandleDeath(InstigatorActor);
	}
}

void UHellunaHealthComponent::HandleDeath(AActor* KillerActor)
{
	AActor* Owner = GetOwner();
	if (!Owner || !Owner->HasAuthority())
		return;

	// OnDeath 브로드캐스트 → EnemyCharacter::OnMonsterDeath → StateTree Signal
	OnDeath.Broadcast(Owner, KillerActor);

	if (bAutoDestroyOwnerOnDeath)
	{
		Owner->SetLifeSpan(FMath::Max(0.1f, DestroyDelayOnDeath));
	}
}

void UHellunaHealthComponent::OnRep_Health(float OldHealth)
{
	OnHealthChanged.Broadcast(this, OldHealth, Health, nullptr);
}

void UHellunaHealthComponent::OnRep_MaxHealth(float OldMaxHealth)
{
	OnHealthChanged.Broadcast(this, Health, Health, nullptr);
}

void UHellunaHealthComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	DOREPLIFETIME(UHellunaHealthComponent, MaxHealth);
	DOREPLIFETIME(UHellunaHealthComponent, Health);
	DOREPLIFETIME(UHellunaHealthComponent, bDead);
}
