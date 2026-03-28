 /**
 * SpaceShipAttackSlotManager.cpp
 */

#include "AI/SpaceShipAttackSlotManager.h"
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

	// NavMesh 초기화 완료 후 슬롯 생성 (BeginPlay 즉시 호출 시 NavMesh 미준비로 0개 생성됨)
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
		UE_LOG(LogTemp, Verbose, TEXT("[SlotManager] NavMesh 없음 - 슬롯 생성 실패"));
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

			// Z는 우주선 중심 기준으로 높이 추적해서 지면을 찾음
			const FVector CandidateXY = FVector(Center.X + Offset.X, Center.Y + Offset.Y, Center.Z + 500.f);

			// 지면 LineTrace
			FHitResult HitResult;
			FCollisionQueryParams TraceParams;
			TraceParams.AddIgnoredActor(Owner);
			const FVector TraceEnd = FVector(CandidateXY.X, CandidateXY.Y, Center.Z - 1000.f);

			FVector Candidate = CandidateXY;
			if (World->LineTraceSingleByChannel(HitResult, CandidateXY, TraceEnd, ECC_WorldStatic, TraceParams))
			{
				Candidate = HitResult.ImpactPoint + FVector(0, 0, 10.f); // 지면 위 10cm
			}

			FVector NavLocation;
			if (!ValidateSlotCandidate(Candidate, NavLocation))
			{
				UE_LOG(LogTemp, Verbose, TEXT("[SlotManager] 후보 거부: Ring=%d, Angle=%d, Pos=%s"),
					Ring, i, *Candidate.ToString());
				continue;
			}

			FAttackSlot Slot;
			Slot.WorldLocation = NavLocation;
			Slot.State = ESlotState::Free;
			Slots.Add(Slot);
			++ValidCount;
		}
	}

	UE_LOG(LogTemp, Verbose, TEXT("[SlotManager] 슬롯 생성 완료: %d / %d 유효 (우주선 위치: %s)"),
		ValidCount, TotalCount, *Center.ToString());

	// ── 디버그: NavMesh가 우주선 주변을 실제로 커버하는지 체크 ──────────
	{
		FNavLocation NavLocCenter;
		const bool bShipOnNav = NavSys->ProjectPointToNavigation(
			Center, NavLocCenter, FVector(500.f, 500.f, 500.f));
		UE_LOG(LogTemp, Warning,
			TEXT("[SlotManager] 우주선 중심 NavMesh 투영: %s → NavPos=%s (NavMesh 위=%d)"),
			*Center.ToString(), *NavLocCenter.Location.ToString(), (int)bShipOnNav);

		if (ValidCount == 0)
		{
			UE_LOG(LogTemp, Error,
				TEXT("[SlotManager] ❌ 유효 슬롯 0개! 우주선 주변 NavMesh가 없거나 메시와 겹칩니다."));
			UE_LOG(LogTemp, Error,
				TEXT("[SlotManager] ❌ 확인 사항: 1) Show→Navigation 으로 NavMesh 범위 확인  2) MinRadius=%.0f / MaxRadius=%.0f 범위 안에 NavMesh 있는지 확인"),
				MinRadius, MaxRadius);
		}
	}

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

	// 2. 우주선 메시와 겹침 여부 체크 — NavMesh 투영이 통과한 위치는 이미
	//    도달 가능하다고 판단하므로, 우주선과의 Overlap으로 거부하지 않는다.
	//    (우주선 메시가 크면 주변 유효 슬롯까지 모두 거부되는 문제 방지)

	OutNavLocation = NavLoc.Location;
	return true;
}

// ============================================================================
// RequestSlot — 가장 가까운 Free 슬롯 배정
// ============================================================================
bool USpaceShipAttackSlotManager::RequestSlot(AActor* Monster, int32& OutSlotIndex, FVector& OutLocation)
{
	if (!Monster) return false;

	// 이미 슬롯을 갖고 있으면 기존 반납
	ReleaseSlot(Monster);

	const FVector MonsterLoc = Monster->GetActorLocation();

	int32 BestIdx = -1;
	float BestDistSq = MAX_FLT;

	for (int32 i = 0; i < Slots.Num(); ++i)
	{
		if (Slots[i].State != ESlotState::Free) continue;

		const float DistSq = FVector::DistSquared(MonsterLoc, Slots[i].WorldLocation);
		if (DistSq < BestDistSq)
		{
			BestDistSq = DistSq;
			BestIdx = i;
		}
	}

	if (BestIdx < 0)
	{
		UE_LOG(LogTemp, Verbose, TEXT("[SlotManager] 사용 가능한 슬롯 없음 (총 %d개)"), Slots.Num());
		return false;
	}

	Slots[BestIdx].State = ESlotState::Reserved;
	Slots[BestIdx].OccupyingMonster = Monster;

	OutSlotIndex = BestIdx;
	OutLocation  = Slots[BestIdx].WorldLocation;

	UE_LOG(LogTemp, Log, TEXT("[SlotManager] 슬롯 예약: 인덱스=%d, 위치=%s, 몬스터=%s"),
		BestIdx, *OutLocation.ToString(), *Monster->GetName());

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

	UE_LOG(LogTemp, Log, TEXT("[SlotManager] 슬롯 점유: 인덱스=%d, 몬스터=%s"),
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

			UE_LOG(LogTemp, Log, TEXT("[SlotManager] 슬롯 반납: 인덱스=%d, 상태=%s, 몬스터=%s"),
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
			Center.Z + 500.f
		);

		// 지면 LineTrace
		FHitResult HitResult;
		FCollisionQueryParams TraceParams;
		TraceParams.AddIgnoredActor(Owner);
		const FVector TraceEnd = FVector(CandidateXY.X, CandidateXY.Y, Center.Z - 1000.f);

		FVector Candidate = CandidateXY;
		if (World->LineTraceSingleByChannel(HitResult, CandidateXY, TraceEnd, ECC_WorldStatic, TraceParams))
			Candidate = HitResult.ImpactPoint + FVector(0, 0, 10.f);

		FNavLocation NavLoc;
		const FVector Extent(NavExtent * 2.f, NavExtent * 2.f, NavExtent * 8.f);
		if (NavSys->ProjectPointToNavigation(Candidate, NavLoc, Extent))
		{
			OutLocation = NavLoc.Location;
			return true;
		}
	}

	return false;
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
		if (Slot.State != ESlotState::Free && !Slot.OccupyingMonster.IsValid())
		{
			UE_LOG(LogTemp, Verbose, TEXT("[SlotManager] 죽은 몬스터 슬롯 자동 반납: 인덱스=%d, 상태=%s"),
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
