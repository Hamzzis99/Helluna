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

	/** 현재 슬롯 상태 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly)
	ESlotState State = ESlotState::Free;

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

	/** 현재 슬롯 배열 읽기 전용 접근 */
	const TArray<FAttackSlot>& GetSlots() const { return Slots; }

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

	/** 우주선 중심에서 후보를 탐색할 최소 반경 (cm) */
	UPROPERTY(EditAnywhere, Category = "슬롯 생성",
		meta=(DisplayName="최소 반경 (cm)", ClampMin="50"))
	float MinRadius = 200.f;

	/** 우주선 중심에서 후보를 탐색할 최대 반경 (cm) */
	UPROPERTY(EditAnywhere, Category = "슬롯 생성",
		meta=(DisplayName="최대 반경 (cm)", ClampMin="100"))
	float MaxRadius = 600.f;

	/** 각도 간격 (도). 작을수록 슬롯 많아짐 (10도 = 링당 36개) */
	UPROPERTY(EditAnywhere, Category = "슬롯 생성",
		meta=(DisplayName="각도 간격 (도)", ClampMin="5", ClampMax="90"))
	float AngleStep = 20.f;

	/** 반경 링 개수 (MinRadius ~ MaxRadius 사이를 이 수로 나눔) */
	UPROPERTY(EditAnywhere, Category = "슬롯 생성",
		meta=(DisplayName="반경 링 수", ClampMin="1", ClampMax="5"))
	int32 RadiusRings = 2;

	/** NavMesh 투영 허용 오차 (cm) */
	UPROPERTY(EditAnywhere, Category = "슬롯 생성",
		meta=(DisplayName="NavMesh 허용 오차 (cm)", ClampMin="10"))
	float NavExtent = 100.f;

	/** 슬롯 후보가 우주선 메시와 겹치는지 체크할 Trace 반경 (cm) */
	UPROPERTY(EditAnywhere, Category = "슬롯 생성",
		meta=(DisplayName="메시 겹침 체크 반경 (cm)", ClampMin="10"))
	float MeshOverlapRadius = 40.f;

	/** 슬롯을 다시 빌드 (런타임 재생성) */
	UFUNCTION(BlueprintCallable, Category = "AI|Slot")
	void RebuildSlots();

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
	/** 슬롯 배열 */
	UPROPERTY()
	TArray<FAttackSlot> Slots;

	/** 디버그 누적 시간 */
	float DebugAccum = 0.f;
};
