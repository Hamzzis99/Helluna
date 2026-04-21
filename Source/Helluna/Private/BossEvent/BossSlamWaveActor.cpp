#include "BossEvent/BossSlamWaveActor.h"

#include "Character/HellunaEnemyCharacter.h"
#include "Character/HellunaHeroCharacter.h"
#include "Components/CapsuleComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/OverlapResult.h"
#include "Engine/World.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/Controller.h"
#include "Kismet/GameplayStatics.h"
#include "Materials/MaterialInstanceDynamic.h"

ABossSlamWaveActor::ABossSlamWaveActor()
{
	PrimaryActorTick.bCanEverTick = true;

	WaveMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("WaveMesh"));
	SetRootComponent(WaveMesh);

	WaveMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	WaveMesh->SetGenerateOverlapEvents(false);
	WaveMesh->SetMobility(EComponentMobility::Movable);
	WaveMesh->SetCastShadow(false);

	// [WaveSelfDamageV1] 서버 권위 스폰 + 자동 복제. 클라는 자동으로 시각 복사본 수신.
	// - 파동은 스폰 후 트랜스폼이 안 바뀌므로 MovementReplicate=false 로 대역폭 절약.
	// - 수명이 1~2초 단위라 NetUpdateFrequency 낮게 (초기 속성 전파 이후엔 사실상 update 없음).
	bReplicates = true;
	SetReplicateMovement(false);
	NetUpdateFrequency = 1.f;
	MinNetUpdateFrequency = 0.5f;
	NetPriority = 1.f;
}

void ABossSlamWaveActor::BeginPlay()
{
	Super::BeginPlay();

	// LifeTime + 약간의 여유 후 자동 Destroy.
	SetLifeSpan(LifeTime + 0.05f);

	WaveMesh->SetWorldScale3D(StartScale);

	// 메쉬 기본 반지름 자동 감지 (스케일 1.0 기준 수평 반지름 cm)
	if (UStaticMesh* SM = WaveMesh->GetStaticMesh())
	{
		const FBox Bounds = SM->GetBoundingBox();
		const FVector Ext = Bounds.GetExtent();
		BaseMeshRadius = FMath::Max(Ext.X, Ext.Y);
	}

	if (WaveMesh->GetMaterial(0))
	{
		WaveMID = WaveMesh->CreateAndSetMaterialInstanceDynamic(0);
		if (WaveMID && bFadeIntensity)
		{
			WaveMID->SetScalarParameterValue(TEXT("Intensity"), StartIntensity);
		}
	}

	// [WaveSelfDamageV1] 서버 인스턴스만 데미지 판정 ON.
	// Multicast 로 복제된 클라 시각 액터는 bReplicates=false 로 별도 로컬 스폰 — HasAuthority 가 false 가 되어 데미지 OFF.
	bDamageActive = HasAuthority();

	UE_LOG(LogTemp, Warning,
		TEXT("[SlamWaveV1] BeginPlay Actor=%s Start=(%.1f,%.1f,%.1f) End=(%.1f,%.1f,%.1f) Life=%.2f DmgActive=%d Dmg=%.1f Thick=%.0f VThick=%.0f"),
		*GetName(),
		StartScale.X, StartScale.Y, StartScale.Z,
		EndScale.X, EndScale.Y, EndScale.Z,
		LifeTime, bDamageActive ? 1 : 0,
		Damage, RingThickness, VerticalThickness);
}

float ABossSlamWaveActor::GetCurrentRingRadius() const
{
	// 비주얼 스케일과 완전히 동일한 공식을 사용 → Zone 의 데미지 링이 시각과 싱크
	const float Alpha = FMath::Clamp(ElapsedTime / FMath::Max(LifeTime, 0.01f), 0.f, 1.f);
	const float EasedAlpha = FMath::Pow(Alpha, FMath::Max(ScaleEasing, 0.01f));
	const float CurrentScaleX = FMath::Lerp(StartScale.X, EndScale.X, EasedAlpha);
	return CurrentScaleX * BaseMeshRadius;
}

void ABossSlamWaveActor::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

	ElapsedTime += DeltaTime;
	const float Alpha = FMath::Clamp(ElapsedTime / FMath::Max(LifeTime, 0.01f), 0.f, 1.f);
	const float EasedAlpha = FMath::Pow(Alpha, FMath::Max(ScaleEasing, 0.01f));

	const FVector NewScale = FMath::Lerp(StartScale, EndScale, EasedAlpha);
	WaveMesh->SetWorldScale3D(NewScale);

	if (bFadeIntensity && WaveMID)
	{
		const float CurrentIntensity = FMath::Lerp(StartIntensity, 0.f, Alpha);
		WaveMID->SetScalarParameterValue(TEXT("Intensity"), CurrentIntensity);
	}

	// [WaveSelfDamageV1] 서버 인스턴스에서만 링 데미지 판정.
	// 같은 프레임에서 스케일 → 반지름 → 오버랩 순으로 실행 → 시각/데미지 완전 싱크.
	if (bDamageActive)
	{
		ProcessRingDamage();
	}
}

// -----------------------------------------------------------------
// ProcessRingDamage — 서버 Tick 에서 매 프레임 링 범위 내 플레이어 피격
// [WaveSelfDamageV1] Zone 의 ProcessWaveHits 로부터 이관. 시각 반지름/위치를 자기 자신에서 직접 사용.
// -----------------------------------------------------------------
void ABossSlamWaveActor::ProcessRingDamage()
{
	UWorld* World = GetWorld();
	if (!World) return;

	const float CurrentRadius = GetCurrentRingRadius();
	const FVector Center = GetActorLocation();
	const float OuterRadius = CurrentRadius + RingThickness * 0.5f;
	const float InnerRadius = FMath::Max(0.f, CurrentRadius - RingThickness * 0.5f);

	// 외부 반지름 구체로 Pawn 오버랩
	TArray<FOverlapResult> Overlaps;
	FCollisionQueryParams QueryParams;
	QueryParams.AddIgnoredActor(this);
	if (DamageInstigator)
	{
		QueryParams.AddIgnoredActor(DamageInstigator);
	}

	World->OverlapMultiByObjectType(
		Overlaps,
		Center,
		FQuat::Identity,
		FCollisionObjectQueryParams(ECC_Pawn),
		FCollisionShape::MakeSphere(OuterRadius),
		QueryParams
	);

	// 주기적 진단 로그 (각 파동 10 틱마다)
	static int32 TickCounter = 0;
	const bool bLog = (++TickCounter % 10 == 0);
	if (bLog)
	{
		UE_LOG(LogTemp, Warning,
			TEXT("[SlamWaveV1][HitTrace] %s ring=%.0f (inner=%.0f outer=%.0f) thick=%.0f overlaps=%d"),
			*GetName(), CurrentRadius, InnerRadius, OuterRadius, RingThickness, Overlaps.Num());
	}

	for (const FOverlapResult& Overlap : Overlaps)
	{
		AHellunaHeroCharacter* Player = Cast<AHellunaHeroCharacter>(Overlap.GetActor());
		if (!IsValid(Player)) continue;

		if (AlreadyHitActors.Contains(Player)) continue;

		const FVector PlayerLoc = Player->GetActorLocation();

		// 캡슐 Z 범위 ↔ 파동 Z 범위 중첩 검사
		const UCapsuleComponent* Capsule = Player->GetCapsuleComponent();
		const float PlayerHalfHeight = Capsule ? Capsule->GetScaledCapsuleHalfHeight() : 88.f;
		const float PlayerBottomZ = PlayerLoc.Z - PlayerHalfHeight;
		const float PlayerTopZ    = PlayerLoc.Z + PlayerHalfHeight;
		const float VHalf         = VerticalThickness * 0.5f;
		const float WaveBottomZ   = Center.Z - VHalf;
		const float WaveTopZ      = Center.Z + VHalf;

		if (PlayerTopZ < WaveBottomZ || PlayerBottomZ > WaveTopZ)
		{
			UE_LOG(LogTemp, Warning,
				TEXT("[SlamWaveV1][HitTrace] reject %s: player Z[%.0f..%.0f] ∩ wave Z[%.0f..%.0f] = ∅"),
				*Player->GetName(), PlayerBottomZ, PlayerTopZ, WaveBottomZ, WaveTopZ);
			continue;
		}

		// 수평 거리로 링 범위 검사
		const float HorizontalDist = FVector::Dist2D(Center, PlayerLoc);
		if (HorizontalDist < InnerRadius || HorizontalDist > OuterRadius)
		{
			UE_LOG(LogTemp, Warning,
				TEXT("[SlamWaveV1][HitTrace] reject %s: dist=%.0f not in ring[%.0f~%.0f]"),
				*Player->GetName(), HorizontalDist, InnerRadius, OuterRadius);
			continue;
		}

		// 점프 회피 — 공중 + 발밑이 파동보다 높으면 회피 성공 (맞지 않음)
		if (bAllowJumpDodge)
		{
			UCharacterMovementComponent* CMC = Player->GetCharacterMovement();
			if (CMC && CMC->IsFalling())
			{
				const float FeetZ = PlayerLoc.Z - PlayerHalfHeight;
				const float DodgeTopZ = Center.Z + JumpDodgeMinHeight;
				if (FeetZ > DodgeTopZ)
				{
					UE_LOG(LogTemp, Warning,
						TEXT("[SlamWaveV1][HitTrace] DODGED by %s (feet=%.0f, dodgeTop=%.0f)"),
						*Player->GetName(), FeetZ, DodgeTopZ);
					continue; // 이번 프레임 회피 — HitActors 등록 X
				}
			}
		}

		// 피격 확정
		AlreadyHitActors.Add(Player);

		AController* InstigatorCtrl = DamageInstigator ? DamageInstigator->GetController() : nullptr;
		UGameplayStatics::ApplyDamage(
			Player,
			Damage,
			InstigatorCtrl,
			DamageInstigator,
			UDamageType::StaticClass()
		);

		UE_LOG(LogTemp, Warning,
			TEXT("[SlamWaveV1][HitTrace] HIT %s dist=%.0f dmg=%.1f ring=%.0f~%.0f"),
			*Player->GetName(), HorizontalDist, Damage, InnerRadius, OuterRadius);
	}
}
