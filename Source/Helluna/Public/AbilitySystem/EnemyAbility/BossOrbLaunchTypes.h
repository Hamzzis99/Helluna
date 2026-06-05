// Capstone Project Helluna

#pragma once

#include "CoreMinimal.h"
#include "BossOrbLaunchTypes.generated.h"

/**
 * 보스 패턴 구체(AStasisSalvoOrb) 발사 방식.
 *   - ForwardToTarget : 보스 전방에서 "범위 내 플레이어 무리 중심" 방향으로 발사 (총알을 쏘는 느낌).
 *   - DropFromSky     : "보스와 플레이어 무리 중심의 중간" 상공에서 직하 (하늘에서 떨어지는 메테오 느낌).
 * 두 보스 패턴(시간 슬로우 / 현실 균열)이 공유한다.
 */
UENUM(BlueprintType)
enum class EBossOrbLaunchMode : uint8
{
	ForwardToTarget UMETA(DisplayName = "전방 조준 발사 (플레이어 무리 방향)"),
	DropFromSky     UMETA(DisplayName = "하늘 낙하 (보스-플레이어 중간 상공)")
};
