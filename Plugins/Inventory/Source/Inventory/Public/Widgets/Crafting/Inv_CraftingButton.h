// Gihyeon's Inventory Project

// ================================================================================================
// 📌 [Craftable 자원 차감 흐름 - 멀티플레이 환경에서 서버/클라이언트 동기화]
// ================================================================================================
//
// 이 파일은 크래프팅 버튼 위젯입니다.
// 제작 시 Craftable 아이템(재료)이 인벤토리에서 어떻게 차감되는지 설명합니다.
//
// ┌──────────────────────────────────────────────────────────────────────────────────────┐
// │ [전체 흐름 요약]                                                                     │
// │                                                                                      │
// │ 1. 클라이언트: 제작 버튼 클릭                                                        │
// │ 2. 클라이언트: 서버에 RPC 요청 전송 (재료 차감 요청)                                │
// │ 3. 서버: InventoryList에서 Craftable 아이템 검색 및 차감                            │
// │ 4. 서버: FastArray 리플리케이션 자동 실행 → 클라이언트로 전송                       │
// │ 5. 클라이언트: 리플리케이션 수신 → UI 슬롯 업데이트                                 │
// │ 6. 클라이언트: 델리게이트 → CraftingButton UI 업데이트 (재료 개수 텍스트 변경)      │
// └──────────────────────────────────────────────────────────────────────────────────────┘
//
// ================================================================================================
// [1단계] 클라이언트 - 제작 버튼 클릭 (OnButtonClicked)
// ================================================================================================
//
// 📍 위치: Inv_CraftingButton.cpp::OnButtonClicked()
// 🎯 실행 환경: 클라이언트
//
// [동작]
// 1. 쿨다운 체크 (연타 방지)
// 2. HasRequiredMaterials() - 로컬에서 재료 충분한지 체크
//    └─> InventoryList.GetAllItems() 순회
//    └─> GameplayTag가 일치하는 Craftable 아이템의 TotalStackCount 합산
//    └─> 예) GameItems.Craftables.FireFernFruit 개수 확인
// 3. ConsumeMaterials() 호출 → [2단계]로 이동
//
// ================================================================================================
// [2단계] 클라이언트 - 서버에 RPC 요청 전송 (ConsumeMaterials)
// ================================================================================================
//
// 📍 위치: Inv_CraftingButton.cpp::ConsumeMaterials()
// 🎯 실행 환경: 클라이언트
//
// [동작]
// 1. RequiredMaterialTag 확인 (예: GameItems.Craftables.FireFernFruit)
// 2. 각 재료마다 Server_ConsumeMaterialsMultiStack() RPC 호출
//    - 재료 1: Server_ConsumeMaterialsMultiStack(MaterialTag1, Amount1)
//    - 재료 2: Server_ConsumeMaterialsMultiStack(MaterialTag2, Amount2)
//    - 재료 3: Server_ConsumeMaterialsMultiStack(MaterialTag3, Amount3)
// 3. RPC 요청 전송 후 즉시 함수 종료 (서버 응답 기다리지 않음)
//
// ⚠️ 주의: 이 함수는 데이터를 수정하지 않습니다. 단지 서버에 요청만 보냅니다!
//
// ================================================================================================
// [3단계] 서버 - 인벤토리에서 재료 차감 (Server_ConsumeMaterialsMultiStack)
// ================================================================================================
//
// 📍 위치: Inv_InventoryComponent.cpp::Server_ConsumeMaterialsMultiStack_Implementation()
// 🎯 실행 환경: 서버 Only (HasAuthority() == true)
//
// [동작 - 여러 스택에서 자동 차감]
// 1. InventoryList.Entries 배열을 순회
// 2. MaterialTag와 일치하는 Craftable 아이템 검색
//    └─> Item->GetItemManifest().GetItemType().MatchesTagExact(MaterialTag)
// 3. 각 Entry에서 필요한 개수만큼 차감
//    예) 필요량 12개, 인벤토리: [10개, 5개, 3개]
//        → 첫 번째 스택: 10개 소진 (제거)
//        → 두 번째 스택: 5개 → 3개로 감소
//        → 세 번째 스택: 3개 그대로 유지
// 4. TotalStackCount 업데이트
//    └─> Entry.Item->SetTotalStackCount(NewCount)
//    └─> StackableFragment->SetStackCount(NewCount)
// 5. FastArray 변경 알림
//    └─> InventoryList.MarkItemDirty(Entry) ← 중요!
// 6. 0개가 된 Entry는 제거
//    └─> InventoryList.RemoveEntry(Item)
//
// [리플리케이션 자동 실행]
// - MarkItemDirty() 호출 시 언리얼 엔진이 자동으로 클라이언트에 전송
// - Multicast RPC 수동 호출 금지! (이중 차감 방지)
//
// ================================================================================================
// [4단계] 클라이언트 - FastArray 리플리케이션 수신 (PostReplicatedChange)
// ================================================================================================
//
// 📍 위치: Inv_FastArray.cpp::PostReplicatedChange()
// 🎯 실행 환경: 클라이언트 (자동 호출)
//
// [동작]
// 1. 언리얼 엔진이 서버로부터 FastArray 변경사항 수신
// 2. PostReplicatedChange 델리게이트 자동 호출
// 3. OnStackChange 델리게이트 브로드캐스트
//    └─> InventoryComponent->OnStackChange.Broadcast(Result)
// 4. 모든 구독자(Observer)에게 알림 전송
//
// ================================================================================================
// [5단계] 클라이언트 - 인벤토리 UI 슬롯 업데이트 (AddStacks)
// ================================================================================================
//
// 📍 위치: Inv_InventoryGrid.cpp::AddStacks()
// 🎯 실행 환경: 클라이언트
//
// [동작 - 여러 스택에 분산된 UI 업데이트]
// 1. 서버에서 받은 NewStackCount(서버 총량) 확인
// 2. 현재 UI 슬롯들의 StackCount 합산 (클라이언트 총량)
// 3. 차감량 계산: 클라이언트 총량 - 서버 총량
//    예) 서버: 10개, 클라이언트: 20개 → 10개 차감 필요
// 4. 매칭된 슬롯들을 큰 스택부터 정렬
// 5. 슬롯별로 차감량 분배
//    └─> GridSlot->SetStackCount(NewCount)
//    └─> SlottedItem->UpdateStackCount(NewCount) ← UI 아이콘 위 숫자 변경!
// 6. 0개가 된 슬롯은 UI에서 제거
//    └─> SlottedItem->RemoveFromParent()
//    └─> GridSlot 상태 초기화
//
// ================================================================================================
// [6단계] 클라이언트 - CraftingButton UI 업데이트 (OnInventoryStackChanged)
// ================================================================================================
//
// 📍 위치: Inv_CraftingButton.cpp::OnInventoryStackChanged()
// 🎯 실행 환경: 클라이언트
//
// [동작]
// 1. OnStackChange 델리게이트 수신
// 2. UpdateMaterialUI() 호출
//    └─> 인벤토리에서 현재 재료 개수 다시 조회
//    └─> Text_Material1Amount 텍스트 업데이트 (예: "20/10" → "8/10")
// 3. UpdateButtonState() 호출
//    └─> HasRequiredMaterials() 재검사
//    └─> 재료 부족 시 버튼 비활성화
//
// ================================================================================================
// [핵심 개념 정리]
// ================================================================================================
//
// 📌 Craftable 아이템이란?
// - GameItems.Craftables.* GameplayTag를 가진 아이템
// - 예) GameItems.Craftables.FireFernFruit, GameItems.Craftables.LuminDaisy
// - 인벤토리의 Craftables 그리드에 저장됨
//
// 📌 여러 스택에 나눠진 재료 처리
// - 서버: InventoryList.Entries를 순회하며 자동 합산
// - 클라이언트 UI: 매칭된 모든 슬롯을 찾아 분산 차감
// - 예) 10개 + 5개 + 3개 스택 → 12개 필요 시 첫 번째 10개 소진, 두 번째에서 2개 차감
//
// 📌 서버만 데이터를 수정합니다
// - 클라이언트는 절대 InventoryList를 직접 수정하지 않음
// - 모든 수정은 서버 RPC를 통해 요청
// - FastArray 리플리케이션으로 클라이언트에 자동 동기화
//
// 📌 UI 업데이트는 델리게이트로 자동화
// - OnStackChange 델리게이트를 구독하면 자동으로 알림 받음
// - 수동으로 Multicast RPC 호출 금지 (이중 차감 방지)
//
// ================================================================================================

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "GameplayTagContainer.h"
#include "Inv_CraftingButton.generated.h"

class UButton;
class UImage;
class UTextBlock;
class UHorizontalBox;
class UInv_InventoryItem;

/**
 * 크래프팅 메뉴에서 개별 아이템 제작 버튼 위젯
 * Building 시스템과 동일한 구조 사용
 */
UCLASS()
class INVENTORY_API UInv_CraftingButton : public UUserWidget
{
	GENERATED_BODY()

public:
	// 제작 아이템 정보 설정 (Blueprint에서 호출 가능)
	UFUNCTION(BlueprintCallable, Category = "Crafting")
	void SetCraftingInfo(const FText& Name, UTexture2D* Icon, TSubclassOf<AActor> ItemActorClass);

protected:
	virtual void NativeOnInitialized() override;
	virtual void NativeConstruct() override;
	virtual void NativeDestruct() override;
	virtual void NativeTick(const FGeometry& MyGeometry, float InDeltaTime) override;

private:
	// 버튼 클릭 이벤트
	UFUNCTION()
	void OnButtonClicked();

	// 재료 체크 함수 (Building과 동일)
	bool HasRequiredMaterials();
	void UpdateButtonState();
	
	// 재료 UI 업데이트 (이미지 표시/숨김)
	void UpdateMaterialUI();

	// 재료 소비 함수 (Building과 동일한 방식)
	void ConsumeMaterials();

	// 제작 완료 후 아이템을 인벤토리에 추가
	void AddCraftedItemToInventory();

	// 델리게이트 바인딩/언바인딩
	void BindInventoryDelegates();
	void UnbindInventoryDelegates();

	// 인벤토리 변경 시 호출될 콜백 함수들
	UFUNCTION()
	void OnInventoryItemAdded(UInv_InventoryItem* Item);

	UFUNCTION()
	void OnInventoryItemRemoved(UInv_InventoryItem* Item);

	UFUNCTION()
	void OnInventoryStackChanged(const FInv_SlotAvailabilityResult& Result);

	// === 블루프린트에서 바인딩할 위젯들 (meta = (BindWidget)) ===
	
	// 클릭 가능한 버튼
	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UButton> Button_Main;

	// 제작할 아이템 아이콘 이미지
	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UImage> Image_ItemIcon;

	// 제작할 아이템 이름 텍스트
	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UTextBlock> Text_ItemName;

	// === 재료 UI 컨테이너 및 위젯 (반드시 Blueprint에 있어야 함) ===
	
	// 재료 1 컨테이너 (HorizontalBox) - 이미지 + 텍스트를 감쌈
	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UHorizontalBox> HorizontalBox_Material1;

	// 재료 1 이미지
	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UImage> Image_Material1;

	// 재료 1 개수 텍스트
	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UTextBlock> Text_Material1Amount;

	// 재료 2 컨테이너 (HorizontalBox) - 이미지 + 텍스트를 감쌈
	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UHorizontalBox> HorizontalBox_Material2;

	// 재료 2 이미지
	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UImage> Image_Material2;

	// 재료 2 개수 텍스트
	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UTextBlock> Text_Material2Amount;

	// 재료 3 컨테이너 (HorizontalBox) - 이미지 + 텍스트를 감쌈
	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UHorizontalBox> HorizontalBox_Material3;

	// 재료 3 이미지
	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UImage> Image_Material3;

	// 재료 3 개수 텍스트
	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UTextBlock> Text_Material3Amount;

	// === 제작 아이템 정보 (블루프린트에서 설정 가능) ===
	
	// 제작할 아이템 이름
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Crafting", meta = (AllowPrivateAccess = "true"))
	FText ItemName;

	// 제작할 아이템 아이콘
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Crafting", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<UTexture2D> ItemIcon;

	// 제작할 아이템 액터 클래스 (PickUp Actor)
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Crafting", meta = (AllowPrivateAccess = "true"))
	TSubclassOf<AActor> ItemActorClass;

	// === 재료 정보 (Building과 동일한 구조) ===

	// 필요한 재료 1 태그 (Craftables 중 선택)
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Crafting|Materials", meta = (AllowPrivateAccess = "true", Categories = "GameItems.Craftables"))
	FGameplayTag RequiredMaterialTag;

	// 필요한 재료 1 개수
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Crafting|Materials", meta = (AllowPrivateAccess = "true"))
	int32 RequiredAmount = 0;

	// 필요한 재료 1 아이콘 (Blueprint에서 직접 설정)
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Crafting|Materials", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<UTexture2D> MaterialIcon1;

	// 필요한 재료 2 태그 (Craftables 중 선택, None이면 미사용)
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Crafting|Materials", meta = (AllowPrivateAccess = "true", Categories = "GameItems.Craftables"))
	FGameplayTag RequiredMaterialTag2;

	// 필요한 재료 2 개수
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Crafting|Materials", meta = (AllowPrivateAccess = "true"))
	int32 RequiredAmount2 = 0;

	// 필요한 재료 2 아이콘 (Blueprint에서 직접 설정)
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Crafting|Materials", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<UTexture2D> MaterialIcon2;

	// 필요한 재료 3 태그 (Craftables 중 선택, None이면 미사용)
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Crafting|Materials", meta = (AllowPrivateAccess = "true", Categories = "GameItems.Craftables"))
	FGameplayTag RequiredMaterialTag3;

	// 필요한 재료 3 개수
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Crafting|Materials", meta = (AllowPrivateAccess = "true"))
	int32 RequiredAmount3 = 0;

	// 필요한 재료 3 아이콘 (Blueprint에서 직접 설정)
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Crafting|Materials", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<UTexture2D> MaterialIcon3;

	// === 버튼 쿨다운 (연타 방지) ===

	// 제작 쿨다운 시간 (초 단위, 블루프린트에서 수정 가능)
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Crafting|Settings", meta = (AllowPrivateAccess = "true", ClampMin = "0.0", ClampMax = "5.0"))
	float CraftingCooldown = 0.5f;

	// 마지막 제작 시간 (내부 사용)
	float LastCraftTime = 0.f;
};

