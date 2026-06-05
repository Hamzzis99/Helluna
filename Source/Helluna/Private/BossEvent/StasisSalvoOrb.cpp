// Capstone Project Helluna

#include "BossEvent/StasisSalvoOrb.h"

#include "BossEvent/BossPatternZoneBase.h"
#include "BossEvent/RealityFractureZone.h"
#include "Character/HellunaEnemyCharacter.h"
#include "Components/SphereComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/World.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/ProjectileMovementComponent.h"
#include "Kismet/GameplayStatics.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "NiagaraComponent.h"
#include "NiagaraFunctionLibrary.h"

#define SSO_LOG(Fmt, ...) UE_LOG(LogTemp, Warning, TEXT("[StasisSalvoOrb] " Fmt), ##__VA_ARGS__)

AStasisSalvoOrb::AStasisSalvoOrb()
{
	// Tick 으로 MaxTravelDistance 체크. 가벼움 (벡터 거리 1회/frame).
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.bStartWithTickEnabled = true;

	bReplicates = true;
	SetReplicateMovement(true);

	CollisionSphere = CreateDefaultSubobject<USphereComponent>(TEXT("CollisionSphere"));
	RootComponent = CollisionSphere;
	CollisionSphere->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	CollisionSphere->SetGenerateOverlapEvents(true);
	CollisionSphere->SetCollisionObjectType(ECC_WorldDynamic);
	CollisionSphere->SetCollisionResponseToAllChannels(ECR_Ignore);
	CollisionSphere->SetCollisionResponseToChannel(ECC_WorldStatic, ECR_Overlap);
	CollisionSphere->SetCollisionResponseToChannel(ECC_WorldDynamic, ECR_Overlap);
	CollisionSphere->SetCollisionResponseToChannel(ECC_Pawn, ECR_Overlap);

	MoveComp = CreateDefaultSubobject<UProjectileMovementComponent>(TEXT("MoveComp"));
	MoveComp->bRotationFollowsVelocity = true;
	MoveComp->bShouldBounce = false;
	MoveComp->ProjectileGravityScale = 0.f;

	TrailComp = CreateDefaultSubobject<UNiagaraComponent>(TEXT("TrailComp"));
	TrailComp->SetupAttachment(RootComponent);
	TrailComp->bAutoActivate = false;

	MeshComp = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("MeshComp"));
	MeshComp->SetupAttachment(RootComponent);
	MeshComp->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	MeshComp->SetCastShadow(false);
}

void AStasisSalvoOrb::BeginPlay()
{
	Super::BeginPlay();

	StartLocation = GetActorLocation();

	if (CollisionSphere)
	{
		CollisionSphere->SetSphereRadius(FMath::Max(1.f, CollisionRadius));
		CollisionSphere->OnComponentBeginOverlap.AddDynamic(this, &AStasisSalvoOrb::OnSphereBeginOverlap);
		if (AActor* OwnerActor = GetOwner())
		{
			CollisionSphere->IgnoreActorWhenMoving(OwnerActor, true);
		}
		if (OwnerEnemy)
		{
			CollisionSphere->IgnoreActorWhenMoving(OwnerEnemy, true);
		}
	}

	if (TrailVFX && TrailComp)
	{
		TrailComp->SetAsset(TrailVFX);
		TrailComp->Activate(true);
		// [OrbColorMatchV1] 트레일 색 파라미터가 지정돼 있으면 OrbBaseColor 로 tint (존 색과 통일).
		if (bApplyOrbBaseColor && !OrbTrailColorParamName.IsNone())
		{
			TrailComp->SetVariableLinearColor(OrbTrailColorParamName, OrbBaseColor);
		}
	}
	if (OrbMesh && MeshComp)
	{
		MeshComp->SetStaticMesh(OrbMesh);
		MeshComp->SetRelativeScale3D(FVector(FMath::Max(0.001f, OrbMeshScale)));

		// [OrbColorMatchV1] 색 직접 지정 시 MID 로 Base_Color 적용 → 존 돔과 동일 머티리얼/색.
		if (bApplyOrbBaseColor)
		{
			UMaterialInterface* BaseMat = OrbMeshMaterial ? OrbMeshMaterial.Get() : MeshComp->GetMaterial(0);
			if (BaseMat)
			{
				UMaterialInstanceDynamic* MID = UMaterialInstanceDynamic::Create(BaseMat, this);
				if (MID)
				{
					if (!OrbBaseColorParamName.IsNone())
					{
						MID->SetVectorParameterValue(OrbBaseColorParamName, OrbBaseColor);
					}
					const int32 NumMats = MeshComp->GetNumMaterials();
					for (int32 i = 0; i < NumMats; ++i)
					{
						MeshComp->SetMaterial(i, MID);
					}
				}
			}
		}
		else if (OrbMeshMaterial)
		{
			MeshComp->SetMaterial(0, OrbMeshMaterial);
		}
	}

	// 안전망 — MaxLifeSeconds 후 거리/충돌 무관 강제 Burst.
	if (HasAuthority())
	{
		FTimerHandle UnusedHandle;
		GetWorldTimerManager().SetTimer(UnusedHandle,
			FTimerDelegate::CreateWeakLambda(this, [this]() { Burst(); }),
			FMath::Max(0.1f, MaxLifeSeconds), false);
	}
}

void AStasisSalvoOrb::Init(AHellunaEnemyCharacter* InOwnerEnemy, ABossPatternZoneBase* InTargetZone, const FVector& InDirection)
{
	OwnerEnemy = InOwnerEnemy;
	TargetZone = InTargetZone;
	SetOwner(InOwnerEnemy);
	SetInstigator(InOwnerEnemy);

	const FVector Dir = InDirection.GetSafeNormal();
	const FVector UseDir = Dir.IsNearlyZero() ? GetActorForwardVector() : Dir;
	SetActorRotation(UseDir.Rotation());
	if (MoveComp)
	{
		MoveComp->Velocity = UseDir * FMath::Max(1.f, Speed);
		MoveComp->InitialSpeed = FMath::Max(1.f, Speed);
		MoveComp->MaxSpeed = FMath::Max(1.f, Speed);
	}
	SSO_LOG("Init — Owner=%s, Zone=%s, Dir=%s, Speed=%.0f, MaxDist=%.0f",
		*GetNameSafe(InOwnerEnemy), *GetNameSafe(InTargetZone), *UseDir.ToString(), Speed, MaxTravelDistance);
}

void AStasisSalvoOrb::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

	if (bBursted) return;
	if (!HasAuthority()) return; // 거리/근접 판정은 서버만 (이동도 서버 권위 → replicate)
	if (bHovering) return;       // [GroundHoverV1] 지면 도달 후 정지(hover) 중 — HoverBurstTimer 가 Burst 호출

	const FVector Loc = GetActorLocation();

	// [GroundDecelV1 / GroundHoverV1] 지면까지 거리 1회 측정 — 높이 기반 감속 + 지면 도달 시 정지→hover→Burst 에 공용.
	//   "위에선 빠르게, 지면에 가까워질수록 느려지고, 터지기 전 잠깐 멈춘다" (하늘 낙하 분신 구체용).
	float GroundDist = TNumericLimits<float>::Max();
	const bool bNeedGround = (BurstHeightAboveGround > 0.f)
		|| (bDecelerateWhileMoving && DecelStartHeightAboveGround > 0.f);
	if (bNeedGround)
	{
		if (UWorld* W = GetWorld())
		{
			FHitResult GroundHit;
			FCollisionQueryParams GParams(SCENE_QUERY_STAT(SSO_GroundBurst), false);
			GParams.AddIgnoredActor(this);
			if (AActor* OwnerActor = GetOwner()) GParams.AddIgnoredActor(OwnerActor);
			if (OwnerEnemy) GParams.AddIgnoredActor(OwnerEnemy);
			const float TraceLen = FMath::Max(BurstHeightAboveGround, DecelStartHeightAboveGround) + 100.f;
			const FVector GroundEnd = Loc - FVector(0.f, 0.f, TraceLen);
			if (W->LineTraceSingleByChannel(GroundHit, Loc, GroundEnd, ECC_Visibility, GParams)
				&& GroundHit.bBlockingHit)
			{
				GroundDist = GroundHit.Distance;
			}
		}
	}

	// [GroundHoverV1] 지면(BurstHeightAboveGround) 도달 → 즉시 정지 후 HoverBeforeBurstSeconds 동안 멈췄다가 Burst.
	if (BurstHeightAboveGround > 0.f && GroundDist <= BurstHeightAboveGround)
	{
		if (MoveComp) MoveComp->StopMovementImmediately();
		if (HoverBeforeBurstSeconds > KINDA_SMALL_NUMBER && GetWorld())
		{
			bHovering = true;
			SSO_LOG("지면 도달 → %.2fs 정지(hover) 후 Burst", HoverBeforeBurstSeconds);
			Multicast_PlayCharge(Loc); // [ChargeV1] 터지기 전 차징 연출 (전 머신)
			GetWorldTimerManager().SetTimer(HoverBurstTimer,
				FTimerDelegate::CreateWeakLambda(this, [this]() { Burst(); }),
				HoverBeforeBurstSeconds, false);
		}
		else
		{
			SSO_LOG("지면 도달 → 즉시 Burst");
			Burst();
		}
		return;
	}

	// [GroundDecelV1] 높이 기반 감속 — DecelStartHeight 위에선 Speed 유지(빠름),
	//   지면(BurstHeight)에 가까워질수록 MinMoveSpeed 로 선형 감속. 방향(아래)은 유지.
	if (bDecelerateWhileMoving && MoveComp
		&& DecelStartHeightAboveGround > BurstHeightAboveGround
		&& GroundDist < DecelStartHeightAboveGround)
	{
		const float Alpha = FMath::Clamp(
			(GroundDist - BurstHeightAboveGround) / (DecelStartHeightAboveGround - BurstHeightAboveGround),
			0.f, 1.f);
		// [GroundDecelV1] 지수 곡선 — DecelExponent>1 이면 감속 시작 직후 더 급격히 느려진다(확 브레이크).
		const float ShapedAlpha = FMath::Pow(Alpha, FMath::Max(1.f, DecelExponent));
		const float TargetSpeed = FMath::Lerp(FMath::Max(20.f, MinMoveSpeed), FMath::Max(1.f, Speed), ShapedAlpha);
		const float CurSpeed = MoveComp->Velocity.Size();
		if (CurSpeed > KINDA_SMALL_NUMBER)
		{
			MoveComp->Velocity = MoveComp->Velocity * (TargetSpeed / CurSpeed);
			MoveComp->MaxSpeed = FMath::Max(MoveComp->MaxSpeed, TargetSpeed);
		}
	}

	// 플레이어 근접 → Burst (존이 플레이어 근처에서 피어나도록).
	if (BurstProximityToPlayer > KINDA_SMALL_NUMBER)
	{
		const float ProxSq = FMath::Square(BurstProximityToPlayer);
		if (UWorld* W = GetWorld())
		{
			for (FConstPlayerControllerIterator It = W->GetPlayerControllerIterator(); It; ++It)
			{
				APlayerController* PC = It->Get();
				if (!PC) continue;
				APawn* P = PC->GetPawn();
				if (!P) continue;
				if (FVector::DistSquared(P->GetActorLocation(), Loc) <= ProxSq)
				{
					SSO_LOG("플레이어 근접 → Burst");
					Burst();
					return;
				}
			}
		}
	}

	// 최대 비행 거리 cap.
	if (FVector::DistSquared(Loc, StartLocation) >= FMath::Square(FMath::Max(1.f, MaxTravelDistance)))
	{
		SSO_LOG("MaxTravelDistance 도달 → Burst");
		Burst();
	}
}

void AStasisSalvoOrb::OnSphereBeginOverlap(UPrimitiveComponent* /*OverlappedComp*/, AActor* OtherActor,
	UPrimitiveComponent* /*OtherComp*/, int32 /*OtherBodyIndex*/, bool /*bFromSweep*/, const FHitResult& /*Sweep*/)
{
	if (bBursted) return;
	if (!HasAuthority()) return;
	if (!OtherActor || OtherActor == this || OtherActor == GetOwner() || OtherActor == OwnerEnemy) return;

	SSO_LOG("충돌 (%s) → Burst", *GetNameSafe(OtherActor));
	Burst();
}

void AStasisSalvoOrb::Burst()
{
	if (bBursted) return;
	bBursted = true;

	const FVector BurstLoc = GetActorLocation();

	// VFX/사운드 — 모든 머신.
	Multicast_PlayBurst(BurstLoc);

	if (CollisionSphere)
	{
		CollisionSphere->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	}
	if (MoveComp)
	{
		MoveComp->StopMovementImmediately();
	}
	// [StasisSalvoV2] burst 후 mesh 를 숨기지 않음 — 잠깐 그 자리에 남아 존의 dome ramp(0→max)가
	//   같은 위치에서 피어나며 자연스럽게 engulf. "총알이 그대로 커져서 존이 되는" 느낌.

	// 서버: 미리 받아둔 zone 발동. burst 위치를 stasis 중심으로 넘겨서 "총알이 부딪힌 자리에서 존이 피어남".
	if (HasAuthority())
	{
		if (ABossPatternZoneBase* Zone = TargetZone)
		{
			if (ARealityFractureZone* RFZone = Cast<ARealityFractureZone>(Zone))
			{
				// RF 존은 액터를 옮기지 않고 내부 stasis 중심만 override.
				RFZone->SetStasisCenterOverride(BurstLoc);
			}
			else
			{
				// [TimeSalvoForwardV1] 그 외 존(예: TimeDistortionZone)은 액터 자체를 burst 위치로 이동 →
				//   "총알이 터진 자리에 존이 생성" (전방 발사형 시간 슬로우). 직하 낙하면 발밑/바닥에 그대로.
				Zone->SetActorLocation(BurstLoc);
			}
			SSO_LOG("Burst — TargetZone->ActivateZone() at %s", *BurstLoc.ToString());
			Zone->ActivateZone();
		}
		else
		{
			SSO_LOG("Burst — TargetZone 가 null (이미 정리됨?)");
		}
		// 멀티캐스트가 채널 통해 나갈 시간 약간 확보 후 파괴.
		// !! Zone->ActivateZone() 이 글로벌 TimeDilation 을 ~0.001 로 떨궈서 LifeSpan timer(게임시간 기준)도
		//    그만큼 느려짐 → 0.15s 가 실시간 ~150초가 됨 (= "총알이 안 사라짐" 버그). 글로벌 TD 만큼 보정.
		const float GlobalTD = FMath::Max(0.001f, UGameplayStatics::GetGlobalTimeDilation(GetWorld()));
		const float LifeSpanGameSec = 0.2f * GlobalTD; // 실시간 ≈ 0.2s
		SetLifeSpan(LifeSpanGameSec);
		SSO_LOG("Burst — LifeSpan %.4f gameSec (GlobalTD=%.4f → ≈0.2s real)", LifeSpanGameSec, GlobalTD);
	}
}

void AStasisSalvoOrb::Multicast_PlayBurst_Implementation(FVector BurstLocation)
{
	UWorld* W = GetWorld();
	if (!W) return;

	if (TrailComp)
	{
		TrailComp->Deactivate();
	}
	if (BurstVFX)
	{
		UNiagaraFunctionLibrary::SpawnSystemAtLocation(W, BurstVFX, BurstLocation, FRotator::ZeroRotator,
			FVector(FMath::Max(0.01f, BurstVFXScale)));
	}
	if (BurstSound)
	{
		UGameplayStatics::PlaySoundAtLocation(W, BurstSound, BurstLocation);
	}
}

void AStasisSalvoOrb::Multicast_PlayCharge_Implementation(FVector ChargeLocation)
{
	UWorld* W = GetWorld();
	if (!W) return;
	if (ChargeVFX)
	{
		UNiagaraFunctionLibrary::SpawnSystemAtLocation(W, ChargeVFX, ChargeLocation, FRotator::ZeroRotator,
			FVector(FMath::Max(0.01f, ChargeVFXScale)));
	}
}

void AStasisSalvoOrb::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	// 안전망 — 어떤 이유로든 burst 전에 파괴되면(레벨 전환 제외) zone 이 영영 발동 안 되는 것 방지.
	if (!bBursted && HasAuthority()
		&& EndPlayReason != EEndPlayReason::LevelTransition
		&& EndPlayReason != EEndPlayReason::EndPlayInEditor
		&& EndPlayReason != EEndPlayReason::Quit)
	{
		bBursted = true;
		if (ABossPatternZoneBase* Zone = TargetZone)
		{
			SSO_LOG("EndPlay 전 burst 안 됨 → 안전 발동");
			Zone->ActivateZone();
		}
	}
	Super::EndPlay(EndPlayReason);
}

#undef SSO_LOG
