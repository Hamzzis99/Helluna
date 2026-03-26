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

	// 캡슐뿐 아니라 모든 PrimitiveComponent의 콜리전을 비활성화.
	// 캡슐만 끄면 SkeletalMesh 등 다른 콜리전 컴포넌트가 남아 있어서
	// Destroy() → UnregisterAllComponents() 과정에서 EndOverlap 발생 시
	// 이미 등록해제된 형제 컴포넌트를 참조하여 bRegistered assertion이 발생한다.
	Enemy->SetActorEnableCollision(false);
	TArray<UPrimitiveComponent*> Prims;
	Enemy->GetComponents<UPrimitiveComponent>(Prims);
	for (UPrimitiveComponent* Prim : Prims)
	{
		if (Prim && Prim->IsRegistered())
		{
			Prim->SetCollisionEnabled(ECollisionEnabled::NoCollision);
			Prim->SetGenerateOverlapEvents(false);
		}
	}

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

	// Destroy 전 남은 콜리전 완전 비활성화 — bRegistered assertion 방지
	Enemy->SetActorEnableCollision(false);

	// EndAbility를 Destroy 전에 호출 — Destroy 후에는 ActorInfo가 무효화될 수 있음
	const FGameplayAbilitySpecHandle Handle         = GetCurrentAbilitySpecHandle();
	const FGameplayAbilityActorInfo* ActorInfo      = GetCurrentActorInfo();
	const FGameplayAbilityActivationInfo ActivationInfo = GetCurrentActivationInfo();
	EndAbility(Handle, ActorInfo, ActivationInfo, true, false);

	// DespawnMassEntityOnServer가 Mass Entity 정리 + Destroy()까지 처리
	Enemy->DespawnMassEntityOnServer(TEXT("GA_Death"));
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
