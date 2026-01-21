// Capstone Project Helluna

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Helluna_FindResourceComponent.generated.h"


class UInv_HighlightableStaticMesh;
class UWidgetComponent;

UCLASS(ClassGroup = (Custom), meta = (BlueprintSpawnableComponent))
class HELLUNA_API UHelluna_FindResourceComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UHelluna_FindResourceComponent();

	virtual void BeginPlay() override;
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

	UFUNCTION(BlueprintCallable, Category = "Mine|Focus")
	AActor* GetFocusedActor() const { return FocusedActor.Get(); }

	UFUNCTION(BlueprintCallable, Category = "Mine|Focus")
	AActor* GetServerFarmingTarget() const { return ServerFarmingTarget; }


private:
	// ✅ 카메라 정면으로 "1개"만 잡는 단순 탐지
	UPROPERTY(EditDefaultsOnly, Category = "Mine|Focus", meta = (DisplayName = "탐지 거리"))
	float TraceDistance = 450.f;

	UPROPERTY(EditDefaultsOnly, Category = "Mine|Focus", meta = (DisplayName = "탐지 반경"))
	float TraceRadius = 60.f;

	UPROPERTY(EditDefaultsOnly, Category = "Mine|Focus", meta = (DisplayName = "필수 액터 태그"))
	FName RequiredActorTag = TEXT("Ore");

	UPROPERTY()
	TObjectPtr<AActor> ServerFarmingTarget = nullptr;

	// ✅ 포커스된 대상(하이라이트/프롬프트 대상)
	TWeakObjectPtr<AActor> FocusedActor;
	TWeakObjectPtr<UInv_HighlightableStaticMesh> FocusedMesh;
	TWeakObjectPtr<UWidgetComponent> FocusedPromptWidget;


	void UpdateFocus();
	bool IsValidTargetActor(AActor* Actor) const;

	UInv_HighlightableStaticMesh* FindHighlightMesh(AActor* Actor) const;
	UWidgetComponent* FindPromptWidget(AActor* Actor) const;
	void SetPromptVisible(AActor* Actor, bool bVisible);

	void ApplyFocus(AActor* NewActor, UInv_HighlightableStaticMesh* NewMesh);
	void ClearFocus();

	// ✅ “파밍 가능 상태”는 FocusedActor + 거리 조건으로만 토글
	void ApplyFarming();
	void ClearFarming();

	bool bFarmingApplied = false;

	UPROPERTY(EditDefaultsOnly, Category = "Farming|Range", meta = (DisplayName = "파밍 거리"))
	float InteractRange = 200.f;

	UFUNCTION(Server, Reliable)
	void ServerSetCanFarming(bool bEnable, AActor* FarmingTarget);


};
								