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

UENUM(BlueprintType)
enum class EWeaponFireMode : uint8
{
	SemiAuto UMETA(DisplayName = "단발"),
	FullAuto UMETA(DisplayName = "연발")
};

UCLASS()
class HELLUNA_API AHellunaHeroWeapon : public AHellunaWeaponBase
{
	GENERATED_BODY()

public:

 // 연사속도

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Weapon|Stats", meta = (DisplayName = "상하 반동"))
	float ReboundUp = 0.5f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Weapon|Stats", meta = (DisplayName = "좌우 반동"))
	float ReboundLeftRight = 0.5f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Weapon|Fire", meta = (DisplayName = "발사 모드"))
	EWeaponFireMode FireMode = EWeaponFireMode::SemiAuto;

	UFUNCTION(BlueprintCallable, Category = "Weapon|Fire")
	virtual void Fire(AController* InstigatorController);

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
