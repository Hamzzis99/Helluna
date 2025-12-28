// Fill out your copyright notice in the Description page of Project Settings.


#include "Character/HellunaBaseCharacter.h"
#include "AbilitySystem/HellunaAttributeSet.h"
#include "AbilitySystem/HellunaAbilitySystemComponent.h"
#include "MotionWarpingComponent.h"

// Sets default values
AHellunaBaseCharacter::AHellunaBaseCharacter()
{
 	// Set this character to call Tick() every frame.  You can turn this off to improve performance if you don't need it.
	PrimaryActorTick.bCanEverTick = false;
	PrimaryActorTick.bStartWithTickEnabled = false;

	GetMesh()->bReceivesDecals = false;

	HellunaAbilitySystemComponent = CreateDefaultSubobject<UHellunaAbilitySystemComponent>(TEXT("HellunaAbilitySystemComponent"));

	HellunaAttributeSet = CreateDefaultSubobject<UHellunaAttributeSet>(TEXT("HellunaAttributeSet"));

	MotionWarpingComponent = CreateDefaultSubobject<UMotionWarpingComponent>(TEXT("MotionWarpingComponent"));
}

UAbilitySystemComponent* AHellunaBaseCharacter::GetAbilitySystemComponent() const
{
	return GetHellunaAbilitySystemComponent();
}

void AHellunaBaseCharacter::PossessedBy(AController* NewController)
{
	Super::PossessedBy(NewController);

	if (HellunaAbilitySystemComponent)
	{
		HellunaAbilitySystemComponent->InitAbilityActorInfo(this, this);

		ensureMsgf(!CharacterStartUpData.IsNull(), TEXT("Forgot to assign start up data to %s"), *GetName());
	}

}



// Called when the game starts or when spawned
