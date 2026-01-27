// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "HellunaWeaponBase.generated.h"

class UBoxComponent;



UCLASS()
class HELLUNA_API AHellunaWeaponBase : public AActor
{
	GENERATED_BODY()
	
public:	

	AHellunaWeaponBase();

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Weapon|Stats", meta = (DisplayName = "데미지"))
	float Damage = 20.f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Weapon|Stats", meta = (DisplayName = "공격 속도"))
	float AttackSpeed = 0.1f;

protected:


	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Weapons")
	UStaticMeshComponent* WeaponMesh;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Weapons")
	UBoxComponent* WeaponCollisionBox;

public:	

	FORCEINLINE UBoxComponent* GetWeaponCollisionBox() const { return WeaponCollisionBox; }




};
