// Capstone Project Helluna

#pragma once

#include "CoreMinimal.h"
#include "MassSpawner.h"
#include "HellunaMassSpawner.generated.h"

/**
 * 
 */
UCLASS()
class HELLUNA_API AHellunaMassSpawner : public AMassSpawner
{
	GENERATED_BODY()
public:
	virtual void DoSpawning() override;

protected:
	
	virtual void BeginPlay() override;

	virtual void OnSpawnDataGenerationFinished(
		TConstArrayView<FMassEntitySpawnDataGeneratorResult> Results,
		FMassSpawnDataGenerator* FinishedGenerator
	) override;

	virtual void SpawnGeneratedEntities(
		TConstArrayView<FMassEntitySpawnDataGeneratorResult> Results
	) override;

private:
	UPROPERTY(EditAnywhere, Category = "Mass|Debug")
	bool bDebugSpawnFlow = false;

	void PrintFlow(const FString& Msg, const FColor& Color) const;
	static FString NetModeToString(ENetMode M);
};
