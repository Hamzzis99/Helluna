// Capstone Project Helluna
//
// HellunaAIAttackZone.h
// 폰 전방 박스(AttackZone)가 타겟 콜리전과 실제로 겹치는지 물리 오버랩으로 판정.
// Chase / TargetSelector / InAttackZone Condition 세 곳이 공유하는 헬퍼.
//
// 우주선은 완벽한 원형이 아니어서 거리 기반 판정이 부정확하기 때문에,
// 콜리전 메시에 실제 닿는지를 AllObjects 오버랩으로 확인한다.

#pragma once

#include "CoreMinimal.h"

class AActor;
class APawn;

namespace HellunaAI
{
	/**
	 * 폰 전방 공격존이 Target의 Blocking 콜리전에 닿는지 검사한다.
	 *
	 * AttackZone 중심 = PawnLoc + Forward * ForwardOffset
	 * AttackZone 회전 = Pawn->GetActorQuat()
	 * AttackZone 반크기 = HalfExtent
	 *
	 * 유효 컴포넌트 조건: Pawn + WorldStatic + WorldDynamic 세 채널 모두 Block.
	 * (Overlap-only / 시각 전용 / 트리거 볼륨은 무시)
	 *
	 * @param Pawn           검사할 폰 (null 시 false)
	 * @param Target         타겟 Actor (null 시 false)
	 * @param HalfExtent     AttackZone 박스 반크기 (cm, 로컬)
	 * @param ForwardOffset  폰 위치에서 전방 오프셋 (cm)
	 * @return  오버랩이면 true
	 */
	HELLUNA_API bool IsTargetInAttackZone(
		const APawn* Pawn,
		const AActor* Target,
		const FVector& HalfExtent,
		float ForwardOffset);
}
