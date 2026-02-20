// Fill out your copyright notice in the Description page of Project Settings.


#include "Character/HellunaEnemyCharacter.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Conponent/EnemyCombatComponent.h"
#include "Engine/AssetManager.h"
#include "DataAsset/DataAsset_EnemyStartUpData.h"
#include "Character/EnemyComponent/HellunaHealthComponent.h"
#include "Kismet/GameplayStatics.h"
#include "GameMode/HellunaDefenseGameMode.h"
#include "Animation/AnimInstance.h"

#include "Components/StateTreeComponent.h"
#include "GameplayTagContainer.h"
#include "AIController.h"

#include "DebugHelper.h"

// ✅ 타이머 기반 Trace 시스템용 헤더
#include "DrawDebugHelpers.h"
#include "GameFramework/DamageType.h"
#include "Character/HellunaHeroCharacter.h"

// ecs 추가
#include "DrawDebugHelpers.h"
#include "MassAgentComponent.h"
#include "MassEntitySubsystem.h"
#include "MassEntityManager.h"
#include "Object/ResourceUsingObject/ResourceUsingObject_SpaceShip.h"
#include "NiagaraFunctionLibrary.h"
#include "NiagaraSystem.h"

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

	// =========================================================
	// ✅ 기존 물리 충돌 컴포넌트 제거됨
	// 타이머 기반 Trace 시스템으로 교체 (성능 12배 향상)
	// =========================================================

	// === Animation URO (Update Rate Optimization) ===
	// 프로파일링 결과: 애니메이션이 Game Thread의 ~40% 점유 (~10ms/24.88ms)
	// URO는 거리/가시성에 따라 애니메이션 업데이트 빈도를 자동 조절한다.
	// 가까운 적 = 매 프레임, 먼 적 = 4~8프레임마다 → CPU 절감 ~5ms
	// @author 김기현
	USkeletalMeshComponent* SkelMesh = GetMesh();
	if (SkelMesh)
	{
		SkelMesh->VisibilityBasedAnimTickOption = EVisibilityBasedAnimTickOption::OnlyTickMontagesWhenNotRendered;
		SkelMesh->bEnableUpdateRateOptimizations = true;
		SkelMesh->bDisplayDebugUpdateRateOptimizations = false;
	}
	
	GetCharacterMovement()->bUseRVOAvoidance = true;
	GetCharacterMovement()->AvoidanceConsiderationRadius = 200.f;
}


void AHellunaEnemyCharacter::PossessedBy(AController* NewController)
{
	Super::PossessedBy(NewController);

	InitEnemyStartUpData();

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
	if (!GetController())
	{
		if (UStateTreeComponent* STComp = FindComponentByClass<UStateTreeComponent>())
		{
			STComp->SetComponentTickEnabled(false);
		}
	}

	Super::BeginPlay();

	if (!HasAuthority()) return;

	if (!GetController()) return;

	if (AHellunaDefenseGameMode* GM = Cast<AHellunaDefenseGameMode>(UGameplayStatics::GetGameMode(this)))
	{
		GM->RegisterAliveMonster(this);
	}
	if (HealthComponent)
	{
		HealthComponent->OnHealthChanged.AddDynamic(this, &AHellunaEnemyCharacter::OnMonsterHealthChanged);
		HealthComponent->OnDeath.RemoveDynamic(this, &ThisClass::OnMonsterDeath);
		HealthComponent->OnDeath.AddUniqueDynamic(this, &ThisClass::OnMonsterDeath);
	}
}


void AHellunaEnemyCharacter::OnMonsterHealthChanged(
	UActorComponent* MonsterHealthComponent,
	float OldHealth,
	float NewHealth,
	AActor* InstigatorActor
)
{
	if (!HasAuthority())
		return;

	// 디버그: 체력 변화량 출력
	const float Delta = OldHealth - NewHealth;
	if (Delta > 0.f)
	{
		Debug::Print(FString::Printf(TEXT("[MonsterHP] %s: -%.1f (%.1f → %.1f)"),
			*GetName(), Delta, OldHealth, NewHealth), FColor::Red);
	}

	// 피격 애니메이션 (데미지를 받았고 아직 살아있을 때만)
	if (Delta > 0.f && NewHealth > 0.f && HitReactMontage)
	{
		Multicast_PlayHitReact();
	}
}


// =========================================================
// ★ 추가: 피격 몽타주 멀티캐스트
// =========================================================
void AHellunaEnemyCharacter::Multicast_PlayHitReact_Implementation()
{
	if (!HitReactMontage) return;

	USkeletalMeshComponent* SkelMesh = GetMesh();
	if (!SkelMesh) return;

	UAnimInstance* AnimInst = SkelMesh->GetAnimInstance();
	if (!AnimInst) return;

	AnimInst->Montage_Play(HitReactMontage);
}


// =========================================================
// ★ 추가: 사망 몽타주 멀티캐스트
// =========================================================
void AHellunaEnemyCharacter::Multicast_PlayDeath_Implementation()
{
	if (!DeathMontage) return;

	USkeletalMeshComponent* SkelMesh = GetMesh();
	if (!SkelMesh) return;

	UAnimInstance* AnimInst = SkelMesh->GetAnimInstance();
	if (!AnimInst) return;

	AnimInst->Montage_Play(DeathMontage);
}

void AHellunaEnemyCharacter::OnDeathMontageEnded(UAnimMontage* Montage, bool bInterrupted)
{
	// GA_Death의 PlayMontageAndWait에서 완료를 처리하므로 여기선 사용 안 함
	// OnDeathMontageFinished는 GA_Death::HandleDeathFinished에서 직접 호출
}


void AHellunaEnemyCharacter::UpdateAnimationLOD(float DistanceToCamera)
{
	USkeletalMeshComponent* SkelMesh = GetMesh();
	if (!SkelMesh) return;

	const float NearDist = 2000.f;
	const float MidDist = 4000.f;

	if (DistanceToCamera < NearDist)
	{
		SkelMesh->bNoSkeletonUpdate = false;
		SkelMesh->SetCastShadow(true);
	}
	else if (DistanceToCamera < MidDist)
	{
		SkelMesh->bNoSkeletonUpdate = false;
		SkelMesh->SetCastShadow(false);
	}
	else
	{
		SkelMesh->bNoSkeletonUpdate = false;
		SkelMesh->SetCastShadow(false);
	}
}

void AHellunaEnemyCharacter::TestDamage(AActor* DamagedActor, float DamageAmount)
{
	if (!DamagedActor) return;

	Debug::Print(
		FString::Printf(TEXT("[TestDamage] %s -> %s | %.1f DMG"),
			*GetName(), *DamagedActor->GetName(), DamageAmount),
		FColor::Orange
	);
}

void AHellunaEnemyCharacter::OnMonsterDeath(AActor* DeadActor, AActor* KillerActor)
{
	if (!HasAuthority())
		return;

	AAIController* AIC = Cast<AAIController>(GetController());
	if (!AIC) return;

	UStateTreeComponent* STComp = AIC->FindComponentByClass<UStateTreeComponent>();
	if (!STComp) return;

	FGameplayTag DeathTag = FGameplayTag::RequestGameplayTag(FName("Enemy.State.Death"), false);
	if (DeathTag.IsValid())
	{
		STComp->SendStateTreeEvent(DeathTag);
	}
}




void AHellunaEnemyCharacter::DespawnMassEntityOnServer(const TCHAR* Where)
{
	if (!HasAuthority())
		return;

	if (!MassAgentComp)
	{
		MassAgentComp = FindComponentByClass<UMassAgentComponent>();
	}

	if (!MassAgentComp)
	{
		Destroy();
		return;
	}

	const FMassEntityHandle Entity = MassAgentComp->GetEntityHandle();

	if (!Entity.IsValid())
	{
		Destroy();
		return;
	}

	UWorld* W = GetWorld();
	if (!W)
		return;

	UMassEntitySubsystem* ES = W->GetSubsystem<UMassEntitySubsystem>();
	if (!ES)
		return;

	FMassEntityManager& EM = ES->GetMutableEntityManager();
	EM.DestroyEntity(Entity);
}

// =========================================================
// 공격 트레이스 시스템 구현 (타이머 기반)
// =========================================================


void AHellunaEnemyCharacter::StartAttackTrace(FName SocketName, float Radius, float Interval, 
	float DamageAmount, bool bDebugDraw)
{
	// 이미 트레이스 중이면 먼저 정지
	StopAttackTrace();
	
	// 설정 저장
	CurrentTraceSocketName = SocketName;
	CurrentTraceRadius = Radius;
	CurrentDamageAmount = DamageAmount;
	bDrawDebugTrace = bDebugDraw;
	
	// 히트 대상 리스트 초기화
	HitActorsThisAttack.Empty();
	
	// 타이머 시작
	GetWorldTimerManager().SetTimer(
		AttackTraceTimerHandle,
		this,
		&AHellunaEnemyCharacter::PerformAttackTrace,
		Interval,
		true // 반복
	);
}

void AHellunaEnemyCharacter::StopAttackTrace()
{
	if (AttackTraceTimerHandle.IsValid())
	{
		GetWorldTimerManager().ClearTimer(AttackTraceTimerHandle);
		AttackTraceTimerHandle.Invalidate();
		
		// 히트 대상 리스트 초기화
		HitActorsThisAttack.Empty();
	}
}

void AHellunaEnemyCharacter::PerformAttackTrace()
{
	if (!HasAuthority())
	{
		return;
	}
	// Null 체크
	if (!GetMesh())
	{
		UE_LOG(LogTemp, Error, TEXT("[AttackTrace] %s: Mesh is null"), *GetName());
		StopAttackTrace();
		return;
	}
	
	UWorld* World = GetWorld();
	if (!World)
	{
		UE_LOG(LogTemp, Error, TEXT("[AttackTrace] %s: World is null"), *GetName());
		StopAttackTrace();
		return;
	}
	
	// 소켓 위치 가져오기
	FVector SocketLocation = GetMesh()->GetSocketLocation(CurrentTraceSocketName);
	if (SocketLocation.IsNearlyZero())
	{
		UE_LOG(LogTemp, Warning, TEXT("[AttackTrace] %s: Socket '%s' not found or invalid location"), 
			*GetName(), *CurrentTraceSocketName.ToString());
		return;
	}
	
	// Sphere Trace 수행
	TArray<FHitResult> HitResults;
	FCollisionShape SphereShape = FCollisionShape::MakeSphere(CurrentTraceRadius);
	
	FCollisionQueryParams QueryParams;
	QueryParams.AddIgnoredActor(this); // 자신 제외
	QueryParams.bTraceComplex = false; // 단순 충돌만 (성능 최적화)
	QueryParams.bReturnPhysicalMaterial = false;
	QueryParams.bReturnFaceIndex = false;
	
	// ECC_Pawn 채널로 트레이스 (플레이어 감지)
	bool bHit = World->SweepMultiByChannel(
		HitResults,
		SocketLocation,
		SocketLocation, // 시작과 끝이 같음 (제자리 Sphere)
		FQuat::Identity,
		ECC_Pawn,
		SphereShape,
		QueryParams
	);
	
	// 디버그 드로우
	if (bDrawDebugTrace)
	{
		DrawDebugSphere(
			World,
			SocketLocation,
			CurrentTraceRadius,
			12,
			bHit ? FColor::Red : FColor::Green,
			false,
			0.1f,
			0,
			2.0f
		);
	}
	
	// 히트 처리
	if (bHit)
	{
		for (const FHitResult& Hit : HitResults)
		{
			AActor* HitActor = Hit.GetActor();
			
			// Null 체크
			if (!IsValid(HitActor))
			{
				continue;
			}
			
			// 이미 맞은 대상 제외
			if (HitActorsThisAttack.Contains(HitActor))
			{
				continue;
			}
			
			// 자신 제외
			if (HitActor == this)
			{
				continue;
			}
			
			// 플레이어 캐릭터만 감지
			AHellunaHeroCharacter* HeroCharacter = Cast<AHellunaHeroCharacter>(HitActor);
			AResourceUsingObject_SpaceShip* SpaceShip = Cast<AResourceUsingObject_SpaceShip>(HitActor);

			if (!HeroCharacter && !SpaceShip)
			continue;

			// 히트 대상 기록
			HitActorsThisAttack.Add(HitActor);
			
			if (HasAuthority())
			{
				ServerApplyDamage(HitActor, CurrentDamageAmount, Hit.Location);
			}
			
			StopAttackTrace();
			return;
		}
	}
}

// ====================================================================
// 서버 RPC: 데미지 적용
// ====================================================================

void AHellunaEnemyCharacter::ServerApplyDamage_Implementation(AActor* Target, float DamageAmount, 
	const FVector& HitLocation)
{
	if (!HasAuthority())
		return;
	
	if (!IsValid(Target))
		return;
	
	// 거리 검증 (안티 치트)
	const float Distance = FVector::Dist(GetActorLocation(), Target->GetActorLocation());
	const float MaxAttackDistance = 500.0f;
	
	if (Distance > MaxAttackDistance)
		return;
	
	// 데미지 적용
	UGameplayStatics::ApplyDamage(
		Target,
		DamageAmount,
		GetController(),
		this,
		UDamageType::StaticClass()
	);

	UE_LOG(LogTemp, Log, TEXT("[Damage] %.1f -> %s"), DamageAmount, *GetNameSafe(Target));
	
	MulticastPlayHitEffect(HitLocation, HitEffectScale);
}

bool AHellunaEnemyCharacter::ServerApplyDamage_Validate(AActor* Target, float DamageAmount, 
	const FVector& HitLocation)
{
	// 데미지 범위 검증
	if (DamageAmount < 0.0f || DamageAmount > 1000.0f)
	{
		UE_LOG(LogTemp, Error, TEXT("[ServerApplyDamage_Validate] Invalid damage: %.1f"), DamageAmount);
		return false;
	}
	
	// 타겟 유효성 검증
	if (!IsValid(Target))
	{
		return false;
	}
	
	// 거리 검증
	float Distance = FVector::Dist(GetActorLocation(), Target->GetActorLocation());
	if (Distance > 500.0f)
	{
		UE_LOG(LogTemp, Warning, TEXT("[ServerApplyDamage_Validate] Target too far: %.1f cm"), Distance);
		return false;
	}
	
	return true;
}

// ====================================================================
// Multicast RPC: 히트 이펙트
// ====================================================================

void AHellunaEnemyCharacter::MulticastPlayHitEffect_Implementation(const FVector& HitLocation, float EffectScale)
{
	// 나이아가라 이펙트 재생
	if (HitNiagaraEffect)
	{
		UNiagaraFunctionLibrary::SpawnSystemAtLocation(
			GetWorld(),
			HitNiagaraEffect,
			HitLocation,
			FRotator::ZeroRotator,
			FVector(EffectScale),
			true,
			true,
			ENCPoolMethod::None,
			true
		);
	}

	// 사운드 재생
	if (HitSound)
	{
		UGameplayStatics::PlaySoundAtLocation(
			this,
			HitSound,
			HitLocation
		);
	}
}

void AHellunaEnemyCharacter::SetServerAttackPoseTickEnabled(bool bEnable)
{
	if (!HasAuthority())
		return;

	USkeletalMeshComponent* M = GetMesh();
	if (!M)
		return;

	if (bEnable)
	{
		if (!bPoseTickSaved)
		{
			SavedAnimTickOption = M->VisibilityBasedAnimTickOption;
			bSavedURO = M->bEnableUpdateRateOptimizations;
			bPoseTickSaved = true;
		}

		M->VisibilityBasedAnimTickOption = EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones;
		M->bEnableUpdateRateOptimizations = false;
	}
	else
	{
		if (!bPoseTickSaved)
			return;

		M->VisibilityBasedAnimTickOption = SavedAnimTickOption;
		M->bEnableUpdateRateOptimizations = bSavedURO;
		bPoseTickSaved = false;

	}
}
