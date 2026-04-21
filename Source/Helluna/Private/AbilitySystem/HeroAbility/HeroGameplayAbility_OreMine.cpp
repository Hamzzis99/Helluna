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
	UE_LOG(LogTemp, Warning,
		TEXT("[OreMine][TraceV1] ActivateAbility enter — Net=%s LocalCtrl=%d"),
		ActorInfo && ActorInfo->AvatarActor.IsValid()
			? (ActorInfo->AvatarActor->HasAuthority() ? TEXT("Server") : TEXT("Client"))
			: TEXT("?"),
		ActorInfo && ActorInfo->IsLocallyControlled() ? 1 : 0);

	if (!ActorInfo || !ActorInfo->AvatarActor.IsValid())
	{
		UE_LOG(LogTemp, Warning, TEXT("[OreMine][TraceV1] FAIL: ActorInfo/Avatar invalid"));
		EndAbility(Handle, ActorInfo, ActivationInfo, true, true);
		return;
	}

	AHellunaHeroCharacter* Hero = Cast<AHellunaHeroCharacter>(ActorInfo->AvatarActor.Get());
	if (!Hero)
	{
		UE_LOG(LogTemp, Warning, TEXT("[OreMine][TraceV1] FAIL: Avatar is not HeroCharacter"));
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

	// 1.5) 파밍 거리 사전 체크 — UI prompt 가 떠 있을 때만 통과 (IsWithinFarmingRange).
	// InteractRange 기반으로 FindResourceComponent 가 관리하는 토글 상태를 그대로 참조 →
	// UI 가 "G키 프롬프트" 를 숨기는 순간 GA 도 바로 막힘. 동기 보장.
	if (ActorInfo->IsLocallyControlled() && FindComp && !FindComp->IsWithinFarmingRange())
	{
		UE_LOG(LogTemp, Warning,
			TEXT("[OreMine][TraceV2] REJECT: UI prompt 밖 (IsWithinFarmingRange=false) — pickaxe swap skipped"));
		Debug::Print(TEXT("[GA_OreMine] 파밍 거리 밖 — 가까이 가서 다시 시도"), FColor::Yellow);
		EndAbility(Handle, ActorInfo, ActivationInfo, true, true);
		return;
	}

	// 2) [PreFaceV1] 곡괭이 스왑/대기 진입 *직전* 에 회전 락 + 타겟 바라보기 선제 수행.
	//    - 기존: CMC 락이 Farming::ActivateAbility(= t=0.15s) 에서만 걸려서
	//      대기 동안 카메라 회전을 따라 몸이 돌고, 첫 스윙이 타겟을 안 바라봄.
	//    - 서버/클라 양쪽에서 동일 시점에 수행 → 네트워크 지연으로 인한 회전 덮어쓰기 방지.
	PrimeFarmingPoseBeforeSwing(ActorInfo);

	// 3) 곡괭이로 임시 교체 — **로컬 클라에서만 RPC 발사** (서버의 Activate 가 자체로 또 쏘면 중복 호출)
	if (ActorInfo->IsLocallyControlled())
	{
		UE_LOG(LogTemp, Warning,
			TEXT("[OreMine][TraceV1] → Server_SwapToPickaxeTemp call (EquipMontage=%s)"),
			PickaxeEquipMontage ? *PickaxeEquipMontage->GetName() : TEXT("NULL"));
		Hero->Server_SwapToPickaxeTemp(PickaxeEquipMontage);
	}

	// 4) 스폰/리플리케이션 보정 대기 후 Farming 본 흐름 시작
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
	UE_LOG(LogTemp, Warning,
		TEXT("[OreMine][TraceV1] StartFarmingAfterSwap fired → Super::ActivateAbility (Farming)"));
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
