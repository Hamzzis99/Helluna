// Fill out your copyright notice in the Description page of Project Settings.


#include "Character/HellunaEnemyCharacter.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Conponent/EnemyCombatComponent.h"
#include "Engine/AssetManager.h"
#include "DataAsset/DataAsset_EnemyStartUpData.h"
#include "Character/EnemyComponent/HellunaHealthComponent.h"
#include "Kismet/GameplayStatics.h"
#include "GameMode/HellunaDefenseGameMode.h"

#include "DebugHelper.h"


AHellunaEnemyCharacter::AHellunaEnemyCharacter()
{
	AutoPossessAI = EAutoPossessAI::PlacedInWorldOrSpawned;

	bUseControllerRotationPitch = false;
	bUseControllerRotationRoll = false;
	bUseControllerRotationYaw = false;

	GetCharacterMovement()->bUseControllerDesiredRotation = false;
	GetCharacterMovement()->bOrientRotationToMovement = true;
	GetCharacterMovement()->RotationRate = FRotator(0.f, 180.f, 0.f);
	GetCharacterMovement()->MaxWalkSpeed = 300.f;
	GetCharacterMovement()->BrakingDecelerationWalking = 1000.f;

	EnemyCombatComponent = CreateDefaultSubobject<UEnemyCombatComponent>("EnemyCombatComponent");
	HealthComponent = CreateDefaultSubobject<UHellunaHealthComponent>(TEXT("HealthComponent"));
}


void AHellunaEnemyCharacter::PossessedBy(AController* NewController)
{
	Super::PossessedBy(NewController);

	InitEnemyStartUpData();
}

void AHellunaEnemyCharacter::InitEnemyStartUpData()
{
	if (CharacterStartUpData.IsNull())
	{
		return;
	}

	UAssetManager::GetStreamableManager().RequestAsyncLoad(
		CharacterStartUpData.ToSoftObjectPath(),
		FStreamableDelegate::CreateLambda(
			[this]()
			{
				if (UDataAsset_BaseStartUpData* LoadedData = CharacterStartUpData.Get())
				{
					LoadedData->GiveToAbilitySystemComponent(HellunaAbilitySystemComponent);
				}
			}
		)
	);
}

void AHellunaEnemyCharacter::BeginPlay()
{
	Super::BeginPlay();

	Debug::Print(FString::Printf(TEXT("[Enemy] BeginPlay Auth=%d NetMode=%d"),
		HasAuthority() ? 1 : 0, (int32)GetNetMode()));


	if (!HasAuthority()) return;
	
	if (AHellunaDefenseGameMode* GM = Cast<AHellunaDefenseGameMode>(UGameplayStatics::GetGameMode(this)))
	{
		// ✅ 스폰 직후 “살아있는 몬스터”로 등록
		GM->RegisterAliveMonster(this);
	}
		// ✅ 죽음 이벤트를 받아서 GameMode에 “죽었다” 통지
	if (HealthComponent)
	{
		HealthComponent->OnHealthChanged.AddDynamic(this, &AHellunaEnemyCharacter::OnMonsterHealthChanged);
		HealthComponent->OnDeath.RemoveDynamic(this, &ThisClass::OnMonsterDeath);
		HealthComponent->OnDeath.AddUniqueDynamic(this, &ThisClass::OnMonsterDeath);
	}

	Debug::Print(TEXT("[HellunaEnemyCharacter] BeginPlay - Monster Registered"));
};

void AHellunaEnemyCharacter::OnMonsterHealthChanged(
	UActorComponent* MonsterHealthComponent,
	float OldHealth,
	float NewHealth,
	AActor* InstigatorActor
)
{
	// 데미지는 서버에서 처리되고, 클라에 Rep될 때도 OnRep_Health로 델리게이트가 한 번 더 불릴 수 있음
	// “중복 출력” 싫으면 서버에서만 출력
	if (!HasAuthority())
		return;

	const float Delta = OldHealth - NewHealth;

	const FString Msg = FString::Printf(
		TEXT("[MonsterHP] -%.0f"),
		Delta
	);

	Debug::Print(Msg);
}

void AHellunaEnemyCharacter::OnMonsterDeath(AActor* DeadActor, AActor* KillerActor)
{

	// ✅ 서버만 카운트/낮밤 전환을 결정해야 함
	if (!HasAuthority())
		return;

	if (AHellunaDefenseGameMode* GM = Cast<AHellunaDefenseGameMode>(UGameplayStatics::GetGameMode(this)))
	{
		GM->NotifyMonsterDied(this);
	}
}


#include "MassAgentComponent.h"
#include "MassEntitySubsystem.h"
#include "MassEntityManager.h"

void AHellunaEnemyCharacter::DespawnMassEntityOnServer(const TCHAR* Where)
{
	// ✅ 서버에서만 엔티티 제거
	if (!HasAuthority())
		return;

	if (!MassAgentComp)
	{
		MassAgentComp = FindComponentByClass<UMassAgentComponent>();
	}

	if (!MassAgentComp)
	{
		Debug::Print(FString::Printf(TEXT("[MassDespawn] %s | No MassAgentComp"), Where), FColor::Red);
		return;
	}

	// UE 버전에 따라 함수명이 조금씩 다를 수 있음
	const FMassEntityHandle Entity = MassAgentComp->GetEntityHandle();

	if (!Entity.IsValid())
	{
		Debug::Print(FString::Printf(TEXT("[MassDespawn] %s | Invalid EntityHandle"), Where), FColor::Red);
		return;
	}

	UWorld* W = GetWorld();
	if (!W)
		return;

	UMassEntitySubsystem* ES = W->GetSubsystem<UMassEntitySubsystem>();
	if (!ES)
	{
		Debug::Print(FString::Printf(TEXT("[MassDespawn] %s | No MassEntitySubsystem"), Where), FColor::Red);
		return;
	}

	// ✅ 엔티티 자체 제거 (이게 핵심: Actor만 죽이면 다시 생성됨)
	FMassEntityManager& EM = ES->GetMutableEntityManager();
	EM.DestroyEntity(Entity);

	Debug::Print(FString::Printf(TEXT("[MassDespawn] %s | DestroyEntity OK"), Where), FColor::Green);

	// ⚠️ 여기서 Actor Destroy() 하지 마
	// 엔티티가 사라지면 Representation이 정리하면서 Actor도 자연스럽게 정리됨(버전/설정에 따라)
}