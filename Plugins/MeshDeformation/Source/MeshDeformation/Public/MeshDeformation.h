// Gihyeon's Deformation Project (Helluna)

#pragma once

#include "CoreMinimal.h"
#include "Modules/ModuleManager.h"

// ============================================
// 디버깅 로그 전처리기 플래그
// Shipping 빌드에서는 자동으로 모두 0 처리됩니다.
// 개발 중 필요한 항목만 1로 설정하세요.
// ============================================

#if UE_BUILD_SHIPPING

// Shipping 빌드: 모든 디버그 로그 강제 비활성화
#define MDF_DEBUG_DEFORM    0
#define MDF_DEBUG_MINIGAME  0
#define MDF_DEBUG_WEAPON    0

#else // Development / Debug 빌드

// 변형 시스템 (DeformableComponent - 변형, 배칭, GUID, 히스토리, 수리)
#define MDF_DEBUG_DEFORM 1

// 미니게임 시스템 (MiniGameComponent - 마킹, 약점, 절단)
#define MDF_DEBUG_MINIGAME 0

// 무기 시스템 (BaseWeapon, RifleWeapon, LaserWeapon, WeaponComponent)
#define MDF_DEBUG_WEAPON 0

#endif // UE_BUILD_SHIPPING

// ============================================

DECLARE_LOG_CATEGORY_EXTERN(LogMeshDeform, Log, All);

class FMeshDeformationModule : public IModuleInterface
{
public:

	/** 모듈이 메모리에 로드될 때 호출됩니다. */
	virtual void StartupModule() override;

	/** 모듈이 언로드될 때 호출됩니다. */
	virtual void ShutdownModule() override;
};
