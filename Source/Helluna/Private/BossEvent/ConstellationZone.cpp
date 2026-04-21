// Capstone Project Helluna

#include "BossEvent/ConstellationZone.h"

#include "Character/HellunaEnemyCharacter.h"
#include "Character/HellunaHeroCharacter.h"
#include "Engine/OverlapResult.h"
#include "EngineUtils.h"
#include "Kismet/GameplayStatics.h"

#define CS_LOG(Fmt, ...) UE_LOG(LogTemp, Warning, TEXT("[Constellation] " Fmt), ##__VA_ARGS__)

AConstellationZone::AConstellationZone()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.bStartWithTickEnabled = false;
	bReplicates = true;
}

// -----------------------------------------------------------------
// ActivateZone
// -----------------------------------------------------------------
void AConstellationZone::ActivateZone()
{
	Super::ActivateZone();

	if (bZoneActive) return;
	bZoneActive = true;
	NextNodeToActivate = 0;
	bFinalWarningStarted = false;
	bFinalExplosionDone = false;
	ActiveLines.Empty();
	LineDamageLastTime.Empty();

	CS_LOG("=== ActivateZone (Nodes=%d, Radius=%.0f) ===", NodeCount, PlacementRadius);

	// 노드 위치 생성
	GenerateNodePositions();

	// 하늘 마법진 VFX
	if (SkyMagicCircleVFX && OwnerEnemy)
	{
		const FVector SkyLoc = GetActorLocation() + FVector(0.f, 0.f, NodeHeight + 300.f);
		OwnerEnemy->MulticastPlayEffect(SkyLoc, SkyMagicCircleVFX, SkyMagicCircleVFXScale, false);
	}

	// 비활성 노드 VFX (전체 표시)
	if (NodeVFX && OwnerEnemy)
	{
		for (const FConstellationNode& Node : Nodes)
		{
			OwnerEnemy->MulticastPlayEffect(Node.SkyPosition, NodeVFX, NodeVFXScale, false);
		}
	}

	// 첫 노드 활성화 (약간 딜레이)
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().SetTimer(
			NodeActivateTimerHandle, this,
			&AConstellationZone::ActivateNextNode,
			NodeActivateInterval, true,
			1.5f // 첫 활성화까지 약간 대기
		);
	}

	SetActorTickEnabled(true);
}

// -----------------------------------------------------------------
// DeactivateZone
// -----------------------------------------------------------------
void AConstellationZone::DeactivateZone()
{
	Super::DeactivateZone();

	bZoneActive = false;
	SetActorTickEnabled(false);
	Nodes.Empty();
	ActiveLines.Empty();
	LineDamageLastTime.Empty();

	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(NodeActivateTimerHandle);
	}
}

// -----------------------------------------------------------------
// GenerateNodePositions — 원형 + 랜덤 오프셋으로 별자리 형태
// -----------------------------------------------------------------
void AConstellationZone::GenerateNodePositions()
{
	Nodes.Empty();
	Nodes.SetNum(NodeCount);

	const FVector Center = GetActorLocation();

	for (int32 i = 0; i < NodeCount; ++i)
	{
		// 기본 원형 배치
		const float BaseAngle = (360.f * static_cast<float>(i)) / static_cast<float>(NodeCount);
		// 약간의 각도 오프셋
		const float AngleOffset = FMath::FRandRange(-15.f, 15.f);
		const float Angle = FMath::DegreesToRadians(BaseAngle + AngleOffset);

		// 반지름 오프셋
		const float RadiusOffset = FMath::FRandRange(-PlacementRandomOffset, PlacementRandomOffset);
		const float R = PlacementRadius + RadiusOffset;

		// 지면 위치
		const FVector GroundPos = Center + FVector(
			FMath::Cos(Angle) * R,
			FMath::Sin(Angle) * R,
			0.f
		);

		// 하늘 위치
		const FVector SkyPos = GroundPos + FVector(0.f, 0.f, NodeHeight);

		Nodes[i].WorldPosition = GroundPos;
		Nodes[i].SkyPosition = SkyPos;
		Nodes[i].bActivated = false;
		Nodes[i].ActivateTime = 0.0;

		CS_LOG("Node %d: (%.0f, %.0f) R=%.0f", i, GroundPos.X, GroundPos.Y, R);
	}
}

// -----------------------------------------------------------------
// ActivateNextNode — 순차 활성화
// -----------------------------------------------------------------
void AConstellationZone::ActivateNextNode()
{
	if (!bZoneActive || NextNodeToActivate >= NodeCount) return;

	UWorld* World = GetWorld();
	if (!World) return;

	FConstellationNode& Node = Nodes[NextNodeToActivate];
	Node.bActivated = true;
	Node.ActivateTime = World->GetTimeSeconds();

	CS_LOG("Node %d ACTIVATED", NextNodeToActivate);

	// 활성화 VFX
	if (NodeActivateVFX && OwnerEnemy)
	{
		OwnerEnemy->MulticastPlayEffect(Node.SkyPosition, NodeActivateVFX, NodeActivateVFXScale, false);
	}

	// 기둥 데미지 (아래로 쏟아지는 에너지)
	DealAreaDamage(Node.WorldPosition, PillarDamage, PillarRadius);

	// 사운드
	if (NodeActivateSound && OwnerEnemy)
	{
		OwnerEnemy->Multicast_PlaySoundAtLocation(NodeActivateSound, Node.WorldPosition);
	}

	// 연결선 갱신
	UpdateLines();

	NextNodeToActivate++;

	// 모든 노드 활성화 시 → 최종 폭발 시작
	if (NextNodeToActivate >= NodeCount)
	{
		if (UWorld* W = GetWorld())
		{
			W->GetTimerManager().ClearTimer(NodeActivateTimerHandle);
		}
		BeginFinalExplosion();
	}
}

// -----------------------------------------------------------------
// UpdateLines — 인접 활성 노드 연결
// -----------------------------------------------------------------
void AConstellationZone::UpdateLines()
{
	ActiveLines.Empty();

	for (int32 i = 0; i < NodeCount; ++i)
	{
		if (!Nodes[i].bActivated) continue;

		// 다음 노드 (순환)
		const int32 Next = (i + 1) % NodeCount;
		if (!Nodes[Next].bActivated) continue;

		FConstellationLine Line;
		Line.NodeA = i;
		Line.NodeB = Next;
		Line.PointA = FVector2D(Nodes[i].WorldPosition.X, Nodes[i].WorldPosition.Y);
		Line.PointB = FVector2D(Nodes[Next].WorldPosition.X, Nodes[Next].WorldPosition.Y);

		ActiveLines.Add(MoveTemp(Line));

		// 연결선 VFX (중점에 표시)
		if (LineVFX && OwnerEnemy)
		{
			const FVector MidPoint = (Nodes[i].WorldPosition + Nodes[Next].WorldPosition) * 0.5f;
			OwnerEnemy->MulticastPlayEffect(MidPoint, LineVFX, LineVFXScale, false);
		}
	}

	CS_LOG("Active lines: %d", ActiveLines.Num());
}

// -----------------------------------------------------------------
// Tick
// -----------------------------------------------------------------
void AConstellationZone::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

	// 게임플레이 로직(데미지/VFX 트리거)은 서버에서만. 클라는 멀티캐스트 수신만.
	if (!HasAuthority()) return;

	if (!bZoneActive) return;

	// 연결선 데미지
	ProcessLineDamage(DeltaTime);

	// 최종 폭발 경고 → 폭발
	if (bFinalWarningStarted && !bFinalExplosionDone)
	{
		UWorld* World = GetWorld();
		if (World && (World->GetTimeSeconds() - FinalWarningStartTime) >= FinalWarningDuration)
		{
			ExecuteFinalExplosion();
		}
	}
}

// -----------------------------------------------------------------
// ProcessLineDamage — 연결선 위 플레이어 데미지
// -----------------------------------------------------------------
void AConstellationZone::ProcessLineDamage(float DeltaTime)
{
	if (ActiveLines.Num() == 0 || !OwnerEnemy) return;

	UWorld* World = GetWorld();
	if (!World) return;

	const double CurrentTime = World->GetTimeSeconds();
	const float DamageThisTick = LineDamagePerSecond * DeltaTime;

	for (TActorIterator<AHellunaHeroCharacter> It(World); It; ++It)
	{
		AHellunaHeroCharacter* Player = *It;
		if (!IsValid(Player)) continue;

		const FVector2D PlayerXY(Player->GetActorLocation().X, Player->GetActorLocation().Y);

		// 높이 체크
		const float HeightDiff = FMath::Abs(Player->GetActorLocation().Z - GetActorLocation().Z);
		if (HeightDiff > 500.f) continue;

		bool bOnLine = false;
		for (const FConstellationLine& Line : ActiveLines)
		{
			const float Dist = PointToSegmentDistance2D(PlayerXY, Line.PointA, Line.PointB);
			if (Dist <= LineWidth * 0.5f)
			{
				bOnLine = true;
				break;
			}
		}

		if (!bOnLine) continue;

		// DPS 쿨타임 (0.25초 간격)
		TWeakObjectPtr<AActor> WeakPlayer(Player);
		const double* LastTime = LineDamageLastTime.Find(WeakPlayer);
		if (LastTime && (CurrentTime - *LastTime) < 0.25) continue;

		LineDamageLastTime.FindOrAdd(WeakPlayer) = CurrentTime;

		UGameplayStatics::ApplyDamage(
			Player, DamageThisTick * 4.f, // 0.25초 간격이므로 4배
			OwnerEnemy->GetController(), OwnerEnemy,
			UDamageType::StaticClass()
		);
	}
}

// -----------------------------------------------------------------
// BeginFinalExplosion — 별자리 완성 → 경고
// -----------------------------------------------------------------
void AConstellationZone::BeginFinalExplosion()
{
	UWorld* World = GetWorld();
	if (!World) return;

	bFinalWarningStarted = true;
	FinalWarningStartTime = World->GetTimeSeconds();

	CS_LOG("=== FINAL WARNING STARTED (%.1f seconds) ===", FinalWarningDuration);

	// 경고 VFX (중심에)
	if (FinalWarningVFX && OwnerEnemy)
	{
		OwnerEnemy->MulticastPlayEffect(GetActorLocation(), FinalWarningVFX, FinalWarningVFXScale, false);
	}

	// 경고 사운드
	if (WarningSound && OwnerEnemy)
	{
		OwnerEnemy->Multicast_PlaySoundAtLocation(WarningSound, GetActorLocation());
	}
}

// -----------------------------------------------------------------
// ExecuteFinalExplosion — 볼록 껍질 내부 대규모 데미지
// -----------------------------------------------------------------
void AConstellationZone::ExecuteFinalExplosion()
{
	bFinalExplosionDone = true;

	CS_LOG("=== FINAL EXPLOSION ===");

	// 볼록 껍질 계산
	TArray<FVector2D> Points;
	for (const FConstellationNode& Node : Nodes)
	{
		Points.Add(FVector2D(Node.WorldPosition.X, Node.WorldPosition.Y));
	}

	const TArray<FVector2D> Hull = ComputeConvexHull(Points);

	// 폭발 VFX (각 노드에서 + 중심)
	if (FinalExplosionVFX && OwnerEnemy)
	{
		OwnerEnemy->MulticastPlayEffect(GetActorLocation(), FinalExplosionVFX, FinalExplosionVFXScale, false);
	}

	// 폭발 사운드
	if (FinalExplosionSound && OwnerEnemy)
	{
		OwnerEnemy->Multicast_PlaySoundAtLocation(FinalExplosionSound, GetActorLocation());
	}

	// 볼록 껍질 내부의 플레이어에게 데미지
	UWorld* World = GetWorld();
	if (World && OwnerEnemy)
	{
		for (TActorIterator<AHellunaHeroCharacter> It(World); It; ++It)
		{
			AHellunaHeroCharacter* Player = *It;
			if (!IsValid(Player)) continue;

			const FVector2D PlayerXY(Player->GetActorLocation().X, Player->GetActorLocation().Y);

			if (IsPointInConvexHull(PlayerXY, Hull))
			{
				UGameplayStatics::ApplyDamage(
					Player, FinalExplosionDamage,
					OwnerEnemy->GetController(), OwnerEnemy,
					UDamageType::StaticClass()
				);
				CS_LOG("Final explosion hit [%s]", *Player->GetName());
			}
		}
	}

	// 패턴 종료
	bZoneActive = false;
	SetActorTickEnabled(false);
	NotifyPatternFinished(false);
}

// -----------------------------------------------------------------
// ComputeConvexHull — Andrew's monotone chain O(n log n)
// -----------------------------------------------------------------
TArray<FVector2D> AConstellationZone::ComputeConvexHull(const TArray<FVector2D>& Points) const
{
	TArray<FVector2D> Sorted = Points;
	Sorted.Sort([](const FVector2D& A, const FVector2D& B)
	{
		return A.X < B.X || (A.X == B.X && A.Y < B.Y);
	});

	const int32 N = Sorted.Num();
	if (N < 3) return Sorted;

	TArray<FVector2D> Hull;
	Hull.Reserve(N * 2);

	// Lower hull
	for (int32 i = 0; i < N; ++i)
	{
		while (Hull.Num() >= 2)
		{
			const FVector2D& A = Hull[Hull.Num() - 2];
			const FVector2D& B = Hull[Hull.Num() - 1];
			const FVector2D& C = Sorted[i];
			// Cross product: (B-A) × (C-A)
			if ((B.X - A.X) * (C.Y - A.Y) - (B.Y - A.Y) * (C.X - A.X) <= 0.f)
			{
				Hull.Pop(EAllowShrinking::No);
			}
			else
			{
				break;
			}
		}
		Hull.Add(Sorted[i]);
	}

	// Upper hull
	const int32 LowerSize = Hull.Num() + 1;
	for (int32 i = N - 2; i >= 0; --i)
	{
		while (Hull.Num() >= LowerSize)
		{
			const FVector2D& A = Hull[Hull.Num() - 2];
			const FVector2D& B = Hull[Hull.Num() - 1];
			const FVector2D& C = Sorted[i];
			if ((B.X - A.X) * (C.Y - A.Y) - (B.Y - A.Y) * (C.X - A.X) <= 0.f)
			{
				Hull.Pop(EAllowShrinking::No);
			}
			else
			{
				break;
			}
		}
		Hull.Add(Sorted[i]);
	}

	Hull.Pop(EAllowShrinking::No); // 마지막 점 제거 (시작점과 동일)
	return Hull;
}

// -----------------------------------------------------------------
// IsPointInConvexHull — Ray casting algorithm
// -----------------------------------------------------------------
bool AConstellationZone::IsPointInConvexHull(const FVector2D& Point, const TArray<FVector2D>& Hull) const
{
	const int32 N = Hull.Num();
	if (N < 3) return false;

	int32 Crossings = 0;
	for (int32 i = 0; i < N; ++i)
	{
		const FVector2D& A = Hull[i];
		const FVector2D& B = Hull[(i + 1) % N];

		// 수평 Ray (양의 X 방향)
		if ((A.Y <= Point.Y && B.Y > Point.Y) || (B.Y <= Point.Y && A.Y > Point.Y))
		{
			const float T = (Point.Y - A.Y) / (B.Y - A.Y);
			if (Point.X < A.X + T * (B.X - A.X))
			{
				Crossings++;
			}
		}
	}

	return (Crossings % 2) == 1;
}

// -----------------------------------------------------------------
// PointToSegmentDistance2D — 점과 선분 사이의 최소 거리
// -----------------------------------------------------------------
float AConstellationZone::PointToSegmentDistance2D(const FVector2D& Point, const FVector2D& A, const FVector2D& B)
{
	const FVector2D AB = B - A;
	const float ABLenSq = AB.SizeSquared();

	if (ABLenSq < KINDA_SMALL_NUMBER)
	{
		return FVector2D::Distance(Point, A);
	}

	// 선분 위의 가장 가까운 점 = A + t * AB (t를 [0,1]로 클램프)
	float T = FVector2D::DotProduct(Point - A, AB) / ABLenSq;
	T = FMath::Clamp(T, 0.f, 1.f);

	const FVector2D Closest = A + T * AB;
	return FVector2D::Distance(Point, Closest);
}

// -----------------------------------------------------------------
// DealAreaDamage
// -----------------------------------------------------------------
void AConstellationZone::DealAreaDamage(const FVector& Location, float Damage, float Radius)
{
	UWorld* World = GetWorld();
	if (!World || !OwnerEnemy) return;

	TArray<FOverlapResult> Overlaps;
	FCollisionQueryParams QueryParams;
	QueryParams.AddIgnoredActor(this);
	QueryParams.AddIgnoredActor(OwnerEnemy);

	World->OverlapMultiByObjectType(
		Overlaps, Location, FQuat::Identity,
		FCollisionObjectQueryParams(ECC_Pawn),
		FCollisionShape::MakeSphere(Radius),
		QueryParams
	);

	TSet<AActor*> AlreadyHit;
	for (const FOverlapResult& Overlap : Overlaps)
	{
		AHellunaHeroCharacter* Player = Cast<AHellunaHeroCharacter>(Overlap.GetActor());
		if (!IsValid(Player) || AlreadyHit.Contains(Player)) continue;
		AlreadyHit.Add(Player);

		UGameplayStatics::ApplyDamage(
			Player, Damage,
			OwnerEnemy->GetController(), OwnerEnemy,
			UDamageType::StaticClass()
		);
	}
}

#undef CS_LOG
