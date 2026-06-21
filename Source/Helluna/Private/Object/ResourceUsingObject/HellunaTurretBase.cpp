// File: Source/Helluna/Private/Object/ResourceUsingObject/HellunaTurretBase.cpp

#include "Object/ResourceUsingObject/HellunaTurretBase.h"

#include "Character/EnemyComponent/HellunaHealthComponent.h"

#include "Helluna.h"
#include "Components/MeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/SphereComponent.h"
#include "Components/BoxComponent.h"
#include "Engine/World.h"
#include "Engine/StaticMesh.h"
#include "Materials/MaterialInterface.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "NiagaraFunctionLibrary.h"
#include "NiagaraSystem.h"
#include "NiagaraComponent.h"
#include "Net/UnrealNetwork.h"
#include "TimerManager.h"

// =========================================================
// 디졸브 스칼라 보간 (가디언과 동일 — M_Dissolve 계열 + 적 디졸브 파라미터 동시 지원)
// =========================================================
namespace
{
	float TurretDeathDissolveSmoothStep(float Edge0, float Edge1, float Value)
	{
		if (FMath::IsNearlyEqual(Edge0, Edge1))
		{
			return Value >= Edge1 ? 1.0f : 0.0f;
		}

		const float T = FMath::Clamp((Value - Edge0) / (Edge1 - Edge0), 0.0f, 1.0f);
		return T * T * (3.0f - 2.0f * T);
	}

	float ResolveTurretDeathDissolveScalarAmount(FName ParameterName, float Alpha)
	{
		const float ClampedAlpha = FMath::Clamp(Alpha, 0.0f, 1.0f);

		if (ParameterName == FName(TEXT("Animation")))
		{
			return FMath::Clamp(FMath::Pow(ClampedAlpha, 0.68f), 0.0f, 1.0f);
		}
		if (ParameterName == FName(TEXT("DissolveAmount")))
		{
			return FMath::Clamp(FMath::Pow(ClampedAlpha, 0.72f), 0.0f, 1.0f);
		}
		if (ParameterName == FName(TEXT("FadeOut")))
		{
			return FMath::Clamp(FMath::Pow(ClampedAlpha, 0.58f), 0.0f, 1.0f);
		}
		if (ParameterName == FName(TEXT("Ash")))
		{
			return FMath::Clamp(TurretDeathDissolveSmoothStep(0.12f, 0.82f, ClampedAlpha) * 1.35f, 0.0f, 1.35f);
		}
		if (ParameterName == FName(TEXT("Erode")))
		{
			return FMath::Lerp(0.10f, 0.95f, TurretDeathDissolveSmoothStep(0.05f, 1.0f, ClampedAlpha));
		}
		if (ParameterName == FName(TEXT("Opacity")))
		{
			return 1.0f - TurretDeathDissolveSmoothStep(0.05f, 1.0f, ClampedAlpha);
		}

		return ClampedAlpha;
	}
}

// =========================================================
// 생성자
// =========================================================

AHellunaTurretBase::AHellunaTurretBase()
{
	// 포탑은 모두 복제 대상(체력/파괴 상태). 자식이 다시 켜도 무해.
	bReplicates = true;

	// 체력 컴포넌트 (캐릭터/적/우주선과 동일 컴포넌트 재사용, 자동 복제)
	TurretHealthComponent = CreateDefaultSubobject<UHellunaHealthComponent>(TEXT("TurretHealthComponent"));

	// 가디언과 동일한 디졸브 머티리얼 기본값 (BP 없이도 동작)
	DeathDissolveOverrideMaterial = TSoftObjectPtr<UMaterialInterface>(
		FSoftObjectPath(TEXT("/Game/Dissolve/Materials/M_Dissolve/M_Dissolve_01.M_Dissolve_01")));

	// 가디언과 동일한 폭발 FX 기본값 (BP 없이도 동작)
	DeathExplosionFX = TSoftObjectPtr<UNiagaraSystem>(
		FSoftObjectPath(TEXT("/Game/Gihyeon/Guardian/VFX/N_ExplosionAir_006_Guardian.N_ExplosionAir_006_Guardian")));

	DeathDissolveScalarParameterNames = {
		FName(TEXT("Animation")),
		FName(TEXT("DissolveAmount")),
		FName(TEXT("FadeOut")),
		FName(TEXT("Ash")),
		FName(TEXT("Erode")),
		FName(TEXT("Opacity")),
	};
	DeathDissolveVectorParameterNames = {
		FName(TEXT("Edge Color")),
		FName(TEXT("DissolveEdgeColor")),
		FName(TEXT("BurnColor")),
		FName(TEXT("EmissiveColor")),
	};
}

// =========================================================
// BeginPlay / EndPlay
// =========================================================

void AHellunaTurretBase::BeginPlay()
{
	Super::BeginPlay();

	if (TurretHealthComponent)
	{
		// 서버에서 디자이너 지정 MaxHealth 를 컴포넌트에 실제로 적용(컴포넌트 기본 100 무시)하고 풀 충전.
		if (HasAuthority())
		{
			TurretHealthComponent->SetMaxHealth(TurretMaxHealth, /*bRefillHealth=*/true);
		}
		TurretHealthComponent->OnDeath.AddUniqueDynamic(this, &ThisClass::HandleTurretDeath);
	}
}

void AHellunaTurretBase::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(DeathDissolveTimerHandle);
		World->GetTimerManager().ClearTimer(DeathDissolveDelayHandle);
	}

	if (TurretHealthComponent && TurretHealthComponent->IsRegistered())
	{
		TurretHealthComponent->OnDeath.RemoveDynamic(this, &ThisClass::HandleTurretDeath);
	}

	DeathDissolveMeshComponents.Empty();
	DeathDissolveMIDs.Empty();

	Super::EndPlay(EndPlayReason);
}

void AHellunaTurretBase::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	DOREPLIFETIME(AHellunaTurretBase, bTurretDestroyed);
}

// =========================================================
// 체력 조회
// =========================================================

float AHellunaTurretBase::GetTurretHealth() const
{
	return TurretHealthComponent ? TurretHealthComponent->GetHealth() : 0.f;
}

float AHellunaTurretBase::GetTurretMaxHealth() const
{
	return TurretHealthComponent ? TurretHealthComponent->GetMaxHealth() : 0.f;
}

float AHellunaTurretBase::GetTurretHealthPercent() const
{
	return TurretHealthComponent ? TurretHealthComponent->GetHealthNormalized() : 0.f;
}

// =========================================================
// 사망 처리 (서버)
// =========================================================

void AHellunaTurretBase::HandleTurretDeath(AActor* /*DeadActor*/, AActor* KillerActor)
{
	if (!HasAuthority())
	{
		return;
	}

	if (bTurretDestroyed)
	{
		return; // 1회 보장
	}
	bTurretDestroyed = true;

	UE_LOG(LogHelluna, Log, TEXT("[TurretHP] %s 파괴됨 (Killer=%s)"),
		*GetName(), *GetNameSafe(KillerActor));

	// 자식별 기능 정지(타이머/타겟/델리게이트 정리)
	OnTurretDestroyed_StopServerLogic();

	// 게임플레이 훅 (서버) — EndGame 과는 무관
	OnTurretDestroyed.Broadcast(KillerActor);

	// 메시 물리 분리 + 폭발 FX + 디졸브 (서버 + 모든 클라 동기)
	Multicast_OnTurretDeathBreak();

	// 일정 시간 후 액터 자동 정리 (서버 권한)
	if (DeathActorLifetimeSeconds > 0.f)
	{
		SetLifeSpan(DeathActorLifetimeSeconds);
	}
}

void AHellunaTurretBase::OnRep_TurretDestroyed()
{
	// late-join 등으로 Multicast 를 못 받은 클라 — 복제 폴백으로 즉시 사라진 상태로 스냅.
	if (bTurretDestroyed)
	{
		PlayDeathBreakLocal(/*bFromReplication=*/true);
	}
}

void AHellunaTurretBase::Multicast_OnTurretDeathBreak_Implementation()
{
	// 실시간 연출 (서버 multicast 수신 시점에 접속해 있던 모든 머신).
	PlayDeathBreakLocal(/*bFromReplication=*/false);
}

// =========================================================
// 사망 연출 (서버·클라 공통, 1회)
// =========================================================

void AHellunaTurretBase::PlayDeathBreakLocal(bool bFromReplication)
{
	if (bDeathBreakPlayedLocally)
	{
		return; // Multicast/OnRep 이중 진입 방지
	}
	bDeathBreakPlayedLocally = true;

	// ── 모든 머신: 포탑 기능 정지 (회전/공격 Tick) ──
	SetActorTickEnabled(false);

	// ── 모든 머신: 월드 블로킹/트리거 콜리전 전면 차단 ── [TurretHP-DediWall-FIX]
	//   루트 DynamicMeshComponent(BlockAll)·스태틱메시 본체·탐지/회복 구체를 모두 포함한다.
	//   데디서버는 아래 물리 분리가 스킵되므로 여기서 끄지 않으면 파괴된 포탑이 SetLifeSpan(20s)
	//   동안 '보이지 않는 벽'으로 남아 폰/적/사격을 막는다(특히 루트가 BlockAll DynamicMesh 인 HealTurret).
	//   클라는 직후 물리 분리에서 스태틱메시만 PhysicsActor 로 콜리전을 다시 켜 낙하 잔해로 만든다.
	{
		TArray<UPrimitiveComponent*> Primitives;
		GetComponents<UPrimitiveComponent>(Primitives);
		for (UPrimitiveComponent* Prim : Primitives)
		{
			if (!IsValid(Prim))
			{
				continue;
			}
			Prim->SetGenerateOverlapEvents(false);
			Prim->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		}
	}

	// 데디서버는 렌더러·물리·VFX 불필요 → 콜리전만 끄고 종료
	if (IsRunningDedicatedServer())
	{
		return;
	}

	// ── 자체 FX 컴포넌트(예: HealTurret 범위 표시 Niagara) 정지 ──
	//   디졸브 가시성 전파로도 대개 숨겨지지만, bEnableDeathDissolve=false 인 구성까지 안전하게 커버.
	{
		TArray<UNiagaraComponent*> FXComponents;
		GetComponents<UNiagaraComponent>(FXComponents);
		for (UNiagaraComponent* FX : FXComponents)
		{
			if (IsValid(FX))
			{
				FX->Deactivate();
				FX->SetVisibility(false, true);
			}
		}
	}

	// ── late-join(복제 폴백): 이미 한참 전에 죽은 포탑 → 폭발/낙하/디졸브 재생 대신 즉시 사라진 상태로 스냅 ──
	//   서버 권위 사망 시각이 없어 경과 시간 보정이 불가하므로, '방금 터진 듯' 재생되는 시각 모순을 피한다.
	if (bFromReplication)
	{
		TArray<UMeshComponent*> Meshes;
		GetComponents<UMeshComponent>(Meshes);
		for (UMeshComponent* Mesh : Meshes)
		{
			if (IsValid(Mesh))
			{
				Mesh->SetVisibility(false, true);
				Mesh->SetHiddenInGame(true, true);
				Mesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
			}
		}
		return;
	}

	UWorld* World = GetWorld();

	// ── 사망 폭발 VFX ──
	if (!DeathExplosionFX.IsNull())
	{
		if (UNiagaraSystem* ExplosionSystem = DeathExplosionFX.LoadSynchronous())
		{
			const FVector FXLocation = GetActorLocation() + FVector(0.f, 0.f, DeathExplosionFXZOffset);
			UNiagaraComponent* DeathFX = UNiagaraFunctionLibrary::SpawnSystemAtLocation(
				this, ExplosionSystem, FXLocation, FRotator::ZeroRotator, DeathExplosionFXScale,
				true, true, ENCPoolMethod::AutoRelease);
			if (DeathFX)
			{
				DeathFX->Activate(true);
			}
		}
	}

	// ── 메시 물리 분리 ──
	if (bEnableDeathPhysicsBreak)
	{
		const FVector ImpulseOrigin = GetActorLocation();

		TArray<UStaticMeshComponent*> StaticMeshes;
		GetComponents<UStaticMeshComponent>(StaticMeshes);
		for (UStaticMeshComponent* Mesh : StaticMeshes)
		{
			// 메시 에셋이 없는 빈 컴포넌트는 물리화 시 경고만 남기므로 스킵
			if (!IsValid(Mesh) || !Mesh->GetStaticMesh())
			{
				continue;
			}

			Mesh->DetachFromComponent(FDetachmentTransformRules::KeepWorldTransform);
			Mesh->SetMobility(EComponentMobility::Movable);
			Mesh->SetCollisionProfileName(TEXT("PhysicsActor"));
			Mesh->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
			Mesh->SetSimulatePhysics(true);

			if (DeathBreakImpulseStrength > 0.f)
			{
				Mesh->AddRadialImpulse(
					ImpulseOrigin,
					DeathBreakImpulseRadius,
					DeathBreakImpulseStrength,
					ERadialImpulseFalloff::RIF_Constant,
					/*bVelChange=*/true);
			}
		}
	}

	// ── 디졸브 (선택적 지연 후 시작) ──
	if (bEnableDeathDissolve)
	{
		if (DeathDissolveStartDelay > 0.f && World)
		{
			TWeakObjectPtr<AHellunaTurretBase> WeakThis(this);
			World->GetTimerManager().SetTimer(DeathDissolveDelayHandle,
				FTimerDelegate::CreateWeakLambda(this, [WeakThis]()
				{
					if (WeakThis.IsValid())
					{
						WeakThis->StartDeathDissolveVisuals();
					}
				}),
				DeathDissolveStartDelay, false);
		}
		else
		{
			StartDeathDissolveVisuals();
		}
	}

	// ── BP 추가 코스메틱 훅 ──
	BP_OnTurretDeathCosmetic();
}

// =========================================================
// 사망 디졸브 (클라이언트 로컬 시각화 — 가디언과 동일 방식)
// =========================================================

void AHellunaTurretBase::StartDeathDissolveVisuals()
{
	if (IsRunningDedicatedServer())
	{
		return;
	}

	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	World->GetTimerManager().ClearTimer(DeathDissolveTimerHandle);
	DeathDissolveMeshComponents.Reset();
	DeathDissolveMIDs.Reset();

	UMaterialInterface* OverrideMaterial = nullptr;
	if (!DeathDissolveOverrideMaterial.IsNull())
	{
		OverrideMaterial = DeathDissolveOverrideMaterial.LoadSynchronous();
		if (!OverrideMaterial)
		{
			UE_LOG(LogHelluna, Warning,
				TEXT("[TurretDeathDissolve] Override material load failed. Turret=%s Path=%s"),
				*GetNameSafe(this),
				*DeathDissolveOverrideMaterial.ToSoftObjectPath().ToString());
		}
	}

	TArray<UMeshComponent*> MeshComponents;
	GetComponents<UMeshComponent>(MeshComponents);

	for (UMeshComponent* MeshComponent : MeshComponents)
	{
		if (!IsValid(MeshComponent) || MeshComponent->GetNumMaterials() <= 0)
		{
			continue;
		}

		bool bAddedMesh = false;
		const int32 MaterialCount = MeshComponent->GetNumMaterials();
		for (int32 MaterialIndex = 0; MaterialIndex < MaterialCount; ++MaterialIndex)
		{
			UMaterialInterface* SourceMaterial = OverrideMaterial ? OverrideMaterial : MeshComponent->GetMaterial(MaterialIndex);
			if (!SourceMaterial)
			{
				continue;
			}

			UMaterialInstanceDynamic* MID = UMaterialInstanceDynamic::Create(SourceMaterial, this);
			if (!MID)
			{
				continue;
			}

			MeshComponent->SetMaterial(MaterialIndex, MID);
			DeathDissolveMIDs.Add(MID);

			for (const FName& VectorParamName : DeathDissolveVectorParameterNames)
			{
				if (!VectorParamName.IsNone())
				{
					MID->SetVectorParameterValue(VectorParamName, DeathDissolveEdgeColor);
				}
			}

			MID->SetScalarParameterValue(FName(TEXT("Edge")), 0.16f);
			MID->SetScalarParameterValue(FName(TEXT("EdgeOffset")), 0.045f);
			MID->SetScalarParameterValue(FName(TEXT("Boost")), 4.0f);
			MID->SetScalarParameterValue(FName(TEXT("Power")), 1.6f);

			bAddedMesh = true;
		}

		if (bAddedMesh)
		{
			DeathDissolveMeshComponents.Add(MeshComponent);
		}
	}

	if (DeathDissolveMIDs.IsEmpty())
	{
		UE_LOG(LogHelluna, Warning,
			TEXT("[TurretDeathDissolve] No dynamic materials were created. Turret=%s MeshComponents=%d"),
			*GetNameSafe(this), MeshComponents.Num());
		return;
	}

	DeathDissolveStartWorldSeconds = World->GetTimeSeconds();
	ApplyDeathDissolveAmount(0.0f);

	const float SafeTickInterval = FMath::Clamp(DeathDissolveTickInterval, 0.016f, 0.25f);
	World->GetTimerManager().SetTimer(
		DeathDissolveTimerHandle,
		FTimerDelegate::CreateWeakLambda(this, [this]()
		{
			UWorld* LocalWorld = GetWorld();
			if (!LocalWorld)
			{
				return;
			}

			const float SafeDuration = FMath::Max(0.1f, DeathDissolveDuration);
			const float Elapsed = LocalWorld->GetTimeSeconds() - DeathDissolveStartWorldSeconds;
			const float Alpha = FMath::Clamp(Elapsed / SafeDuration, 0.0f, 1.0f);
			ApplyDeathDissolveAmount(Alpha);

			if (Alpha >= 1.0f)
			{
				FinishDeathDissolveVisuals();
			}
		}),
		SafeTickInterval,
		true);
}

void AHellunaTurretBase::ApplyDeathDissolveAmount(float Amount)
{
	const float ClampedAmount = FMath::Clamp(Amount, 0.0f, 1.0f);

	for (UMaterialInstanceDynamic* MID : DeathDissolveMIDs)
	{
		if (!IsValid(MID))
		{
			continue;
		}

		for (const FName& ScalarParamName : DeathDissolveScalarParameterNames)
		{
			if (!ScalarParamName.IsNone())
			{
				MID->SetScalarParameterValue(
					ScalarParamName,
					ResolveTurretDeathDissolveScalarAmount(ScalarParamName, ClampedAmount));
			}
		}
	}
}

void AHellunaTurretBase::FinishDeathDissolveVisuals()
{
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(DeathDissolveTimerHandle);
	}

	ApplyDeathDissolveAmount(1.0f);

	for (UMeshComponent* MeshComponent : DeathDissolveMeshComponents)
	{
		if (!IsValid(MeshComponent))
		{
			continue;
		}

		MeshComponent->SetVisibility(false, true);
		MeshComponent->SetHiddenInGame(true, true);
		MeshComponent->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		MeshComponent->SetSimulatePhysics(false);
	}

	DeathDissolveMeshComponents.Empty();
	DeathDissolveMIDs.Empty();
}
