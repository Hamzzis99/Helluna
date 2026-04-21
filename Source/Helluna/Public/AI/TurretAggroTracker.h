/**
 * TurretAggroTracker.h
 *
 * 포탑별 어그로 몬스터 수를 추적하는 정적 유틸리티.
 *
 * ─── 사용 흐름 ───────────────────────────────────────────────
 *  1. Evaluator가 터렛을 타겟으로 선택하기 전에 CanAggro() 확인
 *  2. 타겟 지정 시 Register() 호출
 *  3. 타겟 해제(어그로 이탈/사망/광폭화) 시 Unregister() 호출
 *  4. 몬스터에게 할당된 포탑 주변 각도 조회 GetAssignedAngle()
 *
 * 정적 데이터 → 월드 전체 공유. 싱글플레이어/서버 전용.
 *
 * @author 김민우
 */

#pragma once

#include "CoreMinimal.h"

class HELLUNA_API FTurretAggroTracker
{
public:
	/**
	 * 해당 터렛에 추가 어그로가 가능한지.
	 * @param Turret     터렛 Actor
	 * @param MaxPerTurret  터렛당 최대 어그로 수
	 */
	static bool CanAggro(const AActor* Turret, int32 MaxPerTurret);

	/**
	 * 몬스터를 터렛 어그로 목록에 등록하고 산개 각도를 할당.
	 * @return 할당된 산개 각도 (도)
	 */
	static float Register(const AActor* Turret, const AActor* Monster);

	/** 몬스터를 터렛 어그로 목록에서 해제 */
	static void Unregister(const AActor* Turret, const AActor* Monster);

	/** 몬스터가 현재 어그로 중인 터렛에서 해제 (터렛 모르는 경우) */
	static void UnregisterMonster(const AActor* Monster);

	/** 몬스터에게 할당된 산개 각도 조회 (-1이면 미등록) */
	static float GetAssignedAngle(const AActor* Turret, const AActor* Monster);

	/** 해당 터렛의 현재 어그로 몬스터 수 */
	static int32 GetAggroCount(const AActor* Turret);

	/** 유효하지 않은 항목 정리 (월드 전환 시 등) */
	static void Cleanup();

private:
	struct FMonsterEntry
	{
		TWeakObjectPtr<const AActor> Monster;
		float AssignedAngleDeg = 0.f;
	};

	struct FTurretData
	{
		TArray<FMonsterEntry> Monsters;
		int32 AngleCounter = 0;
	};

	static TMap<TWeakObjectPtr<const AActor>, FTurretData>& GetData();
};
