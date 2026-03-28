// Gihyeon's Inventory Project

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Items/Manifest/Inv_ItemManifest.h"
#include "Inv_ItemComponent.generated.h"

class UWidgetComponent;
class UInv_InteractPromptWidget;

//Blueprintable: 블루프린트 클래스로 만들 수 있게 하는 것.
UCLASS( ClassGroup=(Custom), meta=(BlueprintSpawnableComponent), Blueprintable )
class INVENTORY_API UInv_ItemComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UInv_ItemComponent();
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	void InitItemManifest(FInv_ItemManifest CopyOfManifest); // 아이템 매니페스트 초기화 함수
	const FInv_ItemManifest& GetItemManifest() const { return ItemManifest; } // 아이템 매니페스트 반환 함수 (const 참조)
	FInv_ItemManifest& GetItemManifestMutable() { return ItemManifest; } // 수정 가능한 참조 반환
	FString GetPickupMessage() const { return PickupMessage;  }
	void PickedUp(); // 아이템이 줍혔을 때 호출되는 함수

	// ---- 3D 상호작용 위젯 ----

	/** 3D 상호작용 위젯 표시. true 반환 시 3D 위젯 활성화됨, false면 fallback 필요 */
	bool ShowInteractWidget(const FString& KeyName = TEXT("F"));

	/** 3D 상호작용 위젯 숨김 */
	void HideInteractWidget();

	/** 3D 위젯이 사용 가능한지 (InteractWidgetComp가 생성되었는지) */
	bool HasInteractWidget() const { return InteractWidgetComp != nullptr; }

protected:
	virtual void BeginPlay() override;

	UFUNCTION(BlueprintImplementableEvent, Category = "인벤토리", meta = (DisplayName = "아이템 줍기 이벤트"))
	void OnPickedUp(); // 아이템이 줍혔을 때 호출되는 보호된 함수

private:
	UPROPERTY(Replicated, EditAnywhere, Category = "인벤토리", meta = (DisplayName = "아이템 매니페스트", Tooltip = "이 아이템 컴포넌트의 매니페스트 데이터. 아이템의 모든 프래그먼트 정보를 포함합니다."))
	FInv_ItemManifest ItemManifest;

	UPROPERTY(EditAnywhere, Category = "인벤토리", meta = (DisplayName = "줍기 메시지", Tooltip = "아이템을 주울 때 화면에 표시되는 메시지 텍스트."))
	FString PickupMessage;

	// ---- 3D 상호작용 위젯 ----

	/** 3D 상호작용 위젯 컴포넌트 (BeginPlay에서 동적 생성) */
	UPROPERTY()
	TObjectPtr<UWidgetComponent> InteractWidgetComp;

	/** 위젯 BP 클래스 (CDO에서 설정, 미설정 시 3D 위젯 생성 안 함 → 기존 2D HUD 유지) */
	UPROPERTY(EditDefaultsOnly, Category = "인벤토리|상호작용",
		meta = (DisplayName = "Interact Widget Class (상호작용 위젯 클래스)"))
	TSubclassOf<UInv_InteractPromptWidget> InteractWidgetClass;

	/** 위젯 인스턴스 캐시 */
	UPROPERTY()
	TObjectPtr<UInv_InteractPromptWidget> InteractWidgetInstance;

	/** 위젯이 현재 보이는지 */
	bool bInteractWidgetVisible = false;
};
