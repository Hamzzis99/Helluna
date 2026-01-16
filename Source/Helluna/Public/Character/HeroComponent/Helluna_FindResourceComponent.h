// Capstone Project Helluna

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Helluna_FindResourceComponent.generated.h"


class UInv_HighlightableStaticMesh;

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

private:
	// “대충 근처” 판정 핵심
	UPROPERTY(EditDefaultsOnly, Category = "Mine|Focus")
	float TraceDistance = 450.f;

	UPROPERTY(EditDefaultsOnly, Category = "Mine|Focus")
	float TraceRadius = 60.f;

	// 너무 자주 바꾸면 깜빡거릴 수 있어서 약간 히스테리시스(선택)
	UPROPERTY(EditDefaultsOnly, Category = "Mine|Focus")
	float KeepFocusDistanceBonus = 80.f;

	// 광물 액터를 태그로 제한하고 싶으면 사용 (없으면 빈 값으로 두면 됨)
	UPROPERTY(EditDefaultsOnly, Category = "Mine|Focus")
	FName RequiredActorTag = TEXT("Ore");

	UPROPERTY(EditDefaultsOnly, Category = "Mine|Focus")
	float MaxFocusAngleDeg = 8.f;

	TWeakObjectPtr<AActor> FocusedActor;
	TWeakObjectPtr<UInv_HighlightableStaticMesh> FocusedMesh;

	void UpdateFocus();
	bool IsValidTargetActor(AActor* Actor) const;

	UInv_HighlightableStaticMesh* FindHighlightMesh(AActor* Actor) const;

	void ApplyFocus(AActor* NewActor, UInv_HighlightableStaticMesh* NewMesh);
	void ClearFocus();

	bool IsInViewCone(const FVector& ViewLoc, const FVector& ViewDir, const FVector& TargetLoc) const;

	UFUNCTION(Server, Reliable)
	void ServerSetCanFarming(bool bEnable);
};
