// Capstone Project Helluna

#include "BossEvent/RealityFractureZone.h"

#include "Character/HellunaEnemyCharacter.h"
#include "Character/HellunaEnemyCharacter_Boss.h"
#include "Character/EnemyComponent/HellunaHealthComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/PostProcessComponent.h"
#include "Components/SphereComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerController.h"
#include "Kismet/GameplayStatics.h"
#include "Camera/CameraShakeBase.h"
#include "Sound/SoundBase.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "NiagaraFunctionLibrary.h"
#include "NiagaraComponent.h"
#include "NiagaraSystem.h"
#include "DrawDebugHelpers.h"

// [TimeFreeze] stasis 동안 굴러가는/회전하는 actor freeze 용
#include "Animation/AnimMontage.h"
#include "Animation/AnimSingleNodeInstance.h"
#include "Animation/SkeletalMeshActor.h"
#include "Weapon/Projectile/HellunaProjectile_Enemy.h"
#include "Weapon/Projectile/HellunaProjectile_Launcher.h"
#include "Enemy/Guardian/HellunaGuardianProjectile.h"
#include "EngineUtils.h" // TActorIterator

// [Camera] spring arm 조정
#include "Character/HellunaHeroCharacter.h"
#include "GameFramework/SpringArmComponent.h"

#define RFZ_LOG(Fmt, ...) UE_LOG(LogTemp, Warning, TEXT("[StasisSalvo] " Fmt), ##__VA_ARGS__)

ARealityFractureZone::ARealityFractureZone()
{
	// [AimLineChargeV1] Tick 켬 — Charge ramp 갱신용. ramp 비활성 시 early-return 으로 비용 0.
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.bStartWithTickEnabled = true;
	bReplicates = true;

	// ZoneSphere — ATimeDistortionZone 구조와 동일. RootComponent. overlap 은 사용 안 함.
	ZoneSphere = CreateDefaultSubobject<USphereComponent>(TEXT("ZoneSphere"));
	ZoneSphere->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	ZoneSphere->SetCollisionObjectType(ECC_WorldStatic);
	ZoneSphere->SetCollisionResponseToAllChannels(ECR_Ignore);
	ZoneSphere->SetCollisionResponseToChannel(ECC_Pawn, ECR_Overlap);
	ZoneSphere->SetCollisionResponseToChannel(ECC_WorldDynamic, ECR_Overlap);
	ZoneSphere->SetGenerateOverlapEvents(false); // 이 패턴은 sphere overlap 미사용
	ZoneSphere->SetHiddenInGame(true);
	ZoneSphere->ShapeColor = FColor::Cyan;
	SetRootComponent(ZoneSphere);

	// PP component — TimeDistortionZone 와 동일 구조. bUnbound=true 로 world-wide.
	PostProcessComp = CreateDefaultSubobject<UPostProcessComponent>(TEXT("PostProcess"));
	PostProcessComp->SetupAttachment(RootComponent);
	PostProcessComp->bUnbound = true;
	PostProcessComp->BlendWeight = 0.f;
	PostProcessComp->bEnabled = false;

	// [DomePreviewV1] BP editor viewport 에서 dome material 미리보기용 editor-only mesh.
	//   ZoneSphere 의 child. OnConstruction 에서 ZoneVisualMesh/Material 자동 set.
	//   bIsEditorOnly + bHiddenInGame → 런타임 영향 0, 디자이너 편의 100%.
	ZoneVisualPreviewMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("ZoneVisualPreview"));
	ZoneVisualPreviewMesh->SetupAttachment(ZoneSphere);
	ZoneVisualPreviewMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	ZoneVisualPreviewMesh->SetCastShadow(false);
	ZoneVisualPreviewMesh->SetHiddenInGame(true);
	ZoneVisualPreviewMesh->bIsEditorOnly = true;
}

// ================================================================
// OnConstruction — BP editor 에서 ZoneVisualMesh/Material 변경 시 viewport 즉시 반영.
//   런타임에도 호출되지만 ZoneVisualPreviewMesh 는 hidden in game 이라 시각 영향 없음.
// ================================================================
void ARealityFractureZone::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);

	if (!ZoneVisualPreviewMesh) return;

	if (ZoneVisualMesh)
	{
		ZoneVisualPreviewMesh->SetStaticMesh(ZoneVisualMesh);
	}
	if (ZoneVisualMaterial)
	{
		ZoneVisualPreviewMesh->SetMaterial(0, ZoneVisualMaterial);
	}
	// [ZoneDomeExpandV2] dome 반경 = DecoyRingRadius × ZoneVisualRadiusMul. mesh 의 실제 unit bounds
	//   측정해 어떤 static mesh 도 같은 world radius 보장 (sphere / octagon / cube 무관).
	const float WorldRadius = DecoyRingRadius * FMath::Max(0.1f, ZoneVisualRadiusMul);
	float UnitRadius = 50.f;
	if (ZoneVisualMesh)
	{
		UnitRadius = FMath::Max(0.1f, ZoneVisualMesh->GetBounds().SphereRadius);
	}
	const float Scale = FMath::Max(0.01f, WorldRadius / UnitRadius);
	ZoneVisualPreviewMesh->SetRelativeScale3D(FVector(Scale));
}

void ARealityFractureZone::BeginPlay()
{
	Super::BeginPlay();
}

void ARealityFractureZone::Destroyed()
{
	if (UWorld* W = GetWorld())
	{
		W->GetTimerManager().ClearTimer(ZoneExpandTimer);
		W->GetTimerManager().ClearTimer(DecoySpawnTimer);
		W->GetTimerManager().ClearTimer(StasisEndTimer);
		W->GetTimerManager().ClearTimer(FireTimer);
		W->GetTimerManager().ClearTimer(FinishTimer);
	}
	// 안전망 — 남아있을지 모를 TimeDilation/PP/Ghost 정리
	LocalApplyTimeDilation(1.f);
	LocalDeactivatePostProcess();
	LocalCleanupGhosts();
	Super::Destroyed();
}

// ================================================================
// ActivateZone — 서버 시퀀스 시작
// ================================================================
void ARealityFractureZone::ActivateZone()
{
	RFZ_LOG("[Phase A] ActivateZone — Auth=%d, DecoyCount=%d, Interval=%.2fs, StasisTD=%.3f, Ring=%.0fcm, AimLen=%.0fcm",
		HasAuthority() ? 1 : 0, DecoyCount, DecoySpawnInterval, StasisTimeDilation, DecoyRingRadius, AimLineLength);
	if (!HasAuthority()) return;

	// [DeadBossGuardV1] 보스가 이미 사망(죽음 시퀀스 진행 중)이면 stasis 존을 활성화하지 않는다.
	//   사망 순간 비행 중이던 StasisSalvo 오브가 사후에 burst → ActivateZone 하면, 존이 죽어가는
	//   보스의 GlobalAnimRateScale 을 0 으로 얼려 죽음 몬타주가 멈추고 dissolve·사망처리가 데드락된다.
	//   죽은 보스는 StasisSalvo 패턴을 쓸 수 없으므로 이 존은 의미 없음 → 통째로 무효화.
	if (OwnerEnemy)
	{
		if (UHellunaHealthComponent* HC = OwnerEnemy->FindComponentByClass<UHellunaHealthComponent>())
		{
			if (HC->IsDead())
			{
				RFZ_LOG("[DeadBossGuardV1] ActivateZone ABORT — 보스 사망 상태, stasis 존 무효화 (죽음 몬타주 보호)");
				Destroy();
				return;
			}
		}
	}

	bZoneActive = true;
	CurrentDecoyIndex = 0;
	ServerDecoys.Empty();

	// 보스 본체 freeze — anim 정지 + movement disable
	if (OwnerEnemy)
	{
		bBossWasFrozen = true;
		if (USkeletalMeshComponent* M = OwnerEnemy->GetMesh())
		{
			SavedBossAnimRateScale = M->GlobalAnimRateScale;
			M->GlobalAnimRateScale = 0.f;
			RFZ_LOG("[BossFreeze] AnimRateScale 0 (saved=%.2f)", SavedBossAnimRateScale);
		}
		if (UCharacterMovementComponent* CMC = OwnerEnemy->GetCharacterMovement())
		{
			CMC->StopMovementImmediately();
			CMC->DisableMovement();
			RFZ_LOG("[BossFreeze] CMC StopMovement + DisableMovement");
		}
	}

	// stasis 중심 — StasisSalvoOrb 가 burst 위치를 지정했으면 그걸 쓰고(= "총알이 부딪힌 자리에서 존이 피어남"),
	//   아니면 종전대로 가장 가까운 플레이어 위치. (orb 는 보통 플레이어 근처에서 burst 하므로 ≈플레이어)
	APawn* Player = FindClosestPlayerPawn();
	FVector PlayerLoc;
	if (bHasStasisCenterOverride)
	{
		PlayerLoc = StasisCenterOverride;
		bHasStasisCenterOverride = false; // 1회용
		RFZ_LOG("[Phase A] Stasis center (orb burst override) = %s", *PlayerLoc.ToString());
	}
	else
	{
		PlayerLoc = Player ? Player->GetActorLocation() : GetActorLocation();
		RFZ_LOG("[Phase A] Stasis center (player loc) = %s", *PlayerLoc.ToString());
	}

	// 모든 머신: 시간 정지 시작 (존 visual 확장만 시작, 카메라/분신은 아직 X)
	Multicast_StartStasis(PlayerLoc);
	bStasisActive = true;

	// [ZoneExpandPhaseV1] ZoneExpandDuration 후: 카메라 풀백 + 분신 spawn 시작.
	//   글로벌 TD 적용 중이라 wall-clock 보정 필요 (StasisAdjustedDelay).
	//   기존 StasisStartDelay 후 첫 분신 spawn 흐름은 ServerOnZoneExpanded 안에서 처리.
	if (UWorld* W = GetWorld())
	{
		const float AdjustedDelay = StasisAdjustedDelay(ZoneExpandDuration);
		RFZ_LOG("[Phase A] Schedule zone-expand-done — wall=%.2fs, scaled=%.4fs",
			ZoneExpandDuration, AdjustedDelay);
		W->GetTimerManager().SetTimer(
			ZoneExpandTimer, this,
			&ARealityFractureZone::ServerOnZoneExpanded,
			AdjustedDelay, false
		);
	}
}

void ARealityFractureZone::DeactivateZone()
{
	RFZ_LOG("[End] DeactivateZone — Auth=%d, Decoys=%d, BossFrozen=%d",
		HasAuthority() ? 1 : 0, ServerDecoys.Num(), bBossWasFrozen ? 1 : 0);
	bZoneActive = false;
	bStasisActive = false;
	bZoneExpanding = false;
	bAimLineRampActive = false;
	bHasStasisCenterOverride = false;

	if (UWorld* W = GetWorld())
	{
		W->GetTimerManager().ClearTimer(ZoneExpandTimer);
		W->GetTimerManager().ClearTimer(DecoySpawnTimer);
		W->GetTimerManager().ClearTimer(StasisEndTimer);
		W->GetTimerManager().ClearTimer(FireTimer);
		W->GetTimerManager().ClearTimer(FinishTimer);
	}

	// 보스 freeze 복원 (서버)
	if (HasAuthority() && bBossWasFrozen && OwnerEnemy)
	{
		if (USkeletalMeshComponent* M = OwnerEnemy->GetMesh())
		{
			M->GlobalAnimRateScale = SavedBossAnimRateScale;
			RFZ_LOG("[BossFreeze] Restored AnimRateScale=%.2f", SavedBossAnimRateScale);
		}
		if (UCharacterMovementComponent* CMC = OwnerEnemy->GetCharacterMovement())
		{
			CMC->SetMovementMode(MOVE_Walking);
			RFZ_LOG("[BossFreeze] Restored CMC MOVE_Walking");
		}
		bBossWasFrozen = false;
	}

	if (HasAuthority())
	{
		Multicast_Cleanup();
	}
}

// ================================================================
// 서버 시퀀스 — Phase B (분신 spawn)
// ================================================================
void ARealityFractureZone::ServerSpawnNextDecoy()
{
	if (!bZoneActive || !HasAuthority()) return;

	UWorld* W = GetWorld();

	// player 위치 (정지 상태라 시작 시점 위치와 동일)
	APawn* Player = FindClosestPlayerPawn();
	const FVector PlayerLoc = Player ? Player->GetActorLocation() : GetActorLocation();

	// 위치 결정 — 진짜 랜덤 분포. 각도 분리 강제 X (분신이 ring 안 만들고 한쪽 cluster 가능 → 반대편 escape 길 생김).
	//   대신 min 거리 분리만 강제 (서로 안 겹치게). 반경도 0.3~1.3 으로 폭넓게.
	//   땅 트레이스 + capsule 클리어런스 통합. 최대 12회 attempt 후 폴백.
	constexpr float TraceUp = 500.f;
	constexpr float TraceDown = 2000.f;
	constexpr float WalkableNormalZ = 0.7f;
	constexpr int32 MaxAttempts = 12;
	constexpr float MinDecoyDistance = 250.f; // 분신끼리 최소 간격 (cm)
	const float CapsuleHalfHeight = OwnerEnemy ? OwnerEnemy->GetSimpleCollisionHalfHeight() : 96.f;
	const float CapsuleRadius = OwnerEnemy ? OwnerEnemy->GetSimpleCollisionRadius() : 42.f;

	FVector DecoyOrigin = FVector::ZeroVector;
	bool bFoundValid = false;
	float ChosenAngle = 0.f;

	// 보스 mesh feet 가 ImpactPoint (ground) 에 닿도록 ghost actor 를 위로 들어올릴 양 (스케일 적용).
	//   Character 기본: mesh RelativeZ = -88 × Z scale 2.205 = -194 → ghost actor 를 ground + 194 에 두어야 mesh 발이 ground.
	float GroundLiftZ = 0.f;
	if (OwnerEnemy)
	{
		if (USkeletalMeshComponent* M = OwnerEnemy->GetMesh())
		{
			const float MeshRelZ = M->GetRelativeLocation().Z;          // 보통 -88
			const float ActorScaleZ = OwnerEnemy->GetActorScale3D().Z;  // 보통 2.205
			GroundLiftZ = -MeshRelZ * ActorScaleZ;
		}
	}

	for (int32 Attempt = 0; Attempt < MaxAttempts && !bFoundValid; ++Attempt)
	{
		// [DecoyRadiusRangeV1] 진짜 랜덤 — 각도 0~360, 반경 DecoyMinRadiusScale~DecoyMaxRadiusScale × DecoyRingRadius.
		//   default 0.37~1.3 — ring 2700 기준 최소 ≈ 1000cm(10m). 플레이어 가두지 않으면서 더 가까운 spawn 허용.
		const float TryAngle = FMath::FRandRange(0.f, 360.f);
		const float MinScale = FMath::Max(0.05f, DecoyMinRadiusScale);
		const float MaxScale = FMath::Max(MinScale + 0.01f, DecoyMaxRadiusScale);
		const float RadiusScale = FMath::FRandRange(MinScale, MaxScale);
		const float ActualRadius = DecoyRingRadius * RadiusScale;
		const float Rad = FMath::DegreesToRadians(TryAngle);
		const FVector OffsetXY(FMath::Cos(Rad) * ActualRadius,
		                        FMath::Sin(Rad) * ActualRadius, 0.f);
		const FVector CandidateXY(PlayerLoc.X + OffsetXY.X, PlayerLoc.Y + OffsetXY.Y, PlayerLoc.Z);

		// 기존 분신과 거리 분리 — 첫 절반 attempt 만 강제 (후반은 spawn 보장 위해 완화)
		if (Attempt < MaxAttempts / 2)
		{
			bool bTooClose = false;
			for (const FDecoyInfo& D : ServerDecoys)
			{
				if (FVector::Dist2D(D.Origin, CandidateXY) < MinDecoyDistance)
				{
					bTooClose = true;
					break;
				}
			}
			if (bTooClose) continue;
		}

		if (!W) break;

		FCollisionQueryParams Params(SCENE_QUERY_STAT(RFZ_DecoyGroundTrace), false);
		Params.AddIgnoredActor(this);
		if (Player) Params.AddIgnoredActor(Player);
		if (OwnerEnemy) Params.AddIgnoredActor(OwnerEnemy);
		// 모든 분신 ghost actor 도 무시 (cleanup 안 된 잔존 시 line trace 가 거기에 막힘)
		for (const TWeakObjectPtr<AActor>& W2 : LocalGhostActors)
		{
			if (AActor* G = W2.Get()) Params.AddIgnoredActor(G);
		}

		FHitResult Hit;
		const FVector Start = CandidateXY + FVector(0, 0, TraceUp);
		const FVector End = CandidateXY + FVector(0, 0, -TraceDown);
		// ECC_Visibility 가 floor 트레이스에 가장 안정적 — WorldStatic 은 일부 맵 floor 가 응답 안 할 수 있음
		const bool bGroundHit = W->LineTraceSingleByChannel(Hit, Start, End, ECC_Visibility, Params);
		if (!bGroundHit)
		{
			RFZ_LOG("[GroundTrace] Attempt %d: NO HIT @ angle=%.1f rad=%.0f", Attempt, TryAngle, ActualRadius);
			continue;
		}
		if (Hit.ImpactNormal.Z < WalkableNormalZ)
		{
			RFZ_LOG("[GroundTrace] Attempt %d: SLOPE NormalZ=%.2f", Attempt, Hit.ImpactNormal.Z);
			continue;
		}

		// 클리어런스 — 작은 sphere 로 ground 위 공간 확인. ground surface 와 안 겹치게 충분한 Z offset.
		//   capsule + ground epsilon 트릭은 GetSimpleCollisionHalfHeight 가 actor 별로 비일관적 (보스에서 38 반환됨)
		//   이라 ground 자체를 잡았었음. 단순 sphere 가 안전.
		const FVector ClearanceCenter = Hit.ImpactPoint + FVector(0, 0, 120.f); // 1.2m 위
		FCollisionObjectQueryParams ObjQuery;
		ObjQuery.AddObjectTypesToQuery(ECC_WorldStatic);
		ObjQuery.AddObjectTypesToQuery(ECC_WorldDynamic);
		const bool bBlocked = W->OverlapAnyTestByObjectType(
			ClearanceCenter, FQuat::Identity, ObjQuery,
			FCollisionShape::MakeSphere(80.f), Params);
		if (bBlocked)
		{
			RFZ_LOG("[GroundTrace] Attempt %d: BLOCKED clearance @ %s", Attempt, *ClearanceCenter.ToString());
			continue;
		}

		// Ghost actor 위치 = ground + lift (mesh feet 가 정확히 ground 에 닿도록)
		DecoyOrigin = Hit.ImpactPoint + FVector(0, 0, GroundLiftZ + DecoyZOffset);
		ChosenAngle = TryAngle;
		bFoundValid = true;
	}

	if (!bFoundValid)
	{
		// 폴백 — 균등 분포 ring slot, lift 적용
		const float FallbackAngle = (360.f / FMath::Max(DecoyCount, 1)) * CurrentDecoyIndex;
		const float Rad = FMath::DegreesToRadians(FallbackAngle);
		const FVector OffsetXY(FMath::Cos(Rad) * DecoyRingRadius,
		                        FMath::Sin(Rad) * DecoyRingRadius, GroundLiftZ + DecoyZOffset);
		DecoyOrigin = PlayerLoc + OffsetXY;
		ChosenAngle = FallbackAngle;
		RFZ_LOG("Decoy %d random+ground search failed → flat fallback angle=%.1f at %s",
			CurrentDecoyIndex + 1, FallbackAngle, *DecoyOrigin.ToString());
	}

	// 분신이 player 향한 회전
	FRotator DecoyRot = FRotator::ZeroRotator;
	const FVector ToPlayer = (PlayerLoc - DecoyOrigin).GetSafeNormal2D();
	if (!ToPlayer.IsNearlyZero()) DecoyRot = ToPlayer.Rotation();

	// 서버 record (Phase D 발사 시 사용)
	FDecoyInfo Info;
	Info.Origin = DecoyOrigin;
	Info.AimTarget = PlayerLoc;
	ServerDecoys.Add(Info);

	// 모든 머신 시각: ghost spawn + aim beam
	Multicast_SpawnDecoy(DecoyOrigin, DecoyRot, PlayerLoc, CurrentDecoyIndex);

	RFZ_LOG("Decoy %d/%d spawned — Origin=%s, Aim=%s",
		CurrentDecoyIndex + 1, DecoyCount,
		*DecoyOrigin.ToString(), *PlayerLoc.ToString());

	CurrentDecoyIndex++;

	if (W)
	{
		if (CurrentDecoyIndex < DecoyCount)
		{
			// [StasisSalvoV2] 다음 spawn 간격 — 한 명 나올 때마다 점점 빨라짐 (DecoySpawnInterval × accel^(N-1)),
			//   하한 DecoySpawnIntervalMin. 거기에 ±25% 랜덤만 살짝 얹어 약간 들쭉날쭉.
			const int32 Exp = FMath::Max(0, CurrentDecoyIndex - 1);
			const float AccelMul = FMath::Pow(FMath::Clamp(DecoySpawnIntervalAccel, 0.1f, 1.f), static_cast<float>(Exp));
			float BaseInterval = FMath::Max(DecoySpawnInterval * AccelMul, DecoySpawnIntervalMin);
			const float ActualInterval = BaseInterval * FMath::FRandRange(0.75f, 1.25f);
			W->GetTimerManager().SetTimer(
				DecoySpawnTimer, this,
				&ARealityFractureZone::ServerSpawnNextDecoy,
				StasisAdjustedDelay(ActualInterval), false
			);
			RFZ_LOG("[Decoy] next #%d in %.2fs (base=%.2f, accel^%d=%.2f)",
				CurrentDecoyIndex + 1, ActualInterval, BaseInterval, Exp, AccelMul);
		}
		else
		{
			// 모든 분신 spawn 완료 → idle 후 stasis 해제
			W->GetTimerManager().SetTimer(
				StasisEndTimer, this,
				&ARealityFractureZone::ServerEndStasis,
				StasisAdjustedDelay(StasisHoldAfterSpawn), false
			);
		}
	}
}

// ================================================================
// 서버 시퀀스 — Phase C (시간 정지 해제 + plan window)
// ================================================================
void ARealityFractureZone::ServerEndStasis()
{
	if (!HasAuthority()) return;

	RFZ_LOG("EndStasis — plan window %.2fs 시작", PlanWindowDuration);
	bStasisActive = false;

	Multicast_EndStasis();

	if (UWorld* W = GetWorld())
	{
		W->GetTimerManager().SetTimer(
			FireTimer, this,
			&ARealityFractureZone::ServerFire,
			FMath::Max(PlanWindowDuration, 0.05f), false
		);
	}
}

// ================================================================
// 서버 시퀀스 — Phase D (일제 발사)
// ================================================================
void ARealityFractureZone::ServerFire()
{
	if (!HasAuthority()) return;

	RFZ_LOG("Fire — %d 분신 일제 발사", ServerDecoys.Num());

	Multicast_Fire();

	UWorld* W = GetWorld();
	if (!W) return;

	if (DecoyProjectileClass)
	{
		// 권장 경로 — 보스 BP_Boss_RangeProjectile 같은 projectile 을 각 분신 origin 에서 forward 방향 spawn.
		//   projectile 자체가 ProjectileMovement 으로 진행 + on-hit damage 처리 (replicated).
		APawn* InstigatorPawn = OwnerEnemy ? Cast<APawn>(OwnerEnemy) : nullptr;
		int32 SpawnedCount = 0;
		for (const FDecoyInfo& D : ServerDecoys)
		{
			const FVector Dir = (D.AimTarget - D.Origin).GetSafeNormal();
			if (Dir.IsNearlyZero()) continue;

			FActorSpawnParameters SP;
			SP.Owner = OwnerEnemy;
			SP.Instigator = InstigatorPawn;
			SP.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

			AActor* Proj = W->SpawnActor<AActor>(DecoyProjectileClass, D.Origin, Dir.Rotation(), SP);
			if (Proj)
			{
				SpawnedCount++;
				// HellunaProjectile_Enemy 의 InitProjectile 명시 호출 — velocity/damage 설정.
				if (AHellunaProjectile_Enemy* EnemyProj = Cast<AHellunaProjectile_Enemy>(Proj))
				{
					EnemyProj->InitProjectile(DamagePerLine, Dir, DecoyProjectileSpeed,
						DecoyProjectileLifeSeconds, OwnerEnemy);
				}
				// freeze list 에 우연히 잡혀 frozen 상태일 가능성 안전망
				Proj->CustomTimeDilation = 1.f;
			}
		}
		RFZ_LOG("[Phase D] Projectiles spawned=%d, Speed=%.0f, Damage=%.0f",
			SpawnedCount, DecoyProjectileSpeed, DamagePerLine);
	}
	else
	{
		// fallback — capsule sweep (이전 동작)
		AController* InstigatorController = OwnerEnemy ? OwnerEnemy->GetController() : nullptr;
		const float EffectiveLength = GetEffectiveAimLineLength();
		for (const FDecoyInfo& D : ServerDecoys)
		{
			const FVector Dir = (D.AimTarget - D.Origin).GetSafeNormal();
			if (Dir.IsNearlyZero()) continue;
			const FVector End = D.Origin + Dir * EffectiveLength;

			FCollisionQueryParams Params(SCENE_QUERY_STAT(StasisFire), false);
			Params.AddIgnoredActor(this);
			if (OwnerEnemy) Params.AddIgnoredActor(OwnerEnemy);

			TArray<FHitResult> Hits;
			const bool bHit = W->SweepMultiByChannel(Hits, D.Origin, End, FQuat::Identity,
				ECC_Pawn, FCollisionShape::MakeSphere(FMath::Max(AimLineWidth, 5.f)), Params);

			int32 DamageHitCount = 0;
			if (bHit)
			{
				TSet<AActor*> AlreadyDamaged;
				for (const FHitResult& H : Hits)
				{
					AActor* Hit = H.GetActor();
					if (!Hit || AlreadyDamaged.Contains(Hit)) continue;
					if (Hit == OwnerEnemy || Hit == this) continue;
					AlreadyDamaged.Add(Hit);
					UGameplayStatics::ApplyPointDamage(Hit, DamagePerLine, Dir, H,
						InstigatorController, OwnerEnemy,
						DamageType ? *DamageType : UDamageType::StaticClass());
					DamageHitCount++;
				}
			}
			RFZ_LOG("[Phase D fallback] Line — %d hit(s)", DamageHitCount);
		}
	}

	if (UWorld* W2 = GetWorld())
	{
		W2->GetTimerManager().SetTimer(
			FinishTimer, this,
			&ARealityFractureZone::ServerFinishPattern,
			FMath::Max(PostFireDelay, 0.05f), false
		);
	}
}

void ARealityFractureZone::ServerFinishPattern()
{
	if (!HasAuthority()) return;
	RFZ_LOG("FinishPattern");
	Multicast_Cleanup();
	NotifyPatternFinished(false);
}

APawn* ARealityFractureZone::FindClosestPlayerPawn() const
{
	UWorld* W = GetWorld();
	if (!W) return nullptr;

	APawn* Best = nullptr;
	float BestDistSq = TNumericLimits<float>::Max();
	const FVector BossLoc = OwnerEnemy ? OwnerEnemy->GetActorLocation() : GetActorLocation();

	for (FConstPlayerControllerIterator It = W->GetPlayerControllerIterator(); It; ++It)
	{
		APlayerController* PC = It->Get();
		if (!PC) continue;
		APawn* P = PC->GetPawn();
		if (!P) continue;
		const float D = FVector::DistSquared(P->GetActorLocation(), BossLoc);
		if (D < BestDistSq)
		{
			BestDistSq = D;
			Best = P;
		}
	}
	return Best;
}

// ================================================================
// Multicast — 모든 머신 동기화
// ================================================================
void ARealityFractureZone::Multicast_StartStasis_Implementation(FVector PlayerStasisCenter)
{
	RFZ_LOG("[Phase B] Multicast_StartStasis — Auth=%d, Center=%s, TD=%.3f",
		HasAuthority() ? 1 : 0, *PlayerStasisCenter.ToString(), StasisTimeDilation);

	// [ZoneExpandPhaseV1] 존 확장 ramp 시작 — wall-clock 기준. 분신/카메라/aim line ramp 는 아직 시작 X.
	//   ServerOnZoneExpanded → Multicast_OnZoneExpanded 가 ZoneExpandDuration 후에 켬.
	//   Zone 의 CustomTimeDilation 보정 → Tick 정상 frequency.
	if (UWorld* WMid = GetWorld())
	{
		ZoneExpandStartRealTime = WMid->GetRealTimeSeconds();
	}
	bZoneExpanding = true;
	bAimLineRampActive = false;
	if (StasisTimeDilation > KINDA_SMALL_NUMBER)
	{
		CustomTimeDilation = 1.f / StasisTimeDilation; // global TD × custom = 1.0 effective
	}

	LastStasisCenter = PlayerStasisCenter; // PP zone parameter 용

	// Zone actor 를 player 중심으로 이동 + sphere radius 설정 (TimeDistortionZone 구조 일치).
	//   bUnbound PP 라 collision 위치는 시각에 영향 없지만, ZoneCenter/ZoneRadius MID 파라미터와
	//   디버그 시각화 (ShapeColor) 일치를 위해 세팅.
	{
		const float ZoneRadius = DecoyRingRadius * ZoneVisualRadiusMul;
		if (ZoneSphere)
		{
			ZoneSphere->SetSphereRadius(ZoneRadius);
		}
		SetActorLocation(PlayerStasisCenter);
		RFZ_LOG("[Zone] center=%s, radius=%.0fcm (DecoyRing=%.0f × Mul=%.2f)",
			*PlayerStasisCenter.ToString(), ZoneRadius, DecoyRingRadius, ZoneVisualRadiusMul);
	}

	LocalActivatePostProcess();
	LocalApplyTimeDilation(StasisTimeDilation);

	// Zone visual dome — sphere mesh + translucent material, player 중심
	UWorld* W = GetWorld();
	if (W && ZoneVisualMesh && !ZoneVisualMeshComp)
	{
		ZoneVisualMeshComp = NewObject<UStaticMeshComponent>(this);
		if (ZoneVisualMeshComp)
		{
			ZoneVisualMeshComp->SetStaticMesh(ZoneVisualMesh);
			ZoneVisualMeshComp->SetCollisionEnabled(ECollisionEnabled::NoCollision);
			ZoneVisualMeshComp->SetCastShadow(false);
			ZoneVisualMeshComp->SetMobility(EComponentMobility::Movable);
			ZoneVisualMeshComp->RegisterComponentWithWorld(W);
			ZoneVisualMeshComp->AttachToComponent(GetRootComponent(), FAttachmentTransformRules::KeepRelativeTransform);

			// 머터리얼 override — MID 생성 후 Base_Color HDR 적용 (M_Master_ForceField_01 호환).
			//   TimeDistortion 이 dome 에 (65, 10, 46) HDR 마젠타를 넣어 bloom + atmospheric scattering 으로
			//   하늘/원거리까지 색이 깔리는 시각이 나옴. 동일하게 재현.
			if (ZoneVisualMaterial)
			{
				UMaterialInstanceDynamic* DomeMID = UMaterialInstanceDynamic::Create(ZoneVisualMaterial, this);
				UMaterialInterface* DomeMatToApply = DomeMID ? Cast<UMaterialInterface>(DomeMID) : ZoneVisualMaterial.Get();
				const int32 NumMaterials = ZoneVisualMeshComp->GetNumMaterials();
				for (int32 i = 0; i < NumMaterials; ++i)
				{
					ZoneVisualMeshComp->SetMaterial(i, DomeMatToApply);
				}
				if (DomeMID)
				{
					DomeMID->SetVectorParameterValue(FName("Base_Color"), DomeBaseColor);
				}
			}

			// [ZoneDomeExpandV1] SM_Sphere 빌트인 = 반경 50cm. 시작 scale 0 — Tick 에서 PP ZoneRadius
			//   ramp 와 동일한 Charge 값으로 0 → max 로 커지면서 퍼지는 느낌. 위치는 player 중심 고정.
			ZoneVisualMeshComp->SetWorldScale3D(FVector(KINDA_SMALL_NUMBER));
			ZoneVisualMeshComp->SetWorldLocation(PlayerStasisCenter);
			const float TargetRadius = DecoyRingRadius * ZoneVisualRadiusMul;
			RFZ_LOG("[ZoneDome] spawned — TargetRadius=%.0fcm, Scale=0 (ramp via Tick)", TargetRadius);
		}
	}
	else if (!ZoneVisualMesh)
	{
		RFZ_LOG("[ZoneDome] skip — ZoneVisualMesh BP CDO 미설정");
	}

	// Player movement input lock (각 클라 로컬 PC)
	if (W && bLockPlayerMovementDuringStasis)
	{
		int32 LockedCount = 0;
		for (FConstPlayerControllerIterator It = W->GetPlayerControllerIterator(); It; ++It)
		{
			if (APlayerController* PC = It->Get())
			{
				PC->SetIgnoreMoveInput(true);
				LockedCount++;
			}
		}
		RFZ_LOG("[InputLock] SetIgnoreMoveInput(true) on %d PC(s)", LockedCount);
	}

	if (StasisStartSound)
	{
		UGameplayStatics::PlaySoundAtLocation(W, StasisStartSound, PlayerStasisCenter);
	}

	// [TimeFreeze] stasis 동안 굴러가는/회전하는 actor freeze
	//   글로벌 TD 0.001 만으로는 RotatingMovementComponent / ProjectileMovement / material panner 가
	//   미세하게 진행되는 시각이 보임. CustomTimeDilation = 0 이면 actor 의 모든 component tick 정지.
	if (W)
	{
		int32 FrozenProjectiles = 0;
		int32 FrozenSkelMeshActors = 0;

		for (TActorIterator<AHellunaProjectile_Enemy> It(W); It; ++It)
		{
			if (AActor* A = *It) { A->CustomTimeDilation = 0.f; FrozenProjectiles++; }
		}
		for (TActorIterator<AHellunaProjectile_Launcher> It(W); It; ++It)
		{
			if (AActor* A = *It) { A->CustomTimeDilation = 0.f; FrozenProjectiles++; }
		}
		for (TActorIterator<AHellunaGuardianProjectile> It(W); It; ++It)
		{
			if (AActor* A = *It) { A->CustomTimeDilation = 0.f; FrozenProjectiles++; }
		}
		// 페이즈2 갑옷 조각 등 — RotatingMovement 사용하는 ASkeletalMeshActor freeze.
		//   단 zone 자체와 분신 ghost actor 는 ASkeletalMeshActor 가 아니라 generic AActor 라 영향 없음.
		for (TActorIterator<ASkeletalMeshActor> It(W); It; ++It)
		{
			if (AActor* A = *It) { A->CustomTimeDilation = 0.f; FrozenSkelMeshActors++; }
		}
		RFZ_LOG("[TimeFreeze] frozen Projectiles=%d, SkelMeshActors=%d", FrozenProjectiles, FrozenSkelMeshActors);
	}

	// [ZoneExpandPhaseV1] 카메라 풀백은 Multicast_OnZoneExpanded 에서 (존 완전 확장 후) 처리.
	//   Multicast_StartStasis 에서는 더 이상 카메라를 건드리지 않음.
}

// ================================================================
// [ZoneExpandPhaseV1] 존 visual 완전 확장 (server timer 콜백)
//   → Multicast_OnZoneExpanded (카메라 풀백 + aim line ramp 시작)
//   → 첫 분신 spawn timer 예약 (기존 StasisStartDelay 후 첫 spawn 흐름 재사용)
// ================================================================
void ARealityFractureZone::ServerOnZoneExpanded()
{
	if (!bZoneActive || !HasAuthority()) return;
	RFZ_LOG("[Phase B] ServerOnZoneExpanded — 존 확장 완료, 카메라/분신 시작");

	Multicast_OnZoneExpanded();

	if (UWorld* W = GetWorld())
	{
		const float AdjustedDelay = StasisAdjustedDelay(StasisStartDelay);
		W->GetTimerManager().SetTimer(
			DecoySpawnTimer, this,
			&ARealityFractureZone::ServerSpawnNextDecoy,
			AdjustedDelay, false
		);
	}
}

void ARealityFractureZone::Multicast_OnZoneExpanded_Implementation()
{
	RFZ_LOG("[Phase B] Multicast_OnZoneExpanded — Auth=%d, 카메라 풀백 + aim line ramp 시작",
		HasAuthority() ? 1 : 0);

	// 존 확장 phase 종료, aim line ramp 활성화
	bZoneExpanding = false;
	if (UWorld* WMid = GetWorld())
	{
		AimLineRampStartRealTime = WMid->GetRealTimeSeconds();
	}
	bAimLineRampActive = true;

	// [Camera] player spring arm length 조정 — 회피 시야 확보. 백업 후 변경.
	//   [CameraZoomGradualV1] CameraZoomDuration>0 이면 snap 대신 Tick 에서 점진 lerp.
	UWorld* W = GetWorld();
	if (W && bAdjustCameraDuringPattern)
	{
		int32 CamAdjusted = 0;
		CameraZoomStartLengths.Empty();
		for (FConstPlayerControllerIterator It = W->GetPlayerControllerIterator(); It; ++It)
		{
			APlayerController* PC = It->Get();
			if (!PC) continue;
			APawn* P = PC->GetPawn();
			if (AHellunaHeroCharacter* Hero = Cast<AHellunaHeroCharacter>(P))
			{
				if (USpringArmComponent* CB = Hero->GetCameraBoom())
				{
					SavedSpringArmLengths.Add(P, CB->TargetArmLength);
					if (CameraZoomDuration > KINDA_SMALL_NUMBER)
					{
						CameraZoomStartLengths.Add(P, CB->TargetArmLength);
					}
					else
					{
						CB->TargetArmLength = StasisCameraDistance;
					}
					CamAdjusted++;
				}
			}
		}
		if (CameraZoomDuration > KINDA_SMALL_NUMBER && CameraZoomStartLengths.Num() > 0)
		{
			bCameraZoomActive = true;
			CameraZoomStartRealTime = W->GetRealTimeSeconds();
			RFZ_LOG("[Camera] gradual zoom started on %d hero(es) → %.0fcm over %.2fs",
				CamAdjusted, StasisCameraDistance, CameraZoomDuration);
		}
		else
		{
			RFZ_LOG("[Camera] spring arm SNAP on %d hero(es) → %.0fcm", CamAdjusted, StasisCameraDistance);
		}
	}
}

void ARealityFractureZone::Multicast_SpawnDecoy_Implementation(
	FVector Origin, FRotator Rot, FVector AimTarget, int32 DecoyIndex)
{
	RFZ_LOG("Multicast_SpawnDecoy %d — Origin=%s", DecoyIndex, *Origin.ToString());
	LocalSpawnGhostMesh(Origin, Rot, AimTarget, DecoyIndex);
}

void ARealityFractureZone::Multicast_EndStasis_Implementation()
{
	RFZ_LOG("[Phase C] Multicast_EndStasis — Auth=%d, plan window 시작 (%.2fs)",
		HasAuthority() ? 1 : 0, PlanWindowDuration);
	LocalApplyTimeDilation(1.f);
	LocalDeactivatePostProcess();

	// [AimLineChargeV1] global TD 복원 후 zone 의 CustomTimeDilation 도 1.0 으로 복원 (effective TD 1.0).
	CustomTimeDilation = 1.f;

	// player input 복원
	UWorld* W = GetWorld();
	if (W && bLockPlayerMovementDuringStasis)
	{
		int32 UnlockedCount = 0;
		for (FConstPlayerControllerIterator It = W->GetPlayerControllerIterator(); It; ++It)
		{
			if (APlayerController* PC = It->Get())
			{
				PC->SetIgnoreMoveInput(false);
				UnlockedCount++;
			}
		}
		RFZ_LOG("[InputLock] SetIgnoreMoveInput(false) on %d PC(s)", UnlockedCount);
	}

	// [TimeFreeze] stasis 동안 freeze 한 actor 들 CustomTimeDilation = 1.0 복원
	if (W)
	{
		int32 ThawedProjectiles = 0;
		int32 ThawedSkelMeshActors = 0;
		for (TActorIterator<AHellunaProjectile_Enemy> It(W); It; ++It)
		{
			if (AActor* A = *It) { A->CustomTimeDilation = 1.f; ThawedProjectiles++; }
		}
		for (TActorIterator<AHellunaProjectile_Launcher> It(W); It; ++It)
		{
			if (AActor* A = *It) { A->CustomTimeDilation = 1.f; ThawedProjectiles++; }
		}
		for (TActorIterator<AHellunaGuardianProjectile> It(W); It; ++It)
		{
			if (AActor* A = *It) { A->CustomTimeDilation = 1.f; ThawedProjectiles++; }
		}
		for (TActorIterator<ASkeletalMeshActor> It(W); It; ++It)
		{
			if (AActor* A = *It) { A->CustomTimeDilation = 1.f; ThawedSkelMeshActors++; }
		}
		RFZ_LOG("[TimeFreeze] thawed Projectiles=%d, SkelMeshActors=%d", ThawedProjectiles, ThawedSkelMeshActors);
	}

	// 분신 aim beam 비활성 + 사격 애니 재생 시작
	int32 BeamsKilled = 0;
	int32 AnimsResumed = 0;
	for (TWeakObjectPtr<AActor>& Weak : LocalGhostActors)
	{
		AActor* G = Weak.Get();
		if (!G) continue;

		// aim beam Niagara 즉시 destroy — Deactivate 로는 active particle 잔존 가능
		TArray<UNiagaraComponent*> Niagaras;
		G->GetComponents<UNiagaraComponent>(Niagaras);
		for (UNiagaraComponent* NC : Niagaras)
		{
			if (NC) { NC->DestroyComponent(); BeamsKilled++; }
		}

		// [AimLineChargeV1] aim line static mesh 는 plan window 동안 유지 — Charge ramp 가 cool→hot 으로
		//   가속하며 위협 텔레그래핑. Multicast_Fire 에서 line destroy + cache reset.

		// SkelMesh anim — frame 0 부터 재생 (Shoot notify 자세에서 시작 X, 풀 몬타지 한 번 더 재생).
		//   원거리 공격 몬타지가 처음부터 끝까지 재생되어 진짜 발사 모션 같은 느낌.
		TArray<USkeletalMeshComponent*> SkelMeshes;
		G->GetComponents<USkeletalMeshComponent>(SkelMeshes);
		for (USkeletalMeshComponent* SM : SkelMeshes)
		{
			if (!SM) continue;
			SM->bPauseAnims = false;
			if (UAnimSingleNodeInstance* SingleNode = SM->GetSingleNodeInstance())
			{
				SingleNode->SetPosition(0.f, false);
				SingleNode->SetPlaying(true);
			}
			AnimsResumed++;
		}
	}
	RFZ_LOG("[Phase C] aim beams deactivated=%d, ghost anim resumed=%d", BeamsKilled, AnimsResumed);
}

void ARealityFractureZone::Multicast_Fire_Implementation()
{
	RFZ_LOG("[Phase D] Multicast_Fire — Auth=%d, GhostActors=%d",
		HasAuthority() ? 1 : 0, LocalGhostActors.Num());

	UWorld* W = GetWorld();
	if (!W) return;

	// [AimLineChargeV1] ramp 종료 + line cleanup. fire 직전 최종 charge=1 보장 한 프레임 → projectile 출현.
	bAimLineRampActive = false;
	for (TWeakObjectPtr<UMaterialInstanceDynamic>& WeakMID : AimLineMIDs)
	{
		if (UMaterialInstanceDynamic* MID = WeakMID.Get())
		{
			MID->SetScalarParameterValue(TEXT("Charge"), 1.f);
		}
	}
	for (TWeakObjectPtr<UStaticMeshComponent>& WeakLine : AimLineComps)
	{
		if (UStaticMeshComponent* SMC = WeakLine.Get())
		{
			SMC->DestroyComponent();
		}
	}
	AimLineMIDs.Reset();
	AimLineComps.Reset();

	if (FireSound)
	{
		UGameplayStatics::PlaySoundAtLocation(W, FireSound, GetActorLocation());
	}

	// 발사 순간 — 궤적선 표시 X (실제 projectile 만 날아감, plan window 의 aim beam 은 EndStasis 에서 이미 제거됨)
	//   FireBurstVFX 만 ghost 에 attach (총구 화염 같은 burst 효과).
	int32 BeamCount = 0;
	for (const TWeakObjectPtr<AActor>& Weak : LocalGhostActors)
	{
		AActor* G = Weak.Get();
		if (!G) continue;

		// FireBurstVFX 를 ghost 에 attach — ghost destroy 시 함께 cleanup, NS 누수 방지
		if (FireBurstVFX)
		{
			UNiagaraFunctionLibrary::SpawnSystemAttached(
				FireBurstVFX, G->GetRootComponent(), NAME_None,
				FVector::ZeroVector, FRotator::ZeroRotator,
				EAttachLocation::KeepRelativeOffset, true);
		}
		BeamCount++;
	}
	RFZ_LOG("[Phase D] Visualized %d fire beam(s)", BeamCount);

	if (FireCameraShake)
	{
		for (FConstPlayerControllerIterator It = W->GetPlayerControllerIterator(); It; ++It)
		{
			if (APlayerController* PC = It->Get())
			{
				PC->ClientStartCameraShake(FireCameraShake, 1.f);
			}
		}
	}
}

void ARealityFractureZone::Multicast_Cleanup_Implementation()
{
	RFZ_LOG("[Cleanup] Multicast_Cleanup — Auth=%d", HasAuthority() ? 1 : 0);
	LocalApplyTimeDilation(1.f);
	LocalDeactivatePostProcess();
	LocalCleanupGhosts();

	// Zone visual dome destroy
	if (ZoneVisualMeshComp)
	{
		ZoneVisualMeshComp->DestroyComponent();
		ZoneVisualMeshComp = nullptr;
	}

	// [Camera] spring arm 복원
	for (auto& Pair : SavedSpringArmLengths)
	{
		APawn* P = Pair.Key.Get();
		if (!P) continue;
		if (AHellunaHeroCharacter* Hero = Cast<AHellunaHeroCharacter>(P))
		{
			if (USpringArmComponent* CB = Hero->GetCameraBoom())
			{
				CB->TargetArmLength = Pair.Value;
			}
		}
	}
	SavedSpringArmLengths.Empty();
	// [CameraZoomGradualV1] zoom 상태 reset
	CameraZoomStartLengths.Empty();
	bCameraZoomActive = false;

	// 안전망 — input lock 미해제 케이스 복원 + 혹시 frozen 채로 남은 actor 복원
	UWorld* W = GetWorld();
	if (W)
	{
		for (FConstPlayerControllerIterator It = W->GetPlayerControllerIterator(); It; ++It)
		{
			if (APlayerController* PC = It->Get())
			{
				PC->SetIgnoreMoveInput(false);
			}
		}
		// thaw 안전망 (EndStasis 가 호출 안 된 경우 대비)
		for (TActorIterator<AHellunaProjectile_Enemy> It(W); It; ++It)
		{
			if (AActor* A = *It) A->CustomTimeDilation = 1.f;
		}
		for (TActorIterator<AHellunaProjectile_Launcher> It(W); It; ++It)
		{
			if (AActor* A = *It) A->CustomTimeDilation = 1.f;
		}
		for (TActorIterator<AHellunaGuardianProjectile> It(W); It; ++It)
		{
			if (AActor* A = *It) A->CustomTimeDilation = 1.f;
		}
		for (TActorIterator<ASkeletalMeshActor> It(W); It; ++It)
		{
			if (AActor* A = *It) A->CustomTimeDilation = 1.f;
		}
	}
}

// ================================================================
// 로컬 헬퍼
// ================================================================
void ARealityFractureZone::LocalApplyTimeDilation(float Value)
{
	if (UWorld* W = GetWorld())
	{
		UGameplayStatics::SetGlobalTimeDilation(W, FMath::Clamp(Value, 0.001f, 10.f));
	}
}

void ARealityFractureZone::LocalActivatePostProcess()
{
	if (!PostProcessComp) return;
	if (!StasisPostProcessMaterial) return;

	StasisPPMID = UMaterialInstanceDynamic::Create(StasisPostProcessMaterial, this);
	if (!StasisPPMID) return;

	// [PPScreenRadialV2] M_PP_StasisSalvo = screen-space radial desat/tint (Charge 0..1 로 화면 중앙→외곽 채움).
	//   유저가 좋아한 "전의 색". world-space ZoneCenter/Radius 도 backward-compat 으로 set 하지만 화면 효과는
	//   Charge 가 지배. Charge 는 Tick 에서 dome ramp(0→1) 와 동일 alpha 로 갱신 → 펴지는 느낌 유지.
	const FVector Center = LastStasisCenter;
	const float Radius = DecoyRingRadius * ZoneVisualRadiusMul;
	StasisPPMID->SetVectorParameterValue(FName("ZoneCenter"), FLinearColor(Center.X, Center.Y, Center.Z));
	StasisPPMID->SetScalarParameterValue(FName("ZoneRadius"), 0.f);
	StasisPPMID->SetScalarParameterValue(FName("Charge"), 0.f);
	StasisPPMID->SetScalarParameterValue(FName("Strength"), 0.9f);
	StasisPPMID->SetScalarParameterValue(FName("EdgeSoftness"), 150.f);

	// 기본 vignette/grain 등 초기화 후 PP material 만 add
	PostProcessComp->Settings = FPostProcessSettings();
	PostProcessComp->Settings.WeightedBlendables.Array.Add(FWeightedBlendable(1.f, StasisPPMID));
	PostProcessComp->BlendWeight = 1.f;
	PostProcessComp->bEnabled = true;

	RFZ_LOG("[PP] StasisSalvo screen-radial activated — Center=%s, Radius=%.0f", *Center.ToString(), Radius);
}

void ARealityFractureZone::LocalDeactivatePostProcess()
{
	if (!PostProcessComp) return;
	PostProcessComp->BlendWeight = 0.f;
	PostProcessComp->bEnabled = false;
	PostProcessComp->Settings.WeightedBlendables.Array.Reset();
	StasisPPMID = nullptr;
}

void ARealityFractureZone::LocalSpawnGhostMesh(FVector WorldLoc, FRotator WorldRot,
	FVector AimTarget, int32 DecoyIndex)
{
	UWorld* W = GetWorld();
	if (!W) return;

	AActor* BossOwner = OwnerEnemy ? OwnerEnemy : GetOwner();
	ACharacter* BossChar = Cast<ACharacter>(BossOwner);
	USkeletalMeshComponent* BossMesh = BossChar ? BossChar->GetMesh() : nullptr;
	USkeletalMesh* MeshAsset = BossMesh ? BossMesh->GetSkeletalMeshAsset() : nullptr;
	if (!MeshAsset)
	{
		RFZ_LOG("LocalSpawnGhostMesh — boss mesh asset null, skip");
		return;
	}

	FActorSpawnParameters SpawnParams;
	SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	AActor* Ghost = W->SpawnActor<AActor>(AActor::StaticClass(), WorldLoc, WorldRot, SpawnParams);
	if (!Ghost) return;

	// SceneComponent 를 root 로 만들고 그 밑에 SkelMeshComp 를 붙여야 Character 의 mesh -90 yaw
	//   relative rotation 이 그대로 살아남. SkelMeshComp 가 직접 root 면 SetActorRotation 이 mesh 의
	//   relative 를 덮어써서 mesh +Y forward 가 actor +X 와 90° 어긋남. (이전 버그 — 분신이 옆을 봄)
	USceneComponent* GhostRoot = NewObject<USceneComponent>(Ghost);
	if (!GhostRoot)
	{
		Ghost->Destroy();
		return;
	}
	Ghost->SetRootComponent(GhostRoot);
	GhostRoot->RegisterComponent();

	USkeletalMeshComponent* GhostComp = NewObject<USkeletalMeshComponent>(Ghost);
	if (!GhostComp)
	{
		Ghost->Destroy();
		return;
	}
	GhostComp->SetupAttachment(GhostRoot);
	GhostComp->RegisterComponent();
	GhostComp->SetSkeletalMeshAsset(MeshAsset);
	GhostComp->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	GhostComp->SetCastShadow(false);

	// 보스 SkelMesh 의 RelativeLocation/Rotation 적용 (Character 기본은 mesh comp 가 -90 yaw + Z=-90 offset).
	//   GhostRoot world transform = WorldLoc/WorldRot. GhostComp relative -90 yaw → mesh native +Y forward
	//   가 GhostRoot 의 +X (= WorldRot forward = player 방향) 와 정렬.
	if (BossMesh)
	{
		GhostComp->SetRelativeLocation(BossMesh->GetRelativeLocation());
		GhostComp->SetRelativeRotation(BossMesh->GetRelativeRotation());
		GhostComp->SetRelativeScale3D(BossMesh->GetRelativeScale3D());
	}
	// 보스 크기와 동일하게 — 보스 actor scale (capsule 2.205× 등) 적용. Phase 2 descent scale 도 자동 반영
	//   (BossOwner->GetActorScale3D() 는 runtime 현재 scale 반환).
	const FVector BossActorScale = BossOwner ? BossOwner->GetActorScale3D() : FVector::OneVector;
	Ghost->SetActorScale3D(BossActorScale);
	Ghost->SetActorLocationAndRotation(WorldLoc, WorldRot);

	// 사격 애니 — Shoot notify 지점 자세로 paused.
	//   EndStasis 시 frame 0 부터 재생 (몬타지 풀 재생 → 진짜 발사 느낌).
	//   AnimMontage 면 notify 시간 검색, AnimSequence 면 첫 프레임 폴백.
	if (DecoyAttackAnim)
	{
		GhostComp->PlayAnimation(DecoyAttackAnim, false);

		// Shoot/Fire/Projectile 키워드 매칭 — AM_Boss_Fire 의 notify 는 "EnemyFireProjectile" 이므로
		//   "Fire" substring 으로 잡힘. "Projectile" 도 추가해 다른 보스 몬타지 호환성 확보.
		float ShootNotifyTime = 0.f;
		if (UAnimMontage* Montage = Cast<UAnimMontage>(DecoyAttackAnim))
		{
			for (const FAnimNotifyEvent& Notify : Montage->Notifies)
			{
				const FString NotifyName = Notify.NotifyName.ToString();
				if (NotifyName.Contains(TEXT("Shoot"), ESearchCase::IgnoreCase) ||
				    NotifyName.Contains(TEXT("Fire"), ESearchCase::IgnoreCase) ||
				    NotifyName.Contains(TEXT("Projectile"), ESearchCase::IgnoreCase) ||
				    NotifyName.Contains(TEXT("Cast"), ESearchCase::IgnoreCase))
				{
					ShootNotifyTime = Notify.GetTriggerTime();
					break;
				}
			}
		}

		if (UAnimSingleNodeInstance* SingleNode = GhostComp->GetSingleNodeInstance())
		{
			if (ShootNotifyTime > 0.f) SingleNode->SetPosition(ShootNotifyTime, false);
			SingleNode->SetPlaying(false);
		}
		GhostComp->bPauseAnims = true;
	}
	else
	{
		// 애니 없으면 T-pose
		GhostComp->SetAnimationMode(EAnimationMode::AnimationSingleNode);
		GhostComp->SetAnimation(nullptr);
	}

	// [BossCloneGlowV1] 2페이즈면 분신도 보스와 동일하게 광폭화 발광 적용 (보스 머티리얼 복사 + glow).
	//   분신패턴은 2페이즈 전용이라 평상시 이 분기가 타짐. 2페이즈가 아니면 GhostMaterial 폴백.
	AHellunaEnemyCharacter_Boss* BossEnemy = Cast<AHellunaEnemyCharacter_Boss>(BossOwner);
	if (BossEnemy && BossEnemy->bInPhase2)
	{
		BossEnemy->ApplyBerserkGlowToMesh(GhostComp);
	}
	else if (GhostMaterial)
	{
		const int32 NumMaterials = GhostComp->GetNumMaterials();
		for (int32 i = 0; i < NumMaterials; ++i)
		{
			GhostComp->SetMaterial(i, GhostMaterial);
		}
	}

	// [AimLineMuzzleV2] 시작점 — 원거리 공격 GA 의 projectile spawn 위치와 동일한 방식:
	//   ActorLoc + Forward × FwdOffset + (0, 0, HeightOffset). 본 lookup 불필요.
	const FVector LineStart = Ghost->GetActorLocation()
		+ Ghost->GetActorForwardVector() * AimLineStartForwardOffset
		+ FVector(0.f, 0.f, AimLineStartHeightOffset);

	// Aim line — static mesh (cube X-stretch). Niagara beam 이 user param 매칭 안 돼도 line 보임 보장.
	if (AimLineMesh)
	{
		const FVector BeamDir = (AimTarget - LineStart).GetSafeNormal();
		if (!BeamDir.IsNearlyZero())
		{
			UStaticMeshComponent* LineComp = NewObject<UStaticMeshComponent>(Ghost);
			if (LineComp)
			{
				LineComp->SetStaticMesh(AimLineMesh);
				LineComp->SetMobility(EComponentMobility::Movable);
				LineComp->SetCollisionEnabled(ECollisionEnabled::NoCollision);
				LineComp->SetCastShadow(false);
				// [PPScreenRadialV2] PP 가 M_PP_StasisSalvo (screen-space radial) 로 복귀 — world-space stencil 면제
				//   불필요. CustomDepth 켜면 main 의 M_TeamOutline_PP 가 분신/에임라인에 외곽선을 그려서 끔.
				LineComp->RegisterComponent();
				LineComp->AttachToComponent(GhostComp, FAttachmentTransformRules::KeepRelativeTransform);

				if (AimLineMaterial)
				{
					const int32 NumMats = LineComp->GetNumMaterials();
					for (int32 i = 0; i < NumMats; ++i) LineComp->SetMaterial(i, AimLineMaterial);
				}

				// SM_Cube = 100cm × 100cm × 100cm. X 축 stretch 으로 line.
				//   Length = 분신 손 → 존 외곽 (zone 끝까지 닿게)
				const float EffectiveLength = GetEffectiveAimLineLength();
				const float Half = EffectiveLength * 0.5f;
				const FVector Center = LineStart + BeamDir * Half;
				LineComp->SetWorldLocationAndRotation(Center, BeamDir.Rotation());
				const float ScaleX = EffectiveLength / 100.f;
				const float ScaleYZ = AimLineThickness / 100.f;
				LineComp->SetWorldScale3D(FVector(ScaleX, ScaleYZ, ScaleYZ));

				// [AimLineChargeV1] MID 생성해 Tick 에서 Charge/색/펄스 갱신.
				//   AimLineMaterial 이 M_AimLine_StasisSalvo (Charge 파라미터 노출) 라야 효과 보임.
				//   호환 안 되는 머티리얼이면 SetScalarParameterValue 무시되어 정적 표시 (안전).
				if (UMaterialInstanceDynamic* LineMID =
					LineComp->CreateAndSetMaterialInstanceDynamic(0))
				{
					LineMID->SetScalarParameterValue(TEXT("Charge"), 0.f);
					AimLineMIDs.Add(LineMID);
				}
				AimLineComps.Add(LineComp);
			}
		}
	}

	// Aim beam Niagara — 분신 손/총구 (LineStart) → aim target 까지 길게 표시
	if (AimBeamVFX)
	{
		const FVector BeamDir = (AimTarget - LineStart).GetSafeNormal();
		if (!BeamDir.IsNearlyZero())
		{
			const FVector BeamEnd = LineStart + BeamDir * GetEffectiveAimLineLength();
			UNiagaraComponent* BeamComp = UNiagaraFunctionLibrary::SpawnSystemAttached(
				AimBeamVFX, GhostComp, NAME_None,
				FVector::ZeroVector, FRotator::ZeroRotator,
				EAttachLocation::KeepRelativeOffset, true);
			if (BeamComp)
			{
				BeamComp->SetVariableVec3(FName("BeamStart"), LineStart);
				BeamComp->SetVariableVec3(FName("BeamEnd"), BeamEnd);
				BeamComp->SetVariableVec3(FName("EndPosition"), BeamEnd);
				BeamComp->SetVariableVec3(FName("End"), BeamEnd);
			}
		}
	}

	if (DecoySpawnVFX)
	{
		UNiagaraFunctionLibrary::SpawnSystemAtLocation(W, DecoySpawnVFX, WorldLoc, WorldRot);
	}

	// [PPScreenRadialV2] PP 가 screen-space radial 로 복귀 — 분신에 CustomDepth 안 켬.
	//   (켜면 main 의 팀 외곽선 PP 가 분신을 아군처럼 외곽선 처리해서 "테두리" 생김.)

	Ghost->SetLifeSpan(GhostLifetime);
	LocalGhostActors.Add(Ghost);
}

void ARealityFractureZone::LocalCleanupGhosts()
{
	for (TWeakObjectPtr<AActor>& Weak : LocalGhostActors)
	{
		if (AActor* A = Weak.Get())
		{
			A->Destroy();
		}
	}
	LocalGhostActors.Reset();
	// [AimLineChargeV1] line/MID 캐시도 함께 정리.
	AimLineMIDs.Reset();
	AimLineComps.Reset();
	bAimLineRampActive = false;
}

// ================================================================
// [AimLineChargeV1] Tick — Charge ramp + 두께 펄스
//   AimLine MID 의 'Charge' 파라미터를 0 → 1 lerp.
//   가속 곡선: 0 ~ AccelStart 는 linear, 그 후 sqrt 가속으로 fire 직전 빠른 펄스.
//   두께 펄스: sin 기반 width modulation. 가속 구간 진입 시 1.6× boost.
// ================================================================
void ARealityFractureZone::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

	UWorld* W = GetWorld();
	if (!W) return;

	// =========================================================
	// [ZoneExpandPhaseV1] 존 visual 확장 phase — bZoneExpanding 동안 0→1, 이후 stasis 끝까지 1 유지.
	// =========================================================
	float ZoneAlpha = 1.f;
	if (bZoneExpanding)
	{
		const float ZElapsed = W->GetRealTimeSeconds() - ZoneExpandStartRealTime;
		const float ZDur = FMath::Max(0.05f, ZoneExpandDuration);
		ZoneAlpha = FMath::Clamp(ZElapsed / ZDur, 0.f, 1.f);
	}

	// [StasisSalvoV2 fix] dome / PP 시각 ramp 는 dome mesh 나 PP MID 가 살아있는 동안 유지.
	//   (기존엔 bStasisActive 로 게이트했는데 그건 서버에서만 true → 클라/listen-server 클라뷰에선
	//    dome 이 KINDA_SMALL_NUMBER scale 그대로 = 안 보이고, PP Charge ramp 도 안 돌아 색 변화 안 됨.
	//    ZoneVisualMeshComp / StasisPPMID 는 Multicast_StartStasis 에서 모든 머신이 만들고 Cleanup 에서 파괴.)
	if (ZoneVisualMeshComp.Get() != nullptr || StasisPPMID != nullptr)
	{
		const float ZoneRadiusMax = DecoyRingRadius * FMath::Max(0.1f, ZoneVisualRadiusMul);

		if (StasisPPMID)
		{
			// [PPScreenRadialV1] M_PP_StasisSalvo 가 screen-space radial alpha 로 전환됨.
			//   Charge param (0..1) 가 화면 중앙 → 외곽 covered 영역을 결정. ZoneRadius 는 backward compat.
			StasisPPMID->SetScalarParameterValue(FName("ZoneRadius"), ZoneRadiusMax * ZoneAlpha);
			StasisPPMID->SetScalarParameterValue(FName("Charge"), ZoneAlpha);
		}

		// [ZoneDomeExpandV2] mesh agnostic — 어떤 static mesh 든 world radius = ZoneRadiusMax × ZoneAlpha.
		if (ZoneVisualMeshComp.Get() && ZoneVisualMeshComp->GetStaticMesh())
		{
			const float UnitRadius = FMath::Max(0.1f,
				ZoneVisualMeshComp->GetStaticMesh()->GetBounds().SphereRadius);
			const float TargetWorldRadius = FMath::Max(KINDA_SMALL_NUMBER, ZoneRadiusMax * ZoneAlpha);
			const float DomeScale = TargetWorldRadius / UnitRadius;
			ZoneVisualMeshComp->SetWorldScale3D(FVector(DomeScale));

			if (!bLoggedDomeRamp && ZoneAlpha >= 0.99f)
			{
				bLoggedDomeRamp = true;
				UE_LOG(LogTemp, Warning, TEXT("[StasisSalvo] [DomeRamp] full — Auth=%d, DomeScale=%.3f, WorldRadius=%.0f, UnitRadius=%.1f"),
					HasAuthority() ? 1 : 0, DomeScale, TargetWorldRadius, UnitRadius);
			}
		}
	}

	// =========================================================
	// [CameraZoomGradualV1] 점진적 카메라 풀백 — Multicast_OnZoneExpanded 에서 활성화.
	//   각 hero spring arm 의 ArmLength 를 시작값 → StasisCameraDistance 로 ease-out lerp.
	// =========================================================
	if (bCameraZoomActive && CameraZoomStartLengths.Num() > 0 && bAdjustCameraDuringPattern)
	{
		const float ZElapsed = W->GetRealTimeSeconds() - CameraZoomStartRealTime;
		const float ZDur = FMath::Max(0.05f, CameraZoomDuration);
		const float RawA = FMath::Clamp(ZElapsed / ZDur, 0.f, 1.f);
		// ease-out — 처음엔 빠르게 멀어졌다가 끝으로 갈수록 느려짐 (드라마틱 풀백 느낌).
		const float EaseA = 1.f - FMath::Pow(1.f - RawA, 2.f);

		for (auto& Pair : CameraZoomStartLengths)
		{
			APawn* P = Pair.Key.Get();
			if (!P) continue;
			if (AHellunaHeroCharacter* Hero = Cast<AHellunaHeroCharacter>(P))
			{
				if (USpringArmComponent* CB = Hero->GetCameraBoom())
				{
					const float NewLen = FMath::Lerp(Pair.Value, StasisCameraDistance, EaseA);
					CB->TargetArmLength = NewLen;
				}
			}
		}
		if (RawA >= 1.f)
		{
			bCameraZoomActive = false;
		}
	}

	// =========================================================
	// [AimLineChargeV1] AimLine charge ramp — 존 완전 확장 + 분신 spawn 후에만 active.
	// =========================================================
	if (!bAimLineRampActive) return;
	if (AimLineMIDs.Num() == 0 && AimLineComps.Num() == 0) return;

	const float Elapsed = W->GetRealTimeSeconds() - AimLineRampStartRealTime;
	const float Dur = FMath::Max(0.1f, AimLineRampDuration);
	const float RawAlpha = FMath::Clamp(Elapsed / Dur, 0.f, 1.f);

	// Charge 곡선: AccelStart 까지 linear, 그 후 sqrt 가속 (fire 직전 빠른 ramp).
	float Charge = RawAlpha;
	if (RawAlpha > AimLineRampAccelStart && AimLineRampAccelStart < 1.f)
	{
		const float t = (RawAlpha - AimLineRampAccelStart) / (1.f - AimLineRampAccelStart);
		Charge = AimLineRampAccelStart + FMath::Sqrt(FMath::Clamp(t, 0.f, 1.f)) * (1.f - AimLineRampAccelStart);
	}

	// 두께 펄스 — 가속 구간 진입 시 frequency 와 amplitude 동시에 증가.
	const float PulseFreq = 6.f * (1.f + Charge * 4.f); // 6 → 30 Hz 정도
	const float PulseTheta = W->GetRealTimeSeconds() * PulseFreq;
	const float Pulse01 = FMath::Sin(PulseTheta) * 0.5f + 0.5f; // 0..1
	const float WidthBase = AimLineThickness / 100.f;
	float WidthMul = 1.f + Pulse01 * AimLineWidthPulseDepth * (0.5f + Charge);
	if (RawAlpha >= 1.f) // fire 직전 1 프레임 — 최대 두께 boost
	{
		WidthMul *= AimLineFireImminentScale;
	}
	const float WidthYZ = WidthBase * WidthMul;

	for (TWeakObjectPtr<UMaterialInstanceDynamic>& WeakMID : AimLineMIDs)
	{
		if (UMaterialInstanceDynamic* MID = WeakMID.Get())
		{
			MID->SetScalarParameterValue(TEXT("Charge"), Charge);
		}
	}

	if (AimLineWidthPulseDepth > KINDA_SMALL_NUMBER)
	{
		for (TWeakObjectPtr<UStaticMeshComponent>& WeakLine : AimLineComps)
		{
			UStaticMeshComponent* SMC = WeakLine.Get();
			if (!SMC) continue;
			const FVector CurScale = SMC->GetRelativeScale3D();
			SMC->SetRelativeScale3D(FVector(CurScale.X, WidthYZ, WidthYZ));
		}
	}
}

#undef RFZ_LOG
