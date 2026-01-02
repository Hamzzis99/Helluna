// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "TestLoseActor.generated.h"

class UBoxComponent;
class UStaticMeshComponent;

UENUM(BlueprintType)
enum class EDefenseTriggerAction : uint8
{
	Lose         UMETA(DisplayName = "Lose"),
	SummonBoss   UMETA(DisplayName = "Summon Boss")
};

UCLASS()
class ATestLoseActor : public AActor
{
	GENERATED_BODY()

public:
	ATestLoseActor();

protected:

	UPROPERTY(VisibleAnywhere)
	UBoxComponent* Box;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	UStaticMeshComponent* VisualMesh;

	UFUNCTION()
	void OnBoxBeginOverlap(UPrimitiveComponent* OverlappedComponent,
		AActor* OtherActor, UPrimitiveComponent* OtherComp,
		int32 OtherBodyIndex, bool bFromSweep, const FHitResult& SweepResult);

	// ===== Behavior =====
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Defense")
	EDefenseTriggerAction Action = EDefenseTriggerAction::Lose;

	void ExecuteAction();

};