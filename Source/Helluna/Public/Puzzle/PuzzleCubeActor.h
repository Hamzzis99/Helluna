// Source/Helluna/Public/Puzzle/PuzzleCubeActor.h

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Puzzle/PuzzleTypes.h"
#include "PuzzleCubeActor.generated.h"

class USphereComponent;
class UStaticMeshComponent;
class AHellunaHeroController;

DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnPuzzleGridUpdated);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnPuzzleLockChanged, bool, bLocked);

/**
 * 퍼즐 큐브 액터 — 프로토타입용 엔티티
 * 에너지 회로(파이프) 퍼즐을 풀어야 데미지를 입힐 수 있는 시스템.
 * 퍼즐 상태 변경/데미지 판정/타이머는 서버 권한(HasAuthority).
 *
 * [보스전 로드맵]
 * 이 액터의 퍼즐 로직(PuzzleGrid, GeneratePuzzle, RotateCell, CheckSolved 등)은
 * 추후 UPuzzleShieldComponent로 이관하여 보스 캐릭터에 부착할 예정.
 *
 * 보스 보호막 패턴:
 *   보호막 ON (무적) → 퍼즐 풀기 → 보호막 OFF (딜타임 5분) → 보호막 ON → 반복
 *   bPuzzleLocked → bShieldActive
 *   UnlockDamage() → DeactivateShield()
 *   RelockPuzzle() → ReactivateShield()
 *   DamageTimeSeconds(10초) → ShieldDownDuration(300초/5분)
 *
 * 참고: reedme/pattern/puzzle-boss-shield-roadmap.md
 */
UCLASS()
class HELLUNA_API APuzzleCubeActor : public AActor
{
	GENERATED_BODY()

public:
	APuzzleCubeActor();

	// =========================================================================================
	// 컴포넌트
	// =========================================================================================

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Puzzle|Components")
	TObjectPtr<UStaticMeshComponent> MeshComp;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Puzzle|Components")
	TObjectPtr<USphereComponent> InteractionSphere;

	// =========================================================================================
	// 리플리케이션 프로퍼티
	// =========================================================================================

	/** 4x4 퍼즐 그리드 데이터 */
	UPROPERTY(ReplicatedUsing = OnRep_PuzzleGrid, BlueprintReadOnly, Category = "Puzzle")
	FPuzzleGridData PuzzleGrid;

	/** true면 데미지 불가 */
	UPROPERTY(ReplicatedUsing = OnRep_bPuzzleLocked, BlueprintReadOnly, Category = "Puzzle")
	bool bPuzzleLocked = true;

	/** 누군가 퍼즐 진행 중 */
	UPROPERTY(Replicated, BlueprintReadOnly, Category = "Puzzle")
	bool bPuzzleInUse = false;

	/** 현재 체력 */
	UPROPERTY(ReplicatedUsing = OnRep_CurrentHealth, BlueprintReadOnly, Category = "Puzzle")
	float CurrentHealth = 0.f;

	/** 해제 후 데미지 가능 시간 (초) */
	UPROPERTY(Replicated, EditAnywhere, BlueprintReadOnly, Category = "Puzzle",
		meta = (DisplayName = "Damageable Time (데미지 가능 시간)"))
	float DamageableTime = 10.f;

	/** 최대 체력 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Puzzle",
		meta = (DisplayName = "Max Health (최대 체력)"))
	float MaxHealth = 1000.f;

	/** 퍼즐 제한시간 (초) */
	UPROPERTY(Replicated, EditAnywhere, BlueprintReadOnly, Category = "Puzzle",
		meta = (DisplayName = "Puzzle Time Limit (퍼즐 제한시간)"))
	float PuzzleTimeLimit = 30.f;

	// =========================================================================================
	// 델리게이트
	// =========================================================================================

	UPROPERTY(BlueprintAssignable, Category = "Puzzle|Events")
	FOnPuzzleGridUpdated OnPuzzleGridUpdated;

	UPROPERTY(BlueprintAssignable, Category = "Puzzle|Events")
	FOnPuzzleLockChanged OnPuzzleLockChanged;

	/** 시간 초과 델리게이트 (위젯에서 바인딩) */
	UPROPERTY(BlueprintAssignable, Category = "Puzzle|Events")
	FOnPuzzleGridUpdated OnPuzzleTimedOutDelegate;

	// =========================================================================================
	// 퍼즐 상호작용 (서버 권한 함수)
	// =========================================================================================

	/**
	 * 퍼즐 진입 시도 (서버에서 호출)
	 * @return 진입 성공 여부
	 */
	bool TryEnterPuzzle(AController* Player);

	/** 퍼즐 퇴출 (서버에서 호출) */
	void ExitPuzzle(AController* Player);

	/** 셀 90도 회전 + CheckSolved (서버에서 호출) */
	void RotateCell(int32 CellIndex);

	/** 상호작용 반경 조회 */
	float GetInteractionRadius() const;

protected:
	virtual void BeginPlay() override;
	virtual void Tick(float DeltaTime) override;
	virtual float TakeDamage(float DamageAmount, struct FDamageEvent const& DamageEvent,
		class AController* EventInstigator, AActor* DamageCauser) override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

private:
	// =========================================================================================
	// 퍼즐 생성
	// =========================================================================================

	/** 4x4 랜덤 퍼즐 생성 (해법 경로 보장) */
	void GeneratePuzzle();

	/** DFS 백트래킹으로 Start→End 경로 탐색 */
	bool FindPathDFS(TArray<int32>& Path, TSet<int32>& Visited, int32 CurrentIndex);

	/** 경로 셀에 올바른 파이프 배치 */
	void PlacePathPipes(const TArray<int32>& Path);

	// =========================================================================================
	// 데미지 게이팅
	// =========================================================================================

	/** 데미지 잠금 해제 + 타이머 시작 */
	void UnlockDamage();

	/** 재잠금 + 새 퍼즐 생성 */
	void RelockPuzzle();

	// =========================================================================================
	// OnRep 콜백
	// =========================================================================================

	UFUNCTION()
	void OnRep_PuzzleGrid();

	UFUNCTION()
	void OnRep_bPuzzleLocked();

	UFUNCTION()
	void OnRep_CurrentHealth();

	// =========================================================================================
	// 내부 상태
	// =========================================================================================

	/** 현재 퍼즐 사용자 (서버 전용, 비리플리케이트) */
	TWeakObjectPtr<AController> CurrentPuzzleUser;

	/** 데미지 타이머 내부 카운터 (서버 전용) */
	float RelockTimer = 0.f;

	/** Tick 로그 1초 1회 제한용 */
	int32 LastLoggedSecond = -1;

	// =========================================================================================
	// 퍼즐 제한시간
	// =========================================================================================

	/** 퍼즐 타임아웃 타이머 (서버 전용) */
	FTimerHandle PuzzleTimeoutTimerHandle;

	/** 타이머 시작 (서버) */
	void StartPuzzleTimer();

	/** 시간 초과 처리 (서버) */
	void OnPuzzleTimeout();

	/** 기존 경로 유지, 회전만 셔플 (서버) */
	void ReshufflePuzzle();

	/** 시간 초과 알림 (서버→모든 클라) */
	UFUNCTION(NetMulticast, Reliable)
	void Multicast_PuzzleTimedOut();

};
