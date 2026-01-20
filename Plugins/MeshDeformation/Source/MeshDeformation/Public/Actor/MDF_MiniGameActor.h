// File: Source/InventoryProject/Actor/MDF_MiniGameActor.h
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "MDF_MiniGameActor.generated.h"

class UDynamicMeshComponent;
class UMDF_MiniGameComponent;

UCLASS()
class MESHDEFORMATION_API AMDF_MiniGameActor : public AActor
{
	GENERATED_BODY()
    
public:    
	AMDF_MiniGameActor();

protected:
	virtual void BeginPlay() override;
	virtual void OnConstruction(const FTransform& Transform) override;

public:
	// [추가됨] 좌표 기준점 (이동 시 메쉬 증발 방지)
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "MDF")
	TObjectPtr<USceneComponent> DefaultSceneRoot;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "MDF")
	TObjectPtr<UDynamicMeshComponent> DynamicMeshComponent;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "MDF")
	TObjectPtr<UMDF_MiniGameComponent> MiniGameComponent;
};