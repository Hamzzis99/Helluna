// Capstone Project Helluna

#include "BossEvent/RealityFractureZone.h"

#include "Character/HellunaEnemyCharacter.h"
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
	PrimaryActorTick.bCanEverTick = false;
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
}

void ARealityFractureZone::BeginPlay()
{
	Super::BeginPlay();
}

void ARealityFractureZone::Destroyed()
{
	if (UWorld* W = GetWorld())
	{
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

	// player 시작 위치 record (시간 정지 동안 player 안 움직임)
	APawn* Player = FindClosestPlayerPawn();
	const FVector PlayerLoc = Player ? Player->GetActorLocation() : GetActorLocation();
	RFZ_LOG("[Phase A] Stasis center (player loc) = %s", *PlayerLoc.ToString());

	// 모든 머신: 시간 정지 시작
	Multicast_StartStasis(PlayerLoc);
	bStasisActive = true;

	// StasisStartDelay 후 첫 분신 spawn
	//   글로벌 TimeDilation StasisTimeDilation 적용 중 → World TimerManager 가 그 영향 받음.
	//   wall-clock StasisStartDelay 초 만에 fire 시키려면 delay × StasisTD 로 보정.
	if (UWorld* W = GetWorld())
	{
		const float AdjustedDelay = StasisAdjustedDelay(StasisStartDelay);
		RFZ_LOG("[Phase A] Schedule first decoy spawn — wall=%.2fs, scaled=%.4fs",
			StasisStartDelay, AdjustedDelay);
		W->GetTimerManager().SetTimer(
			DecoySpawnTimer, this,
			&ARealityFractureZone::ServerSpawnNextDecoy,
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

	if (UWorld* W = GetWorld())
	{
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
		// 진짜 랜덤 — 각도 0~360, 반경 0.5~1.3 × DecoyRingRadius (min 450cm distance from player)
		const float TryAngle = FMath::FRandRange(0.f, 360.f);
		const float RadiusScale = FMath::FRandRange(0.5f, 1.3f);
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
			// 다음 spawn 까지 간격 — 0.4× ~ 1.6× 랜덤 variance. 같은 interval 로 균일하게 spawn 안 되고
			//   텔레포트처럼 들쭉날쭉 unpredictable 하게 등장. (LC touch)
			const float IntervalVariance = FMath::FRandRange(0.4f, 1.6f);
			const float ActualInterval = DecoySpawnInterval * IntervalVariance;
			W->GetTimerManager().SetTimer(
				DecoySpawnTimer, this,
				&ARealityFractureZone::ServerSpawnNextDecoy,
				StasisAdjustedDelay(ActualInterval), false
			);
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

			// SM_Sphere 빌트인 = 반경 50cm. 원하는 반경 = DecoyRingRadius × Mul
			const float TargetRadius = DecoyRingRadius * ZoneVisualRadiusMul;
			const float Scale = TargetRadius / 50.f;
			ZoneVisualMeshComp->SetWorldScale3D(FVector(Scale));
			ZoneVisualMeshComp->SetWorldLocation(PlayerStasisCenter);
			RFZ_LOG("[ZoneDome] spawned — TargetRadius=%.0fcm, Scale=%.2f", TargetRadius, Scale);
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

	// [Camera] player spring arm length 조정 — 회피 시야 확보. 백업 후 변경.
	if (W && bAdjustCameraDuringPattern)
	{
		int32 CamAdjusted = 0;
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
					CB->TargetArmLength = StasisCameraDistance;
					CamAdjusted++;
				}
			}
		}
		RFZ_LOG("[Camera] spring arm adjusted on %d hero(es) → %.0fcm", CamAdjusted, StasisCameraDistance);
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

		// aim line static mesh (cube stretch) 도 destroy
		TArray<UStaticMeshComponent*> LineMeshes;
		G->GetComponents<UStaticMeshComponent>(LineMeshes);
		for (UStaticMeshComponent* SMC : LineMeshes)
		{
			if (SMC) SMC->DestroyComponent();
		}

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

	// M_PP_TimeDistortion 은 zone-aware desaturation — ZoneCenter/Radius/Strength/EdgeSoftness 필수.
	// LastStasisCenter 는 Multicast_StartStasis 에서 record (player 중심)
	const FVector Center = LastStasisCenter;
	const float Radius = DecoyRingRadius * ZoneVisualRadiusMul;
	StasisPPMID->SetVectorParameterValue(FName("ZoneCenter"), FLinearColor(Center.X, Center.Y, Center.Z));
	StasisPPMID->SetScalarParameterValue(FName("ZoneRadius"), Radius);
	StasisPPMID->SetScalarParameterValue(FName("Strength"), 0.9f);
	StasisPPMID->SetScalarParameterValue(FName("EdgeSoftness"), 150.f);

	// 기본 vignette/grain 등 초기화 후 PP material 만 add
	PostProcessComp->Settings = FPostProcessSettings();
	PostProcessComp->Settings.WeightedBlendables.Array.Add(FWeightedBlendable(1.f, StasisPPMID));
	PostProcessComp->BlendWeight = 1.f;
	PostProcessComp->bEnabled = true;

	RFZ_LOG("[PP] Desaturation activated — Center=%s, Radius=%.0f", *Center.ToString(), Radius);
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

	if (GhostMaterial)
	{
		const int32 NumMaterials = GhostComp->GetNumMaterials();
		for (int32 i = 0; i < NumMaterials; ++i)
		{
			GhostComp->SetMaterial(i, GhostMaterial);
		}
	}

	// Aim line — static mesh (cube X-stretch). Niagara beam 이 user param 매칭 안 돼도 line 보임 보장.
	if (AimLineMesh)
	{
		const FVector BeamDir = (AimTarget - WorldLoc).GetSafeNormal();
		if (!BeamDir.IsNearlyZero())
		{
			UStaticMeshComponent* LineComp = NewObject<UStaticMeshComponent>(Ghost);
			if (LineComp)
			{
				LineComp->SetStaticMesh(AimLineMesh);
				LineComp->SetMobility(EComponentMobility::Movable);
				LineComp->SetCollisionEnabled(ECollisionEnabled::NoCollision);
				LineComp->SetCastShadow(false);
				LineComp->RegisterComponent();
				LineComp->AttachToComponent(GhostComp, FAttachmentTransformRules::KeepRelativeTransform);

				if (AimLineMaterial)
				{
					const int32 NumMats = LineComp->GetNumMaterials();
					for (int32 i = 0; i < NumMats; ++i) LineComp->SetMaterial(i, AimLineMaterial);
				}

				// SM_Cube = 100cm × 100cm × 100cm. X 축 stretch 으로 line.
				//   Length = 분신 → player → 존 외곽 (zone 끝까지 닿게)
				const float EffectiveLength = GetEffectiveAimLineLength();
				const float Half = EffectiveLength * 0.5f;
				const FVector Center = WorldLoc + BeamDir * Half;
				LineComp->SetWorldLocationAndRotation(Center, BeamDir.Rotation());
				const float ScaleX = EffectiveLength / 100.f;
				const float ScaleYZ = AimLineThickness / 100.f;
				LineComp->SetWorldScale3D(FVector(ScaleX, ScaleYZ, ScaleYZ));
			}
		}
	}

	// Aim beam Niagara — 분신 origin → aim target 까지 길게 표시
	if (AimBeamVFX)
	{
		const FVector BeamDir = (AimTarget - WorldLoc).GetSafeNormal();
		if (!BeamDir.IsNearlyZero())
		{
			const FVector BeamEnd = WorldLoc + BeamDir * GetEffectiveAimLineLength();
			UNiagaraComponent* BeamComp = UNiagaraFunctionLibrary::SpawnSystemAttached(
				AimBeamVFX, GhostComp, NAME_None,
				FVector::ZeroVector, FRotator::ZeroRotator,
				EAttachLocation::KeepRelativeOffset, true);
			if (BeamComp)
			{
				BeamComp->SetVariableVec3(FName("BeamStart"), WorldLoc);
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

	// PP zone-aware desaturation 면제 — TimeDistortionZone 패턴: stencil value 1.
	//   M_PP_TimeDistortion 가 stencil 비교로 zone 안 actor 중 stencil=1 면 색 유지.
	{
		TArray<UPrimitiveComponent*> Prims;
		Ghost->GetComponents<UPrimitiveComponent>(Prims);
		for (UPrimitiveComponent* Prim : Prims)
		{
			if (!Prim) continue;
			Prim->SetRenderCustomDepth(true);
			Prim->SetCustomDepthStencilValue(1);
		}
	}

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
}

#undef RFZ_LOG
