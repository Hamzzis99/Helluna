// File: Source/Helluna/Public/AbilitySystem/AbilityTask/AT_AimCameraInterp.h
#pragma once

#include "CoreMinimal.h"
#include "Abilities/Tasks/AbilityTask.h"
#include "AT_AimCameraInterp.generated.h"

// ═══════════════════════════════════════════════════════════
// [Aim System] 카메라 줌 보간 AbilityTask
//
// GA에서 호출하면 매 틱마다 FOV/ArmLength/SocketOffset을
// 목표값으로 FInterpTo 보간. 완료되면 OnCompleted 브로드캐스트.
// GA가 취소되면 TickTask 자동 중지.
// ═══════════════════════════════════════════════════════════

DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnAimInterpCompleted);

UCLASS()
class HELLUNA_API UAT_AimCameraInterp : public UAbilityTask
{
	GENERATED_BODY()

public:
	/** 줌인/줌아웃 보간 시작 */
	UFUNCTION(BlueprintCallable, Category = "Ability|Tasks",
		meta = (DisplayName = "Aim Camera Interp",
			HidePin = "OwningAbility", DefaultToSelf = "OwningAbility"))
	static UAT_AimCameraInterp* CreateTask(
		UGameplayAbility* OwningAbility,
		float TargetFOV,
		float TargetArmLength,
		FVector TargetSocketOffset,
		float InterpSpeed);

	/** 보간 완료 시 브로드캐스트 */
	UPROPERTY(BlueprintAssignable)
	FOnAimInterpCompleted OnCompleted;

protected:
	virtual void Activate() override;
	virtual void TickTask(float DeltaTime) override;
	virtual void OnDestroy(bool bInOwnerFinished) override;

private:
	float GoalFOV = 90.f;
	float GoalArmLength = 250.f;
	FVector GoalSocketOffset = FVector::ZeroVector;
	float Speed = 10.f;

	/** 보간 완료 판정 허용 오차 */
	static constexpr float Tolerance = 0.5f;

	/** 보간 최대 지속 시간 (초) — 초과 시 강제 스냅 후 완료 */
	static constexpr float MaxDuration = 3.0f;
	float ElapsedTime = 0.f;
};
