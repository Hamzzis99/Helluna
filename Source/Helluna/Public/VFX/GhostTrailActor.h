// Capstone Project Helluna — Ghost Trail After Image

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "GhostTrailActor.generated.h"

class UPoseableMeshComponent;

/**
 * 패링 워프 잔상 Actor
 *
 * SkeletalMeshComponent의 현재 포즈를 PoseableMeshComponent로 복사하고,
 * 모든 머티리얼 슬롯을 반투명 고스트 머티리얼로 오버라이드.
 * Tick에서 Opacity를 감소시켜 페이드아웃 후 자동 Destroy.
 * 로컬에서만 스폰 (코스메틱, 네트워크 동기화 불필요).
 */
UCLASS(NotPlaceable)
class HELLUNA_API AGhostTrailActor : public AActor
{
	GENERATED_BODY()

public:
	AGhostTrailActor();

	/**
	 * 잔상 초기화 — 포즈 복사 + 머티리얼 오버라이드
	 * @param SourceMesh   포즈를 복사할 원본 SkeletalMeshComponent
	 * @param Material     잔상 머티리얼 (Translucent, Unlit). nullptr이면 잔상 표시 안 됨
	 * @param InFadeDuration 페이드아웃 시간 (초)
	 * @param InInitialOpacity 초기 투명도 (0~1)
	 * @param InGhostColor 잔상 색상 (머티리얼의 GhostColor 파라미터)
	 */
	void Initialize(USkeletalMeshComponent* SourceMesh, UMaterialInterface* Material,
		float InFadeDuration, float InInitialOpacity, FLinearColor InGhostColor);

	virtual void Tick(float DeltaTime) override;

private:
	UPROPERTY()
	TObjectPtr<UPoseableMeshComponent> GhostMesh;

	float FadeDuration = 0.5f;
	float ElapsedTime = 0.f;
	float InitialOpacity = 0.4f;

	UPROPERTY()
	TArray<TObjectPtr<UMaterialInstanceDynamic>> DynamicMaterials;
};
