// Capstone Project Helluna - Gun Parry Execution Fire Notify

#include "AnimInstance/AnimNotify_ParryExecutionFire.h"

#include "AbilitySystemBlueprintLibrary.h"
#include "AbilitySystem/HeroAbility/HeroGameplayAbility_GunParry.h"
#include "Character/HellunaHeroCharacter.h"
#include "HellunaGameplayTags.h"

void UAnimNotify_ParryExecutionFire::Notify(USkeletalMeshComponent* MeshComp, UAnimSequenceBase* Animation,
	const FAnimNotifyEventReference& EventReference)
{
	Super::Notify(MeshComp, Animation, EventReference);

	if (!MeshComp)
	{
		UE_LOG(LogGunParry, Warning, TEXT("[ParryNotify] AN_ParryExecutionFire 스킵 — MeshComp=nullptr"));
		return;
	}

	AHellunaHeroCharacter* Hero = Cast<AHellunaHeroCharacter>(MeshComp->GetOwner());
	if (!Hero)
	{
		UE_LOG(LogGunParry, Warning, TEXT("[ParryNotify] AN_ParryExecutionFire 스킵 — Hero 없음"));
		return;
	}

	UE_LOG(LogGunParry, Warning, TEXT("[ParryNotify] AN_ParryExecutionFire 발동 — Character=%s, Authority=%s, Frame=%llu"),
		*Hero->GetName(),
		Hero->HasAuthority() ? TEXT("SERVER") : TEXT("CLIENT"),
		GFrameCounter);

	FGameplayEventData Payload;
	Payload.EventTag = HellunaGameplayTags::Event_Parry_Fire;
	Payload.Instigator = Hero;
	Payload.Target = Hero;
	Payload.OptionalObject = Animation;

	UAbilitySystemBlueprintLibrary::SendGameplayEventToActor(Hero, HellunaGameplayTags::Event_Parry_Fire, Payload);
}
