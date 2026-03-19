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

	/** Player Status tags **/

	HELLUNA_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Player_status_Aim);
	HELLUNA_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Player_status_Run);
	HELLUNA_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Player_status_Shoot);
	HELLUNA_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Player_status_Can_Repair);
	HELLUNA_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Player_status_Can_Farming);

	/** Player Weapon tags **/
	HELLUNA_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Player_Weapon_Gun);
	HELLUNA_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Player_Weapon_Gun_Sniper);
	HELLUNA_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Player_Weapon_Farming);

	/** Enemy tags **/
	HELLUNA_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Enemy_Ability_Melee);
	HELLUNA_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Enemy_Ability_Ranged);
	HELLUNA_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Enemy_State_Death);
	HELLUNA_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Enemy_State_Attacking);

	/** Enemy Event tags **/
	HELLUNA_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Enemy_Event_Enrage);

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

}
