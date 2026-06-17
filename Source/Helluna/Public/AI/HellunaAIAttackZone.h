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

	/**
	 * [SurfaceDistanceV1] FromLoc 에서 Target 표면까지의 최단 거리(cm).
	 *
	 * 우주선처럼 큰 액터는 중심점 거리가 아니라 "표면까지" 거리로 판정해야 한다.
	 * 기존 GetClosestPointOnCollision 단독 방식은 우주선 메시가 복합(trimesh) 콜리전이라
	 * -1(미지원)을 반환 → 전부 원점 거리로 폴백되는 버그가 있었다(2026-06-11 확인).
	 *
	 * 처리 순서(컴포넌트별 최솟값):
	 *  1. "ShipCombatCollision" 태그 컴포넌트 → 없으면 Block 반응 컴포넌트 → 없으면 루트 프리미티브
	 *  2. 각 후보에 GetClosestPointOnCollision 시도 (박스/구/캡슐 등 단순 셰이프면 정확)
	 *  3. -1(복합 콜리전 미지원) 이면 컴포넌트 OBB(로컬 바운드+월드 변환) 표면 최단 거리로 폴백
	 *     → 비원형(길쭉한) 우주선의 실루엣을 반영해 "과접근" 방지. 레이캐스트 없이 순수 수학(저비용).
	 *  4. 모두 실패하면 액터 중심점 거리.
	 *
	 * @param FromLoc  기준 위치(보통 폰 위치)
	 * @param Target   대상 액터
	 * @return 표면까지 최단 거리(cm). Target 이 null 이면 매우 큰 값.
	 */
	HELLUNA_API float GetTargetSurfaceDistance(const FVector& FromLoc, const AActor* Target);
}
