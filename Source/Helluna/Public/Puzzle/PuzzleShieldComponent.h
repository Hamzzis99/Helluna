// Source/Helluna/Public/Puzzle/PuzzleShieldComponent.h

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "PuzzleShieldComponent.generated.h"

class APuzzleCubeActor;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnShieldStateChanged, bool, bActive);

/**
 * 퍼즐 보호막 컴포넌트
 *
 * 보스 캐릭터에 부착하여 퍼즐 기반 보호막 시스템을 구현.
 * 연결된 PuzzleCubeActor의 잠금 해제 이벤트를 수신하여
 * 보호막 ON/OFF를 제어하고, FilterDamage()로 데미지를 차단한다.
 *
 * 흐름:
 *   보호막 ON (bShieldActive=true, 무적)
 *   → 플레이어가 LinkedPuzzleCube에서 퍼즐 풀기
 *   → OnPuzzleLockChanged(false) → bShieldActive=false (딜 가능)
 *   → PuzzleCubeActor의 DamageableTime 경과
 *   → OnPuzzleLockChanged(true) → bShieldActive=true (무적 복귀)
 *   → 반복
 *
 * BP 설정:
 *   1. 보스 BP에 이 컴포넌트 추가
 *   2. LinkedPuzzleCube에 월드에 배치된 BP_PuzzleCube 할당
 *   3. BP_PuzzleCube의 DamageableTime을 300(5분)으로 설정
 *
 * 참고: reedme/pattern/puzzle-boss-shield-roadmap.md
 */
UCLASS(ClassGroup = (Custom), meta = (BlueprintSpawnableComponent,
	DisplayName = "Puzzle Shield Component (퍼즐 보호막)"))
class HELLUNA_API UPuzzleShieldComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UPuzzleShieldComponent();

	// =========================================================================================
	// 보호막 상태
	// =========================================================================================

	/** 보호막 활성 여부 (true = 무적, false = 딜 가능) */
	UPROPERTY(ReplicatedUsing = OnRep_ShieldActive, BlueprintReadOnly, Category = "Shield",
		meta = (DisplayName = "Shield Active (보호막 활성)"))
	bool bShieldActive = true;

	/** 연결된 퍼즐 큐브 (에디터에서 월드 인스턴스 할당) */
	UPROPERTY(EditInstanceOnly, BlueprintReadOnly, Category = "Shield",
		meta = (DisplayName = "Linked Puzzle Cube (연결 퍼즐 큐브)"))
	TObjectPtr<APuzzleCubeActor> LinkedPuzzleCube;

	// =========================================================================================
	// 데미지 필터링
	// =========================================================================================

	/**
	 * 보호막 상태에 따른 데미지 필터링
	 * @param InDamage 원본 데미지
	 * @return bShieldActive=true → 0, false → InDamage 그대로
	 */
	float FilterDamage(float InDamage) const;

	// =========================================================================================
	// 델리게이트
	// =========================================================================================

	/** 보호막 상태 변경 시 브로드캐스트 (VFX/UI 연결용) */
	UPROPERTY(BlueprintAssignable, Category = "Shield|Events")
	FOnShieldStateChanged OnShieldStateChanged;

protected:
	virtual void BeginPlay() override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

private:
	/** PuzzleCubeActor의 OnPuzzleLockChanged 수신 */
	UFUNCTION()
	void OnPuzzleLockChanged(bool bLocked);

	/** 리플리케이션 콜백 */
	UFUNCTION()
	void OnRep_ShieldActive();
};
