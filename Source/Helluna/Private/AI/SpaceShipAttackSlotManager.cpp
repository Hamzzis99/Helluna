 /**
 * SpaceShipAttackSlotManager.cpp
 */

#include "AI/SpaceShipAttackSlotManager.h"
#include "Character/HellunaEnemyCharacter.h"
#include "Character/EnemyComponent/HellunaHealthComponent.h"
#include "NavigationSystem.h"
#include "DrawDebugHelpers.h"
#include "Engine/World.h"
#include "Engine/OverlapResult.h"
#include "GameFramework/Actor.h"

USpaceShipAttackSlotManager::USpaceShipAttackSlotManager()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.TickInterval = 0.1f; // 0.1초마다 디버그 갱신
}

void USpaceShipAttackSlotManager::BeginPlay()
{
	Super::BeginPlay();

	// 데디케이티드 서버: AI는 서버에서만 실행되므로 클라이언트에서는 슬롯 생성 불필요
	AActor* Owner = GetOwner();
	if (!Owner || !Owner->HasAuthority())
	{
		SetComponentTickEnabled(false);
		return;
	}

	// NavMesh 초기화 완료 후 슬롯 생성
	// World Partition 맵에서는 NavMesh가 스트리밍으로 로드되므로
	// 첫 시도에서 실패하면 ScheduleSlotRetry()가 자동으로 재시도한다.
	SlotRetryCount = 0;
	FTimerHandle TimerHandle;
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().SetTimer(TimerHandle, this,
			&USpaceShipAttackSlotManager::BuildSlots, 1.0f, false);
	}
}

// ============================================================================
// BuildSlots — 우주선 주변 NavMesh 위 유효 위치를 슬롯으로 등록
// ============================================================================
void USpaceShipAttackSlotManager::BuildSlots()
{
	Slots.Empty();

	AActor* Owner = GetOwner();
	if (!Owner) return;

	UWorld* World = GetWorld();
	if (!World) return;

	UNavigationSystemV1* NavSys = UNavigationSystemV1::GetCurrent(World);
	if (!NavSys)
	{
		UE_LOG(LogTemp, Verbose, TEXT("[enemybugreport][SlotBuildFail] Reason=MissingNavMesh"));
		return;
	}

	const FVector Center = Owner->GetActorLocation();

	// 반경 링 × 각도 간격으로 후보 생성
	const int32 AngleCount = FMath::Max(1, FMath::RoundToInt(360.f / AngleStep));
	const float RadiusStep = (RadiusRings > 1)
		? (MaxRadius - MinRadius) / (RadiusRings - 1)
		: 0.f;

	int32 ValidCount = 0;
	int32 TotalCount = 0;

	for (int32 Ring = 0; Ring < RadiusRings; ++Ring)
	{
		const float Radius = MinRadius + RadiusStep * Ring;

		for (int32 i = 0; i < AngleCount; ++i)
		{
			++TotalCount;
			const float AngleRad = FMath::DegreesToRadians(i * AngleStep);
			const FVector Offset(FMath::Cos(AngleRad) * Radius, FMath::Sin(AngleRad) * Radius, 0.f);

			// 충분히 높은 곳에서 시작하여 경사 지형에서도 지면을 찾을 수 있도록
			// Center.Z + 2000은 어떤 지형이든 지면 위에 있을 것
			const FVector TraceStart = FVector(Center.X + Offset.X, Center.Y + Offset.Y, Center.Z + 2000.f);
			const FVector TraceEnd   = FVector(Center.X + Offset.X, Center.Y + Offset.Y, Center.Z - 2000.f);

			// 지면 LineTrace (우주선 메시 무시)
			FHitResult HitResult;
			FCollisionQueryParams TraceParams;
			TraceParams.AddIgnoredActor(Owner);

			FVector Candidate = TraceStart;
			if (World->LineTraceSingleByChannel(HitResult, TraceStart, TraceEnd, ECC_WorldStatic, TraceParams))
			{
				Candidate = HitResult.ImpactPoint + FVector(0, 0, 10.f); // 지면 위 10cm
			}
			else
			{
				continue; // 지면을 못 찾으면 스킵
			}

			FVector NavLocation;
			if (!ValidateSlotCandidate(Candidate, NavLocation))
			{
				continue;
			}

			FAttackSlot Slot;
			Slot.WorldLocation = NavLocation;
			Slot.State = ESlotState::Free;
			Slots.Add(Slot);
			++ValidCount;
		}
	}

	// 후보/유효 비율 로그 — 경사 지형에서 슬롯 후보 전부 탈락 시 진단용
	UE_LOG(LogTemp, Log,
		TEXT("[enemybugreport][SlotBuildRatio] Valid=%d Total=%d Ratio=%.1f NavExtent=%.0f"),
		ValidCount, TotalCount,
		TotalCount > 0 ? (float)ValidCount / TotalCount * 100.f : 0.f,
		NavExtent);

	if (ValidCount == 0)
	{
		UE_LOG(LogTemp, Warning,
			TEXT("[enemybugreport][SlotBuildFail] Reason=NoValidSlots Try=%d MaxTry=%d MinRadius=%.0f MaxRadius=%.0f"),
			SlotRetryCount + 1, MaxSlotRetryCount, MinRadius, MaxRadius);

		// World Partition에서 NavMesh가 아직 스트리밍되지 않았을 수 있음 → 재시도
		ScheduleSlotRetry();
		return;
	}

	// 성공: 재시도 타이머 정리
	if (UWorld* TimerWorld = GetWorld())
	{
		TimerWorld->GetTimerManager().ClearTimer(SlotRetryTimerHandle);
	}
	SlotRetryCount = 0;

	UE_LOG(LogTemp, Log, TEXT("[enemybugreport][SlotBuildSuccess] Valid=%d Total=%d Owner=%s"),
		ValidCount, TotalCount, *Center.ToString());

	// 디버그 드로잉
	if (bDebugDraw)
	{
		for (const FAttackSlot& Slot : Slots)
		{
			DrawDebugSphere(World, Slot.WorldLocation, 30.f, 8, FColor::Cyan, false, DebugDrawDuration);
		}
	}
}

// ============================================================================
// ValidateSlotCandidate — NavMesh 투영 + 메시 겹침 체크
// ============================================================================
bool USpaceShipAttackSlotManager::ValidateSlotCandidate(const FVector& Candidate, FVector& OutNavLocation) const
{
	UWorld* World = GetWorld();
	if (!World) return false;

	UNavigationSystemV1* NavSys = UNavigationSystemV1::GetCurrent(World);
	if (!NavSys) return false;

	// 1. NavMesh 투영 — Z 허용 범위를 크게 잡아 경사 지형 대응
	FNavLocation NavLoc;
	const FVector Extent(NavExtent, NavExtent, NavExtent * 8.f);
	if (!NavSys->ProjectPointToNavigation(Candidate, NavLoc, Extent))
		return false;

	// Ground trace 위치와 NavMesh 투영 결과의 높이 차가 너무 크면 공중 슬롯로 판단한다.
	if (FMath::Abs(NavLoc.Location.Z - Candidate.Z) > MaxNavProjectionHeightDelta)
		return false;

	AActor* Owner = GetOwner();
	if (Owner)
	{
		FCollisionQueryParams TraceParams;
		TraceParams.AddIgnoredActor(nullptr);

		// 2. 우주선 밑인지 체크 — 위로 Trace 쏴서 우주선에 막히면 거부
		{
			const FVector UpStart = NavLoc.Location + FVector(0, 0, 10.f);
			const FVector UpEnd   = NavLoc.Location + FVector(0, 0, 1500.f);
			FHitResult UpHit;
			if (World->LineTraceSingleByChannel(UpHit, UpStart, UpEnd, ECC_Visibility, TraceParams))
			{
				if (UpHit.GetActor() == Owner)
					return false;
			}
		}

		// 3. 우주선 위인지 체크 — 아래로 Trace 쏴서 우주선에 맞으면 거부
		//    (슬롯이 우주선 상부 표면 위에 생성되는 것 방지)
		{
			const FVector DownStart = NavLoc.Location + FVector(0, 0, 10.f);
			const FVector DownEnd   = NavLoc.Location - FVector(0, 0, 500.f);
			FHitResult DownHit;
			if (World->LineTraceSingleByChannel(DownHit, DownStart, DownEnd, ECC_Visibility, TraceParams))
			{
				if (DownHit.GetActor() == Owner)
					return false;
			}
		}
	}

	OutNavLocation = NavLoc.Location;
	return true;
}

// ============================================================================
// RequestSlot — 가장 가까운 Free 슬롯 배정
// ============================================================================
bool USpaceShipAttackSlotManager::RequestSlot(AActor* Monster, int32& OutSlotIndex, FVector& OutLocation)
{
	if (!Monster) return false;

	// 이미 같은 몬스터가 슬롯을 갖고 있으면 그대로 재사용한다.
	for (int32 i = 0; i < Slots.Num(); ++i)
	{
		if (Slots[i].OccupyingMonster.Get() != Monster)
		{
			continue;
		}

		OutSlotIndex = i;
		OutLocation = Slots[i].WorldLocation;
		UE_LOG(LogTemp, Log,
			TEXT("[enemybugreport][SlotReuse] Monster=%s Slot=%d State=%s SlotLoc=%s"),
			*GetNameSafe(Monster),
			i,
			Slots[i].State == ESlotState::Reserved ? TEXT("Reserved") :
			(Slots[i].State == ESlotState::Occupied ? TEXT("Occupied") : TEXT("Free")),
			*OutLocation.ToCompactString());
		return true;
	}

	const FVector MonsterLoc = Monster->GetActorLocation();

	int32 BestIdx = -1;
	float BestDistSq = MAX_FLT;
	int32 FreeCount = 0;
	int32 ReservedCount = 0;
	int32 OccupiedCount = 0;

	for (int32 i = 0; i < Slots.Num(); ++i)
	{
		if (Slots[i].State == ESlotState::Free) ++FreeCount;
		else if (Slots[i].State == ESlotState::Reserved) ++ReservedCount;
		else if (Slots[i].State == ESlotState::Occupied) ++OccupiedCount;

		if (Slots[i].State != ESlotState::Free) continue;

		const float HeightDelta = FMath::Abs(MonsterLoc.Z - Slots[i].WorldLocation.Z);
		if (HeightDelta > MaxSlotAssignmentHeightDelta)
			continue;

		const float DistSq = FVector::DistSquared2D(MonsterLoc, Slots[i].WorldLocation);
		if (DistSq < BestDistSq)
		{
			BestDistSq = DistSq;
			BestIdx = i;
		}
	}

	if (BestIdx < 0)
	{
		UE_LOG(LogTemp, Warning,
			TEXT("[enemybugreport][SlotRequestFail] Monster=%s Total=%d Free=%d Reserved=%d Occupied=%d"),
			*GetNameSafe(Monster),
			Slots.Num(),
			FreeCount,
			ReservedCount,
			OccupiedCount);
		return false;
	}

	Slots[BestIdx].State = ESlotState::Reserved;
	Slots[BestIdx].OccupyingMonster = Monster;

	OutSlotIndex = BestIdx;
	OutLocation  = Slots[BestIdx].WorldLocation;

	UE_LOG(LogTemp, Log, TEXT("[enemybugreport][SlotReserveVerbose] Slot=%d Loc=%s Monster=%s"),
		BestIdx, *OutLocation.ToString(), *Monster->GetName());
	UE_LOG(LogTemp, Warning,
		TEXT("[enemybugreport][SlotRequestSuccess] Monster=%s Slot=%d SlotLoc=%s Dist=%.1f Free=%d Reserved=%d Occupied=%d"),
		*GetNameSafe(Monster),
		BestIdx,
		*OutLocation.ToCompactString(),
		FMath::Sqrt(BestDistSq),
		FreeCount,
		ReservedCount,
		OccupiedCount);

	return true;
}

// ============================================================================
// OccupySlot — Reserved → Occupied
// ============================================================================
void USpaceShipAttackSlotManager::OccupySlot(int32 SlotIndex, AActor* Monster)
{
	if (!Slots.IsValidIndex(SlotIndex)) return;
	if (Slots[SlotIndex].OccupyingMonster.Get() != Monster) return;

	Slots[SlotIndex].State = ESlotState::Occupied;

	UE_LOG(LogTemp, Log, TEXT("[enemybugreport][SlotOccupy] Slot=%d Monster=%s"),
		SlotIndex, Monster ? *Monster->GetName() : TEXT("nullptr"));
}

// ============================================================================
// ReleaseSlot — 몬스터 기준으로 슬롯 반납
// ============================================================================
void USpaceShipAttackSlotManager::ReleaseSlot(AActor* Monster)
{
	if (!Monster) return;

	for (int32 i = 0; i < Slots.Num(); ++i)
	{
		if (Slots[i].OccupyingMonster.Get() == Monster)
		{
			const ESlotState OldState = Slots[i].State;
			Slots[i].State = ESlotState::Free;
			Slots[i].OccupyingMonster = nullptr;

			UE_LOG(LogTemp, Log, TEXT("[enemybugreport][SlotRelease] Slot=%d PrevState=%s Monster=%s"),
				i,
				OldState == ESlotState::Reserved ? TEXT("Reserved") : TEXT("Occupied"),
				*Monster->GetName());
			return;
		}
	}
}

// ============================================================================
// ReleaseSlotByIndex
// ============================================================================
void USpaceShipAttackSlotManager::ReleaseSlotByIndex(int32 SlotIndex)
{
	if (!Slots.IsValidIndex(SlotIndex)) return;

	Slots[SlotIndex].State = ESlotState::Free;
	Slots[SlotIndex].OccupyingMonster = nullptr;
}

bool USpaceShipAttackSlotManager::GetMonsterSlotInfo(const AActor* Monster, int32& OutSlotIndex, ESlotState& OutState) const
{
	OutSlotIndex = INDEX_NONE;
	OutState = ESlotState::Free;

	if (!Monster)
	{
		return false;
	}

	for (int32 i = 0; i < Slots.Num(); ++i)
	{
		if (Slots[i].OccupyingMonster.Get() != Monster)
		{
			continue;
		}

		OutSlotIndex = i;
		OutState = Slots[i].State;
		return true;
	}

	return false;
}

// ============================================================================
// GetWaitingPosition — 슬롯 없을 때 우주선 근처 대기 위치 반환
// ============================================================================
bool USpaceShipAttackSlotManager::GetWaitingPosition(AActor* Monster, FVector& OutLocation) const
{
	if (!Monster) return false;

	UWorld* World = GetWorld();
	if (!World) return false;

	UNavigationSystemV1* NavSys = UNavigationSystemV1::GetCurrent(World);
	if (!NavSys) return false;

	AActor* Owner = GetOwner();
	if (!Owner) return false;

	// 우주선 중심 기준 MaxRadius ~ MaxRadius*1.5 사이 랜덤 위치 탐색
	// (슬롯 범위 바깥에서 대기하여 슬롯 자리가 나면 진입)
	const FVector Center = Owner->GetActorLocation();
	const float WaitRadius = MaxRadius * 1.2f;

	for (int32 Try = 0; Try < 8; ++Try)
	{
		const float Angle = FMath::RandRange(0.f, 360.f);
		const float Radius = FMath::RandRange(MaxRadius, WaitRadius);
		const FVector CandidateXY = FVector(
			Center.X + FMath::Cos(FMath::DegreesToRadians(Angle)) * Radius,
			Center.Y + FMath::Sin(FMath::DegreesToRadians(Angle)) * Radius,
			Center.Z + 2000.f
		);

		// 지면 LineTrace
		FHitResult HitResult;
		FCollisionQueryParams TraceParams;
		TraceParams.AddIgnoredActor(Owner);
		const FVector TraceEnd = FVector(CandidateXY.X, CandidateXY.Y, Center.Z - 2000.f);

		FVector Candidate = CandidateXY;
		if (World->LineTraceSingleByChannel(HitResult, CandidateXY, TraceEnd, ECC_WorldStatic, TraceParams))
			Candidate = HitResult.ImpactPoint + FVector(0, 0, 10.f);

		FNavLocation NavLoc;
		const FVector Extent(NavExtent * 2.f, NavExtent * 2.f, NavExtent * 8.f);
		if (NavSys->ProjectPointToNavigation(Candidate, NavLoc, Extent))
		{
			FCollisionQueryParams WaitTraceParams;

			// 우주선 밑인지 체크
			{
				const FVector UpStart = NavLoc.Location + FVector(0, 0, 10.f);
				const FVector UpEnd   = NavLoc.Location + FVector(0, 0, 1500.f);
				FHitResult UpHit;
				if (World->LineTraceSingleByChannel(UpHit, UpStart, UpEnd, ECC_Visibility, WaitTraceParams))
				{
					if (UpHit.GetActor() == Owner)
						continue;
				}
			}

			// 우주선 위인지 체크
			{
				const FVector DownStart = NavLoc.Location + FVector(0, 0, 10.f);
				const FVector DownEnd   = NavLoc.Location - FVector(0, 0, 500.f);
				FHitResult DownHit;
				if (World->LineTraceSingleByChannel(DownHit, DownStart, DownEnd, ECC_Visibility, WaitTraceParams))
				{
					if (DownHit.GetActor() == Owner)
						continue;
				}
			}

			OutLocation = NavLoc.Location;
			UE_LOG(LogTemp, Warning,
				TEXT("[enemybugreport][WaitingPosition] Monster=%s WaitLoc=%s Try=%d"),
				*GetNameSafe(Monster),
				*OutLocation.ToCompactString(),
				Try + 1);
			return true;
		}
	}

	UE_LOG(LogTemp, Warning,
		TEXT("[enemybugreport][WaitingPositionFail] Monster=%s Owner=%s MaxRadius=%.1f WaitRadius=%.1f"),
		*GetNameSafe(Monster),
		*GetNameSafe(Owner),
		MaxRadius,
		WaitRadius);

	return false;
}

// ============================================================================
// TriggerBuildSlotsIfEmpty — 플레이어 접속 시 슬롯 재시도
// ============================================================================
void USpaceShipAttackSlotManager::TriggerBuildSlotsIfEmpty()
{
	if (Slots.Num() == 0)
	{
		UE_LOG(LogTemp, Log, TEXT("[enemybugreport][SlotBuildRetry] Reason=PlayerJoinedSlotsEmpty"));
		SlotRetryCount = 0; // 카운터 리셋하여 재시도 가능하게
		BuildSlots();
	}
}

// ============================================================================
// RebuildSlots — 런타임 재생성
// ============================================================================
void USpaceShipAttackSlotManager::RebuildSlots()
{
	// 현재 예약/점유 중인 슬롯의 몬스터들에게 슬롯 반납 처리
	for (FAttackSlot& Slot : Slots)
	{
		Slot.State = ESlotState::Free;
		Slot.OccupyingMonster = nullptr;
	}
	BuildSlots();
}

// ============================================================================
// TickComponent — 디버그 드로잉
// ============================================================================
void USpaceShipAttackSlotManager::TickComponent(float DeltaTime, ELevelTick TickType,
	FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	// 죽은 몬스터가 점유한 슬롯 자동 반납
	for (int32 i = 0; i < Slots.Num(); ++i)
	{
		FAttackSlot& Slot = Slots[i];
		if (Slot.State == ESlotState::Free)
		{
			continue;
		}

		AActor* OccupyingActor = Slot.OccupyingMonster.Get();
		bool bShouldAutoRelease = !IsValid(OccupyingActor) || OccupyingActor->IsActorBeingDestroyed();

		if (!bShouldAutoRelease && OccupyingActor)
		{
			bShouldAutoRelease = OccupyingActor->IsHidden() || !OccupyingActor->GetActorEnableCollision();
		}

		if (!bShouldAutoRelease)
		{
			if (const AHellunaEnemyCharacter* Enemy = Cast<AHellunaEnemyCharacter>(OccupyingActor))
			{
				if (const UHellunaHealthComponent* HealthComponent = Enemy->FindComponentByClass<UHellunaHealthComponent>())
				{
					bShouldAutoRelease = HealthComponent->IsDead();
				}
			}
		}

		if (bShouldAutoRelease)
		{
			UE_LOG(LogTemp, Verbose, TEXT("[enemybugreport][SlotAutoRelease] Slot=%d State=%s"),
				i, Slot.State == ESlotState::Reserved ? TEXT("Reserved") : TEXT("Occupied"));
			Slot.State = ESlotState::Free;
			Slot.OccupyingMonster = nullptr;
		}
	}

	if (!bDebugDraw) return;

	UWorld* World = GetWorld();
	if (!World) return;

	for (int32 i = 0; i < Slots.Num(); ++i)
	{
		const FAttackSlot& Slot = Slots[i];

		FColor Color;
		switch (Slot.State)
		{
		case ESlotState::Free:     Color = FColor::Green;  break;
		case ESlotState::Reserved: Color = FColor::Yellow; break;
		case ESlotState::Occupied: Color = FColor::Red;    break;
		default:                   Color = FColor::White;  break;
		}

		DrawDebugSphere(World, Slot.WorldLocation, 35.f, 8, Color, false, -1.f, 0, 2.f);

		// 인덱스 + 상태 텍스트
		const FString StateStr = (Slot.State == ESlotState::Free)
			? TEXT("Free")
			: (Slot.State == ESlotState::Reserved ? TEXT("Rsv") : TEXT("Occ"));
		DrawDebugString(World, Slot.WorldLocation + FVector(0, 0, 50.f),
			FString::Printf(TEXT("[%d] %s"), i, *StateStr),
			nullptr, Color, -1.f);
	}
}

// ============================================================================
// ScheduleSlotRetry — BuildSlots 실패 시 재시도 스케줄링
//
// [World Partition NavMesh 스트리밍 대응]
// 패키징 빌드에서 서버 시작 직후에는 NavMesh 데이터가 아직 스트리밍되지 않아
// BuildSlots가 유효 슬롯 0개를 생성할 수 있다.
// 플레이어가 접속하고 World Partition 셀이 로드되면 NavMesh도 함께 로드되므로
// 일정 간격으로 재시도하면 성공한다.
// ============================================================================
void USpaceShipAttackSlotManager::ScheduleSlotRetry()
{
	++SlotRetryCount;

	if (SlotRetryCount >= MaxSlotRetryCount)
	{
		UE_LOG(LogTemp, Error,
			TEXT("[enemybugreport][SlotBuildFail] Reason=RetryExhausted Try=%d"),
			MaxSlotRetryCount);
		return;
	}

	UWorld* World = GetWorld();
	if (!World) return;

	World->GetTimerManager().SetTimer(
		SlotRetryTimerHandle, this,
		&USpaceShipAttackSlotManager::BuildSlots,
		SlotRetryInterval, false);

	UE_LOG(LogTemp, Log,
		TEXT("[enemybugreport][SlotBuildRetryScheduled] Delay=%.1f Try=%d MaxTry=%d"),
		SlotRetryInterval, SlotRetryCount, MaxSlotRetryCount);
}
