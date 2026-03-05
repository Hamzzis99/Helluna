// HellunaTypes.h
// 
// ════════════════════════════════════════════════════════════════════════════════
// 📌 Helluna 공통 타입 정의
// ════════════════════════════════════════════════════════════════════════════════
// 
// 📌 작성자: Gihyeon
// ════════════════════════════════════════════════════════════════════════════════

#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "HellunaTypes.generated.h"

/**
 * ============================================
 * 🎭 캐릭터 타입 Enum
 * ============================================
 * 
 * 플레이어가 선택할 수 있는 캐릭터 종류
 * 
 * 📌 사용처:
 *    - GameMode: HeroCharacterMap (캐릭터 클래스 매핑)
 *    - CharacterSelectWidget: 버튼별 캐릭터 타입 설정
 *    - PlayerState: 선택한 캐릭터 저장
 * 
 * 📌 캐릭터 추가 시:
 *    1. 여기에 Enum 값 추가
 *    2. BP_DefenseGameMode의 HeroCharacterMap에 매핑 추가
 *    3. 캐릭터 선택 UI에 버튼 추가
 */
UENUM(BlueprintType)
enum class EHellunaHeroType : uint8
{
	/** 루이 - Index 0 */
	Lui		UMETA(DisplayName = "루이 (Lui)"),

	/** 루나 - Index 1 */
	Luna	UMETA(DisplayName = "루나 (Luna)"),

	/** 리암 - Index 2 */
	Liam	UMETA(DisplayName = "리암 (Liam)"),

	/** 선택 안 됨 */
	None	UMETA(DisplayName = "선택 안 됨", Hidden)
};

/**
 * ============================================
 * 📌 Enum → int32 변환 헬퍼
 * ============================================
 */
FORCEINLINE int32 HeroTypeToIndex(EHellunaHeroType Type)
{
	return (Type == EHellunaHeroType::None) ? -1 : static_cast<int32>(Type);
}

FORCEINLINE EHellunaHeroType IndexToHeroType(int32 Index)
{
	if (Index < 0 || Index > 2) return EHellunaHeroType::None;
	return static_cast<EHellunaHeroType>(Index);
}

// ════════════════════════════════════════════════════════════════════════════════
// 적 등급 Enum
// ════════════════════════════════════════════════════════════════════════════════

/**
 * 적 캐릭터의 등급.
 * AHellunaEnemyCharacter::EnemyGrade 에 설정하며,
 * GameMode의 NotifyMonsterDied / NotifyBossDied 분기에 사용된다.
 *
 *   Normal   → 일반 몬스터 처리 경로
 *   SemiBoss → 세미보스 처리 경로 (보스 사망 경로, TypeLabel = "세미보스")
 *   Boss     → 정보스 처리 경로  (보스 사망 경로, TypeLabel = "보스")
 */
UENUM(BlueprintType)
enum class EEnemyGrade : uint8
{
	Normal		UMETA(DisplayName = "일반 몬스터"),
	SemiBoss	UMETA(DisplayName = "세미 보스"),
	Boss		UMETA(DisplayName = "보스"),
};

/**
 * 날짜별 일반 몬스터 소환 구성.
 * HellunaDefenseGameMode::NightSpawnTable 배열의 원소로 사용된다.
 *
 * FromDay 방식으로 동작:
 *   - CurrentDay >= FromDay 인 항목 중 FromDay가 가장 큰 항목이 적용됨
 *   - 중간 날짜 설정이 없어도 이전 설정이 자동으로 유지됨
 *   - 예) FromDay=1(근거리3/원거리0), FromDay=3(근거리5/원거리2) 설정 시
 *         2일차에는 FromDay=1 설정이 적용됨
 *
 * 에디터 설정 예시:
 *   [0] FromDay=1, MeleeCount=3, RangeCount=0
 *   [1] FromDay=2, MeleeCount=5, RangeCount=1
 *   [2] FromDay=3, MeleeCount=5, RangeCount=4
 */
USTRUCT(BlueprintType)
struct FNightSpawnConfig
{
	GENERATED_BODY()

	/**
	 * 이 설정이 적용되기 시작하는 Day.
	 * CurrentDay >= FromDay 인 항목 중 FromDay가 가장 큰 항목이 사용됨.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Spawn",
		meta = (DisplayName = "적용 시작 일(Day)", ClampMin = "1"))
	int32 FromDay = 1;

	/** 이 날부터 소환할 근거리 몬스터 수 (MeleeMassSpawner 1개당) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Spawn",
		meta = (DisplayName = "근거리 소환 수", ClampMin = "0"))
	int32 MeleeCount = 3;

	/** 이 날부터 소환할 원거리 몬스터 수 (RangeMassSpawner 1개당) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Spawn",
		meta = (DisplayName = "원거리 소환 수", ClampMin = "0"))
	int32 RangeCount = 0;
};

/**
 * 보스/세미보스 소환 스케줄 한 항목.
 * HellunaDefenseGameMode::BossSchedule 배열의 원소로 사용된다.
 *
 * 에디터에서 배열 원소마다:
 *   SpawnDay  → 소환할 Day 번호 (1-based)
 *   BossClass → 그 날 밤에 소환할 Pawn 클래스
 *               (해당 BP의 EnemyGrade를 SemiBoss 또는 Boss로 설정해야 한다)
 */
USTRUCT(BlueprintType)
struct FBossSpawnEntry
{
	GENERATED_BODY()

	/** 소환할 Day 번호 (1 = 첫 번째 낮이 끝난 뒤 첫 번째 밤) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Boss",
		meta = (DisplayName = "소환 일(Day)", ClampMin = "1"))
	int32 SpawnDay = 1;

	/**
	 * 그 날 밤에 소환할 Pawn 클래스.
	 * AHellunaEnemyCharacter를 상속한 BP를 설정하고
	 * 해당 BP에서 EnemyGrade를 SemiBoss 또는 Boss로 설정해야
	 * 사망 처리가 정상 동작한다.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Boss",
		meta = (DisplayName = "소환할 보스 클래스"))
	TSubclassOf<APawn> BossClass;
};

// ════════════════════════════════════════════════════════════════════════════════
// 장착 슬롯 데이터 (player_equipment 테이블)
// ════════════════════════════════════════════════════════════════════════════════
//
// 게임→로비 복귀 시 장착 상태를 별도 테이블로 영속화
// 추후 방어구/헬멧/백팩 등 다양한 슬롯 확장 가능
//
// SlotId 규칙:
//   "weapon_0" = 주무기, "weapon_1" = 보조무기
//   추후: "helmet", "chest", "backpack" 등
// ════════════════════════════════════════════════════════════════════════════════
USTRUCT()
struct FHellunaEquipmentSlotData
{
	GENERATED_BODY()

	/** 슬롯 식별자 (예: "weapon_0", "weapon_1", 추후 "helmet", "chest") */
	UPROPERTY()
	FString SlotId;

	/** 장착된 아이템 타입 */
	UPROPERTY()
	FGameplayTag ItemType;
};
