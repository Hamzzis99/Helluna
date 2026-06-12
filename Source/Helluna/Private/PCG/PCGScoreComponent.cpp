#include "PCG/PCGScoreComponent.h"

TSubclassOf<AActor> UPCGScoreComponent::PickWeightedOreClass() const
{
	// 유효한 항목들의 총 가중치 합산.
	float TotalWeight = 0.f;
	for (const FOreSpawnEntry& Entry : OreTypes)
	{
		if (Entry.OreClass && Entry.Weight > 0.f)
		{
			TotalWeight += Entry.Weight;
		}
	}

	// 유효 항목이 없으면 단일 오버라이드 폴백.
	if (TotalWeight <= 0.f)
	{
		return SpawnActorClassOverride;
	}

	// [0, TotalWeight) 구간에서 추첨 후 누적합으로 항목 선택.
	const float Roll = FMath::FRand() * TotalWeight;
	float Accum = 0.f;
	for (const FOreSpawnEntry& Entry : OreTypes)
	{
		if (!Entry.OreClass || Entry.Weight <= 0.f)
		{
			continue;
		}
		Accum += Entry.Weight;
		if (Roll < Accum)
		{
			return Entry.OreClass;
		}
	}

	// 부동소수 경계 보정: 마지막 유효 항목 반환.
	for (int32 i = OreTypes.Num() - 1; i >= 0; --i)
	{
		if (OreTypes[i].OreClass && OreTypes[i].Weight > 0.f)
		{
			return OreTypes[i].OreClass;
		}
	}

	return SpawnActorClassOverride;
}
