// File: Source/Helluna/Private/Object/ResourceUsingObject/ResourceUsingObject_HealTurret.cpp

#include "Object/ResourceUsingObject/ResourceUsingObject_HealTurret.h"

#include "Character/HellunaHeroCharacter.h"
#include "Character/EnemyComponent/HellunaHealthComponent.h"
#include "Components/SphereComponent.h"
#include "NiagaraComponent.h"
#include "NiagaraFunctionLibrary.h"
#include "TimerManager.h"
#include "Kismet/GameplayStatics.h"
#include "Components/DynamicMeshComponent.h"
#include "Components/StaticMeshComponent.h"


// =========================================================
// 생성자
// =========================================================

AResourceUsingObject_HealTurret::AResourceUsingObject_HealTurret()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.TickInterval = 0.f; // 매 프레임 (회전 보간)

	bReplicates = true;
	bAlwaysRelevant = true;

	// ── [파트1] 베이스 메쉬 (고정) — Base 클래스의 DynamicMeshComponent ──

	// ── [파트2] 고정 메쉬 ────────────────────────────────────
	MeshPart2 = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("MeshPart2"));
	if (DynamicMeshComponent)
	{
		MeshPart2->SetupAttachment(DynamicMeshComponent);
	}

	// ── [파트3] 고정 메쉬 ────────────────────────────────────
	MeshPart3 = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("MeshPart3"));
	if (DynamicMeshComponent)
	{
		MeshPart3->SetupAttachment(DynamicMeshComponent);
	}

	// ── [파트4] 회전 파트 ────────────────────────────────────
	SpinRoot = CreateDefaultSubobject<USceneComponent>(TEXT("SpinRoot"));
	if (DynamicMeshComponent)
	{
		SpinRoot->SetupAttachment(DynamicMeshComponent);
	}

	MeshSpin = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("MeshSpin"));
	MeshSpin->SetupAttachment(SpinRoot);

	// ── 힐 범위 구체 ────────────────────────────────────────
	HealRangeSphere = CreateDefaultSubobject<USphereComponent>(TEXT("HealRangeSphere"));
	if (DynamicMeshComponent)
	{
		HealRangeSphere->SetupAttachment(DynamicMeshComponent);
	}
	HealRangeSphere->SetSphereRadius(1000.f);
	HealRangeSphere->SetCollisionProfileName(TEXT("Trigger"));
	HealRangeSphere->SetGenerateOverlapEvents(true);
	HealRangeSphere->SetCollisionResponseToAllChannels(ECR_Ignore);
	HealRangeSphere->SetCollisionResponseToChannel(ECC_Pawn, ECR_Overlap);

	// ── 범위 표시 나이아가라 ─────────────────────────────────
	RangeIndicatorComponent = CreateDefaultSubobject<UNiagaraComponent>(TEXT("RangeIndicatorComponent"));
	if (DynamicMeshComponent)
	{
		RangeIndicatorComponent->SetupAttachment(DynamicMeshComponent);
	}
	RangeIndicatorComponent->bAutoActivate = true;
}

// =========================================================
// Tick — 회전 파트 회전
// =========================================================

void AResourceUsingObject_HealTurret::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

	if (SpinRoot && SpinSpeed != 0.f)
	{
		SpinRoot->AddLocalRotation(FRotator(0.f, SpinSpeed * DeltaTime, 0.f));
	}
}

// =========================================================
// BeginPlay / EndPlay
// =========================================================

void AResourceUsingObject_HealTurret::BeginPlay()
{
	Super::BeginPlay();

	// 서버에서만 힐 타이머 시작
	if (HasAuthority())
	{
		GetWorldTimerManager().SetTimer(
			HealTimerHandle,
			this,
			&ThisClass::PerformHeal,
			HealInterval,
			true // 반복
		);

	}
}

void AResourceUsingObject_HealTurret::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	GetWorldTimerManager().ClearTimer(HealTimerHandle);
	Super::EndPlay(EndPlayReason);
}

// =========================================================
// 힐 로직 (서버)
// =========================================================

void AResourceUsingObject_HealTurret::PerformHeal()
{
	if (!HasAuthority())
	{
		return;
	}

	if (!HealRangeSphere)
	{
		return;
	}

	// 범위 내 오버랩 중인 액터 수집
	TArray<AActor*> OverlappingActors;
	HealRangeSphere->GetOverlappingActors(OverlappingActors, AHellunaHeroCharacter::StaticClass());

	if (OverlappingActors.IsEmpty())
	{
		return;
	}

	TArray<AHellunaHeroCharacter*> HealedTargets;

	for (AActor* Actor : OverlappingActors)
	{
		AHellunaHeroCharacter* Hero = Cast<AHellunaHeroCharacter>(Actor);
		if (!Hero)
		{
			continue;
		}

		// HealthComponent 접근
		UHellunaHealthComponent* HealthComp = Hero->FindComponentByClass<UHellunaHealthComponent>();
		if (!HealthComp)
		{
			continue;
		}

		// 이미 사망한 캐릭터는 힐하지 않음
		if (HealthComp->IsDead())
		{
			continue;
		}

		// 이미 최대 체력이면 힐 불필요 (bPlayVFXWhenFull이면 VFX 대상만 수집)
		const bool bIsFull = FMath::IsNearlyEqual(HealthComp->GetHealth(), HealthComp->GetMaxHealth());
		if (bIsFull)
		{
			if (bPlayVFXWhenFull)
			{
				HealedTargets.Add(Hero);
			}
			continue;
		}

		// 힐 적용 (서버 권위)
		HealthComp->Heal(HealAmount, this);

		HealedTargets.Add(Hero);
	}

	// VFX 재생 (힐된 플레이어가 있을 때만)
	if (!HealedTargets.IsEmpty())
	{
		if (HealNiagaraEffect)
		{
			Multicast_PlayHealEffect(HealedTargets);
		}
	}
}

// =========================================================
// VFX 멀티캐스트
// =========================================================

void AResourceUsingObject_HealTurret::Multicast_PlayHealEffect_Implementation(const TArray<AHellunaHeroCharacter*>& HealedTargets)
{
	if (!HealNiagaraEffect)
	{
		return;
	}

	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	UE_LOG(LogTemp, Warning, TEXT("[HealTurret VFX] Multicast 호출 | Effect=%s | 대상=%d명 | 호출측=%s"),
		*HealNiagaraEffect->GetName(), HealedTargets.Num(),
		HasAuthority() ? TEXT("서버") : TEXT("클라"));

	for (AHellunaHeroCharacter* Hero : HealedTargets)
	{
		if (!IsValid(Hero) || Hero->IsActorBeingDestroyed())
		{
			UE_LOG(LogTemp, Warning, TEXT("[HealTurret VFX] 대상 무효 — 스킵"));
			continue;
		}

		USceneComponent* AttachTarget = Hero->GetRootComponent();
		UE_LOG(LogTemp, Warning, TEXT("[HealTurret VFX] 대상=%s | RootComp=%s | 위치=%s"),
			*Hero->GetName(),
			AttachTarget ? *AttachTarget->GetName() : TEXT("nullptr"),
			*Hero->GetActorLocation().ToString());

		// 캐릭터 루트에 Attach — 이동을 따라감
		UNiagaraComponent* VFXComp = UNiagaraFunctionLibrary::SpawnSystemAttached(
			HealNiagaraEffect,
			AttachTarget,
			NAME_None,
			FVector::ZeroVector,
			FRotator::ZeroRotator,
			EAttachLocation::SnapToTarget,
			true,  // bAutoDestroy
			true,  // bAutoActivate
			ENCPoolMethod::AutoRelease
		);

		if (VFXComp)
		{
			VFXComp->SetWorldScale3D(FVector(HealEffectScale));

			// 힐 사운드 재생
			if (HealSound)
			{
				UGameplayStatics::PlaySoundAtLocation(this, HealSound, Hero->GetActorLocation());
			}

			UE_LOG(LogTemp, Warning, TEXT("[HealTurret VFX] 스폰 성공 | Scale=%.1f | IsActive=%d | Asset=%s | 위치=%s"),
				HealEffectScale, VFXComp->IsActive(),
				*VFXComp->GetAsset()->GetName(),
				*VFXComp->GetComponentLocation().ToString());

			// 1초 후 이펙트 강제 종료
			FTimerHandle VFXTimerHandle;
			TWeakObjectPtr<UNiagaraComponent> WeakComp(VFXComp);
			World->GetTimerManager().SetTimer(VFXTimerHandle, FTimerDelegate::CreateLambda([WeakComp]()
			{
				if (WeakComp.IsValid())
				{
					WeakComp->DeactivateImmediate();
				}
			}), 1.0f, false);
		}
		else
		{
			UE_LOG(LogTemp, Error, TEXT("[HealTurret VFX] SpawnSystemAttached 실패! Effect=%s | AttachTarget=%s"),
				*HealNiagaraEffect->GetName(),
				AttachTarget ? *AttachTarget->GetName() : TEXT("nullptr"));
		}
	}
}

