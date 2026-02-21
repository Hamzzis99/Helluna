// Gihyeon's Inventory Project

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Inv_BuildingActor.generated.h"

/**
 * 리플리케이션이 가능한 건물 베이스 액터
 */
UCLASS(Blueprintable)
class INVENTORY_API AInv_BuildingActor : public AActor
{
	GENERATED_BODY()
	
public:	
	// Sets default values for this actor's properties
	AInv_BuildingActor();

protected:
	// Called when the game starts or when spawned
	virtual void BeginPlay() override;

	// 건물 메시 컴포넌트
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "건설", meta = (DisplayName = "건물 메시"))
	TObjectPtr<UStaticMeshComponent> BuildingMesh;

public:
	// Called every frame
	virtual void Tick(float DeltaTime) override;

	// 건물이 배치되었을 때 호출 (블루프린트에서 오버라이드 가능)
	UFUNCTION(BlueprintNativeEvent, Category = "건설", meta = (DisplayName = "건물 배치됨"))
	void OnBuildingPlaced();
	virtual void OnBuildingPlaced_Implementation();
};

