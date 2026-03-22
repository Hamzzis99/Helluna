// File: Source/Helluna/Private/AbilitySystem/AbilityTask/AT_AimCameraInterp.cpp
#include "AbilitySystem/AbilityTask/AT_AimCameraInterp.h"
#include "Character/HellunaHeroCharacter.h"
#include "GameFramework/SpringArmComponent.h"
#include "Camera/CameraComponent.h"

UAT_AimCameraInterp* UAT_AimCameraInterp::CreateTask(
	UGameplayAbility* OwningAbility,
	float TargetFOV,
	float TargetArmLength,
	FVector TargetSocketOffset,
	float InterpSpeed)
{
	UAT_AimCameraInterp* Task = NewAbilityTask<UAT_AimCameraInterp>(OwningAbility);
	Task->GoalFOV = TargetFOV;
	Task->GoalArmLength = TargetArmLength;
	Task->GoalSocketOffset = TargetSocketOffset;
	Task->Speed = InterpSpeed;
	Task->bTickingTask = true;
	return Task;
}

void UAT_AimCameraInterp::Activate()
{
	Super::Activate();
	UE_LOG(LogTemp, Warning, TEXT("[AT_AimCameraInterp] Activate — GoalFOV=%.1f, GoalArm=%.1f, Speed=%.1f"),
		GoalFOV, GoalArmLength, Speed);
}

void UAT_AimCameraInterp::TickTask(float DeltaTime)
{
	if (!Ability || !IsValid(Ability->GetAvatarActorFromActorInfo()))
	{
		EndTask();
		return;
	}

	AHellunaHeroCharacter* Hero = Cast<AHellunaHeroCharacter>(Ability->GetAvatarActorFromActorInfo());
	if (!Hero || !Hero->IsLocallyControlled())
	{
		return;
	}

	USpringArmComponent* Boom = Hero->GetCameraBoom();
	UCameraComponent* Cam = Hero->GetFollowCamera();
	if (!Boom || !Cam) return;

	// ── 보간 ──
	Boom->TargetArmLength = FMath::FInterpTo(Boom->TargetArmLength, GoalArmLength, DeltaTime, Speed);
	Cam->SetFieldOfView(FMath::FInterpTo(Cam->FieldOfView, GoalFOV, DeltaTime, Speed));
	Boom->SocketOffset = FMath::VInterpTo(Boom->SocketOffset, GoalSocketOffset, DeltaTime, Speed);

	// ── 완료 판정 ──
	const bool bFOVDone = FMath::IsNearlyEqual(Cam->FieldOfView, GoalFOV, Tolerance);
	const bool bArmDone = FMath::IsNearlyEqual(Boom->TargetArmLength, GoalArmLength, Tolerance);
	const bool bOffsetDone = GoalSocketOffset.Equals(Boom->SocketOffset, Tolerance);

	if (bFOVDone && bArmDone && bOffsetDone)
	{
		// 정확한 값으로 스냅
		Cam->SetFieldOfView(GoalFOV);
		Boom->TargetArmLength = GoalArmLength;
		Boom->SocketOffset = GoalSocketOffset;

		UE_LOG(LogTemp, Warning, TEXT("[AT_AimCameraInterp] Interp Complete — FOV=%.1f, Arm=%.1f"),
			GoalFOV, GoalArmLength);

		if (ShouldBroadcastAbilityTaskDelegates())
		{
			OnCompleted.Broadcast();
		}
		EndTask();
	}
}

void UAT_AimCameraInterp::OnDestroy(bool bInOwnerFinished)
{
	UE_LOG(LogTemp, Warning, TEXT("[AT_AimCameraInterp] OnDestroy (OwnerFinished=%s)"),
		bInOwnerFinished ? TEXT("Y") : TEXT("N"));
	Super::OnDestroy(bInOwnerFinished);
}
