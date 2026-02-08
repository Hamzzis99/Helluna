// Capstone Project Helluna


#include "Character/EnemyComponent/HellunaHealthComponent.h"

#include "GameFramework/Actor.h"
#include "Net/UnrealNetwork.h"
#include "TimerManager.h"
#include "GameMode/HellunaDefenseGameMode.h"
#include "Kismet/GameplayStatics.h"
#include "Character/HellunaEnemyCharacter.h"

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

	// ✅ 서버에서만 값 보정 + 데미지 바인딩
	if (Owner->HasAuthority())
	{
		MaxHealth = FMath::Max(1.f, MaxHealth);
		Health = FMath::Clamp(Health, 0.f, MaxHealth);

		bDead = (Health <= 0.f);

		// ApplyDamage/ApplyPointDamage/ApplyRadialDamage 모두 여기로 들어옴
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

void UHellunaHealthComponent::Heal(float Amount, AActor* InstigatorActor /*= nullptr*/)
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

void UHellunaHealthComponent::ApplyDirectDamage(float Damage, AActor* InstigatorActor /*= nullptr*/)
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

	// “누가 때렸는지” 추적용 (가능한 범위에서)
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

	// Clamp
	Health = FMath::Clamp(NewHealth, 0.f, MaxHealth);

	// 변화가 있을 때만 알림
	if (!FMath::IsNearlyEqual(OldHealth, Health))
	{
		OnHealthChanged.Broadcast(this, OldHealth, Health, InstigatorActor);
	}

	// 죽음 판정 (딱 한 번만)
	if (!bDead && Health <= 0.f)
	{
		bDead = true;
		HandleDeath(InstigatorActor);
	}
}

void UHellunaHealthComponent::HandleDeath(AActor* KillerActor)
{
	AActor* Owner = GetOwner();
	if (!Owner)
		return;

	// ✅ 서버에서만 “룰/카운트” 처리
	if (!Owner->HasAuthority())
		return;
	// ✅ (선택) 죽음 이벤트는 1회만
	OnDeath.Broadcast(Owner, KillerActor);

	// ✅ 몬스터일 때만 GameMode에 통지
	if (Cast<AHellunaEnemyCharacter>(Owner))
	{
		if (AHellunaDefenseGameMode* GM = Cast<AHellunaDefenseGameMode>(UGameplayStatics::GetGameMode(Owner)))
		{
			GM->NotifyMonsterDied(Owner);
		}
	}
	if (AHellunaEnemyCharacter* Enemy = Cast<AHellunaEnemyCharacter>(Owner))
	{
		Enemy->DespawnMassEntityOnServer(TEXT("HealthComponent::HandleDeath"));
	}
	// ✅ (옵션) 자동 파괴
	if (bAutoDestroyOwnerOnDeath)
	{
		Owner->SetLifeSpan(FMath::Max(0.1f, DestroyDelayOnDeath));
	}
}

void UHellunaHealthComponent::OnRep_Health(float OldHealth)
{
	// 클라에서 UI 갱신용
	OnHealthChanged.Broadcast(this, OldHealth, Health, nullptr);
}

void UHellunaHealthComponent::OnRep_MaxHealth(float OldMaxHealth)
{
	// MaxHealth가 바뀌면 체력 비율(UI)이 달라질 수 있으니 갱신 트리거 용도
	OnHealthChanged.Broadcast(this, Health, Health, nullptr);
}

void UHellunaHealthComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	DOREPLIFETIME(UHellunaHealthComponent, MaxHealth);
	DOREPLIFETIME(UHellunaHealthComponent, Health);
	DOREPLIFETIME(UHellunaHealthComponent, bDead);
}