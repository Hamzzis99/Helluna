// Fill out your copyright notice in the Description page of Project Settings.


#include "AbilitySystem/HellunaAbilitySystemComponent.h"
#include "HellunaGameplayTags.h"
#include "AbilitySystem/HellunaHeroGameplayAbility.h"

#include "DebugHelper.h"


bool UHellunaAbilitySystemComponent::TryActivateAbilityByTag(FGameplayTag AbilityTagToActivate)
{
	check(AbilityTagToActivate.IsValid());

	TArray<FGameplayAbilitySpec*> FoundAbilitySpecs;
	GetActivatableGameplayAbilitySpecsByAllMatchingTags(AbilityTagToActivate.GetSingleTagContainer(), FoundAbilitySpecs);

	if (!FoundAbilitySpecs.IsEmpty())
	{
		for (FGameplayAbilitySpec* SpecToActivate : FoundAbilitySpecs)
		{
			if (!SpecToActivate) continue;

			if (SpecToActivate->IsActive()) continue;

			if (TryActivateAbility(SpecToActivate->Handle))
			{
				return true;
			}
		}
	}

	return false;
}

void UHellunaAbilitySystemComponent::OnAbilityInputPressed(const FGameplayTag& InInputTag)
{
	// ============================================
	// 🔍 [디버깅] 상세 로그 - 입력 시작
	// ============================================
	AActor* MyAvatarActor = GetAvatarActor();
	AActor* MyOwnerActor = GetOwner();
	
	UE_LOG(LogTemp, Warning, TEXT(""));
	UE_LOG(LogTemp, Warning, TEXT("╔══════════════════════════════════════════════════════════════╗"));
	UE_LOG(LogTemp, Warning, TEXT("║  🎮 [ASC] OnAbilityInputPressed 호출                         ║"));
	UE_LOG(LogTemp, Warning, TEXT("╠══════════════════════════════════════════════════════════════╣"));
	UE_LOG(LogTemp, Warning, TEXT("║ InputTag: %s"), *InInputTag.ToString());
	UE_LOG(LogTemp, Warning, TEXT("║ AvatarActor: %s"), MyAvatarActor ? *MyAvatarActor->GetName() : TEXT("nullptr"));
	UE_LOG(LogTemp, Warning, TEXT("║ OwnerActor: %s"), MyOwnerActor ? *MyOwnerActor->GetName() : TEXT("nullptr"));
	
	if (APawn* Pawn = Cast<APawn>(MyAvatarActor))
	{
		UE_LOG(LogTemp, Warning, TEXT("║ IsLocallyControlled: %s"), Pawn->IsLocallyControlled() ? TEXT("TRUE ✅") : TEXT("FALSE ❌"));
		UE_LOG(LogTemp, Warning, TEXT("║ HasAuthority: %s"), Pawn->HasAuthority() ? TEXT("TRUE (서버)") : TEXT("FALSE (클라)"));
		UE_LOG(LogTemp, Warning, TEXT("║ GetLocalRole: %d"), (int32)Pawn->GetLocalRole());
		
		if (AController* Controller = Pawn->GetController())
		{
			UE_LOG(LogTemp, Warning, TEXT("║ Controller: %s"), *Controller->GetName());
			if (APlayerController* PC = Cast<APlayerController>(Controller))
			{
				UE_LOG(LogTemp, Warning, TEXT("║ PC->IsLocalController: %s"), PC->IsLocalController() ? TEXT("TRUE ✅") : TEXT("FALSE ❌"));
			}
		}
		else
		{
			UE_LOG(LogTemp, Warning, TEXT("║ Controller: nullptr ⚠️"));
		}
	}
	UE_LOG(LogTemp, Warning, TEXT("╚══════════════════════════════════════════════════════════════╝"));
	
	if (!InInputTag.IsValid())
	{
		UE_LOG(LogTemp, Warning, TEXT("⛔ [ASC] InputTag 유효하지 않음 - 리턴"));
		return;
	}

	// ============================================
	// ⭐ [멀티플레이 버그 수정] 로컬 제어 캐릭터만 입력 처리
	// ============================================
	if (APawn* Pawn = Cast<APawn>(MyAvatarActor))
	{
		if (!Pawn->IsLocallyControlled())
		{
			UE_LOG(LogTemp, Error, TEXT("⛔⛔⛔ [ASC] 로컬 캐릭터 아님! 입력 무시! Pawn: %s ⛔⛔⛔"), *Pawn->GetName());
			return;
		}
		UE_LOG(LogTemp, Warning, TEXT("✅ [ASC] 로컬 캐릭터 확인됨 - 입력 처리 진행"));
	}

	int32 AbilityCount = 0;
	for (const FGameplayAbilitySpec& AbilitySpec : GetActivatableAbilities())
	{
		AbilityCount++;
		
		if (!AbilitySpec.DynamicAbilityTags.HasTagExact(InInputTag)) continue;

		UE_LOG(LogTemp, Warning, TEXT("🎯 [ASC] 매칭된 어빌리티 발견: %s"), AbilitySpec.Ability ? *AbilitySpec.Ability->GetName() : TEXT("nullptr"));

		const UHellunaHeroGameplayAbility* HellunaGA = Cast<UHellunaHeroGameplayAbility>(AbilitySpec.Ability);
		const EHellunaInputActionPolicy Policy = HellunaGA ? HellunaGA->InputActionPolicy : EHellunaInputActionPolicy::Trigger;

		if (Policy == EHellunaInputActionPolicy::Toggle)
		{
			if (AbilitySpec.IsActive()) 
				CancelAbilityHandle(AbilitySpec.Handle);
			else 
			{
				UE_LOG(LogTemp, Warning, TEXT("🚀 [ASC] TryActivateAbility 호출 (Toggle)"));
				TryActivateAbility(AbilitySpec.Handle);
			}
		}
		else // Trigger
		{
			if (AbilitySpec.IsActive()) continue;

			UE_LOG(LogTemp, Warning, TEXT("🚀 [ASC] TryActivateAbility 호출 (Trigger)"));
			TryActivateAbility(AbilitySpec.Handle);
			return;
		}
	}
	
	UE_LOG(LogTemp, Warning, TEXT("📊 [ASC] 총 어빌리티 수: %d"), AbilityCount);
}

void UHellunaAbilitySystemComponent::OnAbilityInputReleased(const FGameplayTag& InInputTag)
{
	if (!InInputTag.IsValid())
	{
		return;
	}

	// ============================================
	// ⭐ [멀티플레이 버그 수정] 로컬 제어 캐릭터만 입력 처리
	// ⭐ 서버에서 다른 클라이언트의 입력이 잘못 처리되는 것 방지
	// ============================================
	AActor* MyAvatarActor = GetAvatarActor();
	if (APawn* Pawn = Cast<APawn>(MyAvatarActor))
	{
		if (!Pawn->IsLocallyControlled())
		{
			UE_LOG(LogTemp, Warning, TEXT("⭐ [ASC] OnAbilityInputReleased 스킵 - 로컬 제어 캐릭터 아님: %s"), *Pawn->GetName());
			return;
		}
	}

	for (FGameplayAbilitySpec& AbilitySpec : GetActivatableAbilities())
	{
		if (!AbilitySpec.DynamicAbilityTags.HasTagExact(InInputTag))
		{
			continue;
		}

		const UHellunaHeroGameplayAbility* HellunaGA = Cast<UHellunaHeroGameplayAbility>(AbilitySpec.Ability);
		const EHellunaInputActionPolicy Policy =
			HellunaGA ? HellunaGA->InputActionPolicy : EHellunaInputActionPolicy::Trigger;

		if (Policy == EHellunaInputActionPolicy::Hold)
		{
			if (AbilitySpec.IsActive())
			{
				CancelAbilityHandle(AbilitySpec.Handle);  // -> EndAbility
			}
			continue; 
		}

		if (Policy == EHellunaInputActionPolicy::Toggle)
		{
			continue;
		}

		if (AbilitySpec.IsActive())
		{
			AbilitySpec.Ability->InputReleased(
				AbilitySpec.Handle,
				AbilityActorInfo.Get(),
				AbilitySpec.ActivationInfo
			);
		}
	}
}

bool UHellunaAbilitySystemComponent::CancelAbilityByTag(const FGameplayTag AbilityTagToCancel)  //어빌리티 취소 
{
	check(AbilityTagToCancel.IsValid());

	TArray<FGameplayAbilitySpec*> FoundAbilitySpecs;
	GetActivatableGameplayAbilitySpecsByAllMatchingTags(
		AbilityTagToCancel.GetSingleTagContainer(),
		FoundAbilitySpecs
	);

	bool bCanceledAny = false;

	for (FGameplayAbilitySpec* SpecToCancel : FoundAbilitySpecs)
	{
		if (!SpecToCancel) continue;
		if (!SpecToCancel->IsActive()) continue;

		CancelAbilityHandle(SpecToCancel->Handle);
		bCanceledAny = true;
	}

	return bCanceledAny;
}

void UHellunaAbilitySystemComponent::AddStateTag(const FGameplayTag& Tag)
{
	if (!Tag.IsValid()) return;
	AddLooseGameplayTag(Tag);
}

void UHellunaAbilitySystemComponent::RemoveStateTag(const FGameplayTag& Tag)
{
	if (!Tag.IsValid()) return;
	RemoveLooseGameplayTag(Tag);
}

bool UHellunaAbilitySystemComponent::HasStateTag(const FGameplayTag& Tag) const
{
	if (!Tag.IsValid()) return false;
	return HasMatchingGameplayTag(Tag);
}