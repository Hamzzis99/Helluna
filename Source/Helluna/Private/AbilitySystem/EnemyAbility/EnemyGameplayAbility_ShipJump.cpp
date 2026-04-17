/**
 * EnemyGameplayAbility_ShipJump.cpp
 *
 * [ShipJumpV1] 우주선 상단 점프 테스트 GA.
 */

#include "AbilitySystem/EnemyAbility/EnemyGameplayAbility_ShipJump.h"
#include "AbilitySystemComponent.h"
#include "GameplayTagContainer.h"
#include "Character/HellunaEnemyCharacter.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Engine/World.h"
#include "TimerManager.h"

UEnemyGameplayAbility_ShipJump::UEnemyGameplayAbility_ShipJump()
{
	InstancingPolicy = EGameplayAbilityInstancingPolicy::InstancedPerActor;
	NetExecutionPolicy = EGameplayAbilityNetExecutionPolicy::ServerOnly;
}

void UEnemyGameplayAbility_ShipJump::ActivateAbility(
	const FGameplayAbilitySpecHandle Handle,
	const FGameplayAbilityActorInfo* ActorInfo,
	const FGameplayAbilityActivationInfo ActivationInfo,
	const FGameplayEventData* TriggerEventData)
{
	Super::ActivateAbility(Handle, ActorInfo, ActivationInfo, TriggerEventData);

	AHellunaEnemyCharacter* Enemy = GetEnemyCharacterFromActorInfo();
	if (!Enemy)
	{
		EndAbility(Handle, ActorInfo, ActivationInfo, true, true);
		return;
	}

	if (UAbilitySystemComponent* ASC = GetAbilitySystemComponentFromActorInfo())
	{
		FGameplayTagContainer AttackingTag;
		AttackingTag.AddTag(FGameplayTag::RequestGameplayTag(FName("State.Enemy.Attacking")));
		ASC->AddLooseGameplayTags(AttackingTag);
	}

	bHasLanded = false;

	PerformJump();
}

FVector UEnemyGameplayAbility_ShipJump::ComputeLaunchVelocityToShipTop(const AHellunaEnemyCharacter* Enemy, AActor* Ship) const
{
	// 폴백: 타겟 없거나 bound 계산 실패 시 간단한 전방 도약.
	const FVector Fallback = Enemy->GetActorForwardVector() * FallbackHorizontalSpeed
		+ FVector(0.f, 0.f, FallbackVerticalSpeed);

	if (!Ship) return Fallback;

	FVector ShipOrigin, ShipExtent;
	Ship->GetActorBounds(false, ShipOrigin, ShipExtent, true);
	if (ShipExtent.IsNearlyZero())
	{
		return Fallback;
	}

	const FVector StartLoc = Enemy->GetActorLocation();
	const float ShipTopZ = ShipOrigin.Z + ShipExtent.Z;
	const FVector LandingLoc(ShipOrigin.X, ShipOrigin.Y, ShipTopZ);
	const FVector Delta = LandingLoc - StartLoc;

	// 중력 (CMC에서 읽고 절댓값 사용).
	float GravityZ = 980.f;
	if (const UCharacterMovementComponent* CMC = Enemy->GetCharacterMovement())
	{
		GravityZ = FMath::Abs(CMC->GetGravityZ());
		if (GravityZ < KINDA_SMALL_NUMBER) GravityZ = 980.f;
	}

	// 정점(Apex) Z = 시작 + Delta.Z + OvershootHeight. 정점에서 수직 속도 0.
	// Vz^2 = 2 * g * H_apex  ⇒  Vz = sqrt(2 g H_apex)
	const float ApexHeightFromStart = FMath::Max(Delta.Z + OvershootHeight, OvershootHeight);
	const float LaunchVelocityZ = FMath::Sqrt(2.f * GravityZ * ApexHeightFromStart);

	// 상승 시간 (출발 → 정점).
	const float TimeUp = LaunchVelocityZ / GravityZ;

	// 하강 시간 (정점 → 착지 Z). Fall height = ApexHeightFromStart - Delta.Z.
	const float FallHeight = FMath::Max(ApexHeightFromStart - Delta.Z, 1.f);
	const float TimeDown = FMath::Sqrt(2.f * FallHeight / GravityZ);
	const float TotalTime = FMath::Max(TimeUp + TimeDown, 0.1f);

	const FVector2D Horiz(Delta.X, Delta.Y);
	const FVector2D HorizVel = Horiz / TotalTime;

	const FVector Launch(HorizVel.X, HorizVel.Y, LaunchVelocityZ);

	UE_LOG(LogTemp, Warning,
		TEXT("[ShipJumpV1] Launch Enemy=%s Ship=%s DeltaZ=%.0f Apex=%.0f Vz=%.0f Vxy=%.0f T=%.2f"),
		*Enemy->GetName(), *Ship->GetName(),
		Delta.Z, ApexHeightFromStart, LaunchVelocityZ, HorizVel.Size(), TotalTime);

	return Launch;
}

void UEnemyGameplayAbility_ShipJump::PerformJump()
{
	AHellunaEnemyCharacter* Enemy = GetEnemyCharacterFromActorInfo();
	if (!Enemy) return;

	if (UCharacterMovementComponent* MoveComp = Enemy->GetCharacterMovement())
	{
		MoveComp->SetMovementMode(MOVE_Falling);
	}

	AActor* Ship = CurrentTarget.Get();
	const FVector LaunchVelocity = ComputeLaunchVelocityToShipTop(Enemy, Ship);

	// bXYOverride=true, bZOverride=true 로 기존 속도 덮어써서 포물선 정확도 확보.
	Enemy->LaunchCharacter(LaunchVelocity, true, true);

	if (UWorld* World = Enemy->GetWorld())
	{
		// 착지 체크 타이머 (0.1초 반복, 도약 후 0.3초 대기 후 시작).
		World->GetTimerManager().SetTimer(
			LandingCheckTimerHandle,
			[this]()
			{
				AHellunaEnemyCharacter* E = GetEnemyCharacterFromActorInfo();
				if (!E) { OnLanded(); return; }

				UCharacterMovementComponent* MoveComp = E->GetCharacterMovement();
				if (MoveComp && MoveComp->IsMovingOnGround() && !bHasLanded)
				{
					OnLanded();
				}
			},
			0.1f, true, 0.3f);

		// 페일세이프: 너무 오래 공중에 있으면 강제 종료.
		World->GetTimerManager().SetTimer(
			AirborneFailsafeHandle,
			[this]()
			{
				if (!bHasLanded)
				{
					UE_LOG(LogTemp, Warning, TEXT("[ShipJumpV1] Airborne failsafe fired — forcing land"));
					OnLanded();
				}
			},
			MaxAirborneTime, false);
	}
}

void UEnemyGameplayAbility_ShipJump::OnLanded()
{
	if (bHasLanded) return;
	bHasLanded = true;

	AHellunaEnemyCharacter* Enemy = GetEnemyCharacterFromActorInfo();
	UWorld* World = Enemy ? Enemy->GetWorld() : nullptr;
	if (World)
	{
		World->GetTimerManager().ClearTimer(LandingCheckTimerHandle);
		World->GetTimerManager().ClearTimer(AirborneFailsafeHandle);
	}

	UE_LOG(LogTemp, Warning, TEXT("[ShipJumpV1] Landed Enemy=%s"),
		Enemy ? *Enemy->GetName() : TEXT("none"));

	if (Enemy && World)
	{
		World->GetTimerManager().SetTimer(
			DelayedReleaseTimerHandle,
			[this]()
			{
				if (AHellunaEnemyCharacter* E = GetEnemyCharacterFromActorInfo())
				{
					E->UnlockMovement();
				}
				HandleAttackFinished();
			},
			AttackRecoveryDelay, false);
	}
	else
	{
		HandleAttackFinished();
	}
}

void UEnemyGameplayAbility_ShipJump::HandleAttackFinished()
{
	const FGameplayAbilitySpecHandle H = GetCurrentAbilitySpecHandle();
	const FGameplayAbilityActorInfo* AI = GetCurrentActorInfo();
	const FGameplayAbilityActivationInfo AV = GetCurrentActivationInfo();
	EndAbility(H, AI, AV, true, false);
}

void UEnemyGameplayAbility_ShipJump::EndAbility(
	const FGameplayAbilitySpecHandle Handle,
	const FGameplayAbilityActorInfo* ActorInfo,
	const FGameplayAbilityActivationInfo ActivationInfo,
	bool bReplicateEndAbility,
	bool bWasCancelled)
{
	if (AHellunaEnemyCharacter* Enemy = GetEnemyCharacterFromActorInfo())
	{
		Enemy->UnlockMovement();
		if (UWorld* World = Enemy->GetWorld())
		{
			World->GetTimerManager().ClearTimer(LandingCheckTimerHandle);
			World->GetTimerManager().ClearTimer(DelayedReleaseTimerHandle);
			World->GetTimerManager().ClearTimer(AirborneFailsafeHandle);
		}
	}

	if (UAbilitySystemComponent* ASC = GetAbilitySystemComponentFromActorInfo())
	{
		FGameplayTagContainer AttackingTag;
		AttackingTag.AddTag(FGameplayTag::RequestGameplayTag(FName("State.Enemy.Attacking")));
		ASC->RemoveLooseGameplayTags(AttackingTag);
	}

	CurrentTarget = nullptr;
	bHasLanded = false;

	Super::EndAbility(Handle, ActorInfo, ActivationInfo, bReplicateEndAbility, bWasCancelled);
}
