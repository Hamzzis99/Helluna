#pragma once

#include "CoreMinimal.h"
#include "GameFramework/PlayerController.h"
#include "Inv_PlayerController.generated.h"

/**
 * 
 */
class UInv_InventoryComponent;
class UInv_EquipmentComponent;
class UInputMappingContext;
class UInputAction;
class UInv_HUDWidget;


UCLASS()
class INVENTORY_API AInv_PlayerController : public APlayerController
{
	GENERATED_BODY()
public:
	AInv_PlayerController();
	virtual void Tick(float DeltaSeconds) override;

	UFUNCTION(BlueprintCallable, Category = "Inventory")
	void ToggleInventory(); // 인벤토리 토글 함수

protected:
	virtual void BeginPlay() override;
	virtual void SetupInputComponent() override;

private:
	//키 등록들?
	void PrimaryInteract();
	void CreateHUDWidget(); // 위젯 생성 함수 선언
	void TraceForInteractables(); // 아이템 & 크래프팅 스테이션 통합 감지
	
	//블루프린트에서 인벤토리 컴포넌트를 열기 위해 WeakObjectPtr(참조)를 선언했다고? 이유가 뭘까?
	TWeakObjectPtr<UInv_InventoryComponent> InventoryComponent;

	// ============================================
	// ⭐ [WeaponBridge] EquipmentComponent 참조
	// ============================================
	TWeakObjectPtr<UInv_EquipmentComponent> EquipmentComponent;

	UPROPERTY(EditDefaultsOnly, Category = "Inventory")
	TArray<TObjectPtr<UInputMappingContext>> DefaultIMCs; // TArray로부터 단일 포인터를 배열화 시켜가지고 여러개 복수 포인터로 만들 수 있다! 으하하

	UPROPERTY(EditDefaultsOnly, Category = "Inventory", meta = (DisplayName = "상호작용 액션"))
	TObjectPtr<UInputAction> PrimaryInteractAction; // 상호작용 액션

	UPROPERTY(EditDefaultsOnly, Category = "Inventory", meta = (DisplayName = "인벤토리 토글 액션"))
	TObjectPtr<UInputAction> ToggleInventoryAction; // 인벤토리 키 누르는 액션

	// ============================================
	// ⭐ [WeaponBridge] 주무기 전환 InputAction
	// ⭐ Blueprint에서 지정 (1키)
	// ============================================
	UPROPERTY(EditDefaultsOnly, Category = "Inventory|Weapon", meta = (DisplayName = "주무기 전환 액션"))
	TObjectPtr<UInputAction> PrimaryWeaponAction;

	// ============================================
	// ⭐ [WeaponBridge] 보조무기 전환 InputAction
	// ⭐ Blueprint에서 지정 (2키)
	// ============================================
	UPROPERTY(EditDefaultsOnly, Category = "Inventory|Weapon", meta = (DisplayName = "보조무기 전환 액션"))
	TObjectPtr<UInputAction> SecondaryWeaponAction;

	// ============================================
	// ⭐ [WeaponBridge] 주무기 입력 처리 함수
	// ============================================
	void HandlePrimaryWeapon();

	// ============================================
	// ⭐ [WeaponBridge] 보조무기 입력 처리 함수
	// ============================================
	void HandleSecondaryWeapon();

	UPROPERTY(EditDefaultsOnly, Category = "Inventory", meta = (DisplayName = "HUD 위젯 클래스"))
	TSubclassOf<UInv_HUDWidget> HUDWidgetClass; // 위젯 선언

	UPROPERTY()
	TObjectPtr<UInv_HUDWidget> HUDWidget; // 위젯 인스턴스

	UPROPERTY(EditDefaultsOnly, Category = "Inventory", meta = (DisplayName = "추적 길이"))	
	double TraceLength; // 추적 길이

	UPROPERTY(EditDefaultsOnly, Category = "Inventory", meta = (DisplayName = "아이템 추적 채널"))	
	TEnumAsByte<ECollisionChannel> ItemTraceChannel; // 추적 채널? 충동 채널? 왜 굳이 Enum을 쓰는지 보자

	// [추가] 문 작동 요청을 서버로 보내는 함수
	UFUNCTION(Server, Reliable)
	void Server_Interact(AActor* TargetActor);
	

	TWeakObjectPtr<AActor> ThisActor; // 객체에 대한 포인터는 유지하지만 가비지 컬렉션엔 영향은 없음
	TWeakObjectPtr<AActor> LastActor; // 마지막으로 상호작용한 액터
	
	// ⭐ 현재 감지된 크래프팅 스테이션
	TWeakObjectPtr<AActor> CurrentCraftingStation;
};
