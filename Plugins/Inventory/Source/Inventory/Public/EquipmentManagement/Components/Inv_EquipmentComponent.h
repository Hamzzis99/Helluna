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
class UGameplayAbility; // TODO: [독립화] 졸작 후 삭제

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
// @param WeaponSlotIndex - 무기 슬롯 인덱스 (0=주무기, 1=보조무기)
// ============================================
// TODO: [독립화] 졸작 후 5파라미터 → 4파라미터로 변경
// TSubclassOf<UGameplayAbility> SpawnWeaponAbility 파라미터 삭제
// 변경 후:
// DECLARE_DYNAMIC_MULTICAST_DELEGATE_FourParams(FOnWeaponEquipRequested,
//     const FGameplayTag&, WeaponTag,
//     AInv_EquipActor*, BackWeaponActor,
//     bool, bEquip,
//     int32, WeaponSlotIndex);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_FiveParams(FOnWeaponEquipRequested,
	const FGameplayTag&, WeaponTag,
	AInv_EquipActor*, BackWeaponActor,
	TSubclassOf<UGameplayAbility>, SpawnWeaponAbility,
	bool, bEquip,
	int32, WeaponSlotIndex);

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
	// ⭐ [WeaponBridge] 무기 장착 애니메이션 진행 중 플래그
	// ⭐ true일 때 HandlePrimary/SecondaryWeaponInput 차단
	// ⭐ WeaponBridgeComponent에서 SetWeaponEquipping()을 통해 제어
	// ============================================
	UFUNCTION(BlueprintCallable, Category = "Inventory|Weapon", meta = (DisplayName = "무기 장착 중 설정"))
	void SetWeaponEquipping(bool bNewEquipping);

	UFUNCTION(BlueprintPure, Category = "Inventory|Weapon", meta = (DisplayName = "무기 장착 중 여부"))
	bool IsWeaponEquipping() const { return bIsWeaponEquipping; }

	// ============================================
	// ⭐ [WeaponBridge] 현재 활성 무기 슬롯 Getter
	// ============================================
	UFUNCTION(BlueprintPure, Category = "Inventory|Weapon", meta = (DisplayName = "활성 무기 슬롯 가져오기"))
	EInv_ActiveWeaponSlot GetActiveWeaponSlot() const { return ActiveWeaponSlot; }

protected:

	virtual void BeginPlay() override;
	
	// 🆕 [Phase 6] 컴포넌트 파괴 시 장착 액터 정리
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

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
	AInv_EquipActor* SpawnEquippedActor(FInv_EquipmentFragment* EquipmentFragment, const FInv_ItemManifest& Manifest, USkeletalMeshComponent* AttachMesh, int32 WeaponSlotIndex = -1);

	UPROPERTY()
	TArray<TObjectPtr<AInv_EquipActor>> EquippedActors;

	AInv_EquipActor* FindEquippedActor(const FGameplayTag& EquipmentTypeTag);
	void RemoveEquippedActor(const FGameplayTag& EquipmentTypeTag, int32 WeaponSlotIndex = -1);

	UFUNCTION()
	void OnPossessedPawnChange(APawn* OldPawn, APawn* NewPawn); // 멀티플레이 장착 아이템 변경 할 떄 폰 변경 시 호출되는 함수

	bool bIsProxy{ false };

	// ============================================
	// ⭐ [WeaponBridge] 무기 상태 관리
	// ============================================

	// 현재 활성 무기 슬롯
	UPROPERTY(VisibleAnywhere, Category = "Inventory|Weapon", meta = (DisplayName = "활성 무기 슬롯"))
	EInv_ActiveWeaponSlot ActiveWeaponSlot = EInv_ActiveWeaponSlot::None;

	// ⭐ 무기 장착 애니메이션 진행 중 플래그
	UPROPERTY(VisibleAnywhere, Category = "Inventory|Weapon", meta = (DisplayName = "무기 장착 중"))
	bool bIsWeaponEquipping = false;

	// ════════════════════════════════════════════════════════════════
	// TODO: [독립화] 졸작 후 여기에 내장 HandWeapon 모드 플래그 추가
	//
	// UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Inventory|Weapon",
	//     meta = (DisplayName = "내장 HandWeapon 모드"))
	// bool bUseBuiltInHandWeapon = true;
	//
	// true: 플러그인이 EquipActor를 등/손 소켓 간 직접 이동 (게임 코드 불필요)
	// false: 델리게이트만 발사, 게임에서 별도 손 무기 스폰/파괴 (현재 Helluna 방식)
	// ════════════════════════════════════════════════════════════════

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

	//================== 김민우 수정 =====================
	//		UnequipWeapon(); 외부에서 호출하는 함수	추가
	//==================================================
public:
	void ActiveUnequipWeapon();

	// ============================================
	// 🆕 [Phase 7.5] 현재 활성 무기의 EquipActor 반환
	// ============================================
	// [2026-02-18] 작업자: 김기현
	// ────────────────────────────────────────────
	// 목적:
	//   팀원의 GA/무기 코드(Helluna 모듈)에서 EquipActor의
	//   Phase 7 프로퍼티(GetFireSound, GetZoomFOV 등)를 읽기 위한
	//   public 접근 경로 제공
	//
	// 동작:
	//   ActiveWeaponSlot 값에 따라 분기하여
	//   Primary → FindPrimaryWeaponActor()
	//   Secondary → FindSecondaryWeaponActor()
	//   None → nullptr 반환
	//
	// 호출 경로:
	//   AInv_PlayerController::GetCurrentEquipActor()
	//     → UInv_EquipmentComponent::GetActiveWeaponActor()  ← 이 함수
	//       → AInv_EquipActor* 반환
	//
	// 사용 예시 (팀원 코드):
	//   AInv_PlayerController* PC = Cast<AInv_PlayerController>(Hero->GetController());
	//   AInv_EquipActor* EA = PC ? PC->GetCurrentEquipActor() : nullptr;
	//   USoundBase* Sound = EA ? EA->GetFireSound() : nullptr;
	// ============================================
	UFUNCTION(BlueprintCallable, Category = "Inventory|Weapon", meta = (DisplayName = "활성 무기 EquipActor 가져오기"))
	AInv_EquipActor* GetActiveWeaponActor();

	// ============================================
	// 🆕 [Phase 6] 장착된 액터 목록 Getter
	// ⭐ 저장 시 장착 상태 확인용
	// ============================================
	const TArray<TObjectPtr<AInv_EquipActor>>& GetEquippedActors() const { return EquippedActors; }

};

