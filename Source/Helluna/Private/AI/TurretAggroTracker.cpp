/**
 * TurretAggroTracker.cpp
 *
 * @author 김민우
 */

#include "AI/TurretAggroTracker.h"

TMap<TWeakObjectPtr<const AActor>, FTurretAggroTracker::FTurretData>& FTurretAggroTracker::GetData()
{
	static TMap<TWeakObjectPtr<const AActor>, FTurretData> Data;
	return Data;
}

bool FTurretAggroTracker::CanAggro(const AActor* Turret, int32 MaxPerTurret)
{
	if (!Turret || MaxPerTurret <= 0) return false;

	auto& Data = GetData();
	FTurretData* TD = Data.Find(Turret);
	if (!TD) return true;

	// 유효하지 않은 몬스터 정리
	TD->Monsters.RemoveAll([](const FMonsterEntry& E) { return !E.Monster.IsValid(); });

	return TD->Monsters.Num() < MaxPerTurret;
}

float FTurretAggroTracker::Register(const AActor* Turret, const AActor* Monster)
{
	if (!Turret || !Monster) return 0.f;

	auto& Data = GetData();
	FTurretData& TD = Data.FindOrAdd(Turret);

	// 이미 등록된 몬스터인지 확인
	for (const FMonsterEntry& E : TD.Monsters)
	{
		if (E.Monster == Monster)
			return E.AssignedAngleDeg;
	}

	// 산개 각도 할당: 60도 간격으로 분배
	const float Angle = FMath::Fmod(TD.AngleCounter * 60.f, 360.f);
	TD.AngleCounter++;

	FMonsterEntry Entry;
	Entry.Monster = Monster;
	Entry.AssignedAngleDeg = Angle;
	TD.Monsters.Add(Entry);

	return Angle;
}

void FTurretAggroTracker::Unregister(const AActor* Turret, const AActor* Monster)
{
	if (!Turret || !Monster) return;

	auto& Data = GetData();
	FTurretData* TD = Data.Find(Turret);
	if (!TD) return;

	TD->Monsters.RemoveAll([Monster](const FMonsterEntry& E)
	{
		return E.Monster == Monster || !E.Monster.IsValid();
	});

	if (TD->Monsters.Num() == 0)
	{
		Data.Remove(Turret);
	}
}

void FTurretAggroTracker::UnregisterMonster(const AActor* Monster)
{
	if (!Monster) return;

	auto& Data = GetData();
	TArray<TWeakObjectPtr<const AActor>> EmptyTurrets;

	for (auto& Pair : Data)
	{
		Pair.Value.Monsters.RemoveAll([Monster](const FMonsterEntry& E)
		{
			return E.Monster == Monster || !E.Monster.IsValid();
		});

		if (Pair.Value.Monsters.Num() == 0)
		{
			EmptyTurrets.Add(Pair.Key);
		}
	}

	for (const auto& Key : EmptyTurrets)
	{
		Data.Remove(Key);
	}
}

float FTurretAggroTracker::GetAssignedAngle(const AActor* Turret, const AActor* Monster)
{
	if (!Turret || !Monster) return -1.f;

	auto& Data = GetData();
	FTurretData* TD = Data.Find(Turret);
	if (!TD) return -1.f;

	for (const FMonsterEntry& E : TD->Monsters)
	{
		if (E.Monster == Monster)
			return E.AssignedAngleDeg;
	}
	return -1.f;
}

int32 FTurretAggroTracker::GetAggroCount(const AActor* Turret)
{
	if (!Turret) return 0;

	auto& Data = GetData();
	FTurretData* TD = Data.Find(Turret);
	if (!TD) return 0;

	// 무효 정리
	TD->Monsters.RemoveAll([](const FMonsterEntry& E) { return !E.Monster.IsValid(); });
	return TD->Monsters.Num();
}

void FTurretAggroTracker::Cleanup()
{
	auto& Data = GetData();
	TArray<TWeakObjectPtr<const AActor>> ToRemove;

	for (auto& Pair : Data)
	{
		if (!Pair.Key.IsValid())
		{
			ToRemove.Add(Pair.Key);
			continue;
		}

		Pair.Value.Monsters.RemoveAll([](const FMonsterEntry& E) { return !E.Monster.IsValid(); });
		if (Pair.Value.Monsters.Num() == 0)
		{
			ToRemove.Add(Pair.Key);
		}
	}

	for (const auto& Key : ToRemove)
	{
		Data.Remove(Key);
	}
}
