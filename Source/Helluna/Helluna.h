#pragma once

#include "CoreMinimal.h"

// ════════════════════════════════════════════════════════════════════════════════
// 🔧 Helluna 디버깅 로그 전처리기 플래그
// 출시 전에 모두 0으로 변경하세요!
// ════════════════════════════════════════════════════════════════════════════════

// 🔐 로그인 시스템 (LoginController, ProcessLogin, OnLoginSuccess 등)
#define HELLUNA_DEBUG_LOGIN 0

// 🎮 게임모드 (BaseGameMode, PostLogin, SpawnHeroCharacter 등)
#define HELLUNA_DEBUG_GAMEMODE 0

// 🎭 캐릭터 선택 (ProcessCharacterSelection, RegisterCharacterUse 등)
#define HELLUNA_DEBUG_CHARACTER_SELECT 0

// 📦 인벤토리 저장/로드 (SaveAllPlayersInventory, LoadAndSendInventoryToClient 등)
#define HELLUNA_DEBUG_INVENTORY_SAVE 0

// ════════════════════════════════════════════════════════════════════════════════

DECLARE_LOG_CATEGORY_EXTERN(LogHelluna, Log, All);
