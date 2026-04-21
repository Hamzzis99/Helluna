// Fill out your copyright notice in the Description page of Project Settings.


#include "HellunaGameplayTags.h"

namespace HellunaGameplayTags
{
	/** Input Tags **/

	UE_DEFINE_GAMEPLAY_TAG(InputTag_Move, "InputTag.Move");
	UE_DEFINE_GAMEPLAY_TAG(InputTag_Look, "InputTag.Look");
	UE_DEFINE_GAMEPLAY_TAG(InputTag_Jump, "InputTag.Jump");
	UE_DEFINE_GAMEPLAY_TAG(InputTag_Aim, "InputTag.Aim");
	UE_DEFINE_GAMEPLAY_TAG(InputTag_Shoot, "InputTag.Shoot");
	UE_DEFINE_GAMEPLAY_TAG(InputTag_Run, "InputTag.Run");
	UE_DEFINE_GAMEPLAY_TAG(InputTag_SpawnWeapon, "InputTag.SpawnWeapon");
	UE_DEFINE_GAMEPLAY_TAG(InputTag_SpawnWeapon2, "InputTag.SpawnWeapon2");
	UE_DEFINE_GAMEPLAY_TAG(InputTag_Repair, "InputTag.Repair");
	UE_DEFINE_GAMEPLAY_TAG(InputTag_Farming, "InputTag.Farming");
	UE_DEFINE_GAMEPLAY_TAG(InputTag_Reload, "InputTag.Reload");
	UE_DEFINE_GAMEPLAY_TAG(InputTag_Kick, "InputTag.Kick");
	UE_DEFINE_GAMEPLAY_TAG(InputTag_Interaction, "InputTag.Interaction");

	/** Player Ability tags **/

	UE_DEFINE_GAMEPLAY_TAG(Player_Ability_Jump, "Player.Ability.Jump");
	UE_DEFINE_GAMEPLAY_TAG(Player_Ability_Aim, "Player.Ability.aim");
	UE_DEFINE_GAMEPLAY_TAG(Player_Ability_Shoot, "Player.Ability.Shoot");
	UE_DEFINE_GAMEPLAY_TAG(Player_Ability_Run, "Player.Ability.Run");
	UE_DEFINE_GAMEPLAY_TAG(Player_Ability_SpawnWeapon, "Player.Ability.SpawnWeapon");
	UE_DEFINE_GAMEPLAY_TAG(Player_Ability_SpawnWeapon2, "Player.Ability.SpawnWeapon2");
	UE_DEFINE_GAMEPLAY_TAG(Player_Ability_InRepair, "Player.Ability.InRepair");
	UE_DEFINE_GAMEPLAY_TAG(Player_Ability_Repair, "Player.Ability.Repair");
	UE_DEFINE_GAMEPLAY_TAG(Player_Ability_Farming, "Player.Ability.Farming");
	UE_DEFINE_GAMEPLAY_TAG(Player_Ability_Reload, "Player.Ability.Reload")

	/** Player Status tags **/

	UE_DEFINE_GAMEPLAY_TAG(Player_status_Aim, "Player.status.Aim");
	UE_DEFINE_GAMEPLAY_TAG(Player_status_Shoot, "Player.status.Shoot");
	UE_DEFINE_GAMEPLAY_TAG(Player_status_Run, "Player.status.Run");
	UE_DEFINE_GAMEPLAY_TAG(Player_status_Can_Repair, "Player.status.Can.Repair");
	UE_DEFINE_GAMEPLAY_TAG(Player_status_Can_Farming, "Player.status.Can.Farming");

	/** Player Weapon tags **/
	UE_DEFINE_GAMEPLAY_TAG(Player_Weapon_Gun, "Player.Weapon.Gun");
	UE_DEFINE_GAMEPLAY_TAG(Player_Weapon_Gun_Sniper, "Player.Weapon.Gun.Sniper");
	UE_DEFINE_GAMEPLAY_TAG(Player_Weapon_Gun_Pistol, "Player.Weapon.Gun.Pistol");
	UE_DEFINE_GAMEPLAY_TAG(Player_Weapon_Farming, "Player.Weapon.Farming");

	/** Enemy tags **/

	UE_DEFINE_GAMEPLAY_TAG(Enemy_Ability_Melee, "Enemy.Ability.Melee");
	UE_DEFINE_GAMEPLAY_TAG(Enemy_Ability_Ranged, "Enemy.Ability.Ranged");
	UE_DEFINE_GAMEPLAY_TAG(Enemy_State_Death, "Enemy.State.Death");
	UE_DEFINE_GAMEPLAY_TAG(Enemy_State_Attacking, "Enemy.State.Attacking");
	// [JumpLagFix] GA들이 "State.Enemy.Attacking" 로 조회 중 — 네이티브 등록으로 첫 호출 시 ensure/log 비용 제거.
	UE_DEFINE_GAMEPLAY_TAG(State_Enemy_Attacking, "State.Enemy.Attacking");
	// [ShipJumpV3] 점프 착지 후 부여 — StateTree 재점프 게이트용.
	UE_DEFINE_GAMEPLAY_TAG(State_Enemy_OnShip, "State.Enemy.OnShip");
	// [ShipJumpFailV1] 점프 실패 후 재시도 차단용.
	UE_DEFINE_GAMEPLAY_TAG(State_Enemy_ShipJumpFailed, "State.Enemy.ShipJumpFailed");

	/** Enemy Event tags **/
	UE_DEFINE_GAMEPLAY_TAG(Enemy_Event_Enrage, "Enemy.Event.Enrage");

	// ═══════════════════════════════════════════════════════════
	// Gun Parry System Tags
	// ═══════════════════════════════════════════════════════════

	UE_DEFINE_GAMEPLAY_TAG(Player_Ability_GunParry, "Player.Ability.GunParry");
	UE_DEFINE_GAMEPLAY_TAG(Player_State_ParryExecution, "Player.State.ParryExecution");
	UE_DEFINE_GAMEPLAY_TAG(Player_State_Invincible, "Player.State.Invincible");
	UE_DEFINE_GAMEPLAY_TAG(Player_State_PostParryInvincible, "Player.State.PostParryInvincible");
	UE_DEFINE_GAMEPLAY_TAG(Weapon_CanParry, "Weapon.CanParry");
	UE_DEFINE_GAMEPLAY_TAG(Enemy_State_AnimLocked, "Enemy.State.AnimLocked");
	UE_DEFINE_GAMEPLAY_TAG(Enemy_State_PendingDeath, "Enemy.State.PendingDeath");
	UE_DEFINE_GAMEPLAY_TAG(Enemy_Ability_Parryable, "Enemy.Ability.Parryable");
	UE_DEFINE_GAMEPLAY_TAG(Enemy_Type_Humanoid, "Enemy.Type.Humanoid");
	UE_DEFINE_GAMEPLAY_TAG(Enemy_State_Staggered, "Enemy.State.Staggered");
	UE_DEFINE_GAMEPLAY_TAG(Event_Parry_Fire, "Event.Parry.Fire");

	// 발차기 시스템
	UE_DEFINE_GAMEPLAY_TAG(Event_Kick_Impact, "Event.Kick.Impact");
	UE_DEFINE_GAMEPLAY_TAG(Hero_State_Kicking, "Hero.State.Kicking");

	// Downed/Revive System
	UE_DEFINE_GAMEPLAY_TAG(Player_State_Downed, "Player.State.Downed");
	UE_DEFINE_GAMEPLAY_TAG(InputTag_Revive, "InputTag.Revive");

}
