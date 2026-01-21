// Gihyeon's Inventory Project

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "GameplayTagContainer.h"
#include "Inv_EquipmentComponent.generated.h"

struct FGameplayTag;
struct FInv_ItemManifest;
class AInv_EquipActor;
struct FInv_EquipmentFragment;
class UInv_InventoryItem;
class UInv_InventoryComponent;
class APlayerController;
class USkeletalMeshComponent;
class UGameplayAbility;

// ============================================
// ⭐ [WeaponBridge] 활성 무기 슬롯 상태
// ⭐ 현재 손에 어떤 무기가 들려있는지 표시
// ============================================
UENUM(BlueprintType)
enum class EInv_ActiveWeaponSlot : uint8
{
	None      UMETA(DisplayName = "없음"),       // 맨손 (무기가 등에 있음)
	Primary   UMETA(DisplayName = "주무기"),     // 주무기 손에 듦
	Secondary UMETA(DisplayName = "보조무기")    // 보조무기 손에 듦
};

// ============================================
// ⭐ [WeaponBridge] 무기 장착/해제 요청 델리게이트
// ⭐ Inventory 플러그인 → Helluna 모듈로 신호 전달
// @param WeaponTag - 무기 종류 태그
// @param BackWeaponActor - 등 무기 Actor (Hidden 처리용)
// @param SpawnWeaponAbility - 무기 스폰 GA 클래스
// @param bEquip - true: 꺼내기, false: 집어넣기
// ============================================
DECLARE_DYNAMIC_MULTICAST_DELEGATE_FourParams(FOnWeaponEquipRequested,
	const FGameplayTag&, WeaponTag,
	AInv_EquipActor*, BackWeaponActor,
	TSubclassOf<UGameplayAbility>, SpawnWeaponAbility,
	bool, bEquip);

UCLASS(ClassGroup=(Custom), meta=(BlueprintSpawnableComponent), Blueprintable)
class INVENTORY_API UInv_EquipmentComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	void SetOwningSkeletalMesh(USkeletalMeshComponent* OwningMesh);
	void SetIsProxy(bool bProxy) { bIsProxy = bProxy; }
	void InitializeOwner(APlayerController* PlayerController);

	// ============================================
	// ⭐ [WeaponBridge] 무기 장착/해제 델리게이트
	// ⭐ Helluna의 WeaponBridgeComponent에서 바인딩
	// ============================================
	UPROPERTY(BlueprintAssignable, Category = "Inventory|Weapon")
	FOnWeaponEquipRequested OnWeaponEquipRequested;

	// ============================================
	// ⭐ [WeaponBridge] 주무기 입력 처리
	// ⭐ PlayerController에서 호출됨 (1키 입력)
	// ============================================
	UFUNCTION(BlueprintCallable, Category = "Inventory|Weapon", meta = (DisplayName = "주무기 입력 처리"))
	void HandlePrimaryWeaponInput();

	// ============================================
	// ⭐ [WeaponBridge] 보조무기 입력 처리
	// ⭐ PlayerController에서 호출됨 (2키 입력)
	// ============================================
	UFUNCTION(BlueprintCallable, Category = "Inventory|Weapon", meta = (DisplayName = "보조무기 입력 처리"))
	void HandleSecondaryWeaponInput();

	// ============================================
	// ⭐ [WeaponBridge] 현재 활성 무기 슬롯 Getter
	// ============================================
	UFUNCTION(BlueprintPure, Category = "Inventory|Weapon", meta = (DisplayName = "활성 무기 슬롯 가져오기"))
	EInv_ActiveWeaponSlot GetActiveWeaponSlot() const { return ActiveWeaponSlot; }
	
protected:
	
	virtual void BeginPlay() override;

private:
	TWeakObjectPtr<UInv_InventoryComponent> InventoryComponent;
	TWeakObjectPtr<APlayerController> OwningPlayerController;
	TWeakObjectPtr<USkeletalMeshComponent> OwningSkeletalMesh; // 아이템 장착 골격
	
	// 델리게이트 바인딩을 대비하기 위한 함수들 콜백 함수들 
	UFUNCTION()
	void OnItemEquipped(UInv_InventoryItem* EquippedItem, int32 WeaponSlotIndex);

	UFUNCTION()
	void OnItemUnequipped(UInv_InventoryItem* UnequippedItem, int32 WeaponSlotIndex);
	
	void InitPlayerController(); //멀티플레이
	void InitInventoryComponent();
	AInv_EquipActor* SpawnEquippedActor(FInv_EquipmentFragment* EquipmentFragment, const FInv_ItemManifest& Manifest, USkeletalMeshComponent* AttachMesh);
	
	UPROPERTY()
	TArray<TObjectPtr<AInv_EquipActor>> EquippedActors;
	
	AInv_EquipActor* FindEquippedActor(const FGameplayTag& EquipmentTypeTag); 
	void RemoveEquippedActor(const FGameplayTag& EquipmentTypeTag);
	
	UFUNCTION()
	void OnPossessedPawnChange(APawn* OldPawn, APawn* NewPawn); // 멀티플레이 장착 아이템 변경 할 떄 폰 변경 시 호출되는 함수
	
	bool bIsProxy{false};

	// ============================================
	// ⭐ [WeaponBridge] 무기 상태 관리
	// ============================================
	
	// 현재 활성 무기 슬롯
	UPROPERTY(VisibleAnywhere, Category = "Inventory|Weapon", meta = (DisplayName = "활성 무기 슬롯"))
	EInv_ActiveWeaponSlot ActiveWeaponSlot = EInv_ActiveWeaponSlot::None;

	// ============================================
	// ⭐ [WeaponBridge] 무기 꺼내기/집어넣기 내부 함수
	// ============================================
	
	// 주무기 꺼내기 (등 → 손)
	void EquipPrimaryWeapon();
	
	// 보조무기 꺼내기 (등 → 손)
	void EquipSecondaryWeapon();
	
	// 무기 집어넣기 (손 → 등)
	void UnequipWeapon();
	
	// 주무기 Actor 찾기 (EquippedActors에서)
	AInv_EquipActor* FindPrimaryWeaponActor();
	
	// 보조무기 Actor 찾기 (EquippedActors에서)
	AInv_EquipActor* FindSecondaryWeaponActor();
};
