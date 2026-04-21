// Capstone Project Helluna


#include "AbilitySystem/HeroAbility/HeroGameplayAbility_OreMine.h"

#include "Character/HellunaHeroCharacter.h"
#include "Character/HeroComponent/Helluna_FindResourceComponent.h"
#include "Engine/World.h"
#include "TimerManager.h"

#include "DebugHelper.h"

UHeroGameplayAbility_OreMine::UHeroGameplayAbility_OreMine()
{
	// Farming GA의 기본 정책을 그대로 따라간다 (Hold 입력, LocalPredicted, OnTriggered)
	// — 부모 생성자에서 이미 설정되므로 여기서는 별도 변경 없음.
}

void UHeroGameplayAbility_OreMine::ActivateAbility(
	const FGameplayAbilitySpecHandle Handle,
	const FGameplayAbilityActorInfo* ActorInfo,
	const FGameplayAbilityActivationInfo ActivationInfo,
	const FGameplayEventData* TriggerEventData)
{
	if (!ActorInfo || !ActorInfo->AvatarActor.IsValid())
	{
		EndAbility(Handle, ActorInfo, ActivationInfo, true, true);
		return;
	}

	AHellunaHeroCharacter* Hero = Cast<AHellunaHeroCharacter>(ActorInfo->AvatarActor.Get());
	if (!Hero)
	{
		EndAbility(Handle, ActorInfo, ActivationInfo, true, true);
		return;
	}

	// 1) 광석 포커스 사전 검증 (로컬에서만 의미 — Farming GA의 ResolveFarmingTarget 재활용)
	UHelluna_FindResourceComponent* FindComp = Hero->FindComponentByClass<UHelluna_FindResourceComponent>();
	AActor* FocusedOre = (ActorInfo->IsLocallyControlled() && FindComp) ? FindComp->GetFocusedActor() : nullptr;
	if (ActorInfo->IsLocallyControlled() && !FocusedOre)
	{
		Debug::Print(TEXT("[GA_OreMine] 포커스된 광석 없음 — 채집 취소"), FColor::Yellow);
		EndAbility(Handle, ActorInfo, ActivationInfo, true, true);
		return;
	}

	// 2) 곡괭이로 임시 교체 (서버 RPC, 권위 측에서 무기 스폰/탄약 보존 처리)
	Hero->Server_SwapToPickaxeTemp(PickaxeEquipMontage);

	// 3) 스폰/리플리케이션 보정 대기 후 Farming 본 흐름 시작
	PendingHandle = Handle;
	PendingActivationInfo = ActivationInfo;

	if (PickaxeSwapDelay <= 0.f)
	{
		StartFarmingAfterSwap();
		return;
	}

	if (UWorld* World = Hero->GetWorld())
	{
		World->GetTimerManager().SetTimer(
			SwapDelayTimer,
			FTimerDelegate::CreateUObject(this, &UHeroGameplayAbility_OreMine::StartFarmingAfterSwap),
			PickaxeSwapDelay,
			false);
	}
	else
	{
		StartFarmingAfterSwap();
	}
}

void UHeroGameplayAbility_OreMine::StartFarmingAfterSwap()
{
	// 부모 ActivateAbility는 CurrentActorInfo가 이미 GA에 세팅된 상태를 가정.
	// 본 GA는 본 Activate가 한 번 호출되었으므로 CurrentActorInfo는 유효함.
	Super::ActivateAbility(PendingHandle, CurrentActorInfo, PendingActivationInfo, nullptr);
}

void UHeroGameplayAbility_OreMine::EndAbility(
	const FGameplayAbilitySpecHandle Handle,
	const FGameplayAbilityActorInfo* ActorInfo,
	const FGameplayAbilityActivationInfo ActivationInfo,
	bool bReplicateEndAbility,
	bool bWasCancelled)
{
	// 대기 타이머 정리
	if (AHellunaHeroCharacter* Hero = Cast<AHellunaHeroCharacter>(GetAvatarActorFromActorInfo()))
	{
		if (UWorld* World = Hero->GetWorld())
		{
			World->GetTimerManager().ClearTimer(SwapDelayTimer);
		}

		// 원래 무기 복원 (서버 RPC). PrePickaxeWeaponClass는 서버에 백업되어 있음.
		Hero->Server_RestorePrePickaxeWeapon(RestoreEquipMontage);
	}

	Super::EndAbility(Handle, ActorInfo, ActivationInfo, bReplicateEndAbility, bWasCancelled);
}
