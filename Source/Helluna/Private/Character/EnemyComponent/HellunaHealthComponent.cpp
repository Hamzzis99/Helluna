// Capstone Project Helluna

#include "Character/EnemyComponent/HellunaHealthComponent.h"

#include "GameFramework/Actor.h"
#include "Net/UnrealNetwork.h"
#include "TimerManager.h"
#include "Kismet/GameplayStatics.h"

#include "Helluna.h"
#include "DebugHelper.h"
#include "AbilitySystem/HeroAbility/HeroGameplayAbility_GunParry.h"
#include "Character/HellunaEnemyCharacter.h"
#include "Character/HellunaHeroCharacter.h"

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

	if (bDead || bDowned)
		return;

	Internal_SetHealth(NewHealth, nullptr);
}

void UHellunaHealthComponent::Heal(float Amount, AActor* InstigatorActor)
{
	AActor* Owner = GetOwner();
	if (!Owner || !Owner->HasAuthority())
		return;

	if (bDead || bDowned)
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
	{
#if HELLUNA_DEBUG_ENEMY
		UE_LOG(LogTemp, Warning, TEXT("[DeathDiag][DirectDamageIgnored] Owner=%s Damage=%.1f Reason=AlreadyDead"),
			*GetNameSafe(Owner), Damage);
#endif
		return;
	}

	if (Damage <= 0.f)
		return;

	// [GunParry] 무적 상태면 데미지 무시
	if (UHeroGameplayAbility_GunParry::ShouldBlockDamage(Owner))
	{
#if HELLUNA_DEBUG_ENEMY
		UE_LOG(LogTemp, Warning, TEXT("[ParryDiag][DirectDamageBlocked] Owner=%s Damage=%.1f Reason=ShouldBlockDamage"),
			*GetNameSafe(Owner), Damage);
#endif
		return;
	}

	// [Downed] 다운 상태면 출혈 가속
	if (bDowned)
	{
		ApplyBleedoutDamage(Damage);
		return;
	}

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
	{
#if HELLUNA_DEBUG_ENEMY
		UE_LOG(LogTemp, Warning,
			TEXT("[DeathDiag][TakeAnyDamageIgnored] Owner=%s Damage=%.1f DamageCauser=%s Reason=AlreadyDead"),
			*GetNameSafe(Owner), Damage, *GetNameSafe(DamageCauser));
#endif
		return;
	}

	if (Damage <= 0.f)
	{
#if HELLUNA_DEBUG_ENEMY
		UE_LOG(LogTemp, Warning,
			TEXT("[DamageDiag][TakeAnyDamageIgnored] Owner=%s Damage=%.1f DamageCauser=%s Reason=NonPositive"),
			*GetNameSafe(Owner), Damage, *GetNameSafe(DamageCauser));
#endif
		return;
	}

	AActor* InstigatorActor = DamageCauser;
	if (!InstigatorActor && InstigatedBy)
	{
		InstigatorActor = InstigatedBy->GetPawn();
	}

	// [Downed] 다운 상태면 출혈 가속
	if (bDowned)
	{
#if HELLUNA_DEBUG_ENEMY
		UE_LOG(LogTemp, Warning,
			TEXT("[DeathDiag][TakeAnyDamageBleedout] Owner=%s Damage=%.1f DamageCauser=%s BleedoutRemaining=%.1f"),
			*GetNameSafe(Owner), Damage, *GetNameSafe(InstigatorActor), BleedoutTimeRemaining);
#endif
		ApplyBleedoutDamage(Damage);
		return;
	}

	Internal_SetHealth(Health - Damage, InstigatorActor);
}

void UHellunaHealthComponent::Internal_SetHealth(float NewHealth, AActor* InstigatorActor)
{
	const float OldHealth = Health;

	Health = FMath::Clamp(NewHealth, 0.f, MaxHealth);

#if HELLUNA_DEBUG_ENEMY
	UE_LOG(LogTemp, Warning,
		TEXT("[DeathDiag][HealthSet] Owner=%s Old=%.1f Requested=%.1f New=%.1f Instigator=%s Dead=%s Downed=%s"),
		*GetNameSafe(GetOwner()),
		OldHealth,
		NewHealth,
		Health,
		*GetNameSafe(InstigatorActor),
		bDead ? TEXT("Y") : TEXT("N"),
		bDowned ? TEXT("Y") : TEXT("N"));
#endif

	if (!FMath::IsNearlyEqual(OldHealth, Health))
	{
		OnHealthChanged.Broadcast(this, OldHealth, Health, InstigatorActor);
	}

	// 죽음 판정 - OnDeath 브로드캐스트만 담당
	// GameMode 통지 / DespawnMassEntity 는 StateTree DeathTask가 담당
	if (!bDead && Health <= 0.f)
	{
		AActor* Owner = GetOwner();
#if HELLUNA_DEBUG_ENEMY
		UE_LOG(LogTemp, Warning,
			TEXT("[DeathDiag][HealthZero] Owner=%s Instigator=%s IsHero=%s"),
			*GetNameSafe(Owner),
			*GetNameSafe(InstigatorActor),
			(Owner && Owner->IsA(AHellunaHeroCharacter::StaticClass())) ? TEXT("Y") : TEXT("N"));
#endif

		// [Downed] HeroCharacter + 아직 다운 아님 → 다운 진입
		if (!bDowned && Owner && Owner->IsA(AHellunaHeroCharacter::StaticClass()))
		{
			Health = 1.f;  // 다운은 "살아있지만 행동불능" — HP 1 유지
			bDowned = true;
			BleedoutTimeRemaining = BleedoutDuration;

			UE_LOG(LogHelluna, Warning, TEXT("[Phase21-Debug] HealthComp: 다운 진입! %s | HP=1, BleedoutDuration=%.1f"),
				*GetNameSafe(Owner), BleedoutDuration);

			// 1초 간격 출혈 타이머 시작
			if (UWorld* World = GetWorld())
			{
				World->GetTimerManager().SetTimer(
					BleedoutTimerHandle, this, &ThisClass::TickBleedout, 1.f, true);
			}

			UE_LOG(LogHelluna, Warning, TEXT("[Phase21-Debug] HealthComp: OnDowned.Broadcast 호출 직전"));
			OnDowned.Broadcast(Owner, InstigatorActor);
			UE_LOG(LogHelluna, Warning, TEXT("[Phase21-Debug] HealthComp: OnDowned.Broadcast 완료"));
			return;  // bDead로 가지 않음
		}

		bDead = true;
		HandleDeath(InstigatorActor);
	}
}

void UHellunaHealthComponent::HandleDeath(AActor* KillerActor)
{
	AActor* Owner = GetOwner();
	if (!Owner || !Owner->HasAuthority())
		return;

#if HELLUNA_DEBUG_ENEMY
	UE_LOG(LogTemp, Warning,
		TEXT("[DeathDiag][HandleDeath] Owner=%s Killer=%s Dead=%s Downed=%s AutoDestroy=%s"),
		*GetNameSafe(Owner),
		*GetNameSafe(KillerActor),
		bDead ? TEXT("Y") : TEXT("N"),
		bDowned ? TEXT("Y") : TEXT("N"),
		bAutoDestroyOwnerOnDeath ? TEXT("Y") : TEXT("N"));
#endif

	// [GunParry] 처형 중이면 사망 보류 (OnDeath도 안 함)
	if (UHeroGameplayAbility_GunParry::ShouldDeferDeath(Owner))
	{
		if (AHellunaEnemyCharacter* EnemyChar = Cast<AHellunaEnemyCharacter>(Owner))
			EnemyChar->bParryDeferredDeath = true;
#if HELLUNA_DEBUG_ENEMY
		UE_LOG(LogTemp, Warning,
			TEXT("[ParryDiag][DeathDeferred] Owner=%s Killer=%s Reason=AnimLocked"),
			*GetNameSafe(Owner),
			*GetNameSafe(KillerActor));
#endif
		return;
	}

	// OnDeath 브로드캐스트 → EnemyCharacter::OnMonsterDeath → StateTree Signal
#if HELLUNA_DEBUG_ENEMY
	UE_LOG(LogTemp, Warning,
		TEXT("[DeathDiag][BroadcastOnDeath] Owner=%s Killer=%s"),
		*GetNameSafe(Owner),
		*GetNameSafe(KillerActor));
#endif
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

// =========================================================
// Downed/Revive System
// =========================================================

void UHellunaHealthComponent::ApplyBleedoutDamage(float Damage)
{
	AActor* Owner = GetOwner();
	if (!Owner || !Owner->HasAuthority()) return;
	if (!bDowned || bDead) return;

	// reduction = (Damage / MaxHealth) * BleedoutDuration
	const float Reduction = (MaxHealth > 0.f)
		? (Damage / MaxHealth) * BleedoutDuration
		: 0.f;

	BleedoutTimeRemaining = FMath::Max(0.f, BleedoutTimeRemaining - Reduction);

	UE_LOG(LogHelluna, Log, TEXT("[Downed] %s 출혈 가속: -%.1f초 (잔여 %.1f초)"),
		*GetNameSafe(Owner), Reduction, BleedoutTimeRemaining);

	if (BleedoutTimeRemaining <= 0.f)
	{
		ForceKillFromDowned();
	}
}

void UHellunaHealthComponent::TickBleedout()
{
	AActor* Owner = GetOwner();
	if (!Owner || !Owner->HasAuthority()) return;
	if (!bDowned || bDead) return;

	BleedoutTimeRemaining -= 1.f;

	if (BleedoutTimeRemaining <= 0.f)
	{
		BleedoutTimeRemaining = 0.f;
		UE_LOG(LogHelluna, Warning, TEXT("[Downed] %s 출혈 만료 → 사망"), *GetNameSafe(Owner));
		ForceKillFromDowned();
	}
}

void UHellunaHealthComponent::ForceKillFromDowned()
{
	AActor* Owner = GetOwner();
	if (!Owner || !Owner->HasAuthority()) return;
	if (!bDowned) return;

	UE_LOG(LogHelluna, Warning, TEXT("[Phase21-Debug] ForceKillFromDowned 호출: %s | BleedoutRemaining=%.1f"),
		*GetNameSafe(Owner), BleedoutTimeRemaining);

	// 출혈 타이머 정리
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(BleedoutTimerHandle);
	}

	bDowned = false;
	BleedoutTimeRemaining = 0.f;
	Health = 0.f;
	bDead = true;

	UE_LOG(LogHelluna, Log, TEXT("[Downed] %s → ForceKillFromDowned → HandleDeath"), *GetNameSafe(Owner));

	HandleDeath(nullptr);
}

void UHellunaHealthComponent::Revive(float InReviveHealthPercent)
{
	AActor* Owner = GetOwner();
	if (!Owner || !Owner->HasAuthority()) return;
	if (!bDowned || bDead) return;

	// 출혈 타이머 정리
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(BleedoutTimerHandle);
	}

	bDowned = false;
	BleedoutTimeRemaining = 0.f;

	const float NewHealth = FMath::Clamp(MaxHealth * InReviveHealthPercent, 1.f, MaxHealth);
	Internal_SetHealth(NewHealth, nullptr);

	UE_LOG(LogHelluna, Log, TEXT("[Revive] %s 부활 완료 (HP: %.1f)"), *GetNameSafe(Owner), NewHealth);
}

void UHellunaHealthComponent::OnRep_Downed()
{
	// Multicast RPC로 비주얼 처리하므로 여기서는 별도 작업 없음
}

void UHellunaHealthComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	DOREPLIFETIME(UHellunaHealthComponent, MaxHealth);
	DOREPLIFETIME(UHellunaHealthComponent, Health);
	DOREPLIFETIME(UHellunaHealthComponent, bDead);
	DOREPLIFETIME(UHellunaHealthComponent, bDowned);
	DOREPLIFETIME(UHellunaHealthComponent, BleedoutTimeRemaining);
}
