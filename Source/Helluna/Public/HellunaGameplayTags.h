// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "NativeGameplayTags.h"

namespace HellunaGameplayTags
{
	/** Input Tags **/
	HELLUNA_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(InputTag_Move);
	HELLUNA_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(InputTag_Look);
	HELLUNA_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(InputTag_Jump);
	HELLUNA_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(InputTag_Aim);
	HELLUNA_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(InputTag_Shoot);
	HELLUNA_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(InputTag_Run);
	HELLUNA_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(InputTag_SpawnWeapon);
	HELLUNA_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(InputTag_SpawnWeapon2);
	HELLUNA_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(InputTag_Repair);
	HELLUNA_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(InputTag_Farming);
	HELLUNA_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(InputTag_Reload);
	HELLUNA_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(InputTag_Kick);
	HELLUNA_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(InputTag_Interaction);
	HELLUNA_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(InputTag_Block);

	/** Player Ability tags **/

	HELLUNA_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Player_Ability_Jump);
	HELLUNA_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Player_Ability_Aim);
	HELLUNA_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Player_Ability_Shoot);
	HELLUNA_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Player_Ability_Run);
	HELLUNA_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Player_Ability_SpawnWeapon);
	HELLUNA_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Player_Ability_SpawnWeapon2);
	HELLUNA_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Player_Ability_InRepair);
	HELLUNA_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Player_Ability_Repair);
	HELLUNA_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Player_Ability_Farming);
	HELLUNA_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Player_Ability_Reload);
	HELLUNA_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Player_Ability_Block);

	/** Player Status tags **/

	HELLUNA_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Player_status_Aim);
	HELLUNA_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Player_status_Run);
	HELLUNA_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Player_status_Shoot);
	HELLUNA_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Player_status_Can_Repair);
	HELLUNA_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Player_status_Can_Farming);
	HELLUNA_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Player_Status_Blocking);
	HELLUNA_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Player_Status_PerfectBlocking);

	/** GameplayCue tags **/
	HELLUNA_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(GameplayCue_FX_MagicShield);
	HELLUNA_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(GameplayCue_FX_MagicShield_SuceessfulBlock);
	HELLUNA_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(GameplayCue_FX_MagicShield_SuceessfulBlock_PerfectBlock);

	/** Player Weapon tags **/
	HELLUNA_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Player_Weapon_Gun);
	HELLUNA_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Player_Weapon_Gun_Sniper);
	HELLUNA_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Player_Weapon_Gun_Pistol);
	HELLUNA_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Player_Weapon_Farming);

	/** Enemy tags **/
	HELLUNA_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Enemy_Ability_Melee);
	HELLUNA_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Enemy_Ability_Ranged);
	HELLUNA_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Enemy_State_Death);
	HELLUNA_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Enemy_State_Attacking);
	// GA 전반에서 FName("State.Enemy.Attacking") 으로 조회 중. 미등록이면 RequestGameplayTag 가 ensure+Log 비용을 발생시켜 첫 공격 렉의 원인이 됨 → 네이티브 등록으로 해소.
	HELLUNA_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(State_Enemy_Attacking);
	// [ShipJumpV3] 점프 착지 후 부여 — StateTree 재점프 게이트용.
	HELLUNA_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(State_Enemy_OnShip);
	// [ShipJumpFailV1] 우주선 점프 실패(상단 착지 X) 후 부여 — 재시도 영구 차단용.
	HELLUNA_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(State_Enemy_ShipJumpFailed);

	/** Enemy Event tags **/
	HELLUNA_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Enemy_Event_Enrage);

	/** Boss tags **/
	// [BossCinematicGateV1] 보스 소환 시네마틱이 끝나야 부여되는 게이트 태그.
	//   이 태그가 보스 ASC 에 없으면 STEvaluator_BossTarget 가 타겟/패턴을 비워 보스를 idle 로 유지.
	//   StateTree 가 spawn 직후부터 돌더라도 시네마틱 도중에는 아무 행동도 하지 않게 만드는 확정 게이트.
	HELLUNA_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(State_Boss_CinematicReady);

	// ═══════════════════════════════════════════════════════════
	// Gun Parry System Tags
	// ═══════════════════════════════════════════════════════════

	/** 건패링 - 플레이어 어빌리티 **/
	HELLUNA_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Player_Ability_GunParry);

	/** 건패링 - 플레이어 상태 **/
	HELLUNA_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Player_State_ParryExecution);
	HELLUNA_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Player_State_Invincible);
	HELLUNA_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Player_State_PostParryInvincible);

	/** 건패링 - 무기 속성 **/
	HELLUNA_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Weapon_CanParry);

	/** 건패링 - 적 상태 **/
	HELLUNA_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Enemy_State_AnimLocked);
	HELLUNA_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Enemy_State_PendingDeath);
	HELLUNA_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Enemy_Ability_Parryable);

	/** 건패링 - Phase 2 확장용 **/
	HELLUNA_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Enemy_Type_Humanoid);
	HELLUNA_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Enemy_State_Staggered);

	/** 건패링 - GameplayEvent **/
	HELLUNA_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Event_Parry_Fire);

	/** 발차기 시스템 **/
	HELLUNA_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Event_Kick_Impact);
	HELLUNA_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Hero_State_Kicking);

	// ═══════════════════════════════════════════════════════════
	// Downed/Revive System Tags
	// ═══════════════════════════════════════════════════════════

	/** 다운 상태 (Downed State) — 어빌리티 차단용 **/
	HELLUNA_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Player_State_Downed);

	/** [MenuInputLockV1] UI 메뉴(우주선 수리·회복 등) 열림 — 발사/조준 등 전투 Hero GA 차단용 **/
	HELLUNA_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Player_State_MenuOpen);

	/** 부활 입력 (Revive Input) — F키 홀드 **/
	HELLUNA_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(InputTag_Revive);

}
