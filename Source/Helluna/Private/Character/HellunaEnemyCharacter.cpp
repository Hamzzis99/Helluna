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

// 타이머 기반 Trace 시스템용 헤더
#include "DrawDebugHelpers.h"
#include "GameFramework/DamageType.h"
#include "Character/HellunaHeroCharacter.h"

// ECS 관련
#include "MassAgentComponent.h"
#include "MassEntitySubsystem.h"
#include "MassEntityManager.h"
#include "Object/ResourceUsingObject/ResourceUsingObject_SpaceShip.h"

// 나이아가라
#include "NiagaraFunctionLibrary.h"
#include "NiagaraSystem.h"
#include "NiagaraComponent.h"

// 사운드
#include "Components/AudioComponent.h"
#include "Sound/SoundAttenuation.h"

// 리플리케이션
#include "Net/UnrealNetwork.h"

// ============================================================
// 생성자
// ============================================================
AHellunaEnemyCharacter::AHellunaEnemyCharacter()
{
	AutoPossessAI = EAutoPossessAI::PlacedInWorldOrSpawned;

	bUseControllerRotationPitch = false;
	bUseControllerRotationRoll  = false;
	bUseControllerRotationYaw   = false;

	GetCharacterMovement()->bUseControllerDesiredRotation = false;
	GetCharacterMovement()->bOrientRotationToMovement     = true;
	GetCharacterMovement()->RotationRate                  = FRotator(0.f, 180.f, 0.f);
	GetCharacterMovement()->MaxWalkSpeed                  = 300.f;
	GetCharacterMovement()->BrakingDecelerationWalking    = 1000.f;

	EnemyCombatComponent = CreateDefaultSubobject<UEnemyCombatComponent>("EnemyCombatComponent");
	HealthComponent      = CreateDefaultSubobject<UHellunaHealthComponent>(TEXT("HealthComponent"));

	// === Animation URO (Update Rate Optimization) ===
	// 애니메이션이 Game Thread 의 ~40% 를 차지하므로 URO 를 활성화해서
	// 거리/가시성에 따라 업데이트 빈도를 자동으로 줄인다.
	// @author 김기현
	USkeletalMeshComponent* SkelMesh = GetMesh();
	if (SkelMesh)
	{
		SkelMesh->VisibilityBasedAnimTickOption = EVisibilityBasedAnimTickOption::OnlyTickMontagesWhenNotRendered;
		SkelMesh->bEnableUpdateRateOptimizations        = true;
		SkelMesh->bDisplayDebugUpdateRateOptimizations  = false;
	}
}

// ============================================================
// PossessedBy — AI 컨트롤러가 빙의될 때 어빌리티/체력 초기화
// ============================================================
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

// ============================================================
// InitEnemyStartUpData — DataAsset 비동기 로드 후 GAS 어빌리티 부여
// ============================================================
void AHellunaEnemyCharacter::InitEnemyStartUpData()
{
	if (CharacterStartUpData.IsNull()) return;

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

// ============================================================
// BeginPlay
// ============================================================
void AHellunaEnemyCharacter::BeginPlay()
{
	// 컨트롤러 없이 배치된 경우 StateTree 틱을 꺼서 불필요한 연산 방지
	if (!GetController())
	{
		if (UStateTreeComponent* STComp = FindComponentByClass<UStateTreeComponent>())
		{
			STComp->SetComponentTickEnabled(false);
		}
	}

	Super::BeginPlay();

	if (!HasAuthority()) return;
	if (!GetController())  return;

	// 게임모드에 생존 몬스터로 등록
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

// ============================================================
// OnMonsterHealthChanged — 피격 시 피격 애니메이션 트리거
// ============================================================
void AHellunaEnemyCharacter::OnMonsterHealthChanged(
	UActorComponent* MonsterHealthComponent,
	float OldHealth,
	float NewHealth,
	AActor* InstigatorActor
)
{
	if (!HasAuthority()) return;

	const float Delta = OldHealth - NewHealth;
	// 살아있는 상태에서 데미지를 받았을 때만 피격 애니메이션 재생
	if (Delta > 0.f && NewHealth > 0.f && HitReactMontage)
	{
		Multicast_PlayHitReact();
	}
}

// ============================================================
// Multicast_PlayHitReact — 피격 몽타주 (모든 클라이언트)
// ============================================================
void AHellunaEnemyCharacter::Multicast_PlayHitReact_Implementation()
{
	if (!HitReactMontage) return;

	USkeletalMeshComponent* SkelMesh = GetMesh();
	if (!SkelMesh) return;

	UAnimInstance* AnimInst = SkelMesh->GetAnimInstance();
	if (!AnimInst) return;

	AnimInst->Montage_Play(HitReactMontage);
}

// ============================================================
// Multicast_PlayDeath — 사망 몽타주 (모든 클라이언트)
// ============================================================
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
	// GA_Death 의 PlayMontageAndWait 에서 완료를 처리하므로 여기서는 사용하지 않음
	// OnDeathMontageFinished 는 GA_Death::HandleDeathFinished 에서 직접 호출
}

// ============================================================
// UpdateAnimationLOD — 거리 기반 그림자/스켈레톤 품질 조절
// @author 김기현
// ============================================================
void AHellunaEnemyCharacter::UpdateAnimationLOD(float DistanceToCamera)
{
	USkeletalMeshComponent* SkelMesh = GetMesh();
	if (!SkelMesh) return;

	const float NearDist = 2000.f;
	const float MidDist  = 4000.f;

	if (DistanceToCamera < NearDist)
	{
		// 가까운 거리: 풀 품질
		SkelMesh->bNoSkeletonUpdate = false;
		SkelMesh->SetCastShadow(true);
	}
	else if (DistanceToCamera < MidDist)
	{
		// 중간 거리: 그림자 제거
		SkelMesh->bNoSkeletonUpdate = false;
		SkelMesh->SetCastShadow(false);
	}
	else
	{
		// 먼 거리: 그림자 제거
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

// ============================================================
// OnMonsterDeath — StateTree 에 Death 이벤트 전송
// ============================================================
void AHellunaEnemyCharacter::OnMonsterDeath(AActor* DeadActor, AActor* KillerActor)
{
	if (!HasAuthority()) return;

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

// ============================================================
// DespawnMassEntityOnServer — Mass 엔티티 제거 (재생성 방지)
// ============================================================
void AHellunaEnemyCharacter::DespawnMassEntityOnServer(const TCHAR* Where)
{
	if (!HasAuthority()) return;

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
	if (!W) return;

	UMassEntitySubsystem* ES = W->GetSubsystem<UMassEntitySubsystem>();
	if (!ES) return;

	FMassEntityManager& EM = ES->GetMutableEntityManager();
	EM.DestroyEntity(Entity);
}

// ============================================================
// 공격 트레이스 시스템 (타이머 기반)
// ============================================================

void AHellunaEnemyCharacter::StartAttackTrace(FName SocketName, float Radius, float Interval,
	float DamageAmount, bool bDebugDraw)
{
	// 이전 트레이스가 살아있으면 먼저 중단
	StopAttackTrace();

	CurrentTraceSocketName = SocketName;
	CurrentTraceRadius     = Radius;
	CurrentDamageAmount    = DamageAmount;
	bDrawDebugTrace        = bDebugDraw;

	// 이번 공격의 히트 목록 초기화 (중복 피해 방지)
	HitActorsThisAttack.Empty();

	// 지정 간격마다 PerformAttackTrace 호출
	GetWorldTimerManager().SetTimer(
		AttackTraceTimerHandle,
		this,
		&AHellunaEnemyCharacter::PerformAttackTrace,
		Interval,
		true // 반복 실행
	);
}

void AHellunaEnemyCharacter::StopAttackTrace()
{
	if (AttackTraceTimerHandle.IsValid())
	{
		GetWorldTimerManager().ClearTimer(AttackTraceTimerHandle);
		AttackTraceTimerHandle.Invalidate();
		HitActorsThisAttack.Empty();
	}
}

void AHellunaEnemyCharacter::PerformAttackTrace()
{
	// 트레이스는 서버에서만 판정
	if (!HasAuthority()) return;

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

	// 지정 소켓 위치에서 구체 트레이스 실행
	const FVector SocketLocation = GetMesh()->GetSocketLocation(CurrentTraceSocketName);
	if (SocketLocation.IsNearlyZero())
	{
		UE_LOG(LogTemp, Warning, TEXT("[AttackTrace] %s: Socket '%s' not found"),
			*GetName(), *CurrentTraceSocketName.ToString());
		return;
	}

	TArray<FHitResult> HitResults;
	FCollisionShape SphereShape = FCollisionShape::MakeSphere(CurrentTraceRadius);

	FCollisionQueryParams QueryParams;
	QueryParams.AddIgnoredActor(this);
	QueryParams.bTraceComplex          = false;
	QueryParams.bReturnPhysicalMaterial = false;
	QueryParams.bReturnFaceIndex        = false;

	const bool bHit = World->SweepMultiByChannel(
		HitResults,
		SocketLocation,
		SocketLocation, // 시작 = 끝 (정적 구체)
		FQuat::Identity,
		ECC_Pawn,
		SphereShape,
		QueryParams
	);

	if (bDrawDebugTrace)
	{
		DrawDebugSphere(World, SocketLocation, CurrentTraceRadius, 12,
			bHit ? FColor::Red : FColor::Green, false, 0.1f, 0, 2.f);
	}

	if (!bHit) return;

	for (const FHitResult& Hit : HitResults)
	{
		AActor* HitActor = Hit.GetActor();
		if (!IsValid(HitActor) || HitActor == this) continue;

		// 플레이어 또는 우주선만 피해 대상
		const bool bIsPlayer   = Cast<AHellunaHeroCharacter>(HitActor) != nullptr;
		const bool bIsShip     = Cast<AResourceUsingObject_SpaceShip>(HitActor) != nullptr;
		if (!bIsPlayer && !bIsShip) continue;

		// 이번 공격에서 이미 맞은 액터는 스킵 (중복 피해 방지)
		if (HitActorsThisAttack.Contains(HitActor)) continue;
		HitActorsThisAttack.Add(HitActor);

		// 광폭화 시 데미지 배율 적용
		const float FinalDamage = bEnraged
			? CurrentDamageAmount * EnrageDamageMultiplier
			: CurrentDamageAmount;

		ServerApplyDamage(HitActor, FinalDamage, Hit.Location);
	}
}

// ============================================================
// ServerApplyDamage — 서버 RPC: 거리 검증 후 데미지 적용
// ============================================================
void AHellunaEnemyCharacter::ServerApplyDamage_Implementation(AActor* Target, float DamageAmount,
	const FVector& HitLocation)
{
	if (!HasAuthority() || !IsValid(Target)) return;

	// 안티 치트: HitLocation 기준으로 거리 검증
	// (우주선처럼 큰 오브젝트는 중심 거리보다 표면이 훨씬 가까울 수 있으므로 HitLocation 기준 사용)
	const float DistanceToHit    = FVector::Dist(GetActorLocation(), HitLocation);
	const float MaxAttackDistance = 600.f;

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
	if (DamageAmount < 0.f || DamageAmount > 1000.f) return false;
	if (!IsValid(Target)) return false;

	const float DistanceToHit = FVector::Dist(GetActorLocation(), HitLocation);
	if (DistanceToHit > 600.f)
	{
		UE_LOG(LogTemp, Warning, TEXT("[Validate] Hit too far: %.1f cm"), DistanceToHit);
		return false;
	}

	return true;
}

// ============================================================
// MulticastPlayEffect — 나이아가라 FX + 사운드 전파 (모든 클라이언트)
// ============================================================
void AHellunaEnemyCharacter::MulticastPlayEffect_Implementation(
	const FVector& SpawnLocation, UNiagaraSystem* Effect, float EffectScale, bool bPlaySound)
{
	if (Effect)
	{
		UNiagaraFunctionLibrary::SpawnSystemAtLocation(
			GetWorld(), Effect, SpawnLocation,
			FRotator::ZeroRotator, FVector(EffectScale),
			true, true, ENCPoolMethod::None, true
		);
	}

	if (bPlaySound && HitSound)
	{
		if (UAudioComponent* AudioComp = UGameplayStatics::SpawnSoundAtLocation(this, HitSound, SpawnLocation))
		{
			if (HitSoundAttenuation)
			{
				AudioComp->AttenuationSettings = HitSoundAttenuation;
			}
		}
	}
}

// ============================================================
// LockMovementAndFaceTarget / UnlockMovement
// ============================================================
void AHellunaEnemyCharacter::LockMovementAndFaceTarget(AActor* TargetActor)
{
	if (!HasAuthority()) return;

	UCharacterMovementComponent* MoveComp = GetCharacterMovement();
	if (!MoveComp) return;

	// 중복 호출 시 SavedMaxWalkSpeed 덮어쓰기 방지
	if (!bMovementLocked)
	{
		SavedMaxWalkSpeed = MoveComp->MaxWalkSpeed;
		bMovementLocked   = true;
	}
	MoveComp->MaxWalkSpeed = 0.f;
}

void AHellunaEnemyCharacter::UnlockMovement()
{
	if (!HasAuthority() || !bMovementLocked) return;

	UCharacterMovementComponent* MoveComp = GetCharacterMovement();
	if (!MoveComp) return;

	MoveComp->MaxWalkSpeed = SavedMaxWalkSpeed;
	bMovementLocked        = false;
}

// ============================================================
// SetServerAttackPoseTickEnabled
// 공격 중 소켓 위치 정확도를 위해 강제로 AlwaysTickPoseAndRefreshBones 로 전환
// ============================================================
void AHellunaEnemyCharacter::SetServerAttackPoseTickEnabled(bool bEnable)
{
	if (!HasAuthority()) return;

	USkeletalMeshComponent* M = GetMesh();
	if (!M) return;

	if (bEnable)
	{
		if (!bPoseTickSaved)
		{
			// 원래 설정 저장 — 공격 종료 후 복원
			SavedAnimTickOption = M->VisibilityBasedAnimTickOption;
			bSavedURO           = M->bEnableUpdateRateOptimizations;
			bPoseTickSaved      = true;
		}
		M->VisibilityBasedAnimTickOption    = EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones;
		M->bEnableUpdateRateOptimizations   = false;
	}
	else
	{
		if (!bPoseTickSaved) return;

		// 저장된 원래 설정으로 복원
		M->VisibilityBasedAnimTickOption   = SavedAnimTickOption;
		M->bEnableUpdateRateOptimizations  = bSavedURO;
		bPoseTickSaved = false;
	}
}

// ============================================================
// GetLifetimeReplicatedProps — 복제 프로퍼티 등록
// ============================================================
void AHellunaEnemyCharacter::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(AHellunaEnemyCharacter, bEnraged);
}

// ============================================================
// EnterEnraged — 광폭화 진입 (서버 전용)
// ============================================================
void AHellunaEnemyCharacter::EnterEnraged()
{
	if (!HasAuthority() || bEnraged) return;

	bEnraged = true;

	// 진행 중인 공격 즉시 중단 (광폭화 몽타주 재생 전 정리)
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

	// 이동 속도 증가
	if (UCharacterMovementComponent* MoveComp = GetCharacterMovement())
	{
		// bMovementLocked 중이면 SavedMaxWalkSpeed 가 실제 기본 속도를 보관 중
		const float BaseSpeed    = bMovementLocked ? SavedMaxWalkSpeed : MoveComp->MaxWalkSpeed;
		const float EnragedSpeed = BaseSpeed * EnrageMoveSpeedMultiplier;

		// UnlockMovement() 복원 이후에도 광폭화 속도가 유지되도록 SavedMaxWalkSpeed 갱신
		SavedMaxWalkSpeed = EnragedSpeed;

		if (!bMovementLocked)
		{
			MoveComp->MaxWalkSpeed = EnragedSpeed;
		}
	}

	// 광폭화 몽타주 재생
	if (EnrageMontage)
	{
		// OnMontageEnded 콜백은 서버에서만 바인딩 (STTask_Enrage 에게 완료 알림 용)
		if (USkeletalMeshComponent* SkelMesh = GetMesh())
		{
			if (UAnimInstance* AnimInst = SkelMesh->GetAnimInstance())
			{
				AnimInst->OnMontageEnded.RemoveDynamic(this, &AHellunaEnemyCharacter::OnEnrageMontageEnded);
				AnimInst->OnMontageEnded.AddDynamic   (this, &AHellunaEnemyCharacter::OnEnrageMontageEnded);
			}
		}
		Multicast_PlayEnrage();
	}
	else
	{
		// 몽타주 없으면 즉시 완료 신호
		OnEnrageMontageFinished.ExecuteIfBound();
	}

	UE_LOG(LogTemp, Log, TEXT("[Enrage] %s 광폭화 진입"), *GetName());
}

// ============================================================
// Multicast_PlayEnrage_Implementation
//
// [VFX 설계 이유]
//  - VFX 는 모든 클라이언트에서 SpawnSystemAttached 로 스폰해서
//    ActiveEnrageVFXComp 에 저장한다.
//  - 몽타주 완료(OnEnrageMontageEnded)는 서버에서만 호출되므로
//    VFX 종료도 Multicast 로 별도 전파해야 한다.
//    → Multicast_StopEnrageVFX 가 그 역할을 담당한다.
// ============================================================
void AHellunaEnemyCharacter::Multicast_PlayEnrage_Implementation()
{
	// 1. 광폭화 진입 몽타주 재생 (재생 속도 고정 1.0)
	if (EnrageMontage)
	{
		if (USkeletalMeshComponent* SkelMesh = GetMesh())
		{
			if (UAnimInstance* AnimInst = SkelMesh->GetAnimInstance())
			{
				AnimInst->Montage_Play(EnrageMontage, 1.0f);
			}
		}
	}

	// 2. 광폭화 VFX 스폰 — SpawnSystemAttached 로 컴포넌트 참조를 캐싱
	//    몽타주 완료 시 Multicast_StopEnrageVFX 로 끈다
	if (EnrageNiagaraEffect)
	{
		if (USkeletalMeshComponent* SkelMesh = GetMesh())
		{
			ActiveEnrageVFXComp = UNiagaraFunctionLibrary::SpawnSystemAttached(
				EnrageNiagaraEffect,
				SkelMesh,
				NAME_None,
				FVector::ZeroVector,
				FRotator::ZeroRotator,
				EAttachLocation::SnapToTarget,
				true // bAutoDestroy — DeactivateImmediate 후 자동 제거
			);
			if (ActiveEnrageVFXComp)
			{
				ActiveEnrageVFXComp->SetRelativeScale3D(FVector(EnrageEffectScale));
			}
		}
	}
}

// ============================================================
// Multicast_StopEnrageVFX_Implementation
// 서버의 OnEnrageMontageEnded 에서 호출 → 모든 클라이언트 VFX 종료
// ============================================================
void AHellunaEnemyCharacter::Multicast_StopEnrageVFX_Implementation()
{
	if (ActiveEnrageVFXComp)
	{
		ActiveEnrageVFXComp->DeactivateImmediate();
		ActiveEnrageVFXComp = nullptr;
	}
}

// ============================================================
// OnEnrageMontageEnded — 서버에서 몽타주 완료 감지
// ============================================================
void AHellunaEnemyCharacter::OnEnrageMontageEnded(UAnimMontage* Montage, bool bInterrupted)
{
	if (Montage != EnrageMontage) return;

	// 바인딩 해제 (1회용 콜백)
	if (USkeletalMeshComponent* SkelMesh = GetMesh())
	{
		if (UAnimInstance* AnimInst = SkelMesh->GetAnimInstance())
		{
			AnimInst->OnMontageEnded.RemoveDynamic(this, &AHellunaEnemyCharacter::OnEnrageMontageEnded);
		}
	}

	// 모든 클라이언트의 VFX 를 끈다 (Multicast)
	Multicast_StopEnrageVFX();

	// STTask_Enrage 에게 완료 알림 → Tick 에서 Succeeded 반환
	OnEnrageMontageFinished.ExecuteIfBound();
}
