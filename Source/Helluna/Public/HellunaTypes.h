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
