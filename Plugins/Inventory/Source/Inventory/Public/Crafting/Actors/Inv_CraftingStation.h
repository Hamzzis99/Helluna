// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Building/Actor/Inv_BuildableActor.h"
#include "Crafting/Interfaces/Inv_CraftingInterface.h"
#include "Inv_CraftingStation.generated.h"

class UUserWidget;
class UInv_CraftingRecipeDA;
class UWidgetComponent;
class UInv_InteractPromptWidget;

/**
 * 크래프팅 스테이션 베이스 액터
 * BP_WeaponCrafting, BP_PotionCrafting 등으로 블루프린트 확장 가능
 *
 * 변경: AActor → AInv_BuildableActor 상속
 * - StationMesh 제거 → 부모의 BuildingMesh 사용
 * - 건설 데이터(이름, 아이콘, 재료, 프리뷰)는 BuildableActor의 Class Defaults에서 설정
 */
UCLASS(Blueprintable)
class INVENTORY_API AInv_CraftingStation : public AInv_BuildableActor, public IInv_CraftingInterface
{
	GENERATED_BODY()

public:
	AInv_CraftingStation();

	// IInv_CraftingInterface 구현
	virtual void OnInteract_Implementation(APlayerController* PlayerController) override;
	virtual TSubclassOf<UUserWidget> GetCraftingMenuClass_Implementation() const override;
	virtual float GetInteractionDistance_Implementation() const override;

	// 상호작용 메시지 가져오기 (ItemComponent와 동일한 방식)
	UFUNCTION(BlueprintCallable, Category = "제작", meta = (DisplayName = "상호작용 메시지 가져오기"))
	FString GetPickupMessage() const { return PickupMessage; }

	// === 3D 상호작용 위젯 (Phase 18) ===

	/** 3D 위젯 표시 (바인딩 키 이름 전달) */
	bool ShowInteractWidget(const FString& KeyName = TEXT("E"));

	/** 3D 위젯 숨기기 */
	void HideInteractWidget();

	/** 3D 위젯이 있는지 */
	bool HasInteractWidget() const { return InteractWidgetComp != nullptr; }

protected:
	virtual void BeginPlay() override;

	// 크래프팅 메뉴 위젯 클래스 (블루프린트에서 설정)
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "제작",
		meta = (DisplayName = "제작 메뉴 위젯 클래스", Tooltip = "이 제작 스테이션에서 열릴 제작 메뉴 위젯 블루프린트 클래스입니다."))
	TSubclassOf<UUserWidget> CraftingMenuClass;

	// 상호작용 메시지 (블루프린트에서 수정 가능 - ItemComponent와 동일)
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "제작",
		meta = (DisplayName = "상호작용 메시지", Tooltip = "플레이어가 가까이 갔을 때 화면에 표시되는 상호작용 안내 메시지입니다."))
	FString PickupMessage = "E - Craft";

	// 크래프팅 레시피 데이터 (탭형 크래프팅 메뉴용)
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "제작",
		meta = (DisplayName = "크래프팅 레시피 데이터", Tooltip = "탭형 크래프팅 메뉴에서 사용할 레시피 DataAsset입니다."))
	TObjectPtr<UInv_CraftingRecipeDA> CraftingRecipeData;

	// 상호작용 가능 거리
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "제작",
		meta = (DisplayName = "상호작용 거리", Tooltip = "플레이어가 이 거리(cm) 이내에 있어야 상호작용이 가능합니다."))
	float InteractionDistance = 300.0f;

	// 거리 체크 간격 (초 단위)
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "제작",
		meta = (DisplayName = "거리 체크 간격", Tooltip = "플레이어와의 거리를 확인하는 주기(초). 메뉴가 열린 상태에서 거리를 벗어나면 자동으로 닫힙니다.", ClampMin = "0.1", ClampMax = "5.0"))
	float DistanceCheckInterval = 0.5f;

	// === 3D 상호작용 위젯 프로퍼티 ===

	/** 3D 상호작용 위젯 컴포넌트 */
	UPROPERTY()
	TObjectPtr<UWidgetComponent> InteractWidgetComp;

	/** 3D 위젯 클래스 (에디터에서 할당) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "제작|위젯",
		meta = (DisplayName = "3D Interact Widget Class (3D 상호작용 위젯 클래스)"))
	TSubclassOf<UInv_InteractPromptWidget> InteractWidgetClass;

	/** 3D 위젯 인스턴스 (런타임) */
	UPROPERTY()
	TObjectPtr<UInv_InteractPromptWidget> InteractWidgetInstance;

	/** 위젯 표시 여부 */
	bool bInteractWidgetVisible = false;

	/** 3D 위젯 Z오프셋 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "제작|위젯",
		meta = (DisplayName = "Widget Height Offset (위젯 높이 오프셋)", ClampMin = "0", ClampMax = "300"))
	float InteractWidgetZOffset = 80.0f;

private:
	// 플레이어별 크래프팅 메뉴 맵 (멀티플레이 지원)
	UPROPERTY()
	TMap<TObjectPtr<APlayerController>, TObjectPtr<UUserWidget>> PlayerMenuMap;

	// 플레이어별 거리 체크 타이머 맵
	TMap<TObjectPtr<APlayerController>, FTimerHandle> PlayerTimerMap;

	// 특정 플레이어의 거리 체크 함수
	UFUNCTION()
	void CheckDistanceToPlayer(APlayerController* PC);

	// 특정 플레이어의 메뉴를 강제로 닫는 함수
	void ForceCloseMenu(APlayerController* PC);
};
