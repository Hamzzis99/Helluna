/**
 * HellunaAIDebugSettings.cpp
 *
 * Project Settings 토글 → 콘솔 변수 동기화.
 *
 * @author 김민우
 */

#include "AI/HellunaAIDebugSettings.h"
#include "HAL/IConsoleManager.h"

UHellunaAIDebugSettings::UHellunaAIDebugSettings()
{
	CategoryName = FName("Helluna");
	SectionName = FName("AI Debug");
}

void UHellunaAIDebugSettings::PostInitProperties()
{
	Super::PostInitProperties();
	SyncCVars();
}

#if WITH_EDITOR
void UHellunaAIDebugSettings::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);
	SyncCVars();
}
#endif

void UHellunaAIDebugSettings::SyncCVars() const
{
	IConsoleManager& CM = IConsoleManager::Get();

	// ai.debug.attackzone
	if (IConsoleVariable* CVar = CM.FindConsoleVariable(TEXT("ai.debug.attackzone")))
	{
		CVar->Set(bDrawAttackZone ? 1 : 0, ECVF_SetByProjectSetting);
	}
	else
	{
		// Condition 쪽이 아직 CVar를 등록하지 않았으면 여기서 선등록.
		CM.RegisterConsoleVariable(
			TEXT("ai.debug.attackzone"),
			bDrawAttackZone ? 1 : 0,
			TEXT("1 = Draw AttackZone debug boxes (set via Project Settings > Helluna > AI Debug)"),
			ECVF_Default);
	}
}
