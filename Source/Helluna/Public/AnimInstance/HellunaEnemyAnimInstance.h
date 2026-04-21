// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "AnimInstance/HellunaCharacterAnimInstance.h"
#include "HellunaEnemyAnimInstance.generated.h"

class UAnimSequence;

/**
 * 적(몬스터) 전용 AnimInstance.
 * HellunaCharacterAnimInstance를 부모로 하며, 공중 상태 애니메이션을
 * 에디터에서 지정할 수 있도록 확장한다.
 */
UCLASS()
class HELLUNA_API UHellunaEnemyAnimInstance : public UHellunaCharacterAnimInstance
{
	GENERATED_BODY()

public:
	virtual void NativeUpdateAnimation(float DeltaSeconds) override;
	virtual void NativeThreadSafeUpdateAnimation(float DeltaSeconds) override;

protected:
	/** 몬스터가 공중에 있을 때 재생할 애니메이션 시퀀스 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "AnimData|Air",
		meta = (DisplayName = "공중 애니메이션"))
	TObjectPtr<UAnimSequence> InAirAnim = nullptr;

	/** 공중(낙하/점프) 상태인지 — AnimGraph에서 분기용 */
	UPROPERTY(VisibleDefaultsOnly, BlueprintReadOnly, Category = "AnimData|Air",
		meta = (DisplayName = "공중 상태"))
	bool bIsInAir = false;

	/** AnimGraph(Worker Thread)에서 안전하게 공중 애니메이션을 가져오는 함수 */
	UFUNCTION(BlueprintPure, Category = "AnimData|Air",
		meta = (BlueprintThreadSafe, DisplayName = "공중 애니메이션 가져오기"))
	UAnimSequence* GetInAirAnim() const { return InAirAnim; }

	/** AnimGraph(Worker Thread)에서 공중 상태를 가져오는 함수 */
	UFUNCTION(BlueprintPure, Category = "AnimData|Air",
		meta = (BlueprintThreadSafe, DisplayName = "공중 상태인지"))
	bool IsInAir() const { return bIsInAir; }
};
