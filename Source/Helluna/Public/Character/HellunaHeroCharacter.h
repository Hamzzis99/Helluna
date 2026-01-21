// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Character/HellunaBaseCharacter.h"
#include "GameplayTagContainer.h"
#include "AbilitySystem/HellunaGameplayAbility.h"
#include "HellunaHeroCharacter.generated.h"


class USpringArmComponent;
class UCameraComponent;
class UDataAsset_InputConfig;
class UHeroCombatComponent;
class AHellunaHeroWeapon;
struct FInputActionValue;
class UHelluna_FindResourceComponent;
class UWeaponBridgeComponent;


/**
 * 
 */

UCLASS()
class HELLUNA_API AHellunaHeroCharacter : public AHellunaBaseCharacter
{
	GENERATED_BODY()
	

public:
	AHellunaHeroCharacter();

protected:
	virtual void SetupPlayerInputComponent(class UInputComponent* PlayerInputComponent) override;
	virtual void BeginPlay() override;

	virtual void PossessedBy(AController* NewController) override;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Resource", meta = (AllowPrivateAccess = "true"))
	UHelluna_FindResourceComponent* FindResourceComponent;




private:

#pragma region Components

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Camera", meta = (AllowPrivateAccess = "true"))
	USpringArmComponent* CameraBoom;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Camera", meta = (AllowPrivateAccess = "true"))
	UCameraComponent* FollowCamera;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Combat", meta = (AllowPrivateAccess = "true"))
	UHeroCombatComponent* HeroCombatComponent;

	// ============================================
	// ⭐ [WeaponBridge] Inventory 연동 컴포넌트
	// ⭐ Inventory 플러그인의 EquipmentComponent와 통신
	// ⭐ 무기 꺼내기/집어넣기 처리
	// ============================================
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Weapon", meta = (AllowPrivateAccess = "true", DisplayName = "무기 브릿지 컴포넌트"))
	UWeaponBridgeComponent* WeaponBridgeComponent;

	UPROPERTY(Replicated, VisibleInstanceOnly, Category = "Weapon")
	TObjectPtr<AHellunaHeroWeapon> CurrentWeapon;


	


#pragma endregion

#pragma region Inputs

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "CharacterData", meta = (AllowPrivateAccess = "true"))
	UDataAsset_InputConfig* InputConfigDataAsset;

	void Input_Move(const FInputActionValue& InputActionValue);
	void Input_Look(const FInputActionValue& InputActionValue);



	void Input_AbilityInputPressed(FGameplayTag InInputTag);
	void Input_AbilityInputReleased(FGameplayTag InInputTag);


#pragma endregion

public:
	FORCEINLINE UHeroCombatComponent* GetHeroCombatComponent() const { return HeroCombatComponent; }
	FORCEINLINE UCameraComponent* GetFollowCamera() const { return FollowCamera;}
	FORCEINLINE USpringArmComponent* GetCameraBoom() const { return CameraBoom; }


	AHellunaHeroWeapon* GetCurrentWeapon() const { return CurrentWeapon; }
	void SetCurrentWeapon(AHellunaHeroWeapon* NewWeapon) { CurrentWeapon = NewWeapon; }

	// ⭐ SpaceShip 수리 RPC (PlayerController가 소유하므로 작동!)
	// @param Material1Tag - 재료 1 태그
	// @param Material1Amount - 재료 1 개수
	// @param Material2Tag - 재료 2 태그
	// @param Material2Amount - 재료 2 개수
	UFUNCTION(Server, Reliable, BlueprintCallable, Category = "Repair")
	void Server_RepairSpaceShip(FGameplayTag Material1Tag, int32 Material1Amount, FGameplayTag Material2Tag, int32 Material2Amount);

	// 무기 스폰 RPC
	UFUNCTION(Server, Reliable)  
	void Server_RequestSpawnWeapon(TSubclassOf<class AHellunaHeroWeapon> InWeaponClass, FName InAttachSocket, UAnimMontage* EquipMontage);

	// ============================================
	// ⭐ [WeaponBridge] 무기 제거 RPC
	// ⭐ 클라이언트에서 호출 → 서버에서 CurrentWeapon Destroy
	// ============================================
	UFUNCTION(Server, Reliable)
	void Server_RequestDestroyWeapon();

	UFUNCTION(NetMulticast, Reliable)
	void Multicast_PlayEquipMontageExceptOwner(UAnimMontage* Montage);


};
