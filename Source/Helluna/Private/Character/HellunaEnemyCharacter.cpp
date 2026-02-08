// Fill out your copyright notice in the Description page of Project Settings.


#include "Character/HellunaEnemyCharacter.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Conponent/EnemyCombatComponent.h"
#include "Engine/AssetManager.h"
#include "DataAsset/DataAsset_EnemyStartUpData.h"
#include "Character/EnemyComponent/HellunaHealthComponent.h"
#include "Kismet/GameplayStatics.h"
#include "GameMode/HellunaDefenseGameMode.h"

#include "Components/StateTreeComponent.h"

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

	// 델리게이트 바인딩 (BeginPlay에서 이동)
	// Normal actor: PossessedBy가 BeginPlay 전에 호출됨 → 여기서 바인딩
	// Pool actor: ActivateActor → SpawnDefaultController → PossessedBy → 여기서 바인딩
	if (HealthComponent)
	{
		HealthComponent->OnHealthChanged.AddUniqueDynamic(this, &AHellunaEnemyCharacter::OnMonsterHealthChanged);
		HealthComponent->OnDeath.RemoveDynamic(this, &ThisClass::OnMonsterDeath);
		HealthComponent->OnDeath.AddUniqueDynamic(this, &ThisClass::OnMonsterDeath);
	}
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
	// Pool 생성 상태(Controller 없음)면 StateTree 자동 시작 방지
	// Super::BeginPlay() → Component BeginPlay → StateTreeComponent::StartTree() 호출됨
	// Controller 없으면 Pawn 컨텍스트 못 찾아서 에러 60×3줄 스팸 발생
	// → ActivateActor에서 SpawnDefaultController 후 수동 시작
	if (!GetController())
	{
		if (UStateTreeComponent* STComp = FindComponentByClass<UStateTreeComponent>())
		{
			STComp->SetComponentTickEnabled(false);
		}
	}

	Super::BeginPlay();

	if (!HasAuthority()) return;

	// Pool 생성 상태(Controller 없음)면 GameMode 등록 스킵
	// → ActivateActor에서 등록 예정 (TODO)
	if (!GetController()) return;

	if (AHellunaDefenseGameMode* GM = Cast<AHellunaDefenseGameMode>(UGameplayStatics::GetGameMode(this)))
	{
		GM->RegisterAliveMonster(this);
	}
}

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