// Capstone Project Helluna - Melee Kick Impact Notify

#include "AnimInstance/AnimNotify_KickImpact.h"

#include "AbilitySystemBlueprintLibrary.h"
#include "Character/HellunaHeroCharacter.h"
#include "HellunaGameplayTags.h"

void UAnimNotify_KickImpact::Notify(USkeletalMeshComponent* MeshComp, UAnimSequenceBase* Animation,
	const FAnimNotifyEventReference& EventReference)
{
	Super::Notify(MeshComp, Animation, EventReference);

	if (!MeshComp) return;

	AHellunaHeroCharacter* Hero = Cast<AHellunaHeroCharacter>(MeshComp->GetOwner());
	if (!Hero) return;

	UE_LOG(LogTemp, Warning, TEXT("[KickNotify] AN_KickImpact 발동 — Character=%s, Authority=%s, Frame=%llu"),
		*Hero->GetName(),
		Hero->HasAuthority() ? TEXT("SERVER") : TEXT("CLIENT"),
		GFrameCounter);

	FGameplayEventData Payload;
	Payload.EventTag = HellunaGameplayTags::Event_Kick_Impact;
	Payload.Instigator = Hero;
	Payload.Target = Hero;
	Payload.OptionalObject = Animation;

	UAbilitySystemBlueprintLibrary::SendGameplayEventToActor(Hero, HellunaGameplayTags::Event_Kick_Impact, Payload);
}
