// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Character/HellunaBaseCharacter.h"
#include "GameplayTagContainer.h"
#include "AbilitySystem/HellunaGameplayAbility.h"
#include "AbilitySystemInterface.h"
#include "HellunaHeroCharacter.generated.h"


class USpringArmComponent;
class UCameraComponent;
class UDataAsset_InputConfig;
class UHeroCombatComponent;
class AHellunaHeroWeapon;
struct FInputActionValue;
class UHelluna_FindResourceComponent;
class UWeaponBridgeComponent;
class AHeroWeapon_GunBase;
class UHellunaHealthComponent;
class UInv_LootContainerComponent;

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
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;  // ⭐ 인벤토리 저장용

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
	
	// 서버에 애니 재생 요청
	// Unreliable: 코스메틱 애니메이션 동기화. 유실돼도 다음 애니메이션에서 보정
	UFUNCTION(Server, Unreliable)
	void Server_RequestPlayMontageExceptOwner(UAnimMontage* Montage);


	// 이동,카메라 입력 잠금/해제에 관한 함수들 ====================
	void LockMoveInput();
	void UnlockMoveInput();
	void LockLookInput();
	void UnlockLookInput();

private:
	UPROPERTY(VisibleAnywhere, Category = "Input")
	bool bMoveInputLocked = false;

	UPROPERTY(VisibleAnywhere, Category = "Input")
	bool bLookInputLocked = false;

	FRotator CachedLockedControlRotation = FRotator::ZeroRotator;

	// ============================================
	// ✅ GAS: Ability System Interface 구현 -> 웨폰태그 적용

public:

	// ✅ 현재 무기 태그(서버가 결정 → 클라로 복제)
	UPROPERTY(ReplicatedUsing = OnRep_CurrentWeaponTag)
	FGameplayTag CurrentWeaponTag;


private:
	// ✅ ASC를 캐릭터가 소유하도록 
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "GAS", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<UHellunaAbilitySystemComponent> AbilitySystemComponent; // ✅ 추가

protected:
	// ✅ OnRep: 클라에서 태그를 ASC에 적용
	UFUNCTION()
	void OnRep_CurrentWeaponTag();

	// ✅ 서버/클라 공통으로 태그 적용 유틸
	void ApplyTagToASC(const FGameplayTag& OldTag, const FGameplayTag& NewTag);

	// ✅ 클라에서 “이전에 적용했던 태그” 저장용 (RepNotify에서 Old 값을 못 받는 경우 대비)
	FGameplayTag LastAppliedWeaponTag;

// 총알 개수 저장
// UPROPERTY()
private:
	UPROPERTY()
	TMap<TObjectPtr<UClass>, int32> SavedMagByWeaponClass;

	void SaveCurrentMagByClass(AHellunaHeroWeapon* Weapon);
	void ApplySavedCurrentMagByClass(AHellunaHeroWeapon* Weapon);
	
	
	// 애니메이션을 전체 재생할 것인가
public:
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Animation")
	bool PlayFullBody = false;

	// =========================================================
	// ★ 추가: 피격 / 사망 애니메이션
	// =========================================================

	/** 피격 시 재생할 몽타주 */
	UPROPERTY(EditDefaultsOnly, Category = "Animation|Combat",
		meta = (DisplayName = "피격 몽타주",
			ToolTip = "데미지를 받았을 때 재생할 Hit React 애니메이션 몽타주입니다."))
	TObjectPtr<UAnimMontage> HitReactMontage = nullptr;

	/** 사망 시 재생할 몽타주 */
	UPROPERTY(EditDefaultsOnly, Category = "Animation|Combat",
		meta = (DisplayName = "사망 몽타주",
			ToolTip = "HP가 0이 되어 사망할 때 재생할 Death 애니메이션 몽타주입니다."))
	TObjectPtr<UAnimMontage> DeathMontage = nullptr;

protected:
	/** HealthComponent (피격/사망 처리) */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Component",
		meta = (DisplayName = "체력 컴포넌트"))
	TObjectPtr<UHellunaHealthComponent> HeroHealthComponent = nullptr;

	/** Phase 9: 사체 루팅용 컨테이너 (사망 시 아이템 이전) */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Component",
		meta = (DisplayName = "루트 컨테이너 컴포넌트"))
	TObjectPtr<UInv_LootContainerComponent> LootContainerComponent = nullptr;

	UFUNCTION()
	void OnHeroHealthChanged(UActorComponent* HealthComp, float OldHealth, float NewHealth, AActor* InstigatorActor);

	UFUNCTION()
	void OnHeroDeath(AActor* DeadActor, AActor* KillerActor);

	/** 피격 몽타주 멀티캐스트 */
	UFUNCTION(NetMulticast, Reliable)
	void Multicast_PlayHeroHitReact();

	/** 사망 몽타주 멀티캐스트 */
	UFUNCTION(NetMulticast, Reliable)
	void Multicast_PlayHeroDeath();
};
