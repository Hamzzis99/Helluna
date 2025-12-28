#pragma once

#include "CoreMinimal.h"
#include "GameFramework/PlayerController.h"
#include "Inv_PlayerController.generated.h"

/**
 * 
 */
class UInv_InventoryComponent;
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
	void TraceForItem(); // 아이템 등록
	
	//블루프린트에서 인벤토리 컴포넌트를 열기 위해 WeakObjectPtr(참조)를 선언했다고? 이유가 뭘까?
	TWeakObjectPtr<UInv_InventoryComponent> InventoryComponent;

	UPROPERTY(EditDefaultsOnly, Category = "Inventory")
	TArray<TObjectPtr<UInputMappingContext>> DefaultIMCs; // TArray로부터 단일 포인터를 배열화 시켜가지고 여러개 복수 포인터로 만들 수 있다! 으하하

	UPROPERTY(EditDefaultsOnly, Category = "Inventory")
	TObjectPtr<UInputAction> PrimaryInteractAction; // 상호작용 액션

	UPROPERTY(EditDefaultsOnly, Category = "Inventory")
	TObjectPtr<UInputAction> ToggleInventoryAction; // 인벤토리 키 누르는 액션

	UPROPERTY(EditDefaultsOnly, Category = "Inventory")
	TSubclassOf<UInv_HUDWidget> HUDWidgetClass; // 위젯 선언

	UPROPERTY()
	TObjectPtr<UInv_HUDWidget> HUDWidget; // 위젯 인스턴스

	UPROPERTY(EditDefaultsOnly, Category = "Inventory")	
	double TraceLength; // 추적 길이

	UPROPERTY(EditDefaultsOnly, Category = "Inventory")	
	TEnumAsByte<ECollisionChannel> ItemTraceChannel; // 추적 채널? 충동 채널? 왜 굳이 Enum을 쓰는지 보자

	TWeakObjectPtr<AActor> ThisActor; // 객체에 대한 포인터는 유지하지만 가비지 컬렉션엔 영향은 없음
	TWeakObjectPtr<AActor> LastActor; // 마지막으로 상호작용한 액터
};
