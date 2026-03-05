// ════════════════════════════════════════════════════════════════════════════════
// HellunaDefenseGameMode.h
// ════════════════════════════════════════════════════════════════════════════════
//
// 게임 로직 전용 GameMode
// 로그인/인벤토리 시스템은 HellunaBaseGameMode에서 상속
//
// 🎮 이 클래스의 역할:
//    - InitializeGame() : 게임 시작 (BaseGameMode에서 호출됨)
//    - EnterDay() / EnterNight() : 낮밤 전환
//    - SpawnTestMonsters() : 몬스터 스폰
//    - TrySummonBoss() : 보스 소환
//
// 📌 작성자: Gihyeon
// ════════════════════════════════════════════════════════════════════════════════

#pragma once

#include "CoreMinimal.h"
#include "GameMode/HellunaBaseGameMode.h"
#include "HellunaTypes.h"
#include "HellunaDefenseGameMode.generated.h"

class ATargetPoint;
class AHellunaEnemyMassSpawner;

UCLASS()
class HELLUNA_API AHellunaDefenseGameMode : public AHellunaBaseGameMode
{
	GENERATED_BODY()

public:
	AHellunaDefenseGameMode();

protected:
	virtual void BeginPlay() override;

public:
	// ════════════════════════════════════════════════════════════════════════════════
	// 게임 초기화 (BaseGameMode의 virtual 함수 override)
	// ════════════════════════════════════════════════════════════════════════════════

	/**
	 * 게임 초기화 - 첫 플레이어 캐릭터 소환 후 자동 호출됨
	 *
	 * 이 함수가 호출되면:
	 * - 게임이 본격적으로 시작됨
	 * - EnterDay()가 호출되어 낮/밤 사이클 시작
	 */
	virtual void InitializeGame() override;

	/** 게임 재시작 (AGameMode override) */
	virtual void RestartGame() override;

	// ════════════════════════════════════════════════════════════════════════════════
	// 낮/밤 사이클 시스템
	// ════════════════════════════════════════════════════════════════════════════════
protected:
	FTimerHandle TimerHandle_ToNight;
	FTimerHandle TimerHandle_ToDay;

	/**
	 * 현재 진행 일(Day) 수. EnterDay() 호출마다 1 증가.
	 * 0 → 게임 시작 전, 1 → 첫 번째 낮.
	 * BossSpawnDays 와 비교하여 보스 소환 여부를 결정한다.
	 */
	UPROPERTY(BlueprintReadOnly, Category = "Defense(게임)|DayNight(낮밤)",
		meta = (DisplayName = "현재 일(Day) 수"))
	int32 CurrentDay = 0;

	/** 낮 지속 시간 (초) */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Defense(게임)|DayNight(낮밤)",
		meta = (DisplayName = "낮 지속 시간(초)"))
	float TestDayDuration = 10.f;

	/** 밤 실패 후 낮으로 돌아가는 딜레이 (초) */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Defense(게임)|DayNight(낮밤)",
		meta = (DisplayName = "밤→낮 전환 딜레이(초)"))
	float TestNightFailToDayDelay = 5.f;

	/** 낮 시작 */
	void EnterDay();

	/** 밤 시작 */
	void EnterNight();

	/** 우주선 수리 완료 여부 체크 */
	bool IsSpaceShipFullyRepaired(int32& OutCurrent, int32& OutNeed) const;

	// ════════════════════════════════════════════════════════════════════════════════
	// 몬스터 스폰 시스템
	// ════════════════════════════════════════════════════════════════════════════════
protected:
	/**
	 * 이번 밤 남은 몬스터 수.
	 * EnterNight()에서 MassSpawnerCount + DirectSpawnCount 합산으로 확정.
	 * 몬스터 사망 시 1 차감, 0이 되면 낮 전환.
	 *
	 * TWeakObjectPtr 기반 AliveMonsters 대신 이 카운터를 사용하는 이유:
	 * ECS 몬스터는 DoSpawning() 후 Actor 전환까지 프레임 딜레이가 있어
	 * BeginPlay(등록 시점)가 낮으로 넘어간 후일 수 있기 때문.
	 */
	int32 RemainingMonstersThisNight = 0;

	/** 근거리 MassSpawner 캐시 */
	UPROPERTY()
	TArray<TObjectPtr<AHellunaEnemyMassSpawner>> CachedMeleeSpawners;

	/** 원거리 MassSpawner 캐시 */
	UPROPERTY()
	TArray<TObjectPtr<AHellunaEnemyMassSpawner>> CachedRangeSpawners;

	/**
	 * 날짜별 근거리/원거리 소환 수 테이블.
	 * FromDay 방식: CurrentDay 이하인 항목 중 FromDay가 가장 큰 설정이 적용됨.
	 * 비어있으면 MassSpawnCountPerNight를 근거리에 적용.
	 *
	 * 에디터 설정 예시:
	 *   [0] FromDay=1, MeleeCount=3, RangeCount=0
	 *   [1] FromDay=2, MeleeCount=5, RangeCount=1
	 *   [2] FromDay=3, MeleeCount=5, RangeCount=4
	 */
	UPROPERTY(EditDefaultsOnly, Category = "Defense(게임)|Monster(몬스터)",
		meta = (DisplayName = "날짜별 소환 테이블"))
	TArray<FNightSpawnConfig> NightSpawnTable;

	/** 근거리 몬스터용 MassSpawner 클래스 */
	UPROPERTY(EditDefaultsOnly, Category = "Defense(게임)|Monster(몬스터)",
		meta = (DisplayName = "근거리 MassSpawner 클래스"))
	TSubclassOf<AHellunaEnemyMassSpawner> MeleeMassSpawnerClass;

	/** 원거리 몬스터용 MassSpawner 클래스 */
	UPROPERTY(EditDefaultsOnly, Category = "Defense(게임)|Monster(몬스터)",
		meta = (DisplayName = "원거리 MassSpawner 클래스"))
	TSubclassOf<AHellunaEnemyMassSpawner> RangeMassSpawnerClass;

	/** NightSpawnTable 미설정 시 근거리 기본 소환 수 */
	UPROPERTY(EditDefaultsOnly, Category = "Defense(게임)|Monster(몬스터)",
		meta = (DisplayName = "기본 소환 수 (NightSpawnTable 미설정 시)"))
	int32 MassSpawnCountPerNight = 3;

	/**
	 * 몬스터 스폰 포인트 태그.
	 * 근거리: MeleeSpawnTag, 원거리: RangeSpawnTag 로 분리해서 사용.
	 * 같은 위치를 쓰더라도 태그는 반드시 분리해야 Spawner가 올바르게 캐싱됨.
	 */
	UPROPERTY(EditDefaultsOnly, Category = "Defense(게임)|Monster(몬스터)",
		meta = (DisplayName = "근거리 스폰 포인트 태그"))
	FName MeleeSpawnTag = TEXT("MeleeSpawn");

	UPROPERTY(EditDefaultsOnly, Category = "Defense(게임)|Monster(몬스터)",
		meta = (DisplayName = "원거리 스폰 포인트 태그"))
	FName RangeSpawnTag = TEXT("RangeSpawn");

	/** 근거리 스폰 포인트 목록 (BeginPlay에서 캐싱) */
	UPROPERTY()
	TArray<ATargetPoint*> MeleeSpawnPoints;

	/** 원거리 스폰 포인트 목록 (BeginPlay에서 캐싱) */
	UPROPERTY()
	TArray<ATargetPoint*> RangeSpawnPoints;

	void CacheMeleeSpawnPoints();
	void CacheRangeSpawnPoints();

	/** CurrentDay에 맞는 NightSpawnConfig 반환. 없으면 nullptr */
	const FNightSpawnConfig* GetCurrentNightConfig() const;

	void TriggerMassSpawning();

public:
	UFUNCTION(BlueprintCallable, Category = "Defense(게임)|Monster(몬스터)")
	void NotifyMonsterDied(AActor* DeadMonster);

	UFUNCTION(BlueprintPure, Category = "Defense(게임)|Monster(몬스터)")
	int32 GetRemainingMonsterCount() const { return RemainingMonstersThisNight; }

	// RegisterAliveMonster는 더 이상 카운터 역할을 하지 않지만
	// 기존 BP/코드 호환성을 위해 빈 함수로 유지
	UFUNCTION(BlueprintCallable, Category = "Defense(게임)|Monster(몬스터)")
	void RegisterAliveMonster(AActor* Monster);

	// ════════════════════════════════════════════════════════════════════════════════
	// 보스 스폰 시스템
	// ════════════════════════════════════════════════════════════════════════════════
public:
	UFUNCTION(BlueprintCallable, Category = "Defense(게임)|Boss(보스)")
	void SetBossReady(bool bReady);

	/**
	 * 보스/세미보스 사망 알림.
	 * NotifyMonsterDied 내부에서 bIsBoss == true 일 때 호출된다.
	 * 현재는 디버그 출력만 수행.
	 */
	void NotifyBossDied(AActor* DeadBoss);

protected:
	// ────────────────────────────────────────────────────────────────────────────
	// 보스 소환 스케줄
	// ────────────────────────────────────────────────────────────────────────────

	/**
	 * 보스/세미보스 소환 스케줄 배열.
	 *
	 * 에디터에서 원소를 추가해 각 항목에 다음을 설정한다.
	 *   - SpawnDay   : 소환할 Day 번호 (1-based)
	 *   - BossClass  : 그 날 밤에 소환할 Pawn 클래스
	 *   - bIsSemiBoss: true이면 세미보스 처리 경로, false이면 정보스 처리 경로
	 *
	 * 예시 (에디터 배열):
	 *   [0] SpawnDay=3,  BossClass=BP_SemiBoss_A, bIsSemiBoss=true
	 *   [1] SpawnDay=7,  BossClass=BP_Boss_Final,  bIsSemiBoss=false
	 *
	 * ⚠ 같은 Day에 중복 항목이 있으면 첫 번째 항목만 사용된다.
	 * ⚠ BossClass가 null인 항목은 소환 시 오류 메시지를 출력하고 스킵된다.
	 */
	UPROPERTY(EditDefaultsOnly, Category = "Defense(게임)|Boss(보스)",
		meta = (DisplayName = "보스 소환 스케줄",
			ToolTip = "날짜별 소환할 보스/세미보스 클래스를 지정합니다.\nSpawnDay에 일차, BossClass에 소환할 Pawn 클래스, bIsSemiBoss로 보스 등급을 설정하세요."))
	TArray<FBossSpawnEntry> BossSchedule;

	// ────────────────────────────────────────────────────────────────────────────
	// 공통 설정
	// ────────────────────────────────────────────────────────────────────────────

	/** 보스 스폰 포인트 태그 */
	UPROPERTY(EditDefaultsOnly, Category = "Defense(게임)|Boss(보스)",
		meta = (DisplayName = "보스 스폰 포인트 태그",
			ToolTip = "레벨의 TargetPoint 액터에 이 태그를 붙이면 보스 스폰 위치로 사용됩니다."))
	FName BossSpawnPointTag = TEXT("BossSpawn");

	/** 보스 스폰 Z 오프셋 (cm) */
	UPROPERTY(EditDefaultsOnly, Category = "Defense(게임)|Boss(보스)",
		meta = (DisplayName = "보스 스폰 Z 오프셋(cm)"))
	float SpawnZOffset = 150.f;

	/** 보스 소환 준비 상태 플래그 */
	UPROPERTY(BlueprintReadOnly, Category = "Defense(게임)|Boss(보스)")
	bool bBossReady = false;

	/** 현재 살아있는 보스 (단일). 사망 시 nullptr로 초기화 */
	UPROPERTY()
	TWeakObjectPtr<AActor> AliveBoss;

	/** BeginPlay에서 캐싱한 보스 스폰 포인트 목록 */
	UPROPERTY()
	TArray<ATargetPoint*> BossSpawnPoints;

	void CacheBossSpawnPoints();

	/**
	 * 보스 소환 진입점.
	 * EnterNight()에서 BossSchedule 배열을 조회해 CurrentDay에 맞는
	 * FBossSpawnEntry를 찾은 뒤 이 함수에 전달한다.
	 */
	void TrySummonBoss(const FBossSpawnEntry& Entry);
};
