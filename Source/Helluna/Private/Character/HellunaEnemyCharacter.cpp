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
#include "Components/AudioComponent.h"
#include "Sound/SoundAttenuation.h"

#include "DebugHelper.h"

// 리플리케이션
#include "Net/UnrealNetwork.h"

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
	
	//GetCharacterMovement()->bUseRVOAvoidance = true;
	//GetCharacterMovement()->AvoidanceConsiderationRadius = 200.f;
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
			
			if (!IsValid(HitActor)) continue;
			if (HitActor == this) continue;

			AHellunaHeroCharacter* HeroCharacter = Cast<AHellunaHeroCharacter>(HitActor);
			AResourceUsingObject_SpaceShip* SpaceShip = Cast<AResourceUsingObject_SpaceShip>(HitActor);

			if (!HeroCharacter && !SpaceShip) continue;

			if (HitActorsThisAttack.Contains(HitActor)) continue;

			HitActorsThisAttack.Add(HitActor);

			// 광폭화 상태이면 공격력 배율 적용
			const float FinalDamage = bEnraged
				? CurrentDamageAmount * EnrageDamageMultiplier
				: CurrentDamageAmount;

			ServerApplyDamage(HitActor, FinalDamage, Hit.Location);
		}
	}
}

// ====================================================================
// 서버 RPC: 데미지 적용
// ====================================================================

void AHellunaEnemyCharacter::ServerApplyDamage_Implementation(AActor* Target, float DamageAmount, 
	const FVector& HitLocation)
{
	if (!HasAuthority()) return;
	if (!IsValid(Target)) return;
	
	// 거리 검증 (안티 치트)
	// 우주선처럼 큰 오브젝트는 중심까지의 거리가 멀어도 표면은 가까울 수 있으므로
	// HitLocation(실제 충돌 지점)과 자신의 거리로 검증한다.
	const float DistanceToHit = FVector::Dist(GetActorLocation(), HitLocation);
	const float MaxAttackDistance = 600.0f;
	
	if (DistanceToHit > MaxAttackDistance)
	{
		UE_LOG(LogTemp, Error, TEXT("[ServerApplyDamage] Hit too far: %.1f cm (max %.1f)"),
			DistanceToHit, MaxAttackDistance);
		return;
	}
	
	UGameplayStatics::ApplyDamage(Target, DamageAmount, GetController(), this, UDamageType::StaticClass());
	UE_LOG(LogTemp, Log, TEXT("[Damage] %.1f -> %s"), DamageAmount, *GetNameSafe(Target));
	MulticastPlayEffect(HitLocation, HitNiagaraEffect, HitEffectScale, true);
}

bool AHellunaEnemyCharacter::ServerApplyDamage_Validate(AActor* Target, float DamageAmount, 
	const FVector& HitLocation)
{
	if (DamageAmount < 0.0f || DamageAmount > 1000.0f) return false;
	if (!IsValid(Target)) return false;

	// HitLocation 기준 거리 검증 (우주선 등 큰 오브젝트 대응)
	const float DistanceToHit = FVector::Dist(GetActorLocation(), HitLocation);
	if (DistanceToHit > 600.0f)
	{
		UE_LOG(LogTemp, Warning, TEXT("[Validate] Hit too far: %.1f cm"), DistanceToHit);
		return false;
	}
	
	return true;
}

// ====================================================================
// Multicast RPC: 히트 이펙트
// ====================================================================

void AHellunaEnemyCharacter::MulticastPlayEffect_Implementation(
	const FVector& SpawnLocation, UNiagaraSystem* Effect, float EffectScale, bool bPlaySound)
{
	if (Effect)
	{
		UNiagaraFunctionLibrary::SpawnSystemAtLocation(
			GetWorld(),
			Effect,
			SpawnLocation,
			FRotator::ZeroRotator,
			FVector(EffectScale),
			true,
			true,
			ENCPoolMethod::None,
			true
		);
	}

	if (bPlaySound && HitSound)
	{
		if (UAudioComponent* AudioComp = UGameplayStatics::SpawnSoundAtLocation(
			this, HitSound, SpawnLocation))
		{
			if (HitSoundAttenuation)
			{
				AudioComp->AttenuationSettings = HitSoundAttenuation;
			}
		}
	}
}

void AHellunaEnemyCharacter::LockMovementAndFaceTarget(AActor* TargetActor)
{
	if (!HasAuthority()) return;

	UCharacterMovementComponent* MoveComp = GetCharacterMovement();
	if (!MoveComp) return;

	// 이동 잠금: MaxWalkSpeed를 0으로 설정
	// 중복 호출을 대비해 이미 잠긴 상태면 SavedMaxWalkSpeed를 덮어쓰지 않는다.
	if (!bMovementLocked)
	{
		SavedMaxWalkSpeed = MoveComp->MaxWalkSpeed;
		bMovementLocked = true;
	}
	MoveComp->MaxWalkSpeed = 0.f;
}

void AHellunaEnemyCharacter::UnlockMovement()
{
	if (!HasAuthority()) return;
	if (!bMovementLocked) return;

	UCharacterMovementComponent* MoveComp = GetCharacterMovement();
	if (!MoveComp) return;

	// MaxWalkSpeed를 잠금 전 원래 값으로 복원
	MoveComp->MaxWalkSpeed = SavedMaxWalkSpeed;
	bMovementLocked = false;
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
		// ★ 공격 몽타주 시작 시 호출
		// 서버는 카메라에서 멀리 있으면 VisibilityBasedAnimTickOption에 의해
		// 애니메이션 업데이트가 생략될 수 있다.
		// 소켓 위치 기반 AttackTrace가 정확하려면 본(Bone) 위치가 매 프레임 갱신되어야
		// 하므로, 공격 중에는 강제로 AlwaysTickPoseAndRefreshBones로 전환한다.
		// URO(Update Rate Optimization)도 함께 비활성화해 프레임 스킵을 방지한다.
		if (!bPoseTickSaved)
		{
			// 원래 설정 저장 (공격 종료 시 복원용)
			SavedAnimTickOption = M->VisibilityBasedAnimTickOption;
			bSavedURO = M->bEnableUpdateRateOptimizations;
			bPoseTickSaved = true;
		}

		M->VisibilityBasedAnimTickOption = EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones;
		M->bEnableUpdateRateOptimizations = false;
	}
	else
	{
		// ★ 공격 몽타주 종료 시 호출 (EndAbility에서 호출)
		// 저장해둔 원래 설정으로 복원해 불필요한 CPU 사용을 줄인다.
		// bPoseTickSaved가 false면 Enable이 호출된 적 없는 것이므로 복원 불필요.
		if (!bPoseTickSaved)
			return;

		M->VisibilityBasedAnimTickOption = SavedAnimTickOption;
		M->bEnableUpdateRateOptimizations = bSavedURO;
		bPoseTickSaved = false;
	}
}

// =========================================================
// 리플리케이션 등록
// =========================================================

void AHellunaEnemyCharacter::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(AHellunaEnemyCharacter, bEnraged);
}

// =========================================================
// 광폭화 진입 (서버 전용)
// =========================================================

void AHellunaEnemyCharacter::EnterEnraged()
{
	if (!HasAuthority()) return;

	if (bEnraged) return;

	bEnraged = true;

	// 진행 중인 공격 몽타주 즉시 중단 (광폭화 몽타주 재생 전에 정리)
	StopAttackTrace();
	if (USkeletalMeshComponent* SkelMesh = GetMesh())
	{
		if (UAnimInstance* AnimInst = SkelMesh->GetAnimInstance())
		{
			if (AttackMontage && AnimInst->Montage_IsPlaying(AttackMontage))
			{
				AnimInst->Montage_Stop(0.1f, AttackMontage);
			}
		}
	}

	if (UCharacterMovementComponent* MoveComp = GetCharacterMovement())
	{
		// 광폭화 배율은 이동 잠금 여부와 무관하게 기준 속도에 곱해야 한다.
		// LockMovementAndFaceTarget()이 SavedMaxWalkSpeed를 덮어쓸 수 있으므로
		// 광폭화 기준이 되는 속도를 별도로 계산한다.
		// bMovementLocked 중이면 SavedMaxWalkSpeed가 잠금 전 실제 속도를 담고 있음
		const float BaseSpeed = bMovementLocked ? SavedMaxWalkSpeed : MoveComp->MaxWalkSpeed;
		const float EnragedSpeed = BaseSpeed * EnrageMoveSpeedMultiplier;

		// SavedMaxWalkSpeed를 광폭화 속도로 갱신해두면
		// UnlockMovement() 복원 시에도 광폭화 속도가 유지된다.
		SavedMaxWalkSpeed = EnragedSpeed;

		if (!bMovementLocked)
		{
			MoveComp->MaxWalkSpeed = EnragedSpeed;
		}
		// bMovementLocked == true 이면 UnlockMovement()에서 SavedMaxWalkSpeed로 복원할 때 자동 적용
	}

	// 광폭화 몽타주 재생 + 완료 델리게이트 바인딩
	if (EnrageMontage)
	{
		// 완료 콜백은 서버에서만 바인딩 (델리게이트 → STTask_Enrage에 알림)
		if (USkeletalMeshComponent* SkelMesh = GetMesh())
		{
			if (UAnimInstance* AnimInst = SkelMesh->GetAnimInstance())
			{
				// 중복 바인딩 방지
				AnimInst->OnMontageEnded.RemoveDynamic(this, &AHellunaEnemyCharacter::OnEnrageMontageEnded);
				AnimInst->OnMontageEnded.AddDynamic(this, &AHellunaEnemyCharacter::OnEnrageMontageEnded);
			}
		}
		Multicast_PlayEnrage();
	}
	else
	{
		// 몽타주 없으면 즉시 완료 신호
		OnEnrageMontageFinished.ExecuteIfBound();
	}

	// 광폭화 VFX (액터 위치 기준)
	MulticastPlayEffect(GetActorLocation(), EnrageNiagaraEffect, EnrageEffectScale, false);

	Debug::Print(
		FString::Printf(TEXT("[Enrage] ★ %s 광폭화 진입! Speed x%.1f / Damage x%.1f / Cooldown x%.1f"),
			*GetName(),
			EnrageMoveSpeedMultiplier,
			EnrageDamageMultiplier,
			EnrageCooldownMultiplier),
		FColor::Red
	);

	UE_LOG(LogTemp, Log, TEXT("[Enrage] %s 광폭화 진입: MoveSpeed x%.1f, Damage x%.1f, CooldownMult x%.1f"),
		*GetName(), EnrageMoveSpeedMultiplier, EnrageDamageMultiplier, EnrageCooldownMultiplier);
}

// =========================================================
// 광폭화 몽타주 멀티캐스트
// =========================================================

void AHellunaEnemyCharacter::Multicast_PlayEnrage_Implementation()
{
	if (!EnrageMontage) return;

	USkeletalMeshComponent* SkelMesh = GetMesh();
	if (!SkelMesh) return;

	UAnimInstance* AnimInst = SkelMesh->GetAnimInstance();
	if (!AnimInst) return;

	AnimInst->Montage_Play(EnrageMontage);
}

void AHellunaEnemyCharacter::OnEnrageMontageEnded(UAnimMontage* Montage, bool bInterrupted)
{
	if (Montage != EnrageMontage) return;

	// 바인딩 해제 (1회용)
	if (USkeletalMeshComponent* SkelMesh = GetMesh())
	{
		if (UAnimInstance* AnimInst = SkelMesh->GetAnimInstance())
		{
			AnimInst->OnMontageEnded.RemoveDynamic(this, &AHellunaEnemyCharacter::OnEnrageMontageEnded);
		}
	}

	// STTask_Enrage에 완료 알림 → Tick에서 Succeeded 반환
	OnEnrageMontageFinished.ExecuteIfBound();
}
