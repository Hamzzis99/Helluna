// Capstone Project Helluna

#include "BossEvent/BossPatternZoneBase.h"

ABossPatternZoneBase::ABossPatternZoneBase()
{
	PrimaryActorTick.bCanEverTick = false;
	bReplicates = true;
}

void ABossPatternZoneBase::SetOwnerEnemy(AHellunaEnemyCharacter* InEnemy)
{
	OwnerEnemy = InEnemy;
}

void ABossPatternZoneBase::SetPatternDuration(float InDuration)
{
	PatternDuration = InDuration;
}

void ABossPatternZoneBase::ActivateZone()
{
	// 파생 클래스에서 오버라이드
}

void ABossPatternZoneBase::DeactivateZone()
{
	// 파생 클래스에서 오버라이드
}

void ABossPatternZoneBase::NotifyPatternFinished(bool bWasBroken)
{
	OnPatternFinished.Broadcast(bWasBroken);
}
