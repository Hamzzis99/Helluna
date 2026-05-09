// Capstone Project Helluna

#pragma once

#include "CoreMinimal.h"

class AActor;
class UObject;

/**
 * [BossCinematicCameraUtilsV1] 보스 시네마틱 카메라 보정 공통 함수.
 *
 *   A: PushCameraOutOfActorOccluders — 카메라↔보스 시야선의 액터 occluder 회피.
 *   G: ClampCameraAboveGround — 카메라가 지형 아래로 내려가지 않도록 Z 하한 보정.
 *
 *   세 시네마틱 트리거 (Summon / Phase2 / Death) 가 공통 호출.
 *   비용 최소화: 매 frame LineTrace 1~2회. 각 클라 로컬 (멀티플레이 영향 X).
 *
 *   설계 원칙:
 *     - WorldContext 만 받고 actor 의존성 없음 → 어떤 트리거에서도 호출 가능.
 *     - IgnoreActors 에 보스 + 카메라 자체 액터를 넣어 자기 mesh 오인 방지.
 *     - 두 함수 단독 효과만 작동 — 서로 호출 안 함 (조합은 호출자가 결정).
 */
namespace BossCinematicCameraUtils
{
	/**
	 * A — Actor occluder push-back.
	 *   Target → Camera 방향으로 LineTrace. 액터 occluder hit 시 카메라를 hit point 보다
	 *   Margin 만큼 보스 쪽으로 당김 (= 카메라가 occluder 앞에 위치하게).
	 *
	 *   채널: ECC_Camera (Camera trace channel — 일반적으로 캐릭터/우주선 등 액터 mesh blocking).
	 *         Landscape 는 일반적으로 Camera 채널 무시 → 지형은 G 가 처리.
	 *
	 *   @param WorldContext  GetWorld 용 (Actor* 또는 UObject*).
	 *   @param DesiredCamLoc 보정 전 의도된 카메라 위치 (월드).
	 *   @param TargetLoc     카메라 시선 끝 (보통 보스 head 본 위치).
	 *   @param IgnoreActors  trace 무시 (보스 + 카메라 액터 + 트리거 자신 권장).
	 *   @param Margin        hit point 에서 target 쪽으로 당기는 거리 (cm).
	 *   @return 보정된 카메라 위치 (occluder 없으면 DesiredCamLoc 그대로).
	 */
	HELLUNA_API FVector PushCameraOutOfActorOccluders(
		const UObject* WorldContext,
		const FVector& DesiredCamLoc,
		const FVector& TargetLoc,
		const TArray<AActor*>& IgnoreActors,
		float Margin = 30.f);

	/**
	 * G — Ground Z floor.
	 *   BossHeadLoc 에서 아래로 LineTrace 해서 지형 Z 를 측정한 후,
	 *   카메라 Z 가 ground + MarginZ 보다 낮으면 위로 끌어올림.
	 *
	 *   채널: ECC_Visibility (Landscape 가 응답하는 일반 채널).
	 *
	 *   @param WorldContext  GetWorld 용.
	 *   @param DesiredCamLoc 보정 전 카메라 위치.
	 *   @param BossHeadLoc   down trace 시작점 (보스 머리 또는 ActorLocation).
	 *   @param IgnoreActors  trace 무시 (보스 + 카메라 권장).
	 *   @param MarginZ       ground 위로 띄울 최소 거리 (cm).
	 *   @param MaxDownTrace  trace 최대 거리 (cm). 너무 깊으면 지형 못 만남.
	 *   @return 보정된 카메라 위치 (지형이 충분히 멀면 DesiredCamLoc 그대로).
	 */
	HELLUNA_API FVector ClampCameraAboveGround(
		const UObject* WorldContext,
		const FVector& DesiredCamLoc,
		const FVector& BossHeadLoc,
		const TArray<AActor*>& IgnoreActors,
		float MarginZ = 100.f,
		float MaxDownTrace = 5000.f);
}
