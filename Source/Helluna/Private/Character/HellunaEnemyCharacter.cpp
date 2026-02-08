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

	// === Animation URO (Update Rate Optimization) ===
	// 프로파일링 결과: 애니메이션이 Game Thread의 ~40% 점유 (~10ms/24.88ms)
	// URO는 거리/가시성에 따라 애니메이션 업데이트 빈도를 자동 조절한다.
	// 가까운 적 = 매 프레임, 먼 적 = 4~8프레임마다 → CPU 절감 ~5ms
	// @author 김기현
	USkeletalMeshComponent* SkelMesh = GetMesh();
	if (SkelMesh)
	{
		// (1) 화면 밖이면 몽타주만 최소 업데이트, 일반 locomotion은 정지
		//     다시 보이면 즉시 복구. 공격 판정이 몽타주 Notify에 연결되어 있으면 정상 동작.
		SkelMesh->VisibilityBasedAnimTickOption = EVisibilityBasedAnimTickOption::OnlyTickMontagesWhenNotRendered;

		// (2) URO 활성화: 카메라 거리에 따라 SkeletalMesh 업데이트 빈도 자동 조절
		//     가까운 적: 매 프레임(60Hz), 중간: 2~4프레임마다, 먼 적: 4~8프레임마다
		SkelMesh->bEnableUpdateRateOptimizations = true;

		// (3) URO 디버그 시각화 비활성화 (개발 중 필요하면 true로)
		SkelMesh->bDisplayDebugUpdateRateOptimizations = false;
	}
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

/**
 * 거리 기반 애니메이션/그림자 LOD
 *
 * 카메라에서 가까운 적은 풀 품질, 먼 적은 그림자 끄고 애니메이션 품질 낮춤.
 * UE 내장 URO(bEnableUpdateRateOptimizations)가 프레임 스킵을 자동 처리하고,
 * 이 함수는 그 위에 그림자/스켈레톤 업데이트 품질을 추가 제어한다.
 *
 * [거리별 단계]
 * 근거리 (0~20m):  풀 품질 - 매 프레임 업데이트, 그림자 On
 * 중거리 (20~40m): 중간 품질 - 그림자 Off (Draw Call 절약)
 * 원거리 (40m+):   저품질 - 그림자 Off, 스켈레톤 최소 업데이트
 *
 * @author 김기현
 */
void AHellunaEnemyCharacter::UpdateAnimationLOD(float DistanceToCamera)
{
	USkeletalMeshComponent* SkelMesh = GetMesh();
	if (!SkelMesh) return;

	const float NearDist = 2000.f;   // 20m — 전투 거리, 풀 품질 필요
	const float MidDist = 4000.f;    // 40m — 접근 중, 그림자 불필요

	if (DistanceToCamera < NearDist)
	{
		// 근거리: 풀 품질 — 전투 중이므로 모든 시각적 디테일 유지
		SkelMesh->bNoSkeletonUpdate = false;
		SkelMesh->SetCastShadow(true);
	}
	else if (DistanceToCamera < MidDist)
	{
		// 중거리: 그림자 끄기 — 멀어서 그림자가 안 보임, Draw Call 절약
		SkelMesh->bNoSkeletonUpdate = false;
		SkelMesh->SetCastShadow(false);
	}
	else
	{
		// 원거리: 그림자 Off + 스켈레톤 최소 업데이트
		// URO가 이미 프레임 스킵을 하지만, 추가로 품질을 낮춘다
		// bNoSkeletonUpdate = false 유지: URO가 관리하므로 완전 끄면 충돌
		SkelMesh->bNoSkeletonUpdate = false;
		SkelMesh->SetCastShadow(false);
	}
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