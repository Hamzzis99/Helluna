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
#include "HellunaDefenseGameMode.generated.h"

class ATargetPoint;
class UHellunaGameResultWidget;

// ════════════════════════════════════════════════════════════════════════════════
// Phase 7: 게임 종료 사유
// ════════════════════════════════════════════════════════════════════════════════
UENUM(BlueprintType)
enum class EHellunaGameEndReason : uint8
{
	None,
	Escaped        UMETA(DisplayName = "탈출 성공"),
	AllDead        UMETA(DisplayName = "전원 사망"),
	ServerShutdown UMETA(DisplayName = "서버 셧다운"),
};

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
	/** 살아있는 몬스터 목록 */
	UPROPERTY()
	TSet<TWeakObjectPtr<AActor>> AliveMonsters;

	/** 몬스터 스폰 포인트 (맵에서 캐싱) */
	UPROPERTY()
	TArray<ATargetPoint*> MonsterSpawnPoints;

public:
	/**
	 * 몬스터 등록 (몬스터 BeginPlay에서 호출)
	 */
	UFUNCTION(BlueprintCallable, Category = "Defense(게임)|Monster(몬스터)")
	void RegisterAliveMonster(AActor* Monster);

	/**
	 * 몬스터 사망 알림 (몬스터 사망 시 호출)
	 * 모든 몬스터가 죽으면 자동으로 EnterDay() 호출됨
	 */
	UFUNCTION(BlueprintCallable, Category = "Defense(게임)|Monster(몬스터)")
	void NotifyMonsterDied(AActor* DeadMonster);

	/** 현재 살아있는 몬스터 수 */
	UFUNCTION(BlueprintPure, Category = "Defense(게임)|Monster(몬스터)")
	int32 GetAliveMonsterCount() const { return AliveMonsters.Num(); }

protected:
	/** 스폰할 몬스터 클래스 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Defense(게임)|Monster(몬스터)",
		meta = (DisplayName = "테스트 몬스터 클래스"))
	TSubclassOf<APawn> TestMonsterClass;

	/** 몬스터 스폰 포인트 태그 */
	UPROPERTY(EditDefaultsOnly, Category = "Defense(게임)|Monster(몬스터)")
	FName MonsterSpawnPointTag = TEXT("MonsterSpawn");

	/** 한 번에 스폰할 몬스터 수 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Defense(게임)|Monster(몬스터)",
		meta = (DisplayName = "몬스터 스폰 개수", ClampMin = "0"))
	int32 TestMonsterSpawnCount = 3;

	/** 몬스터 스폰 포인트 캐싱 (BeginPlay에서 호출) */
	void CacheMonsterSpawnPoints();

	/** 테스트 몬스터 스폰 */
	void SpawnTestMonsters();

	// ════════════════════════════════════════════════════════════════════════════════
	// 보스 스폰 시스템
	// ════════════════════════════════════════════════════════════════════════════════
public:
	UFUNCTION(BlueprintCallable, Category = "Defense(게임)|Boss(보스)")
	void SetBossReady(bool bReady);

protected:
	/** 보스 클래스 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Defense(게임)|Boss(보스)",
		meta = (DisplayName = "보스 클래스"))
	TSubclassOf<APawn> BossClass;

	/** 보스 스폰 포인트 태그 */
	UPROPERTY(EditDefaultsOnly, Category = "Defense(게임)|Boss(보스)")
	FName BossSpawnPointTag = TEXT("BossSpawn");

	/** 보스 스폰 Z 오프셋 */
	UPROPERTY(EditDefaultsOnly, Category = "Defense(게임)|Boss(보스)")
	float SpawnZOffset = 150.f;

	/** 보스 준비 상태 */
	UPROPERTY(BlueprintReadOnly, Category = "Defense(게임)|Boss(보스)")
	bool bBossReady = false;

	/** 보스 스폰 포인트 */
	UPROPERTY()
	TArray<ATargetPoint*> BossSpawnPoints;

	void CacheBossSpawnPoints();
	void TrySummonBoss();

	// ════════════════════════════════════════════════════════════════════════════════
	// Phase 7: 게임 종료 + 결과 반영 + 로비 복귀
	// ════════════════════════════════════════════════════════════════════════════════
public:
	/**
	 * 게임 종료 메인 함수
	 * 각 플레이어의 인벤토리를 수집하여 Stash에 병합하고 결과 UI를 표시한다.
	 * - 생존자: 현재 인벤토리 전체 보존
	 * - 사망자: 아이템 전부 손실
	 *
	 * @param Reason 게임 종료 사유
	 */
	UFUNCTION(BlueprintCallable, Category = "Defense(게임)|GameEnd(게임종료)",
		meta = (DisplayName = "게임 종료 (EndGame)"))
	void EndGame(EHellunaGameEndReason Reason);

	virtual void Logout(AController* Exiting) override;

protected:
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	/** 게임 종료 완료 플래그 (중복 호출 방지) */
	UPROPERTY(BlueprintReadOnly, Category = "Defense(게임)|GameEnd(게임종료)")
	bool bGameEnded = false;

	/** 로비 서버 URL (BP에서 설정) */
	UPROPERTY(EditDefaultsOnly, Category = "Defense(게임)|GameEnd(게임종료)",
		meta = (DisplayName = "로비 서버 URL"))
	FString LobbyServerURL;

	/** 결과 UI 위젯 클래스 (BP에서 설정) */
	UPROPERTY(EditDefaultsOnly, Category = "Defense(게임)|GameEnd(게임종료)",
		meta = (DisplayName = "결과 위젯 클래스"))
	TSubclassOf<UHellunaGameResultWidget> GameResultWidgetClass;

	/** 결과 UI 표시 → 로비 복귀까지 대기 시간 (초) */
	UPROPERTY(EditDefaultsOnly, Category = "Defense(게임)|GameEnd(게임종료)",
		meta = (DisplayName = "결과 화면 표시 시간(초)", ClampMin = "1.0"))
	float ResultDisplayDuration = 10.f;

public:
	/** 콘솔 커맨드 핸들러 (디버그용): EndGame Escaped / EndGame AllDead */
	static void CmdEndGame(const TArray<FString>& Args, UWorld* World);

private:
	/**
	 * 단일 플레이어의 게임 결과를 DB에 반영한다.
	 * @param PC 대상 PlayerController
	 * @param bSurvived 생존 여부 (false = 사망, 빈 배열로 Merge)
	 */
	void ProcessPlayerGameResult(APlayerController* PC, bool bSurvived);
};
