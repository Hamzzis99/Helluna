// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Weapon/HellunaWeaponBase.h"
#include "HellunaGameplayTags.h"
#include "HellunaHeroWeapon.generated.h"


/**
 * 
 */

USTRUCT(BlueprintType)
struct FWeaponAnimationSet
{
	GENERATED_BODY()

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, meta = (DisplayName = "장착 애니메이션"))
	UAnimMontage* Equip;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, meta = (DisplayName = "공격 애니메이션"))
	UAnimMontage* Attack;

};


UCLASS()
class HELLUNA_API AHellunaHeroWeapon : public AHellunaWeaponBase
{
	GENERATED_BODY()

public:

 // 연사속도



	UPROPERTY(EditDefaultsOnly, Category = "Weapon|Animation", meta = (DisplayName = "적용할 애니메이션"))
	FWeaponAnimationSet AnimSet;

	const FWeaponAnimationSet& GetAnimSet() const { return AnimSet; }

	// 소켓 관련 함수
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Weapon|Attach", meta = (DisplayName = "장착 소켓"))
	FName EquipSocketName = TEXT("WeaponSocket");

	UFUNCTION(BlueprintPure, Category = "Weapon|Attach")
	FName GetEquipSocketName() const { return EquipSocketName; }

	//웨폰 태그 함수
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Weapon|Tags", meta = (DisplayName = "무기 태그"))
	FGameplayTag WeaponTag;

	UFUNCTION(BlueprintPure, Category = "Weapon|Tags")
	FGameplayTag GetWeaponTag() const { return WeaponTag; }

	
};
