// Gihyeon's Inventory Project

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "GameplayTagContainer.h"
#include "Inv_EquipActor.generated.h"

class UGameplayAbility;

UCLASS()
class INVENTORY_API AInv_EquipActor : public AActor
{
	GENERATED_BODY()

public:
	AInv_EquipActor();
	
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
	
	FGameplayTag GetEquipmentType() const { return EquipmentType; }
	void SetEquipmentType(FGameplayTag Type) { EquipmentType = Type; }

	// ============================================
	// ⭐ [WeaponBridge] 무기 스폰 GA 클래스 Getter
	// ⭐ 팀원의 GA_SpawnWeapon을 직접 호출하기 위함
	// ============================================
	TSubclassOf<UGameplayAbility> GetSpawnWeaponAbility() const { return SpawnWeaponAbility; }

	// ============================================
	// ⭐ [WeaponBridge] 무기 슬롯 인덱스 (0=주무기, 1=보조무기)
	// ============================================
	int32 GetWeaponSlotIndex() const { return WeaponSlotIndex; }
	void SetWeaponSlotIndex(int32 Index) { WeaponSlotIndex = Index; }

	// ============================================
	// ⭐ [WeaponBridge] 등 소켓 이름 Getter
	// ⭐ WeaponSlotIndex에 따라 적절한 소켓 반환
	// ============================================
	FName GetBackSocketName() const
	{
		return (WeaponSlotIndex == 1) ? SecondaryBackSocket : PrimaryBackSocket;
	}

	// ============================================
	// ⭐ [WeaponBridge] 무기 숨김/표시 (서버 RPC + 리플리케이트)
	// ⭐ 클라이언트에서 호출 → 서버로 RPC → 리플리케이트
	// ============================================
	void SetWeaponHidden(bool bNewHidden);
	bool IsWeaponHidden() const { return bIsWeaponHidden; }

protected:
	// ⭐ [WeaponBridge] Hidden 상태 변경 시 호출 (리플리케이션)
	UFUNCTION()
	void OnRep_IsWeaponHidden();
	
	// ⭐ [WeaponBridge] 서버 RPC - 클라이언트→서버
	UFUNCTION(Server, Reliable)
	void Server_SetWeaponHidden(bool bNewHidden);

private:

	UPROPERTY(EditAnywhere, Category = "Inventory", meta = (DisplayName = "장비 타입 태그"))
	FGameplayTag EquipmentType;

	// ============================================
	// ⭐ [WeaponBridge] 무기 스폰 GA
	// ⭐ 팀원이 만든 GA_Hero_SpawnWeapon 블루프린트 지정
	// ⭐ 1키 입력 시 이 GA를 활성화하여 무기 스폰
	// ⭐ 예: GA_Hero_SpawnWeapon (도끼), GA_Hero_SpawnWeapon2 (총) 등
	// ============================================
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Inventory|Weapon", meta = (AllowPrivateAccess = "true", DisplayName = "무기 스폰 GA"))
	TSubclassOf<UGameplayAbility> SpawnWeaponAbility;

	// ============================================
	// ⭐ [WeaponBridge] 무기 슬롯 인덱스
	// ⭐ 0 = 주무기 슬롯, 1 = 보조무기 슬롯
	// ⭐ 장착 시 EquipmentComponent에서 설정
	// ============================================
	UPROPERTY(Replicated, VisibleAnywhere, Category = "Inventory|Weapon", meta = (DisplayName = "무기 슬롯 인덱스"))
	int32 WeaponSlotIndex = -1;

	// ============================================
	// ⭐ [WeaponBridge] 무기 숨김 상태 (리플리케이트)
	// ⭐ 손에 무기를 들면 true, 집어넣으면 false
	// ============================================
	UPROPERTY(ReplicatedUsing = OnRep_IsWeaponHidden, VisibleAnywhere, Category = "Inventory|Weapon")
	bool bIsWeaponHidden = false;

	// ============================================
	// ⭐ [WeaponBridge] 등 장착 소켓 (블루프린트에서 설정)
	// ⭐ 주무기(SlotIndex=0)일 때 사용할 소켓
	// ============================================
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Inventory|Weapon|Socket", meta = (AllowPrivateAccess = "true", DisplayName = "주무기 등 소켓"))
	FName PrimaryBackSocket = TEXT("WeaponSocket_Primary");

	// ============================================
	// ⭐ [WeaponBridge] 등 장착 소켓 (블루프린트에서 설정)
	// ⭐ 보조무기(SlotIndex=1)일 때 사용할 소켓
	// ============================================
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Inventory|Weapon|Socket", meta = (AllowPrivateAccess = "true", DisplayName = "보조무기 등 소켓"))
	FName SecondaryBackSocket = TEXT("WeaponSocket_Secondary");
};