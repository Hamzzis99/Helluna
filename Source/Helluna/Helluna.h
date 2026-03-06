#pragma once

#include "CoreMinimal.h"

// ════════════════════════════════════════════════════════════════════════════════
// 🔧 Helluna 디버깅 로그 전처리기 플래그
// 출시 전에 모두 0으로 변경하세요!
// ════════════════════════════════════════════════════════════════════════════════

// 🌐 서버 접속 (LoginGameMode, ServerConnectController - 서버 시작/접속)
#define HELLUNA_DEBUG_SERVERCONNECTION 0

// 🔐 로그인 컨트롤러 (HellunaLoginController - BeginPlay, 위젯 생성, RPC 등)
#define HELLUNA_DEBUG_LOGINCONTROLLER 0

// 🔐 로그인 처리 (BaseGameMode - ProcessLogin, OnLoginSuccess 등)
#define HELLUNA_DEBUG_LOGIN 0

// 🎮 게임모드 (BaseGameMode - PostLogin, SpawnHeroCharacter 등)
#define HELLUNA_DEBUG_GAMEMODE 0

// 🎭 캐릭터 선택 (ProcessCharacterSelection, RegisterCharacterUse 등)
#define HELLUNA_DEBUG_CHARACTER_SELECT 0

// 🎭 캐릭터 프리뷰 (PreviewActor 스폰/파괴, SceneCapture, Hover 등)
#define HELLUNA_DEBUG_CHARACTER_PREVIEW 0

// 🎭 캐릭터 프리뷰 V2 (SceneV2 - 3캐릭터 1카메라, Overlay Highlight 등)
#define HELLUNA_DEBUG_CHARACTER_PREVIEW_V2 0

// ☀️ UDS 날씨/시간 디버그 (PrintUDSDebug - 1초마다 TimeOfDay, Animate, Phase 출력)
#define HELLUNA_DEBUG_UDS 0

// 🎮 어빌리티 시스템 (ASC - OnAbilityInputPressed/Released, TryActivateAbility 등)
#define HELLUNA_DEBUG_ASC 0

// 📦 인벤토리 저장/로드 (SaveAllPlayersInventory, LoadAndSendInventoryToClient 등)
#define HELLUNA_DEBUG_INVENTORY_SAVE 0

// 🛡️ 디펜스 GameState (HellunaDefenseGameState - Phase 전환, DayNight, 새벽Lerp, 몬스터 수, 날씨)
#define HELLUNA_DEBUG_DEFENSE 0

// 🦸 히어로 캐릭터 (HellunaHeroCharacter - EndPlay 저장, 입력 바인딩, 무기 스폰/파괴, 우주선 수리)
#define HELLUNA_DEBUG_HERO 0

// 🔫 무기 브릿지 (WeaponBridgeComponent - 장비 요청, 어태치먼트 비주얼, 무기 장착/해제)
#define HELLUNA_DEBUG_WEAPON_BRIDGE 0

// 🔧 Debug::Print 유틸리티 (DebugHelper.h - 화면 메시지 + LogTemp 출력)
#define HELLUNA_DEBUG_PRINT 0

// 🔧 수리 시스템 (RepairComponent, RepairMaterialWidget, HeroGameplayAbility_Repair/InRepair)
#define HELLUNA_DEBUG_REPAIR 0

// 👾 적 AI/전투 (AnimNotify, STTask_Enrage, EnemyMassTrait, EnemyGameplayAbility_RangedAttack)
#define HELLUNA_DEBUG_ENEMY 0

// 👤 플레이어 상태 (HellunaPlayerState - 로그인/로그아웃/캐릭터 선택)
#define HELLUNA_DEBUG_PLAYERSTATE 0

// 🏆 게임 결과 (HellunaGameResultWidget - 로비 복귀)
#define HELLUNA_DEBUG_GAMERESULT 0

// ════════════════════════════════════════════════════════════════════════════════

DECLARE_LOG_CATEGORY_EXTERN(LogHelluna, Log, All);
