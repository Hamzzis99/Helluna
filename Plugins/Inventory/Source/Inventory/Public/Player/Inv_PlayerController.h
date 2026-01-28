#pragma once

#include "CoreMinimal.h"
#include "GameFramework/PlayerController.h"
#include "Inv_PlayerController.generated.h"

/**
 * ============================================
 * 📌 Inv_PlayerController
 * 
 * [Phase B 역할]:
 * - 게임 플레이 중 플레이어 입력 처리
 * - 인벤토리, 장비, 상호작용
 * - 로그인 UI 표시 및 로그인 RPC 처리
 * 
 * [로그인 흐름]:
 * 1. GihyeonMap에서 BeginPlay 호출
 * 2. 로그인 여부 체크 (PlayerState)
 * 3. 로그인 안 됨 → 로그인 UI 표시
 * 4. 로그인 버튼 클릭 → Server_RequestLogin RPC
 * 5. 서버에서 검증 → Client_LoginResult RPC
 * 6. 로그인 성공 → UI 숨기고 HeroCharacter 소환 (GameMode)
 * ============================================
 */

class UInv_InventoryComponent;
class UInv_EquipmentComponent;
class UInputMappingContext;
class UInputAction;
class UInv_HUDWidget;
class UHellunaLoginWidget;

UCLASS()
class INVENTORY_API AInv_PlayerController : public APlayerController
{
	GENERATED_BODY()
	
public:
	AInv_PlayerController();
	virtual void Tick(float DeltaSeconds) override;

	UFUNCTION(BlueprintCallable, Category = "Inventory")
	void ToggleInventory();

	// ============================================
	// 📌 [Phase B] 로그인 관련 함수
	// ============================================

	/** 로그인 UI에서 버튼 클릭 시 호출 */
	UFUNCTION(BlueprintCallable, Category = "Login")
	void OnLoginButtonClicked(const FString& PlayerId, const FString& Password);

	/** 로그인 UI 표시 */
	UFUNCTION(BlueprintCallable, Category = "Login")
	void ShowLoginUI();

	/** 로그인 UI 숨기기 */
	UFUNCTION(BlueprintCallable, Category = "Login")
	void HideLoginUI();

protected:
	virtual void BeginPlay() override;
	virtual void SetupInputComponent() override;

	// ============================================
	// 📌 [Phase B] 로그인 RPC
	// ============================================

	/** 서버에 로그인 요청 (Server RPC) */
	UFUNCTION(Server, Reliable)
	void Server_RequestLogin(const FString& PlayerId, const FString& Password);

	/** 로그인 결과 수신 (Client RPC) */
	UFUNCTION(Client, Reliable)
	void Client_LoginResult(bool bSuccess, const FString& ErrorMessage);

	/** 로그인 UI 표시 요청 (Client RPC) */
	UFUNCTION(Client, Reliable)
	void Client_ShowLoginUI();

	// ============================================
	// 📌 [Phase B] 로그인 위젯 설정
	// ============================================

	/** 
	 * 로그인 위젯 클래스
	 * Blueprint에서 WBP_HellunaLoginWidget 설정
	 */
	UPROPERTY(EditDefaultsOnly, Category = "Login", meta = (DisplayName = "로그인 위젯 클래스"))
	TSubclassOf<UHellunaLoginWidget> LoginWidgetClass;

	/** 로그인 위젯 인스턴스 */
	UPROPERTY()
	TObjectPtr<UHellunaLoginWidget> LoginWidget;

private:
	// ============================================
	// 📌 인벤토리 & 상호작용
	// ============================================
	
	void PrimaryInteract();
	void CreateHUDWidget();
	void TraceForInteractables();
	
	TWeakObjectPtr<UInv_InventoryComponent> InventoryComponent;
	TWeakObjectPtr<UInv_EquipmentComponent> EquipmentComponent;

	UPROPERTY(EditDefaultsOnly, Category = "Inventory")
	TArray<TObjectPtr<UInputMappingContext>> DefaultIMCs;

	UPROPERTY(EditDefaultsOnly, Category = "Inventory", meta = (DisplayName = "상호작용 액션"))
	TObjectPtr<UInputAction> PrimaryInteractAction;

	UPROPERTY(EditDefaultsOnly, Category = "Inventory", meta = (DisplayName = "인벤토리 토글 액션"))
	TObjectPtr<UInputAction> ToggleInventoryAction;

	UPROPERTY(EditDefaultsOnly, Category = "Inventory|Weapon", meta = (DisplayName = "주무기 전환 액션"))
	TObjectPtr<UInputAction> PrimaryWeaponAction;

	UPROPERTY(EditDefaultsOnly, Category = "Inventory|Weapon", meta = (DisplayName = "보조무기 전환 액션"))
	TObjectPtr<UInputAction> SecondaryWeaponAction;

	void HandlePrimaryWeapon();
	void HandleSecondaryWeapon();

	UPROPERTY(EditDefaultsOnly, Category = "Inventory", meta = (DisplayName = "HUD 위젯 클래스"))
	TSubclassOf<UInv_HUDWidget> HUDWidgetClass;

	UPROPERTY()
	TObjectPtr<UInv_HUDWidget> HUDWidget;

	UPROPERTY(EditDefaultsOnly, Category = "Inventory", meta = (DisplayName = "추적 길이"))	
	double TraceLength;

	UPROPERTY(EditDefaultsOnly, Category = "Inventory", meta = (DisplayName = "아이템 추적 채널"))	
	TEnumAsByte<ECollisionChannel> ItemTraceChannel;

	UFUNCTION(Server, Reliable)
	void Server_Interact(AActor* TargetActor);

	TWeakObjectPtr<AActor> ThisActor;
	TWeakObjectPtr<AActor> LastActor;
	TWeakObjectPtr<AActor> CurrentCraftingStation;
};
