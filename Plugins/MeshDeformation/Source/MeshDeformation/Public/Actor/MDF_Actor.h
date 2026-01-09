// Gihyeon's Inventory Project (Helluna)
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "MDF_Actor.generated.h"

class UDynamicMeshComponent;
class UMDF_DeformableComponent;

UCLASS()
class MESHDEFORMATION_API AMDF_Actor : public AActor
{
	GENERATED_BODY()

public:
	AMDF_Actor();

	// [Step 6 핵심] 에디터에서 수치를 바꾸거나 배치할 때마다 실행되는 함수
	virtual void OnConstruction(const FTransform& Transform) override;

protected:
	virtual void BeginPlay() override;

public:
	// 실제로 찌그러질 다이나믹 메시 (루트로 설정하여 스케일 조절 대응)
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "MDF", meta = (AllowPrivateAccess = "true"))
	UDynamicMeshComponent* DynamicMeshComponent;

	// 변형 로직을 담당하는 핵심 컴포넌트
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "MDF", meta = (AllowPrivateAccess = "true"))
	UMDF_DeformableComponent* DeformableComponent;
};