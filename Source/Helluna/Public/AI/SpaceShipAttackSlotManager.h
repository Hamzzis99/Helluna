/**
 * SpaceShipAttackSlotManager.h
 *
 * ─── 역할 ────────────────────────────────────────────────────
 * 우주선(AResourceUsingObject_SpaceShip) 에 붙어서
 * 몬스터들의 공격 슬롯을 관리하는 ActorComponent.
 *
 * ─── 슬롯 생성 방식 ──────────────────────────────────────────
 * BeginPlay 에서 NavMesh + 지면 검증을 거쳐 슬롯을 자동 생성.
 * 우주선이 기울어져 있거나 지형에 묻혀 있어도 NavMesh 위의
 * 실제 도달 가능한 위치만 슬롯으로 등록된다.
 *
 * ─── 슬롯 상태 ───────────────────────────────────────────────
 *   Free      : 아무도 예약/점유 안 함
 *   Reserved  : 몬스터가 이동 중 (아직 도착 안 함)
 *   Occupied  : 몬스터가 슬롯 위치에 도착해 공격 중
 *
 * ─── 사용 흐름 ───────────────────────────────────────────────
 *   몬스터 ChaseTarget Task 진입
 *     → RequestSlot(Monster)       : 가장 가까운 Free 슬롯 예약
 *     → 해당 슬롯 위치로 MoveToLocation
 *     → 도착 판정 시 OccupySlot(Monster)
 *   몬스터 이탈/사망/재탐색
 *     → ReleaseSlot(Monster)       : 슬롯 반납
 *
 * @author 자동 생성
 */

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "SpaceShipAttackSlotManager.generated.h"

// ─── 슬롯 상태 ────────────────────────────────────────────────
UENUM(BlueprintType)
enum class ESlotState : uint8
{
	Free      UMETA(DisplayName = "비어있음"),
	Reserved  UMETA(DisplayName = "예약됨 (이동중)"),
	Occupied  UMETA(DisplayName = "점유됨 (공격중)"),
};

// ─── 공격 슬롯 구조체 ──────────────────────────────────────────
USTRUCT(BlueprintType)
struct FAttackSlot
{
	GENERATED_BODY()

	/** NavMesh 위 검증된 월드 위치 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly)
	FVector WorldLocation = FVector::ZeroVector;

	/** 우주선 표면 기준 기준점 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly)
	FVector SurfaceLocation = FVector::ZeroVector;

	/** 표면에서 바깥을 향하는 2D 노멀 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly)
	FVector SurfaceNormal = FVector::ForwardVector;

	/** 현재 슬롯 상태 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly)
	ESlotState State = ESlotState::Free;

	/** 우주선 기준 슬롯 섹터 각도 (Yaw, 0~360) */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly)
	float SectorAngleDegrees = 0.f;

	/** 이 슬롯을 예약/점유한 몬스터 */
	UPROPERTY()
	TWeakObjectPtr<AActor> OccupyingMonster = nullptr;

	/** 슬롯이 유효한지 (위치가 설정됐는지) */
	bool IsValid() const { return !WorldLocation.IsZero(); }
};

// ─── 컴포넌트 ─────────────────────────────────────────────────
UCLASS(ClassGroup=(AI), meta=(BlueprintSpawnableComponent),
	meta=(DisplayName="SpaceShip Attack Slot Manager"))
class HELLUNA_API USpaceShipAttackSlotManager : public UActorComponent
{
	GENERATED_BODY()

public:
	USpaceShipAttackSlotManager();

protected:
	virtual void BeginPlay() override;

	// ─── 슬롯 생성 ────────────────────────────────────────────

	/** 슬롯 자동 생성 (BeginPlay + RebuildSlots 시 호출) */
	void BuildSlots();

	/** 후보 점 하나가 슬롯으로 쓸 수 있는지 검증 */
	bool ValidateSlotCandidate(const FVector& Candidate, FVector& OutNavLocation) const;

	/** 우주선 실제 크기를 반영한 슬롯 생성 반경을 계산한다. */
	void GetEffectiveSlotRadii(float& OutMinRadius, float& OutMaxRadius) const;

public:
	// ─── 공개 API ─────────────────────────────────────────────

	/**
	 * 몬스터에게 가장 적합한 Free 슬롯을 배정한다.
	 * @param Monster   슬롯을 요청하는 몬스터 액터
	 * @param OutSlotIndex  배정된 슬롯 인덱스 (-1 = 실패)
	 * @param OutLocation   배정된 슬롯 위치
	 * @return 배정 성공 여부
	 */
	UFUNCTION(BlueprintCallable, Category = "AI|Slot")
	bool RequestSlot(AActor* Monster, int32& OutSlotIndex, FVector& OutLocation);

	/**
	 * 슬롯에 도착했을 때 Reserved → Occupied 전환.
	 * @param SlotIndex  RequestSlot에서 받은 인덱스
	 * @param Monster    점유할 몬스터
	 */
	UFUNCTION(BlueprintCallable, Category = "AI|Slot")
	void OccupySlot(int32 SlotIndex, AActor* Monster);

	/**
	 * 슬롯을 반납한다 (사망/재탐색/이탈 시).
	 * @param Monster  반납할 몬스터 (슬롯 인덱스 몰라도 됨)
	 */
	UFUNCTION(BlueprintCallable, Category = "AI|Slot")
	void ReleaseSlot(AActor* Monster);

	/**
	 * 특정 인덱스의 슬롯을 직접 반납.
	 */
	UFUNCTION(BlueprintCallable, Category = "AI|Slot")
	void ReleaseSlotByIndex(int32 SlotIndex);

	/**
	 * 슬롯보다 느슨한 "섹터 분산" 위치를 요청한다.
	 * 정확한 1:1 슬롯 점유 대신 우주선 둘레 섹터별 점유 수를 균등화한다.
	 */
	UFUNCTION(BlueprintCallable, Category = "AI|Sector")
	bool RequestSectorPosition(AActor* Monster, int32& OutSectorIndex, FVector& OutLocation);

	/** 섹터 예약을 반납한다. */
	UFUNCTION(BlueprintCallable, Category = "AI|Sector")
	void ReleaseSectorReservation(AActor* Monster);

	/** 슬롯/섹터 예약을 한 번에 반납한다. */
	UFUNCTION(BlueprintCallable, Category = "AI|Slot")
	void ReleaseEngagementReservation(AActor* Monster);

	// ─── [ShipTopV1] 우주선 상단 점프 슬롯 예약 ───────────────
	// 목적: 모든 몬스터가 점프하면 선체 위가 꽉 차므로 고정 수량만 허용.
	// 슬롯 위치는 계산하지 않음(ShipJump GA가 bounds로 계산) → 예약 카운터만 관리.

	/** 우주선 상단 슬롯 예약 시도. 빈 슬롯이 있으면 true 반환. */
	UFUNCTION(BlueprintCallable, Category = "AI|ShipTop")
	bool TryReserveTopSlot(AActor* Monster);

	/** 예약한 상단 슬롯을 반납한다. */
	UFUNCTION(BlueprintCallable, Category = "AI|ShipTop")
	void ReleaseTopSlot(AActor* Monster);

	/** 특정 몬스터가 상단 슬롯을 예약했는지 조회. */
	UFUNCTION(BlueprintCallable, Category = "AI|ShipTop")
	bool HasTopSlot(const AActor* Monster) const;

	/** 현재 예약된 상단 슬롯 수. */
	UFUNCTION(BlueprintCallable, Category = "AI|ShipTop")
	int32 GetTopSlotUsage() const { return ReservedTopSlotMonsters.Num(); }

	/** 현재 슬롯 배열 읽기 전용 접근 */
	const TArray<FAttackSlot>& GetSlots() const { return Slots; }

	/**
	 * 특정 몬스터가 현재 어떤 슬롯을 들고 있는지 조회한다.
	 * @param Monster      조회할 몬스터
	 * @param OutSlotIndex 몬스터가 예약/점유 중인 슬롯 인덱스
	 * @param OutState     해당 슬롯 상태
	 * @return 슬롯을 보유 중이면 true
	 */
	UFUNCTION(BlueprintCallable, Category = "AI|Slot")
	bool GetMonsterSlotInfo(const AActor* Monster, int32& OutSlotIndex, ESlotState& OutState) const;

	/**
	 * 슬롯이 없을 때 대기할 위치를 반환한다.
	 * 우주선 주변 NavMesh 위 랜덤 위치로 천천히 배회하게 만든다.
	 * @param Monster  대기할 몬스터
	 * @param OutLocation  대기 위치
	 * @return 유효한 위치를 찾았는지
	 */
	UFUNCTION(BlueprintCallable, Category = "AI|Slot")
	bool GetWaitingPosition(AActor* Monster, FVector& OutLocation) const;

	// ─── 설정 ─────────────────────────────────────────────────

	/** 우주선 외곽 표면에서 바깥으로 띄울 최소 거리 (cm) */
	UPROPERTY(EditAnywhere, Category = "슬롯 생성",
		meta=(DisplayName="최소 반경 (cm)", ClampMin="50"))
	float MinRadius = 80.f;

	/** 우주선 외곽 표면에서 바깥으로 띄울 최대 거리 (cm) */
	UPROPERTY(EditAnywhere, Category = "슬롯 생성",
		meta=(DisplayName="최대 반경 (cm)", ClampMin="100"))
	float MaxRadius = 140.f;

	/** 외곽 표면 샘플 각도 간격 (도). 작을수록 슬롯 많아짐 */
	UPROPERTY(EditAnywhere, Category = "슬롯 생성",
		meta=(DisplayName="각도 간격 (도)", ClampMin="5", ClampMax="90"))
	float AngleStep = 15.f;

	/** 외곽 표면 바깥 오프셋 링 개수 (MinRadius ~ MaxRadius 사이를 이 수로 나눔) */
	UPROPERTY(EditAnywhere, Category = "슬롯 생성",
		meta=(DisplayName="반경 링 수", ClampMin="1", ClampMax="5"))
	int32 RadiusRings = 3;

	/** NavMesh 투영 허용 오차 (cm) — 경사 지형에서 슬롯 후보 탈락 방지를 위해 넉넉하게 설정 */
	UPROPERTY(EditAnywhere, Category = "슬롯 생성",
		meta=(DisplayName="NavMesh 허용 오차 (cm)", ClampMin="10"))
	float NavExtent = 200.f;

	/** 슬롯 후보가 우주선 메시와 겹치는지 체크할 Trace 반경 (cm) */
	UPROPERTY(EditAnywhere, Category = "슬롯 생성",
		meta=(DisplayName="메시 겹침 체크 반경 (cm)", ClampMin="10"))
	float MeshOverlapRadius = 40.f;

	/** 지면 LineTrace 결과와 NavMesh 투영 높이 차이가 이 값을 넘으면 공중/상부 슬롯으로 보고 버린다. */
	UPROPERTY(EditAnywhere, Category = "슬롯 생성",
		meta=(DisplayName="Nav 투영 최대 높이 차 (cm)", ClampMin="0"))
	float MaxNavProjectionHeightDelta = 120.f;

	/** 슬롯 배정 시 몬스터와 슬롯의 높이 차가 너무 크면 후보에서 제외한다. */
	UPROPERTY(EditAnywhere, Category = "슬롯 생성",
		meta=(DisplayName="슬롯 배정 최대 높이 차 (cm)", ClampMin="0"))
	float MaxSlotAssignmentHeightDelta = 250.f;

	/** 대기 위치가 몬스터와 너무 다른 층에 잡히는 것을 방지한다. */
	UPROPERTY(EditAnywhere, Category = "슬롯 생성",
		meta=(DisplayName="대기 위치 최대 높이 차 (cm)", ClampMin="0"))
	float MaxWaitingPositionHeightDelta = 350.f;

	/** 실제 이동 목표를 표면에서 얼마나 바깥쪽에 둘지 최소 거리 */
	UPROPERTY(EditAnywhere, Category = "슬롯 생성",
		meta=(DisplayName="공격 접근 최소 거리 (cm)", ClampMin="0"))
	float ApproachDistanceMin = 80.f;

	/** 실제 이동 목표를 표면에서 얼마나 바깥쪽에 둘지 최대 거리 */
	UPROPERTY(EditAnywhere, Category = "슬롯 생성",
		meta=(DisplayName="공격 접근 최대 거리 (cm)", ClampMin="0"))
	float ApproachDistanceMax = 140.f;

	/** 같은 섹터 안에서 좌우로 얼마나 퍼뜨릴지 */
	UPROPERTY(EditAnywhere, Category = "슬롯 생성",
		meta=(DisplayName="공격 접근 측면 분산 (cm)", ClampMin="0"))
	float ApproachLateralSpread = 90.f;

	/** 슬롯 시스템이 실패하거나 비활성일 때 사용할 섹터 기반 분산 활성화 */
	UPROPERTY(EditAnywhere, Category = "섹터 분산",
		meta=(DisplayName="섹터 분산 사용"))
	bool bEnableSectorDistribution = true;

	/** 우주선을 몇 개의 섹터로 나눠서 분산할지 */
	UPROPERTY(EditAnywhere, Category = "섹터 분산",
		meta=(DisplayName="섹터 수", ClampMin="2", ClampMax="16"))
	int32 SectorCount = 6;

	/** 섹터 접근 위치의 최소 거리 */
	UPROPERTY(EditAnywhere, Category = "섹터 분산",
		meta=(DisplayName="섹터 접근 최소 거리 (cm)", ClampMin="50"))
	float SectorApproachDistanceMin = 180.f;

	/** 섹터 접근 위치의 최대 거리 */
	UPROPERTY(EditAnywhere, Category = "섹터 분산",
		meta=(DisplayName="섹터 접근 최대 거리 (cm)", ClampMin="80"))
	float SectorApproachDistanceMax = 420.f;

	/** 같은 섹터 안에서 위치를 약간 퍼뜨리기 위한 각도 지터 */
	UPROPERTY(EditAnywhere, Category = "섹터 분산",
		meta=(DisplayName="섹터 각도 지터 (도)", ClampMin="0", ClampMax="45"))
	float SectorAngleJitter = 10.f;

	/** 같은 섹터 안에서 측면 분산 정도 */
	UPROPERTY(EditAnywhere, Category = "섹터 분산",
		meta=(DisplayName="섹터 측면 분산 (cm)", ClampMin="0"))
	float SectorLateralSpread = 140.f;

	/** 슬롯을 다시 빌드 (런타임 재생성) */
	UFUNCTION(BlueprintCallable, Category = "AI|Slot")
	void RebuildSlots();

	/** [ShipTopV1] 동시에 우주선 위에 점프해 있을 수 있는 최대 몬스터 수. */
	UPROPERTY(EditAnywhere, Category = "상단 점프",
		meta=(DisplayName="상단 점프 최대 슬롯 수", ClampMin="1", ClampMax="20"))
	int32 MaxTopSlots = 5;

	/**
	 * 플레이어 접속 시 호출 — 슬롯이 아직 0개면 즉시 BuildSlots 재시도.
	 * World Partition에서 플레이어 접속 후 NavMesh가 로드되므로 이 시점에 재시도하면 성공 확률이 높다.
	 */
	UFUNCTION(BlueprintCallable, Category = "AI|Slot")
	void TriggerBuildSlotsIfEmpty();

	/** 디버그 드로잉 활성화 */
	UPROPERTY(EditAnywhere, Category = "디버그",
		meta=(DisplayName="디버그 드로잉 활성화"))
	bool bDebugDraw = false;

	/** 디버그 드로잉 유지 시간 (초) */
	UPROPERTY(EditAnywhere, Category = "디버그",
		meta=(DisplayName="디버그 유지 시간 (초)", ClampMin="0.1"))
	float DebugDrawDuration = 5.f;

	/** 틱마다 슬롯 상태를 드로잉 (bDebugDraw true일 때만) */
	virtual void TickComponent(float DeltaTime, ELevelTick TickType,
		FActorComponentTickFunction* ThisTickFunction) override;

private:
	float ComputeSectorAngleDegrees(const FVector& WorldLocation) const;
	float FindOrRememberPreferredSectorAngle(AActor* Monster, const FVector& ReferenceLocation);
	void RememberPreferredSectorAngle(AActor* Monster, float SectorAngleDegrees);
	bool GetPreferredSectorAngle(AActor* Monster, float& OutSectorAngleDegrees) const;
	void CleanupPreferredSectorAngles();
	void CleanupSectorReservations();
	bool FindApproachLocationForSlot(const FAttackSlot& Slot, const FVector& MonsterLocation, FVector& OutApproachLocation) const;
	bool FindApproachLocationForSector(int32 SectorIndex, const AActor* Monster, const FVector& MonsterLocation, FVector& OutApproachLocation) const;
	float ComputeOwnerCollisionDistance(const FVector& Location) const;
	int32 GetReservedSectorOccupancy(int32 SectorIndex, const AActor* IgnoredMonster = nullptr) const;
	float GetSectorAngleDegrees(int32 SectorIndex) const;

	/** BuildSlots 재시도 래퍼 — 슬롯 0개 시 자동 재시도 */
	void TryBuildSlots();

	/** 재시도 타이머 핸들 */
	FTimerHandle BuildSlotTimerHandle;

	/** 재시도 횟수 카운터 */
	int32 BuildSlotRetryCount = 0;

	/** 최대 재시도 횟수 */
	static constexpr int32 MaxBuildSlotRetries = 5;

	/** 재시도 간격 (초) */
	static constexpr float BuildSlotRetryInterval = 2.0f;

	/** 슬롯 배열 */
	UPROPERTY()
	TArray<FAttackSlot> Slots;

	/** 몬스터별 선호 슬롯 섹터 캐시 */
	TMap<TWeakObjectPtr<AActor>, float> PreferredSectorAngles;

	/** 몬스터별 예약된 섹터 인덱스 */
	TMap<TWeakObjectPtr<AActor>, int32> ReservedSectorIndices;

	/** [ShipTopV1] 상단 슬롯을 예약한 몬스터 집합. */
	TSet<TWeakObjectPtr<AActor>> ReservedTopSlotMonsters;

	/** [ShipTopV1] 무효화된 항목을 정리. */
	void CleanupTopSlotReservations();

	/** 디버그 누적 시간 */
	float DebugAccum = 0.f;

	// ─── BuildSlots 재시도 (World Partition NavMesh 스트리밍 대응) ──
	/** BuildSlots 재시도 타이머 핸들 */
	FTimerHandle SlotRetryTimerHandle;

	/** 현재까지 재시도한 횟수 */
	int32 SlotRetryCount = 0;

	/** 최대 재시도 횟수 (패키징에서 NavMesh 스트리밍 지연이 길 수 있으므로 넉넉하게) */
	static constexpr int32 MaxSlotRetryCount = 30;

	/** 재시도 간격 (초) */
	static constexpr float SlotRetryInterval = 2.0f;

	/** BuildSlots 실패 시 재시도 스케줄링 */
	void ScheduleSlotRetry();

	/** 우주선 메시 내부 또는 너무 가까운 지점인지 검사한다. */
	bool IsNearOwnerCollision(const FVector& Location, float Clearance) const;
};
