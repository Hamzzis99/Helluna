// Gihyeon's Inventory Project
// @author 김기현

// 기존 Inv_CraftingButton의 로직을 FInv_CraftingRecipe 기반으로 재구현
// 핵심 흐름: 클라이언트 공간 체크 -> Server_CraftItemWithMaterials RPC (서버에서 재료 차감 + 아이템 생성)

#include "Widgets/Crafting/Inv_TabbedCraftingEntry.h"
#include "Inventory.h"
#include "Components/Button.h"
#include "Components/Image.h"
#include "Components/TextBlock.h"
#include "Components/HorizontalBox.h"
#include "InventoryManagement/Components/Inv_InventoryComponent.h"
#include "InventoryManagement/Utils/Inv_InventoryStatics.h"
#include "Items/Inv_InventoryItem.h"
#include "Items/Components/Inv_ItemComponent.h"
#include "Items/Fragments/Inv_ItemFragment.h"
#include "Widgets/Inventory/InventoryBase/Inv_InventoryBase.h"
#include "Widgets/Inventory/Spatial/Inv_SpatialInventory.h"
#include "Widgets/Inventory/Spatial/Inv_InventoryGrid.h"
#include "Persistence/Inv_SaveGameMode.h"

void UInv_TabbedCraftingEntry::NativeOnInitialized()
{
	Super::NativeOnInitialized();

	if (IsValid(Button_Craft))
	{
		Button_Craft->OnClicked.AddUniqueDynamic(this, &ThisClass::OnCraftButtonClicked);
	}
}

void UInv_TabbedCraftingEntry::NativeConstruct()
{
	Super::NativeConstruct();

	// 초기 UI 업데이트
	RefreshMaterialUI();
	UpdateButtonState();
}

void UInv_TabbedCraftingEntry::InitFromRecipe(const FInv_CraftingRecipe& Recipe)
{
	CachedRecipe = Recipe;

	// 아이템 이름 설정
	if (IsValid(Text_ItemName))
	{
		Text_ItemName->SetText(Recipe.ItemName);
	}

	// 아이템 아이콘 설정
	if (IsValid(Image_ItemIcon) && IsValid(Recipe.ItemIcon))
	{
		Image_ItemIcon->SetBrushFromTexture(Recipe.ItemIcon);
	}

	// 재료 아이콘 설정
	if (IsValid(Image_Material1) && IsValid(Recipe.MaterialIcon1))
	{
		Image_Material1->SetBrushFromTexture(Recipe.MaterialIcon1);
	}

	if (IsValid(Image_Material2) && IsValid(Recipe.MaterialIcon2))
	{
		Image_Material2->SetBrushFromTexture(Recipe.MaterialIcon2);
	}

	if (IsValid(Image_Material3) && IsValid(Recipe.MaterialIcon3))
	{
		Image_Material3->SetBrushFromTexture(Recipe.MaterialIcon3);
	}

	// 재료 UI + 버튼 상태 초기화
	RefreshMaterialUI();
	UpdateButtonState();
}

void UInv_TabbedCraftingEntry::RefreshMaterialUI()
{
	UInv_InventoryComponent* InvComp = UInv_InventoryStatics::GetInventoryComponent(GetOwningPlayer());

	// [최적화] GetAllItems() 1회 호출, 3개 재료 동시 카운트 (기존 3회 → 1회)
	const bool bNeedMat1 = CachedRecipe.MaterialTag1.IsValid() && CachedRecipe.MaterialAmount1 > 0;
	const bool bNeedMat2 = CachedRecipe.MaterialTag2.IsValid() && CachedRecipe.MaterialAmount2 > 0;
	const bool bNeedMat3 = CachedRecipe.MaterialTag3.IsValid() && CachedRecipe.MaterialAmount3 > 0;

	int32 Count1 = 0, Count2 = 0, Count3 = 0;

	if (IsValid(InvComp) && (bNeedMat1 || bNeedMat2 || bNeedMat3))
	{
		const auto& AllItems = InvComp->GetInventoryList().GetAllItems();
		for (UInv_InventoryItem* Item : AllItems)
		{
			if (!IsValid(Item)) continue;

			const FGameplayTag ItemType = Item->GetItemManifest().GetItemType();
			const int32 StackCount = Item->GetTotalStackCount();

			if (bNeedMat1 && ItemType.MatchesTagExact(CachedRecipe.MaterialTag1))
			{
				Count1 += StackCount;
			}
			if (bNeedMat2 && ItemType.MatchesTagExact(CachedRecipe.MaterialTag2))
			{
				Count2 += StackCount;
			}
			if (bNeedMat3 && ItemType.MatchesTagExact(CachedRecipe.MaterialTag3))
			{
				Count3 += StackCount;
			}
		}
	}

	// === 재료 1 UI ===
	if (bNeedMat1)
	{
		if (IsValid(HorizontalBox_Material1))
		{
			HorizontalBox_Material1->SetVisibility(ESlateVisibility::Visible);
		}
		if (IsValid(Text_Material1Amount))
		{
			FString AmountText = FString::Printf(TEXT("%d/%d"), Count1, CachedRecipe.MaterialAmount1);
			Text_Material1Amount->SetText(FText::FromString(AmountText));
		}
	}
	else
	{
		if (IsValid(HorizontalBox_Material1))
		{
			HorizontalBox_Material1->SetVisibility(ESlateVisibility::Collapsed);
		}
	}

	// === 재료 2 UI ===
	if (bNeedMat2)
	{
		if (IsValid(HorizontalBox_Material2))
		{
			HorizontalBox_Material2->SetVisibility(ESlateVisibility::Visible);
		}
		if (IsValid(Text_Material2Amount))
		{
			FString AmountText = FString::Printf(TEXT("%d/%d"), Count2, CachedRecipe.MaterialAmount2);
			Text_Material2Amount->SetText(FText::FromString(AmountText));
		}
	}
	else
	{
		if (IsValid(HorizontalBox_Material2))
		{
			HorizontalBox_Material2->SetVisibility(ESlateVisibility::Collapsed);
		}
	}

	// === 재료 3 UI ===
	if (bNeedMat3)
	{
		if (IsValid(HorizontalBox_Material3))
		{
			HorizontalBox_Material3->SetVisibility(ESlateVisibility::Visible);
		}
		if (IsValid(Text_Material3Amount))
		{
			FString AmountText = FString::Printf(TEXT("%d/%d"), Count3, CachedRecipe.MaterialAmount3);
			Text_Material3Amount->SetText(FText::FromString(AmountText));
		}
	}
	else
	{
		if (IsValid(HorizontalBox_Material3))
		{
			HorizontalBox_Material3->SetVisibility(ESlateVisibility::Collapsed);
		}
	}

#if INV_DEBUG_CRAFT
	UE_LOG(LogTemp, Log, TEXT("TabbedCraftingEntry: 재료 UI 업데이트 (%s)"), *CachedRecipe.ItemName.ToString());
#endif

	UpdateButtonState();
}

bool UInv_TabbedCraftingEntry::UsesMaterial(const FGameplayTag& MaterialTag) const
{
	if (!MaterialTag.IsValid()) return false;

	return MaterialTag.MatchesTagExact(CachedRecipe.MaterialTag1)
		|| MaterialTag.MatchesTagExact(CachedRecipe.MaterialTag2)
		|| MaterialTag.MatchesTagExact(CachedRecipe.MaterialTag3);
}

bool UInv_TabbedCraftingEntry::HasRequiredMaterials() const
{
	UInv_InventoryComponent* InvComp = UInv_InventoryStatics::GetInventoryComponent(GetOwningPlayer());
	if (!IsValid(InvComp)) return false;

	// [최적화] GetAllItems() 1번만 호출, 루프 1번만 순회
	const bool bNeedMaterial1 = CachedRecipe.MaterialTag1.IsValid() && CachedRecipe.MaterialAmount1 > 0;
	const bool bNeedMaterial2 = CachedRecipe.MaterialTag2.IsValid() && CachedRecipe.MaterialAmount2 > 0;
	const bool bNeedMaterial3 = CachedRecipe.MaterialTag3.IsValid() && CachedRecipe.MaterialAmount3 > 0;

	// 필요한 재료가 없으면 바로 true 반환
	if (!bNeedMaterial1 && !bNeedMaterial2 && !bNeedMaterial3)
	{
		return true;
	}

	const TArray<UInv_InventoryItem*> AllItems = InvComp->GetInventoryList().GetAllItems();

	int32 TotalCount1 = 0;
	int32 TotalCount2 = 0;
	int32 TotalCount3 = 0;

	for (UInv_InventoryItem* Item : AllItems)
	{
		if (!IsValid(Item)) continue;

		const FGameplayTag ItemType = Item->GetItemManifest().GetItemType();
		const int32 StackCount = Item->GetTotalStackCount();

		if (bNeedMaterial1 && ItemType.MatchesTagExact(CachedRecipe.MaterialTag1))
		{
			TotalCount1 += StackCount;
		}
		if (bNeedMaterial2 && ItemType.MatchesTagExact(CachedRecipe.MaterialTag2))
		{
			TotalCount2 += StackCount;
		}
		if (bNeedMaterial3 && ItemType.MatchesTagExact(CachedRecipe.MaterialTag3))
		{
			TotalCount3 += StackCount;
		}
	}

	if (bNeedMaterial1 && TotalCount1 < CachedRecipe.MaterialAmount1)
	{
#if INV_DEBUG_CRAFT
		UE_LOG(LogTemp, Log, TEXT("TabbedCraftingEntry: 재료1 부족: %d/%d (%s)"),
			TotalCount1, CachedRecipe.MaterialAmount1, *CachedRecipe.MaterialTag1.ToString());
#endif
		return false;
	}

	if (bNeedMaterial2 && TotalCount2 < CachedRecipe.MaterialAmount2)
	{
#if INV_DEBUG_CRAFT
		UE_LOG(LogTemp, Log, TEXT("TabbedCraftingEntry: 재료2 부족: %d/%d (%s)"),
			TotalCount2, CachedRecipe.MaterialAmount2, *CachedRecipe.MaterialTag2.ToString());
#endif
		return false;
	}

	if (bNeedMaterial3 && TotalCount3 < CachedRecipe.MaterialAmount3)
	{
#if INV_DEBUG_CRAFT
		UE_LOG(LogTemp, Log, TEXT("TabbedCraftingEntry: 재료3 부족: %d/%d (%s)"),
			TotalCount3, CachedRecipe.MaterialAmount3, *CachedRecipe.MaterialTag3.ToString());
#endif
		return false;
	}

	return true;
}

void UInv_TabbedCraftingEntry::UpdateButtonState()
{
	if (!IsValid(Button_Craft)) return;

	const bool bHasMaterials = HasRequiredMaterials();
	Button_Craft->SetIsEnabled(bHasMaterials);
}

void UInv_TabbedCraftingEntry::OnCraftButtonClicked()
{
	// World 유효성 체크
	UWorld* World = GetWorld();
	if (!IsValid(World))
	{
#if INV_DEBUG_CRAFT
		UE_LOG(LogTemp, Error, TEXT("TabbedCraftingEntry: GetWorld() 실패!"));
#endif
		return;
	}

	// 쿨다운 체크 (연타 방지)
	const float CurrentTime = World->GetTimeSeconds();
	if (CurrentTime - LastCraftTime < CachedRecipe.CraftCooldown)
	{
#if INV_DEBUG_CRAFT
		UE_LOG(LogTemp, Warning, TEXT("TabbedCraftingEntry: 제작 쿨다운 중! (%s)"), *CachedRecipe.ItemName.ToString());
#endif
		return;
	}

	if (!HasRequiredMaterials())
	{
#if INV_DEBUG_CRAFT
		UE_LOG(LogTemp, Warning, TEXT("TabbedCraftingEntry: 재료 부족! (%s)"), *CachedRecipe.ItemName.ToString());
#endif
		return;
	}

	// 쿨다운 시간 기록
	LastCraftTime = CurrentTime;

#if INV_DEBUG_CRAFT
	UE_LOG(LogTemp, Warning, TEXT("TabbedCraftingEntry: 제작 시작 - %s"), *CachedRecipe.ItemName.ToString());
#endif

	// 즉시 버튼 비활성화 (연타 방지)
	if (IsValid(Button_Craft))
	{
		Button_Craft->SetIsEnabled(false);
	}

	// 쿨다운 후 버튼 상태 재검사 타이머 설정 (WeakThis 패턴)
	FTimerHandle CooldownTimerHandle;
	TWeakObjectPtr<UInv_TabbedCraftingEntry> WeakThis = this;
	World->GetTimerManager().SetTimer(
		CooldownTimerHandle,
		[WeakThis]()
		{
			if (!WeakThis.IsValid()) return;
			WeakThis->RefreshMaterialUI();
			WeakThis->UpdateButtonState();
#if INV_DEBUG_CRAFT
			UE_LOG(LogTemp, Log, TEXT("TabbedCraftingEntry: 제작 쿨다운 완료! 버튼 상태 재계산"));
#endif
		},
		CachedRecipe.CraftCooldown,
		false
	);

	// 제작 (서버에서 재료 차감 + 아이템 생성)
	AddCraftedItemToInventory();
}

void UInv_TabbedCraftingEntry::AddCraftedItemToInventory()
{
#if INV_DEBUG_CRAFT
	UE_LOG(LogTemp, Warning, TEXT("TabbedCraftingEntry: [CLIENT] AddCraftedItemToInventory 시작 (%s)"), *CachedRecipe.ItemName.ToString());
#endif

	// ResultActorClass 확인
	if (!CachedRecipe.ResultActorClass)
	{
#if INV_DEBUG_CRAFT
		UE_LOG(LogTemp, Error, TEXT("TabbedCraftingEntry: [CLIENT] ResultActorClass가 설정되지 않았습니다!"));
#endif
		return;
	}

	// PlayerController 가져오기
	APlayerController* PC = GetOwningPlayer();
	if (!IsValid(PC))
	{
#if INV_DEBUG_CRAFT
		UE_LOG(LogTemp, Error, TEXT("TabbedCraftingEntry: [CLIENT] PlayerController를 찾을 수 없습니다!"));
#endif
		return;
	}

	// InventoryComponent 가져오기
	UInv_InventoryComponent* InvComp = PC->FindComponentByClass<UInv_InventoryComponent>();
	if (!IsValid(InvComp))
	{
#if INV_DEBUG_CRAFT
		UE_LOG(LogTemp, Error, TEXT("TabbedCraftingEntry: [CLIENT] InventoryComponent를 찾을 수 없습니다!"));
#endif
		return;
	}

	// [최적화] CDO 기반 ItemManifest 추출 (SpawnActorDeferred 제거)
	// AInv_SaveGameMode::FindItemComponentTemplate — Blueprint SCS 노드까지 검색
	UInv_ItemComponent* ItemCompTemplate = AInv_SaveGameMode::FindItemComponentTemplate(CachedRecipe.ResultActorClass);
	if (!IsValid(ItemCompTemplate))
	{
#if INV_DEBUG_CRAFT
		UE_LOG(LogTemp, Error, TEXT("TabbedCraftingEntry: [CLIENT] CDO에서 ItemComponent를 찾을 수 없습니다!"));
#endif
		return;
	}

	FInv_ItemManifest ItemManifest = ItemCompTemplate->GetItemManifest();
	ItemManifest.BuildFragmentCache(); // Fragment 조회 O(1) 보장
	EInv_ItemCategory Category = ItemManifest.GetItemCategory();

	// StackableFragment에서 제작 개수 자동 읽기
	int32 CraftedAmount = CachedRecipe.CraftedAmount;

	const FInv_StackableFragment* StackableFragment = ItemManifest.GetFragmentOfType<FInv_StackableFragment>();
	if (StackableFragment)
	{
		// StackableFragment가 있으면 해당 StackCount 사용 (탄약 등)
		const int32 FragmentStackCount = StackableFragment->GetStackCount();
		if (FragmentStackCount > 1)
		{
			CraftedAmount = FragmentStackCount;
		}
#if INV_DEBUG_CRAFT
		UE_LOG(LogTemp, Warning, TEXT("TabbedCraftingEntry: [CLIENT] StackableFragment 감지! CraftedAmount=%d"), CraftedAmount);
#endif
	}

	// InventoryMenu 가져오기
	UInv_InventoryBase* InventoryMenu = InvComp->GetInventoryMenu();
	if (!IsValid(InventoryMenu))
	{
#if INV_DEBUG_CRAFT
		UE_LOG(LogTemp, Warning, TEXT("TabbedCraftingEntry: [CLIENT] InventoryMenu nullptr - 공간 체크 스킵, 서버로 전송"));
#endif
		InvComp->Server_CraftItemWithMaterials(
			CachedRecipe.ResultActorClass,
			CachedRecipe.MaterialTag1, CachedRecipe.MaterialAmount1,
			CachedRecipe.MaterialTag2, CachedRecipe.MaterialAmount2,
			CachedRecipe.MaterialTag3, CachedRecipe.MaterialAmount3,
			CraftedAmount
		);
		return;
	}

	// SpatialInventory 캐스팅
	UInv_SpatialInventory* SpatialInv = Cast<UInv_SpatialInventory>(InventoryMenu);
	if (!IsValid(SpatialInv))
	{
#if INV_DEBUG_CRAFT
		UE_LOG(LogTemp, Warning, TEXT("TabbedCraftingEntry: [CLIENT] SpatialInventory 캐스팅 실패 - 공간 체크 스킵"));
#endif
		InvComp->Server_CraftItemWithMaterials(
			CachedRecipe.ResultActorClass,
			CachedRecipe.MaterialTag1, CachedRecipe.MaterialAmount1,
			CachedRecipe.MaterialTag2, CachedRecipe.MaterialAmount2,
			CachedRecipe.MaterialTag3, CachedRecipe.MaterialAmount3,
			CraftedAmount
		);
		return;
	}

	// 카테고리에 맞는 Grid 가져오기
	UInv_InventoryGrid* TargetGrid = nullptr;
	switch (Category)
	{
	case EInv_ItemCategory::Equippable:
		TargetGrid = SpatialInv->GetGrid_Equippables();
		break;
	case EInv_ItemCategory::Consumable:
		TargetGrid = SpatialInv->GetGrid_Consumables();
		break;
	case EInv_ItemCategory::Craftable:
		TargetGrid = SpatialInv->GetGrid_Craftables();
		break;
	default:
#if INV_DEBUG_CRAFT
		UE_LOG(LogTemp, Warning, TEXT("TabbedCraftingEntry: [CLIENT] 알 수 없는 카테고리: %d"), static_cast<int32>(Category));
#endif
		break;
	}

	if (!IsValid(TargetGrid))
	{
#if INV_DEBUG_CRAFT
		UE_LOG(LogTemp, Warning, TEXT("TabbedCraftingEntry: [CLIENT] TargetGrid nullptr - 공간 체크 스킵"));
#endif
		InvComp->Server_CraftItemWithMaterials(
			CachedRecipe.ResultActorClass,
			CachedRecipe.MaterialTag1, CachedRecipe.MaterialAmount1,
			CachedRecipe.MaterialTag2, CachedRecipe.MaterialAmount2,
			CachedRecipe.MaterialTag3, CachedRecipe.MaterialAmount3,
			CraftedAmount
		);
		return;
	}

	// 실제 UI Grid 상태 기반 공간 체크
	bool bHasRoom = TargetGrid->HasRoomInActualGrid(ItemManifest);

#if INV_DEBUG_CRAFT
	UE_LOG(LogTemp, Warning, TEXT("TabbedCraftingEntry: [CLIENT] 공간 체크 결과: %s (%s)"),
		bHasRoom ? TEXT("공간 있음") : TEXT("공간 없음"), *CachedRecipe.ItemName.ToString());
#endif

	if (!bHasRoom)
	{
		// 공간 없음! 서버 RPC 전송 X
#if INV_DEBUG_CRAFT
		UE_LOG(LogTemp, Warning, TEXT("TabbedCraftingEntry: [CLIENT] 인벤토리 공간 부족! 제작 취소"));
#endif
		InvComp->NoRoomInInventory.Broadcast();
		return;
	}

	// 공간 있음! 서버 RPC 호출
#if INV_DEBUG_CRAFT
	UE_LOG(LogTemp, Warning, TEXT("TabbedCraftingEntry: [CLIENT] 서버로 제작 요청 전송 (개수: %d)"), CraftedAmount);
#endif
	InvComp->Server_CraftItemWithMaterials(
		CachedRecipe.ResultActorClass,
		CachedRecipe.MaterialTag1, CachedRecipe.MaterialAmount1,
		CachedRecipe.MaterialTag2, CachedRecipe.MaterialAmount2,
		CachedRecipe.MaterialTag3, CachedRecipe.MaterialAmount3,
		CraftedAmount
	);
}
