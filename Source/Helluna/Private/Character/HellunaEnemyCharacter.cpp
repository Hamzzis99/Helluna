// Fill out your copyright notice in the Description page of Project Settings.

#include "Character/HellunaEnemyCharacter.h"
#include "Helluna.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Components/CapsuleComponent.h"
#include "Conponent/EnemyCombatComponent.h"
#include "Engine/AssetManager.h"
#include "DataAsset/DataAsset_EnemyStartUpData.h"
#include "Character/EnemyComponent/HellunaHealthComponent.h"
#include "Kismet/GameplayStatics.h"
#include "Camera/CameraShakeBase.h"
#include "AbilitySystemComponent.h"
#include "AbilitySystem/EnemyAbility/EnemyGameplayAbility_RangedAttack.h"
#include "GameMode/HellunaDefenseGameMode.h"
#include "Animation/AnimInstance.h"
#include "Components/StateTreeComponent.h"
#include "GameplayTagContainer.h"
#include "AIController.h"
#include "DebugHelper.h"
#include "AbilitySystem/HeroAbility/HeroGameplayAbility_GunParry.h"

// 타이머 기반 Trace 시스템용 헤더
#include "DrawDebugHelpers.h"
#include "GameFramework/DamageType.h"
#include "Character/HellunaHeroCharacter.h"

// ECS 관련
#include "MassAgentComponent.h"
#include "MassEntitySubsystem.h"
#include "MassEntityManager.h"
#include "Object/ResourceUsingObject/ResourceUsingObject_SpaceShip.h"
#include "Object/ResourceUsingObject/HellunaTurretBase.h"

// 나이아가라
#include "NiagaraFunctionLibrary.h"
#include "NiagaraSystem.h"
#include "NiagaraComponent.h"

// 사운드
#include "Components/AudioComponent.h"
#include "Sound/SoundAttenuation.h"

// 리플리케이션
#include "Net/UnrealNetwork.h"

// 퍼즐 보호막
#include "Puzzle/PuzzleShieldComponent.h"

// [PrewarmV1] GA 사전 인스턴스화
#include "AbilitySystemComponent.h"
#include "AbilitySystem/HellunaEnemyGameplayAbility.h"

// [MontagePrewarmV1] Montage 예열용
#include "Animation/AnimMontage.h"
#include "Components/SkeletalMeshComponent.h"

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

	// === 네트워크 레플리케이션 빈도 제한 ===
	// 50마리 × 100Hz = 초당 5000패킷 → 서버 포화 방지
	SetNetUpdateFrequency(10.f);
	SetMinNetUpdateFrequency(2.f);

	// === 원거리 레플리케이션 컬링 ===
	// 6000cm(60m) 밖의 몬스터는 레플리케이션 자체를 차단
	SetNetCullDistanceSquared(6000.f * 6000.f);

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
//
// [보스 SpawnActor 소환 시 타이밍 이슈 해결]
// SpawnActor로 런타임 소환 시 실행 순서:
//   1. AIController 생성 → StateTree StartLogic 시도
//      → 이 시점엔 아직 Pawn이 없어서 "Could not find context actor of type Pawn" 에러 발생
//      → StateTree 틱 비활성화됨
//   2. Pawn(보스) BeginPlay
//   3. PossessedBy 호출 ← 여기서 Pawn이 확정됨
//
// 따라서 PossessedBy에서 다음 프레임(NextTick)에 StateTree를 Stop→Start로 재시작해야
// Pawn 컨텍스트를 정상적으로 찾을 수 있다.
//
// 레벨에 배치된 일반 몬스터는 이 문제가 없으므로 영향 없음.
// ============================================================
void AHellunaEnemyCharacter::PossessedBy(AController* NewController)
{
	Super::PossessedBy(NewController);

	UE_LOG(LogTemp, Verbose, TEXT("[PossessedBy] %s → Controller: %s"),
		*GetName(), NewController ? *NewController->GetName() : TEXT("null"));

	// Pawn 자체에 StateTreeComponent가 붙어있는 경우:
	// BeginPlay에서 컨트롤러 없음으로 틱이 꺼졌을 수 있으므로 다시 켜줌
	if (UStateTreeComponent* STComp = FindComponentByClass<UStateTreeComponent>())
	{
		STComp->SetComponentTickEnabled(true);
		UE_LOG(LogTemp, Verbose, TEXT("[PossessedBy] Pawn의 StateTree 틱 재활성화"));
	}

	// AIController에 StateTreeComponent가 붙어있는 경우 (보스 포함 일반적인 구조):
	// Pawn 빙의 완료 후 다음 프레임에 StateTree를 재시작해서 Pawn 컨텍스트를 정상 등록
	AAIController* AIC = Cast<AAIController>(NewController);
	if (AIC)
	{
		UStateTreeComponent* STComp = AIC->FindComponentByClass<UStateTreeComponent>();
		UE_LOG(LogTemp, Verbose, TEXT("[PossessedBy] AIController StateTree: %s"),
			STComp ? TEXT("found") : TEXT("not found"));

		if (STComp)
		{
			// NextTick 이유: PossessedBy 호출 시점에도 AIController->GetPawn()이
			// 내부적으로 아직 완전히 등록되지 않아 StartLogic이 실패하는 경우가 있음.
			// 한 프레임 뒤에 재시작하면 Pawn이 확실히 등록된 상태가 보장됨.
			UStateTreeComponent* STCompPtr = STComp;
			AHellunaEnemyCharacter* SelfPtr = this;
			GetWorldTimerManager().SetTimerForNextTick([STCompPtr, SelfPtr]()
			{
				if (IsValid(STCompPtr) && IsValid(SelfPtr))
				{
					STCompPtr->StopLogic(TEXT("Restart after possess"));
					STCompPtr->StartLogic();
					UE_LOG(LogTemp, Verbose, TEXT("[PossessedBy] NextTick StateTree Stop→Start — %s"), *SelfPtr->GetName());
				}
			});
		}
	}

	InitEnemyStartUpData();

	// HealthComponent 바인딩: PossessedBy에서도 바인딩해서 BeginPlay보다 늦게 빙의되는 경우도 커버
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
//
// [HealthComponent 바인딩 설계]
// SpawnActor 소환 시 BeginPlay → PossessedBy 순서로 실행되므로
// BeginPlay 시점에 컨트롤러가 없을 수 있음.
// 기존 코드는 컨트롤러 없으면 early return해서 HealthComponent 바인딩이
// 통째로 스킵되는 문제가 있었음 → 보스가 맞아도 죽지 않는 버그 원인.
//
// 수정: HealthComponent 바인딩은 컨트롤러 유무와 무관하게 항상 수행.
// PossessedBy에서도 동일하게 바인딩(AddUniqueDynamic)하므로 중복 등록 없음.
// ============================================================

// ============================================================
// TakeDamage — PuzzleShieldComponent 보호막 필터링
// ============================================================
float AHellunaEnemyCharacter::TakeDamage(float DamageAmount,
	FDamageEvent const& DamageEvent, AController* EventInstigator, AActor* DamageCauser)
{
	// PuzzleShieldComponent가 부착되어 있으면 보호막 필터링
	if (UPuzzleShieldComponent* Shield = FindComponentByClass<UPuzzleShieldComponent>())
	{
		const float FilteredDamage = Shield->FilterDamage(DamageAmount);
		if (FilteredDamage <= 0.f)
		{
			UE_LOG(LogTemp, Warning,
				TEXT("[DamageDiag][ShieldBlocked] Enemy=%s Incoming=%.1f Instigator=%s Causer=%s"),
				*GetNameSafe(this),
				DamageAmount,
				*GetNameSafe(EventInstigator ? EventInstigator->GetPawn() : nullptr),
				*GetNameSafe(DamageCauser));
			return 0.f;
		}
		UE_LOG(LogTemp, Warning,
			TEXT("[DamageDiag][ShieldFiltered] Enemy=%s Incoming=%.1f Filtered=%.1f Instigator=%s Causer=%s"),
			*GetNameSafe(this),
			DamageAmount,
			FilteredDamage,
			*GetNameSafe(EventInstigator ? EventInstigator->GetPawn() : nullptr),
			*GetNameSafe(DamageCauser));
		DamageAmount = FilteredDamage;
	}

	UE_LOG(LogTemp, Warning,
		TEXT("[DamageDiag][TakeDamagePassThrough] Enemy=%s Damage=%.1f Instigator=%s Causer=%s"),
		*GetNameSafe(this),
		DamageAmount,
		*GetNameSafe(EventInstigator ? EventInstigator->GetPawn() : nullptr),
		*GetNameSafe(DamageCauser));
	return Super::TakeDamage(DamageAmount, DamageEvent, EventInstigator, DamageCauser);
}

// ============================================================
void AHellunaEnemyCharacter::BeginPlay()
{
	// Pawn에 StateTreeComponent가 직접 붙어있고 컨트롤러가 없는 경우
	// (레벨 배치 전 또는 ECS 몬스터): 불필요한 StateTree 연산 방지를 위해 틱 비활성화.
	// PossessedBy에서 컨트롤러가 붙으면 다시 활성화됨.
	if (!GetController())
	{
		if (UStateTreeComponent* STComp = FindComponentByClass<UStateTreeComponent>())
		{
			STComp->SetComponentTickEnabled(false);
		}
	}

	Super::BeginPlay();

	if (!HasAuthority()) return;

	// HealthComponent 바인딩 — 컨트롤러 유무와 무관하게 항상 수행
	// (보스처럼 SpawnActor로 소환되는 경우 BeginPlay 시점엔 컨트롤러가 없을 수 있으므로
	//  컨트롤러 체크로 early return하면 바인딩이 누락됨)
	if (HealthComponent)
	{
		HealthComponent->OnHealthChanged.RemoveDynamic(this, &AHellunaEnemyCharacter::OnMonsterHealthChanged);
		HealthComponent->OnHealthChanged.AddUniqueDynamic(this, &AHellunaEnemyCharacter::OnMonsterHealthChanged);
		HealthComponent->OnDeath.RemoveDynamic(this, &ThisClass::OnMonsterDeath);
		HealthComponent->OnDeath.AddUniqueDynamic(this, &ThisClass::OnMonsterDeath);
	}

	// GameMode 등록은 컨트롤러가 있는 경우에만 수행
	// ECS 몬스터는 컨트롤러 없이 시작하며 카운터는 RequestSpawn 시점에 확정되므로 등록 불필요
	if (GetController())
	{
		if (AHellunaDefenseGameMode* GM = Cast<AHellunaDefenseGameMode>(UGameplayStatics::GetGameMode(this)))
		{
			GM->RegisterAliveMonster(this);
		}
	}

	// =========================================================
	// [PrewarmV1] 서버 전용: 지정 GA 목록을 BeginPlay에서 미리 GiveAbility.
	// 첫 발동 시 클래스 로딩/스펙 할당 비용을 이 시점으로 당겨 렉 완화.
	// =========================================================
	if (UAbilitySystemComponent* ASC = GetAbilitySystemComponent())
	{
		int32 GrantedCount = 0;
		for (const TSubclassOf<UHellunaEnemyGameplayAbility>& AbilityClass : AbilitiesToPrewarm)
		{
			if (!*AbilityClass) continue;

			FGameplayAbilitySpec Spec(AbilityClass, 1, INDEX_NONE, this);
			const FGameplayAbilitySpecHandle Handle = ASC->GiveAbility(Spec);

			FGameplayAbilitySpec* FoundSpec = ASC->FindAbilitySpecFromHandle(Handle);
			UGameplayAbility* PrimaryInst = FoundSpec ? FoundSpec->GetPrimaryInstance() : nullptr;

			UE_LOG(LogTemp, Warning,
				TEXT("[Prewarm] Monster=%s Class=%s Handle=%s PrimaryInst=%s"),
				*GetNameSafe(this),
				*AbilityClass->GetName(),
				Handle.IsValid() ? TEXT("OK") : TEXT("INVALID"),
				PrimaryInst ? *PrimaryInst->GetName() : TEXT("null(lazy)"));

			++GrantedCount;
		}

		if (GrantedCount > 0)
		{
			UE_LOG(LogTemp, Warning,
				TEXT("[Prewarm] Monster=%s completed — %d ability classes pre-granted."),
				*GetNameSafe(this), GrantedCount);
		}
	}

	// =========================================================
	// [MontagePrewarmV1] 첫 공격/점프 발동 시 Montage 슬롯/커브/트랙 세팅 비용을
	// 스폰 시점으로 이관. 패키지 환경 첫 몬타주 재생 때 수백 ms 렉이 자주 잡히는 원인.
	//   - Montage_Play → 즉시 Montage_Stop 으로 실제 재생은 0프레임.
	//   - 서버/클라 양쪽 BeginPlay 에서 호출되므로 두 쪽 모두 데미징 없이 예열.
	// =========================================================
	if (USkeletalMeshComponent* SkelMesh = GetMesh())
	{
		if (UAnimInstance* AnimInst = SkelMesh->GetAnimInstance())
		{
			auto PrewarmMontage = [AnimInst](UAnimMontage* M, const TCHAR* Label)
			{
				if (!M) return;
				const float Played = AnimInst->Montage_Play(M, 1.f, EMontagePlayReturnType::Duration, 0.f, false);
				AnimInst->Montage_Stop(0.f, M);
				UE_LOG(LogTemp, Warning,
					TEXT("[MontagePrewarm] %s Montage=%s Played=%.3f"),
					Label, *M->GetName(), Played);
			};

			PrewarmMontage(AttackMontage,      TEXT("Attack"));
			PrewarmMontage(HitReactMontage,    TEXT("HitReact"));
			PrewarmMontage(DeathMontage,       TEXT("Death"));
			PrewarmMontage(EnrageMontage,      TEXT("Enrage"));
			PrewarmMontage(ParryVictimMontage, TEXT("ParryVictim"));
		}
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
#if HELLUNA_DEBUG_ENEMY
	UE_LOG(LogTemp, Warning,
		TEXT("[DeathDiag][HealthChanged] Enemy=%s Old=%.1f New=%.1f Delta=%.1f Instigator=%s"),
		*GetNameSafe(this),
		OldHealth,
		NewHealth,
		Delta,
		*GetNameSafe(InstigatorActor));

	if (NewHealth <= 0.f)
	{
		UE_LOG(LogTemp, Warning,
			TEXT("[DeathDiag][HealthReachedZero] Enemy=%s Controller=%s"),
			*GetNameSafe(this),
			*GetNameSafe(GetController()));
	}
#endif

	// 살아있는 상태에서 데미지를 받았을 때만 피격 애니메이션 재생
	// #11 최적화: 0.2초 쿨다운 — 산탄총 동시 히트 시 RPC 폭발 방지
	if (Delta > 0.f && NewHealth > 0.f && HitReactMontage)
	{
		static int32 HitReactBlockedCount = 0;
		static int32 HitReactPassedCount = 0;

		const double Now = GetWorld()->GetTimeSeconds();
		if (Now - LastHitReactTime >= 0.2)
		{
			LastHitReactTime = Now;
			Multicast_PlayHitReact();
			HitReactPassedCount++;
		}
		else
		{
			HitReactBlockedCount++;
			if (HitReactBlockedCount % 10 == 0)
			{
				UE_LOG(LogTemp, Log,
					TEXT("[fast][#11 HitReact쿨다운] 누적: 차단=%d, 통과=%d (절감율=%.0f%%)"),
					HitReactBlockedCount, HitReactPassedCount,
					(HitReactBlockedCount + HitReactPassedCount) > 0
						? (HitReactBlockedCount * 100.f / (HitReactBlockedCount + HitReactPassedCount))
						: 0.f);
			}
		}
	}
}

// ============================================================
// Multicast_PlayHitReact — 피격 몽타주 (모든 클라이언트)
// ============================================================
void AHellunaEnemyCharacter::Multicast_PlayHitReact_Implementation()
{
	// [GunParry] 처형 중 피격 모션 차단
	if (UHeroGameplayAbility_GunParry::ShouldBlockHitReact(this)) return;

	if (!HitReactMontage) return;

	USkeletalMeshComponent* SkelMesh = GetMesh();
	if (!SkelMesh) return;

	UAnimInstance* AnimInst = SkelMesh->GetAnimInstance();
	if (!AnimInst) return;

	AnimInst->Montage_Play(HitReactMontage);
}

// ============================================================
// Multicast_PlayParryVictim — 건패링 처형 피격 몽타주 (모든 클라이언트)
// ============================================================
void AHellunaEnemyCharacter::Multicast_PlayParryVictim_Implementation()
{
	if (!ParryVictimMontage) return;

	USkeletalMeshComponent* SkelMesh = GetMesh();
	if (!SkelMesh) return;

	UAnimInstance* AnimInst = SkelMesh->GetAnimInstance();
	if (!AnimInst) return;

	AnimInst->Montage_Play(ParryVictimMontage, 1.0f);
}

// ============================================================
// Multicast_ActivateRagdoll — 래그돌 전환 + 임펄스 (모든 클라이언트)
// ============================================================
void AHellunaEnemyCharacter::Multicast_ActivateRagdoll_Implementation(
	FVector Impulse, FVector ImpulseLocation)
{
	// 데디케이티드 서버에서는 시각적 래그돌 불필요
	if (GetNetMode() == NM_DedicatedServer) return;

	USkeletalMeshComponent* EnemyMesh = GetMesh();
	if (!EnemyMesh) return;

	// 몽타주 중단 (래그돌 전환 전에)
	if (UAnimInstance* AnimInst = EnemyMesh->GetAnimInstance())
	{
		AnimInst->StopAllMontages(0.f);
	}

	// 캡슐 충돌 비활성화 (래그돌이 캡슐과 충돌하면 튀어오름)
	if (UCapsuleComponent* Capsule = GetCapsuleComponent())
	{
		Capsule->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	}

	// 이동 비활성화
	if (UCharacterMovementComponent* MoveComp = GetCharacterMovement())
	{
		MoveComp->DisableMovement();
		MoveComp->StopMovementImmediately();
	}

	// 래그돌 활성화
	EnemyMesh->SetCollisionProfileName(TEXT("Ragdoll"));
	// Pawn 채널 Ignore — 래그돌이 캐릭터를 막지 않도록 (바닥/벽 충돌은 유지)
	EnemyMesh->SetCollisionResponseToChannel(ECC_Pawn, ECR_Ignore);
	EnemyMesh->SetAllBodiesSimulatePhysics(true);
	EnemyMesh->SetSimulatePhysics(true);
	EnemyMesh->WakeAllRigidBodies();

	// 임펄스 적용 (처형 방향으로 날려보내기)
	EnemyMesh->AddImpulseAtLocation(Impulse, ImpulseLocation);

	UE_LOG(LogGunParry, Warning, TEXT("[Ragdoll] 래그돌 활성화 — Impulse=%s"),
		*Impulse.ToString());
}

// ============================================================
// Multicast_SetStaggerVisual — Stagger 비주얼 ON/OFF
// ============================================================
void AHellunaEnemyCharacter::Multicast_SetStaggerVisual_Implementation(
	UMaterialInterface* StaggerMat, UAnimMontage* StaggerAnim, bool bEnable)
{
	if (GetNetMode() == NM_DedicatedServer) return;

	USkeletalMeshComponent* EnemyMesh = GetMesh();
	if (!EnemyMesh) return;

	if (bEnable)
	{
		// 원본 머티리얼 저장
		SavedOriginalMaterials.Empty();
		for (int32 i = 0; i < EnemyMesh->GetNumMaterials(); i++)
		{
			SavedOriginalMaterials.Add(EnemyMesh->GetMaterial(i));
		}

		// Stagger 오버레이 머티리얼 교체
		if (StaggerMat)
		{
			EnemyMesh->SetOverlayMaterial(StaggerMat);
		}

		// Stagger 몽타주 재생
		if (StaggerAnim)
		{
			PlayAnimMontage(StaggerAnim, 1.0f);
		}
	}
	else
	{
		// 오버레이 제거
		EnemyMesh->SetOverlayMaterial(nullptr);

		// Stagger 몽타주 중단
		StopAnimMontage(nullptr);
	}
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
// OnMonsterDeath — HealthComponent OnDeath 델리게이트 콜백
//
// HP가 0이 되면 HealthComponent가 이 함수를 호출.
// AIController의 StateTreeComponent에 "Enemy.State.Death" 태그 이벤트를 전송해서
// StateTree의 Death 상태로 전환시킨다.
//
// StateTree Death 상태 → STTask_Death → GA_Death(사망 몽타주 재생)
//   → HandleDeathFinished → NotifyMonsterDied(GameMode 카운터 차감) → Destroy
//
// 주의: 이 함수에서 직접 Destroy를 호출하지 않는다.
//       반드시 StateTree → GA_Death 경로를 거쳐야 사망 애니메이션과 GameMode 통보가 보장됨.
// ============================================================
void AHellunaEnemyCharacter::OnMonsterDeath(AActor* DeadActor, AActor* KillerActor)
{
	if (!HasAuthority()) return;

	UE_LOG(LogTemp, Warning,
		TEXT("[DeathDiag][OnMonsterDeathBegin] Enemy=%s DeadActor=%s Killer=%s Controller=%s"),
		*GetNameSafe(this),
		*GetNameSafe(DeadActor),
		*GetNameSafe(KillerActor),
		*GetNameSafe(GetController()));

	AAIController* AIC = Cast<AAIController>(GetController());
	if (!AIC)
	{
		UE_LOG(LogTemp, Error,
			TEXT("[DeathDiag][OnMonsterDeathFail] Enemy=%s Reason=MissingAIController Controller=%s"),
			*GetNameSafe(this),
			*GetNameSafe(GetController()));
		return;
	}

	UStateTreeComponent* STComp = AIC->FindComponentByClass<UStateTreeComponent>();
	if (!STComp)
	{
		UE_LOG(LogTemp, Error,
			TEXT("[DeathDiag][OnMonsterDeathFail] Enemy=%s Reason=MissingStateTree AIController=%s"),
			*GetNameSafe(this),
			*GetNameSafe(AIC));
		return;
	}

	FGameplayTag DeathTag = FGameplayTag::RequestGameplayTag(FName("Enemy.State.Death"), false);
	if (!DeathTag.IsValid())
	{
		UE_LOG(LogTemp, Error,
			TEXT("[DeathDiag][OnMonsterDeathFail] Enemy=%s Reason=MissingDeathTag"),
			*GetNameSafe(this));
		return;
	}

	// 사망 즉시 캡슐 충돌 비활성화 — 사망 애니메이션 중 Overlap 판정 방지
	if (UCapsuleComponent* Capsule = GetCapsuleComponent())
	{
		Capsule->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	}
	// 메시 충돌도 완전 비활성화 (Physical Asset 바디가 캐릭터를 밀지 않도록)
	if (USkeletalMeshComponent* EnemyMesh = GetMesh())
	{
		EnemyMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	}
	if (UCharacterMovementComponent* MoveComp = GetCharacterMovement())
	{
		MoveComp->StopMovementImmediately();
		MoveComp->DisableMovement();
	}
#if HELLUNA_DEBUG_ENEMY
	UE_LOG(LogTemp, Warning,
		TEXT("[DeathDiag][OnMonsterDeathSendEvent] Enemy=%s AIController=%s StateTree=%s Tag=%s"),
		*GetNameSafe(this),
		*GetNameSafe(AIC),
		*GetNameSafe(STComp),
		*DeathTag.ToString());
#endif
	STComp->SendStateTreeEvent(DeathTag);
}

// ============================================================
// DespawnMassEntityOnServer — Mass 엔티티 제거 + 액터 파괴
// ============================================================
void AHellunaEnemyCharacter::DespawnMassEntityOnServer(const TCHAR* Where)
{
	if (!HasAuthority()) return;

	// 중복 호출 방지
	if (bDespawnStarted) return;
	bDespawnStarted = true;

	// 1. 모든 콜리전 즉시 비활성화 — 디퍼드 오버랩 이벤트 방지
	SetActorEnableCollision(false);
	TArray<UPrimitiveComponent*> Prims;
	GetComponents<UPrimitiveComponent>(Prims);
	for (UPrimitiveComponent* Prim : Prims)
	{
		if (Prim && Prim->IsRegistered())
		{
			Prim->SetCollisionEnabled(ECollisionEnabled::NoCollision);
			Prim->SetGenerateOverlapEvents(false);
		}
	}

	// 2. Mass Entity 정리
	if (!MassAgentComp)
	{
		MassAgentComp = FindComponentByClass<UMassAgentComponent>();
	}

	if (MassAgentComp)
	{
		const FMassEntityHandle Entity = MassAgentComp->GetEntityHandle();
		if (Entity.IsValid())
		{
			UWorld* W = GetWorld();
			if (W)
			{
				if (UMassEntitySubsystem* ES = W->GetSubsystem<UMassEntitySubsystem>())
				{
					FMassEntityManager& EM = ES->GetMutableEntityManager();
					EM.DestroyEntity(Entity);
				}
			}
		}

		MassAgentComp->DestroyComponent();
		MassAgentComp = nullptr;
	}

	// 3. Destroy()를 다음 틱으로 지연 — 같은 프레임 내 DestroyComponent()의
	//    OnUnregister 콜백이 오버랩 플러시를 유발하여 형제 컴포넌트를
	//    재진입(re-entrant) Unregister하는 것을 방지한다.
	UWorld* W = GetWorld();
	if (W)
	{
		W->GetTimerManager().SetTimerForNextTick([WeakThis = TWeakObjectPtr<AHellunaEnemyCharacter>(this)]()
		{
			if (WeakThis.IsValid())
			{
				WeakThis->Destroy();
			}
		});
	}
	else
	{
		Destroy();
	}
}

// ============================================================
// 공격 트레이스 시스템 (타이머 기반)
// ============================================================

void AHellunaEnemyCharacter::StartAttackTrace(FName SocketName, float Radius, float Interval,
	float DamageAmount, bool bDebugDraw)
{
	// 이전 트레이스가 살아있으면 먼저 중단
	StopAttackTrace();
	SetServerAttackPoseTickEnabled(true);

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
	SetServerAttackPoseTickEnabled(false);

	if (AttackTraceTimerHandle.IsValid())
	{
		GetWorldTimerManager().ClearTimer(AttackTraceTimerHandle);
		AttackTraceTimerHandle.Invalidate();
	}

	HitActorsThisAttack.Empty();
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
		UE_LOG(LogTemp, Verbose, TEXT("[AttackTrace] %s: Socket '%s' not found"),
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

		// 플레이어 / 우주선 / 타워 계열 모두 피해 대상
		const bool bIsPlayer = Cast<AHellunaHeroCharacter>(HitActor) != nullptr;
		const bool bIsShip   = Cast<AResourceUsingObject_SpaceShip>(HitActor) != nullptr;
		const bool bIsTurret = Cast<AHellunaTurretBase>(HitActor) != nullptr;
		if (!bIsPlayer && !bIsShip && !bIsTurret) continue;

		// 이번 공격에서 이미 맞은 액터는 스킵 (중복 피해 방지)
		if (HitActorsThisAttack.Contains(HitActor)) continue;
		HitActorsThisAttack.Add(HitActor);

		// 광폭화 시 데미지 배율 적용
		const float FinalDamage = bEnraged
			? CurrentDamageAmount * EnrageDamageMultiplier
			: CurrentDamageAmount;

		ServerApplyDamage(HitActor, FinalDamage, Hit.Location);

		// [HitVFXV1] GA 가 캐싱한 HitVFX 를 Hit.Location 에 멀티캐스트 스폰
		// [HitVFX.Diag] 트레이스 히트 / VFX 캐시 / 멀티캐스트 전송 로그
		const TCHAR* TargetTypeStr = bIsPlayer ? TEXT("Player")
			: bIsShip ? TEXT("Ship")
			: bIsTurret ? TEXT("Turret")
			: TEXT("?");
		UE_LOG(LogTemp, Warning,
			TEXT("[HitVFX.Diag][Trace.Hit] Monster=%s TargetType=%s CachedHitVFX=%s HitLoc=%s"),
			*GetName(), TargetTypeStr,
			CachedHitVFX ? *CachedHitVFX->GetName() : TEXT("NULL(GA 에 HitVFX 미할당)"),
			*Hit.Location.ToString());

		if (CachedHitVFX)
		{
			Multicast_SpawnHitVFX(CachedHitVFX, Hit.Location, CachedHitVFXScale);
		}
	}
}

// ============================================================
// ServerApplyDamage — 서버 RPC: 거리 검증 후 데미지 적용
// ============================================================
void AHellunaEnemyCharacter::ServerApplyDamage(AActor* Target, float DamageAmount,
	const FVector& HitLocation)
{
	if (!HasAuthority() || !IsValid(Target)) return;

	// 거리 검증
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

	// 이펙트 RPC 쓰로틀링: 같은 몬스터에서 0.1초 내 중복 Multicast 생략
	const double Now = FPlatformTime::Seconds();
	if (Now - LastEffectRPCTime >= 0.1)
	{
		LastEffectRPCTime = Now;
		MulticastPlayEffect(HitLocation, HitNiagaraEffect, HitEffectScale, false);

		// [HitSoundV1] 사운드는 GA 가 CachedHitSound 에 넣어둔 값을 사용해 별도 멀티캐스트.
		TryPlayCachedHitSound(HitLocation);
	}
}

// ============================================================
// MulticastPlayEffect — 나이아가라 FX 전파 (모든 클라이언트)
// [HitSoundV1] 사운드 재생은 이 RPC 에서 제거되었음.
//   GA(UHellunaEnemyGameplayAbility)의 HitSound 를 Multicast_PlayHitSound 로 별도 전송.
//   bPlaySound 파라미터는 레거시 호출자를 위해 시그니처만 유지 (내부 무시).
// ============================================================
void AHellunaEnemyCharacter::MulticastPlayEffect_Implementation(
	const FVector& SpawnLocation, UNiagaraSystem* Effect, float EffectScale, bool /*bPlaySound*/)
{
	if (Effect)
	{
		UNiagaraFunctionLibrary::SpawnSystemAtLocation(
			GetWorld(), Effect, SpawnLocation,
			FRotator::ZeroRotator, FVector(EffectScale),
			true, true, ENCPoolMethod::None, true
		);
	}
}

// ============================================================
// [HitSoundV1] GA 에 설정된 타격 사운드를 지정 위치에서 재생
// ============================================================
void AHellunaEnemyCharacter::Multicast_PlayHitSound_Implementation(
	USoundBase* Sound, FVector HitLocation, float VolumeMultiplier, USoundAttenuation* Attenuation)
{
	if (GetNetMode() == NM_DedicatedServer || !Sound) return;

	UGameplayStatics::PlaySoundAtLocation(
		this, Sound, HitLocation,
		VolumeMultiplier, 1.f, 0.f, Attenuation
	);
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

	// 서버 회전 모드: 이동 방향 회전 OFF → 컨트롤러 기반으로 전환
	MoveComp->bOrientRotationToMovement    = false;
	MoveComp->bUseControllerDesiredRotation = true;

	if (TargetActor)
	{
		const FVector ToTarget = (TargetActor->GetActorLocation() - GetActorLocation()).GetSafeNormal2D();
		if (!ToTarget.IsNearlyZero())
		{
			const FRotator TargetRot(0.f, ToTarget.Rotation().Yaw, 0.f);
			SetActorRotation(TargetRot, ETeleportType::TeleportPhysics);

			if (AController* OwnerController = GetController())
			{
				OwnerController->SetControlRotation(TargetRot);
			}
		}
	}

	// 공격 중 AIController Focus 차단 — UpdateControlRotation 자동 추적 방지.
	// Aim Offset이 ControlRotation 델타를 읽어 상체/머리를 틀던 문제 제거.
	// per-tick 컨트롤러 회전 계산도 사라지므로 100마리 규모에서 CPU 이득.
	if (AAIController* AIC = Cast<AAIController>(GetController()))
	{
		if (!bFocusSaved)
		{
			SavedFocusActor = AIC->GetFocusActor();
			bFocusSaved     = true;
		}
		AIC->ClearFocus(EAIFocusPriority::Gameplay);
	}

	// 클라 회전 끊김(20-30fps 느낌) 완화 — 공격 락 동안 NetUpdateFrequency를
	// 30Hz로 일시 부스트. StateTree Task가 매 틱 SetActorRotation으로 Yaw를
	// 갱신하지만, 기본 10Hz로는 클라가 초당 10회만 보간 입력을 받아 끊겨 보임.
	// 공격 한 사이클(2~3초)만 부스트하므로 100마리 규모에서도 부담 적음.
	if (!bNetFreqBoosted)
	{
		SavedNetUpdateFrequency = GetNetUpdateFrequency();
		bNetFreqBoosted = true;
	}
	SetNetUpdateFrequency(30.f);

	// 클라이언트에도 즉시 동기화 (회전 모드 + 속도)
	Multicast_SetMovementLocked(true, 0.f);
}

void AHellunaEnemyCharacter::UnlockMovement()
{
	if (!HasAuthority() || !bMovementLocked) return;

	UCharacterMovementComponent* MoveComp = GetCharacterMovement();
	if (!MoveComp) return;

	MoveComp->MaxWalkSpeed = SavedMaxWalkSpeed;
	bMovementLocked        = false;

	// 서버 회전 모드 복원: 이동 방향 회전 ON
	MoveComp->bOrientRotationToMovement    = true;
	MoveComp->bUseControllerDesiredRotation = false;

	// Focus는 복원하지 않음 — 쿨다운 중 Task의 수동 SetControlRotation(RInterpTo)이
	// 부드럽게 타겟을 따라가도록. AIC->UpdateControlRotation의 Focus 스냅이 들어오면
	// 보간이 매 틱 덮어써져 상체/머리 끊김 발생.
	// Chase/Evaluator 전이 시 ExitState 또는 해당 Task가 필요하면 다시 SetFocus함.
	SavedFocusActor.Reset();
	bFocusSaved = false;

	// NetUpdateFrequency 원복 (10Hz)
	if (bNetFreqBoosted)
	{
		SetNetUpdateFrequency(SavedNetUpdateFrequency);
		bNetFreqBoosted = false;
	}

	// 클라이언트에도 복원 동기화 (회전 모드 + 속도)
	Multicast_SetMovementLocked(false, SavedMaxWalkSpeed);
}

void AHellunaEnemyCharacter::Multicast_SetMovementLocked_Implementation(bool bLock, float WalkSpeed)
{
	// 서버는 이미 위에서 직접 설정했으므로 클라이언트에서만 적용
	if (HasAuthority()) return;

	UCharacterMovementComponent* MoveComp = GetCharacterMovement();
	if (!MoveComp) return;

	MoveComp->MaxWalkSpeed = WalkSpeed;

	// [RotFixV2] 클라이언트 회전 모드 — as-client 에서 공격 중 보스 방향 어긋남 수정:
	//  - bOrientRotationToMovement  : 속도 기반 회전 방지 (항상 false)
	//  - bUseControllerDesiredRotation : AI 폰의 ControlRotation 은 클라에 복제되지 않음.
	//    true 로 하면 클라 CMC 가 stale 값으로 pawn 을 돌리려 해 FRepMovement 와 충돌.
	//    false 로 고정 → FRepMovement(서버 actor 회전) 만이 회전 유일 소스.
	MoveComp->bOrientRotationToMovement    = false;
	MoveComp->bUseControllerDesiredRotation = false;

	if (bLock)
	{
		// [RotFixV2] 공격 락 동안 스무딩 OFF — Linear 는 시간 함수라 SyncAttackRotation 스냅
		// 직전까지 중간 상태가 시각으로 노출됨 (프로젝타일 발사와 타이밍 겹쳐서 "뒤보고 쏨").
		// NetUpdateFrequency=30Hz 부스트가 걸려 있어 Disabled 로 두어도 30Hz 즉시 반영 → 끊김 미미.
		SavedSmoothingMode = MoveComp->NetworkSmoothingMode;
		MoveComp->NetworkSmoothingMode = ENetworkSmoothingMode::Disabled;
	}
	else
	{
		// 공격 종료: 스무딩 복원
		MoveComp->NetworkSmoothingMode = SavedSmoothingMode;
	}
}

void AHellunaEnemyCharacter::Multicast_PlayAttackMontage_Implementation(
	UAnimMontage* Montage,
	float PlayRate,
	FRotator FacingRotation)
{
	if (HasAuthority() || GetNetMode() == NM_DedicatedServer || !Montage)
	{
		return;
	}

	// ═══ [MPAim] 클라이언트 회전 진단 로그 (재빌드 후 수집용) ═══
	const FRotator PrevClientRot = GetActorRotation();

	// 즉시 회전 적용 — CMC 스무딩이 비활성화된 상태에서 snap
	const FRotator AttackRot(0.f, FacingRotation.Yaw, 0.f);
	SetActorRotation(AttackRot, ETeleportType::TeleportPhysics);

	const FRotator PostClientRot = GetActorRotation();
	float MeshWorldYaw = 0.f;
	if (USkeletalMeshComponent* MeshDiag = GetMesh())
	{
		MeshWorldYaw = MeshDiag->GetComponentRotation().Yaw;
	}

	UE_LOG(LogTemp, Warning,
		TEXT("[MPAim][Client] Montage=%s ServerYaw=%.1f PrevCliYaw=%.1f PostCliYaw=%.1f MeshYaw=%.1f"),
		*Montage->GetName(), FacingRotation.Yaw, PrevClientRot.Yaw, PostClientRot.Yaw, MeshWorldYaw);

	// 클라이언트 측 이동 즉시 정지
	UCharacterMovementComponent* MoveComp = GetCharacterMovement();
	if (MoveComp)
	{
		MoveComp->StopMovementImmediately();
	}

	USkeletalMeshComponent* SkelMesh = GetMesh();
	if (!SkelMesh) return;

	UAnimInstance* AnimInst = SkelMesh->GetAnimInstance();
	if (!AnimInst) return;

	// ASC 리플리케이션이 먼저 도착해서 재생 중이면 처음부터 재시작
	// (ASC는 서버 시점 기준이라 중간부터 시작할 수 있음 → 처음부터 재생해야 자연스러움)
	if (AnimInst->Montage_IsPlaying(Montage))
	{
		AnimInst->Montage_Stop(0.1f, Montage);
	}
	AnimInst->Montage_Play(Montage, PlayRate);
}

// ============================================================
// [AttackAssetsV1] GA 소유 공격 시작 사운드 멀티캐스트
// ============================================================
void AHellunaEnemyCharacter::Multicast_PlayAttackSound_Implementation(
	USoundBase* Sound, float VolumeMultiplier)
{
	if (GetNetMode() == NM_DedicatedServer || !Sound) return;
	UGameplayStatics::PlaySoundAtLocation(
		this, Sound, GetActorLocation(), VolumeMultiplier, 1.f);
}

// ============================================================
// [HitVFXV1] 타격 지점 VFX 멀티캐스트 스폰
// ============================================================
void AHellunaEnemyCharacter::Multicast_SpawnHitVFX_Implementation(
	UNiagaraSystem* VFX, FVector HitLocation, float Scale)
{
	// [HitVFX.Diag] 멀티캐스트 수신 로그 (NetMode 별 분기 파악)
	const TCHAR* NetModeStr = TEXT("?");
	switch (GetNetMode())
	{
	case NM_DedicatedServer: NetModeStr = TEXT("DediServer"); break;
	case NM_ListenServer:    NetModeStr = TEXT("ListenSrv"); break;
	case NM_Client:          NetModeStr = TEXT("Client"); break;
	case NM_Standalone:      NetModeStr = TEXT("Standalone"); break;
	default: break;
	}
	UE_LOG(LogTemp, Warning,
		TEXT("[HitVFX.Diag][Multicast.Recv] Monster=%s NetMode=%s VFX=%s Loc=%s"),
		*GetName(), NetModeStr,
		VFX ? *VFX->GetName() : TEXT("NULL"),
		*HitLocation.ToString());

	if (GetNetMode() == NM_DedicatedServer || !VFX) return;

	UNiagaraComponent* NC = UNiagaraFunctionLibrary::SpawnSystemAtLocation(
		GetWorld(), VFX, HitLocation, FRotator::ZeroRotator);

	UE_LOG(LogTemp, Warning,
		TEXT("[HitVFX.Diag][Multicast.Spawn] Monster=%s NC=%s"),
		*GetName(), NC ? TEXT("OK") : TEXT("FAIL"));

	if (NC)
	{
		NC->SetWorldScale3D(FVector(Scale));
	}
}

// ============================================================
// [LocVFX] 일반 위치 VFX 멀티캐스트 스폰 (attach 없음, DashAttack 마무리 등)
// ============================================================
void AHellunaEnemyCharacter::Multicast_SpawnLocationVFX_Implementation(
	UNiagaraSystem* VFX, FVector Location, FRotator Rotation, float Scale)
{
	// 데디케이티드 서버는 렌더 없음 → 스킵 (네트워크 트래픽만 발생)
	if (GetNetMode() == NM_DedicatedServer || !VFX) return;

	UNiagaraComponent* NC = UNiagaraFunctionLibrary::SpawnSystemAtLocation(
		GetWorld(), VFX, Location, Rotation);
	if (NC)
	{
		NC->SetWorldScale3D(FVector(FMath::Max(0.01f, Scale)));
	}
}

// ============================================================
// [AttachVFX] 캐릭터 루트 부착 VFX 멀티캐스트 스폰 (DashTrail 등)
// ============================================================
void AHellunaEnemyCharacter::Multicast_SpawnAttachedVFX_Implementation(
	UNiagaraSystem* VFX)
{
	if (GetNetMode() == NM_DedicatedServer || !VFX || !GetRootComponent()) return;

	UNiagaraFunctionLibrary::SpawnSystemAttached(
		VFX,
		GetRootComponent(),
		NAME_None,
		FVector::ZeroVector,
		FRotator::ZeroRotator,
		EAttachLocation::KeepRelativeOffset,
		true /*bAutoDestroy*/
	);
}

// ============================================================
// [CamShake] 월드 카메라 쉐이크 멀티캐스트
// ============================================================
void AHellunaEnemyCharacter::Multicast_PlayWorldCameraShake_Implementation(
	TSubclassOf<UCameraShakeBase> ShakeClass,
	FVector Origin,
	float InnerRadius,
	float OuterRadius,
	float Falloff)
{
	if (!ShakeClass || !GetWorld()) return;

	// 데디케이티드 서버에는 로컬 플레이어 컨트롤러가 없어서 어차피 아무 일 없음 — 조기 리턴.
	if (GetNetMode() == NM_DedicatedServer) return;

	UGameplayStatics::PlayWorldCameraShake(
		GetWorld(), ShakeClass, Origin, InnerRadius, OuterRadius, Falloff);
}

// ============================================================
// [RangedSync] 발사 시점 클라 Yaw 강제 스냅
// 몬타지 시작 스냅 후 2.5초 사이 클라가 스무딩으로 서버를 따라가는데,
// 플레이어가 빠르게 움직이면 스무딩 지연으로 Notify 발화 시 클라 각도가 서버와 불일치.
// 이 RPC 로 Notify 직전에 한 번 더 강제 스냅해 연출 밀림 차단.
// ============================================================
void AHellunaEnemyCharacter::Multicast_SyncAttackRotation_Implementation(FRotator NewRotation)
{
	if (HasAuthority() || GetNetMode() == NM_DedicatedServer) return;

	const FRotator SnapRot(0.f, NewRotation.Yaw, 0.f);
	SetActorRotation(SnapRot, ETeleportType::TeleportPhysics);

	UE_LOG(LogTemp, Warning,
		TEXT("[MPAim][SyncFire] Client snapped Yaw=%.1f"), NewRotation.Yaw);
}

// ============================================================
// [RangedNotify] 활성화된 RangedAttack GA 찾아 투사체 발사
// ============================================================
void AHellunaEnemyCharacter::FireActiveRangedProjectile()
{
	// ServerOnly GA — 서버에서만 실제 스폰 수행. 클라 노티 콜은 무시.
	if (!HasAuthority()) return;

	UAbilitySystemComponent* ASC = GetAbilitySystemComponent();
	if (!ASC) return;

	for (FGameplayAbilitySpec& Spec : ASC->GetActivatableAbilities())
	{
		if (!Spec.IsActive()) continue;

		// InstancedPerActor → PrimaryInstance만 확인하면 충분
		if (UGameplayAbility* Instance = Spec.GetPrimaryInstance())
		{
			if (auto* Ranged = Cast<UEnemyGameplayAbility_RangedAttack>(Instance))
			{
				Ranged->FireProjectileFromNotify();
				return;
			}
		}
	}
}

// ============================================================
// Persistent VFX Multicast (시간 왜곡 등 지속형 VFX)
// ============================================================
void AHellunaEnemyCharacter::Multicast_SpawnPersistentVFX_Implementation(
	uint8 SlotIndex, UNiagaraSystem* Effect, float EffectScale)
{
	if (SlotIndex >= 2 || !Effect) return;

	// 기존 슬롯 정리
	if (PersistentVFXSlots[SlotIndex])
	{
		PersistentVFXSlots[SlotIndex]->DeactivateImmediate();
		PersistentVFXSlots[SlotIndex] = nullptr;
	}

	PersistentVFXSlots[SlotIndex] = UNiagaraFunctionLibrary::SpawnSystemAttached(
		Effect,
		GetRootComponent(),
		NAME_None,
		GetActorLocation(),
		FRotator::ZeroRotator,
		FVector(EffectScale),
		EAttachLocation::KeepWorldPosition,
		true,
		ENCPoolMethod::None,
		true
	);
}

void AHellunaEnemyCharacter::Multicast_StopPersistentVFX_Implementation(uint8 SlotIndex)
{
	if (SlotIndex >= 2) return;

	if (PersistentVFXSlots[SlotIndex])
	{
		PersistentVFXSlots[SlotIndex]->DeactivateImmediate();
		PersistentVFXSlots[SlotIndex] = nullptr;
	}
}

void AHellunaEnemyCharacter::Multicast_PlaySoundAtLocation_Implementation(
	USoundBase* Sound, FVector Location)
{
	if (Sound)
	{
		UGameplayStatics::PlaySoundAtLocation(GetWorld(), Sound, Location);
	}
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
