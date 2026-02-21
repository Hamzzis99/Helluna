// Capstone Project Helluna


#include "ECS/HellunaMassSpawner.h"

#include "TimerManager.h" // (없어도 되지만 프로젝트에서 종종 포함되어 있음)
#include "Engine/World.h"

// 네가 쓰는 디버그 헬퍼
#include "DebugHelper.h"  // 프로젝트 경로에 맞게

FString AHellunaMassSpawner::NetModeToString(ENetMode M)
{
	switch (M)
	{
	case NM_Standalone:      return TEXT("Standalone");
	case NM_DedicatedServer: return TEXT("DedicatedServer");
	case NM_ListenServer:    return TEXT("ListenServer");
	case NM_Client:          return TEXT("Client");
	default:                 return TEXT("Unknown");
	}
}

void AHellunaMassSpawner::PrintFlow(const FString& Msg, const FColor& Color) const
{
	if (!bDebugSpawnFlow) return;
	Debug::Print(Msg, Color);
}

void AHellunaMassSpawner::BeginPlay()
{
	Super::BeginPlay();

	PrintFlow(FString::Printf(
		TEXT("[MassSpawner] BeginPlay | NetMode=%s | Auth=%d"),
		*NetModeToString(GetNetMode()),
		HasAuthority() ? 1 : 0
	), HasAuthority() ? FColor::Yellow : FColor::Cyan);
}


void AHellunaMassSpawner::OnSpawnDataGenerationFinished(
	TConstArrayView<FMassEntitySpawnDataGeneratorResult> Results,
	FMassSpawnDataGenerator* FinishedGenerator
)
{
	int32 SumEntities = 0;
	for (const FMassEntitySpawnDataGeneratorResult& R : Results)
	{
		SumEntities += R.NumEntities;
	}

	// ✅ 결과 요약만
	PrintFlow(FString::Printf(
		TEXT("[MassSpawner] GenFinished | Results=%d | Sum=%d"),
		Results.Num(),
		SumEntities
	), (SumEntities > 0) ? FColor::Green : FColor::Red);

	Super::OnSpawnDataGenerationFinished(Results, FinishedGenerator);
}

void AHellunaMassSpawner::SpawnGeneratedEntities(
	TConstArrayView<FMassEntitySpawnDataGeneratorResult> Results
)
{
	int32 SumEntities = 0;
	for (const FMassEntitySpawnDataGeneratorResult& R : Results)
	{
		SumEntities += R.NumEntities;
	}

	PrintFlow(FString::Printf(
		TEXT("[MassSpawner] SpawnGeneratedEntities ENTER | Results=%d | Sum=%d"),
		Results.Num(),
		SumEntities
	), (SumEntities > 0) ? FColor::Green : FColor::Red);

	Super::SpawnGeneratedEntities(Results);

	PrintFlow(TEXT("[MassSpawner] SpawnGeneratedEntities DONE"), FColor::Green);
}