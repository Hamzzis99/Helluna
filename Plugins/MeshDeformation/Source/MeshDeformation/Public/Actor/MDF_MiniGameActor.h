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
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "MDF", meta = (DisplayName = "씬 루트"))
	TObjectPtr<USceneComponent> DefaultSceneRoot;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "MDF", meta = (DisplayName = "다이나믹메시컴포넌트"))
	TObjectPtr<UDynamicMeshComponent> DynamicMeshComponent;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "MDF", meta = (DisplayName = "미니게임컴포넌트"))
	TObjectPtr<UMDF_MiniGameComponent> MiniGameComponent;
};