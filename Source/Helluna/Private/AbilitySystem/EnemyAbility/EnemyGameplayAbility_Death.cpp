#include "AbilitySystem/EnemyAbility/EnemyGameplayAbility_Death.h"

#include "AbilitySystemComponent.h"
#include "Abilities/Tasks/AbilityTask_PlayMontageAndWait.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Components/CapsuleComponent.h"
#include "Kismet/GameplayStatics.h"

#include "Character/HellunaEnemyCharacter.h"
#include "GameMode/HellunaDefenseGameMode.h"

UEnemyGameplayAbility_Death::UEnemyGameplayAbility_Death()
{
	InstancingPolicy  = EGameplayAbilityInstancingPolicy::InstancedPerActor;
	NetExecutionPolicy = EGameplayAbilityNetExecutionPolicy::ServerOnly;
}

void UEnemyGameplayAbility_Death::ActivateAbility(
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

	// 이동 정지 / 콜리전 비활성화
	if (UCharacterMovementComponent* MoveComp = Enemy->GetCharacterMovement())
		MoveComp->DisableMovement();
	if (UCapsuleComponent* Capsule = Enemy->GetCapsuleComponent())
		Capsule->SetCollisionEnabled(ECollisionEnabled::NoCollision);

	UAnimMontage* DeathMontage = Enemy->DeathMontage;
	if (!DeathMontage)
	{
		HandleDeathFinished();
		return;
	}

	UAbilityTask_PlayMontageAndWait* MontageTask = UAbilityTask_PlayMontageAndWait::CreatePlayMontageAndWaitProxy(
		this, NAME_None, DeathMontage, 1.f, NAME_None, false);

	if (!MontageTask)
	{
		HandleDeathFinished();
		return;
	}

	MontageTask->OnCompleted.AddDynamic(this, &UEnemyGameplayAbility_Death::OnMontageCompleted);
	MontageTask->OnCancelled.AddDynamic(this, &UEnemyGameplayAbility_Death::OnMontageCancelled);
	MontageTask->OnInterrupted.AddDynamic(this, &UEnemyGameplayAbility_Death::OnMontageCancelled);

	// 몽타주 70% 지점에서 조기 후처리
	const float EarlyFinishTime = DeathMontage->GetPlayLength() * 0.7f;
	UWorld* World = Enemy->GetWorld();
	if (World)
	{
		FTimerHandle EarlyFinishTimer;
		World->GetTimerManager().SetTimer(EarlyFinishTimer,
			[this]() { HandleDeathFinished(); },
			EarlyFinishTime, false);
	}

	MontageTask->ReadyForActivation();
}

void UEnemyGameplayAbility_Death::OnMontageCompleted()
{
	HandleDeathFinished();
}

void UEnemyGameplayAbility_Death::OnMontageCancelled()
{
	HandleDeathFinished();
}

void UEnemyGameplayAbility_Death::HandleDeathFinished()
{
	if (bDeathHandled) return;
	bDeathHandled = true;

	AHellunaEnemyCharacter* Enemy = GetEnemyCharacterFromActorInfo();
	if (!Enemy) return;

	UWorld* World = Enemy->GetWorld();
	if (!World) return;

	Enemy->OnDeathMontageFinished.ExecuteIfBound();

	if (AHellunaDefenseGameMode* GM = Cast<AHellunaDefenseGameMode>(
		UGameplayStatics::GetGameMode(World)))
	{
		GM->NotifyMonsterDied(Enemy);
	}

	Enemy->DespawnMassEntityOnServer(TEXT("GA_Death"));
	Enemy->SetLifeSpan(0.1f);

	const FGameplayAbilitySpecHandle Handle         = GetCurrentAbilitySpecHandle();
	const FGameplayAbilityActorInfo* ActorInfo      = GetCurrentActorInfo();
	const FGameplayAbilityActivationInfo ActivationInfo = GetCurrentActivationInfo();
	EndAbility(Handle, ActorInfo, ActivationInfo, true, false);
}

void UEnemyGameplayAbility_Death::EndAbility(
	const FGameplayAbilitySpecHandle Handle,
	const FGameplayAbilityActorInfo* ActorInfo,
	const FGameplayAbilityActivationInfo ActivationInfo,
	bool bReplicateEndAbility,
	bool bWasCancelled)
{
	Super::EndAbility(Handle, ActorInfo, ActivationInfo, bReplicateEndAbility, bWasCancelled);
}
