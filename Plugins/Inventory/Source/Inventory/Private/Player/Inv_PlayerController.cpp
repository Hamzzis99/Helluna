#include "Player/Inv_PlayerController.h"
#include "Inventory.h"

#include "EnhancedInputComponent.h"
#include "EnhancedInputSubsystems.h"
#include "Interaction/Inv_Highlightable.h"
#include "Crafting/Interfaces/Inv_CraftingInterface.h"
#include "Crafting/Actors/Inv_CraftingStation.h"
#include "InventoryManagement/Components/Inv_InventoryComponent.h"
#include "EquipmentManagement/Components/Inv_EquipmentComponent.h"
#include "EquipmentManagement/EquipActor/Inv_EquipActor.h"
#include "Items/Components/Inv_ItemComponent.h"
#include "Kismet/GameplayStatics.h"
#include "Widgets/HUD/Inv_HUDWidget.h"
#include "Widgets/Inventory/InventoryBase/Inv_InventoryBase.h"
#include "Widgets/Inventory/Spatial/Inv_SpatialInventory.h"
#include "Widgets/Inventory/Spatial/Inv_InventoryGrid.h"
#include "Widgets/Inventory/GridSlots/Inv_EquippedGridSlot.h"
#include "Items/Inv_InventoryItem.h"
#include "Widgets/Inventory/SlottedItems/Inv_EquippedSlottedItem.h"
#include "InventoryManagement/Utils/Inv_InventoryStatics.h"
#include "Interfaces/Inv_Interface_Primary.cpp"

AInv_PlayerController::AInv_PlayerController()
{
	PrimaryActorTick.bCanEverTick = true;
	TraceLength = 500.0;
	ItemTraceChannel = ECC_GameTraceChannel1;
}

void AInv_PlayerController::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	TraceForInteractables();
}

void AInv_PlayerController::ToggleInventory()
{
	if (!InventoryComponent.IsValid()) return;
	InventoryComponent->ToggleInventoryMenu();

	if (InventoryComponent->IsMenuOpen())
	{
		HUDWidget->SetVisibility(ESlateVisibility::Hidden);
	}
	else
	{
		HUDWidget->SetVisibility(ESlateVisibility::HitTestInvisible);

		// ============================================
		// ⭐ [Phase 3 테스트] 인벤토리 닫을 때 Grid 상태 수집
		// ============================================
		// TODO: Phase 4 완료 후 이 코드 제거 (RPC로 대체)
#if INV_DEBUG_PLAYER
		UE_LOG(LogTemp, Warning, TEXT(""));
		UE_LOG(LogTemp, Warning, TEXT("🧪 [Phase 3 테스트] 인벤토리 닫힘 → CollectInventoryGridState() 호출"));
#endif

		TArray<FInv_SavedItemData> CollectedData = CollectInventoryGridState();

#if INV_DEBUG_PLAYER
		UE_LOG(LogTemp, Warning, TEXT("🧪 [Phase 3 테스트] 수집 완료! %d개 아이템"), CollectedData.Num());
#endif
	}
}

void AInv_PlayerController::BeginPlay()
{
	Super::BeginPlay();

#if INV_DEBUG_PLAYER
	UE_LOG(LogTemp, Warning, TEXT(""));
	UE_LOG(LogTemp, Warning, TEXT("╔════════════════════════════════════════════════════════════╗"));
	UE_LOG(LogTemp, Warning, TEXT("║         [Inv_PlayerController] BeginPlay                   ║"));
	UE_LOG(LogTemp, Warning, TEXT("╠════════════════════════════════════════════════════════════╣"));
	UE_LOG(LogTemp, Warning, TEXT("║ Controller: %s"), *GetName());
	UE_LOG(LogTemp, Warning, TEXT("║ IsLocalController: %s"), IsLocalController() ? TEXT("TRUE") : TEXT("FALSE"));
	UE_LOG(LogTemp, Warning, TEXT("║ NetMode: %d"), static_cast<int32>(GetNetMode()));
	UE_LOG(LogTemp, Warning, TEXT("║ Pawn: %s"), GetPawn() ? *GetPawn()->GetName() : TEXT("nullptr"));
	UE_LOG(LogTemp, Warning, TEXT("╚════════════════════════════════════════════════════════════╝"));
	UE_LOG(LogTemp, Warning, TEXT(""));
#endif

	UEnhancedInputLocalPlayerSubsystem* Subsystem = ULocalPlayer::GetSubsystem<UEnhancedInputLocalPlayerSubsystem>(GetLocalPlayer());
	if (IsValid(Subsystem))
	{
		for (UInputMappingContext* CurrentContext : DefaultIMCs)
		{
			Subsystem->AddMappingContext(CurrentContext, 0);
		}
	}

	InventoryComponent = FindComponentByClass<UInv_InventoryComponent>();
	EquipmentComponent = FindComponentByClass<UInv_EquipmentComponent>();

	if (EquipmentComponent.IsValid())
	{
#if INV_DEBUG_PLAYER
		UE_LOG(LogTemp, Warning, TEXT("[Inv_PlayerController] EquipmentComponent 찾음"));
#endif
	}

	CreateHUDWidget();
}

void AInv_PlayerController::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
#if INV_DEBUG_PLAYER
	UE_LOG(LogTemp, Warning, TEXT(""));
	UE_LOG(LogTemp, Warning, TEXT("╔════════════════════════════════════════════════════════════╗"));
	UE_LOG(LogTemp, Warning, TEXT("║ [Inv_PlayerController] EndPlay                             ║"));
	UE_LOG(LogTemp, Warning, TEXT("╠════════════════════════════════════════════════════════════╣"));
	UE_LOG(LogTemp, Warning, TEXT("║ Controller: %s"), *GetName());
	UE_LOG(LogTemp, Warning, TEXT("║ EndPlayReason: %d"), static_cast<int32>(EndPlayReason));
	UE_LOG(LogTemp, Warning, TEXT("║ HasAuthority: %s"), HasAuthority() ? TEXT("TRUE") : TEXT("FALSE"));
	UE_LOG(LogTemp, Warning, TEXT("╚════════════════════════════════════════════════════════════╝"));
#endif

	// ⭐ 서버에서만 처리 (인벤토리 저장 + 로그아웃)
	if (HasAuthority())
	{
		// 인벤토리 데이터 수집
		TArray<FInv_SavedItemData> CollectedData;

		if (InventoryComponent.IsValid())
		{
			CollectedData = InventoryComponent->CollectInventoryDataForSave();
#if INV_DEBUG_PLAYER
			UE_LOG(LogTemp, Warning, TEXT("[EndPlay] ✅ InventoryComponent에서 %d개 아이템 수집"), CollectedData.Num());
#endif

			// ============================================
			// 🆕 [Phase 6] EquipmentComponent에서 장착 상태 추가
			// ============================================
			if (EquipmentComponent.IsValid())
			{
				// 🔧 수정: SlotIndex → ItemType 맵 (같은 타입 다중 장착 지원)
				TMap<int32, FGameplayTag> SlotToItemMap;
				const TArray<TObjectPtr<AInv_EquipActor>>& EquippedActors = EquipmentComponent->GetEquippedActors();

#if INV_DEBUG_PLAYER
				UE_LOG(LogTemp, Warning, TEXT("[EndPlay] 🔍 [Phase 6] EquippedActors 개수: %d"), EquippedActors.Num());
#endif

				for (const TObjectPtr<AInv_EquipActor>& EquipActor : EquippedActors)
				{
					if (EquipActor.Get())
					{
						FGameplayTag ItemType = EquipActor->GetEquipmentType();
						int32 SlotIndex = EquipActor->GetWeaponSlotIndex();
						SlotToItemMap.Add(SlotIndex, ItemType);

#if INV_DEBUG_PLAYER
						UE_LOG(LogTemp, Warning, TEXT("[EndPlay]    ⚔️ 장착됨: Slot %d → %s"),
							SlotIndex, *ItemType.ToString());
#endif
					}
				}

				// CollectedData에 장착 상태 추가 (슬롯별로 매칭)
				int32 EquippedCount = 0;
				for (auto& Pair : SlotToItemMap)
				{
					int32 SlotIndex = Pair.Key;
					FGameplayTag& ItemType = Pair.Value;

					// 같은 ItemType이고 아직 장착 표시 안 된 아이템 찾기
					for (FInv_SavedItemData& Item : CollectedData)
					{
						if (Item.ItemType == ItemType && !Item.bEquipped)
						{
							Item.bEquipped = true;
							Item.WeaponSlotIndex = SlotIndex;
							EquippedCount++;

#if INV_DEBUG_PLAYER
							UE_LOG(LogTemp, Warning, TEXT("[EndPlay]    ✅ %s → bEquipped=true, WeaponSlotIndex=%d"),
								*Item.ItemType.ToString(), SlotIndex);
#endif
							break;  // 하나만 매칭하고 다음 슬롯으로
						}
					}
				}

#if INV_DEBUG_PLAYER
				UE_LOG(LogTemp, Warning, TEXT("[EndPlay] 🆕 [Phase 6] 장착 상태 추가 완료: %d개"), EquippedCount);
#endif
			}
			else
			{
#if INV_DEBUG_PLAYER
				UE_LOG(LogTemp, Warning, TEXT("[EndPlay] ⚠️ EquipmentComponent가 nullptr - 장착 상태 추가 불가"));
#endif
			}
		}
		else
		{
#if INV_DEBUG_PLAYER
			UE_LOG(LogTemp, Warning, TEXT("[EndPlay] ⚠️ InventoryComponent가 nullptr"));
#endif
		}

		// 델리게이트 브로드캐스트 (GameMode에서 저장 + 로그아웃 처리)
		if (OnControllerEndPlay.IsBound())
		{
#if INV_DEBUG_PLAYER
			UE_LOG(LogTemp, Warning, TEXT("[EndPlay] ✅ OnControllerEndPlay 델리게이트 브로드캐스트"));
#endif
			OnControllerEndPlay.Broadcast(this, CollectedData);
		}
		else
		{
#if INV_DEBUG_PLAYER
			UE_LOG(LogTemp, Warning, TEXT("[EndPlay] ⚠️ OnControllerEndPlay 바인딩 안 됨"));
#endif
		}
	}

#if INV_DEBUG_PLAYER
	UE_LOG(LogTemp, Warning, TEXT(""));
#endif

	Super::EndPlay(EndPlayReason);
}

void AInv_PlayerController::SetupInputComponent()
{
	Super::SetupInputComponent();

	UEnhancedInputComponent* EnhancedInputComponent = CastChecked<UEnhancedInputComponent>(InputComponent);

	EnhancedInputComponent->BindAction(PrimaryInteractAction, ETriggerEvent::Started, this, &AInv_PlayerController::PrimaryInteract);
	EnhancedInputComponent->BindAction(ToggleInventoryAction, ETriggerEvent::Started, this, &AInv_PlayerController::ToggleInventory);

	if (PrimaryWeaponAction)
	{
		EnhancedInputComponent->BindAction(PrimaryWeaponAction, ETriggerEvent::Started, this, &AInv_PlayerController::HandlePrimaryWeapon);
	}

	if (SecondaryWeaponAction)
	{
		EnhancedInputComponent->BindAction(SecondaryWeaponAction, ETriggerEvent::Started, this, &AInv_PlayerController::HandleSecondaryWeapon);
	}
}

void AInv_PlayerController::PrimaryInteract()
{
	if (!ThisActor.IsValid()) return;

	if (ThisActor->Implements<UInv_Interface_Primary>())
	{
		Server_Interact(ThisActor.Get());
		return;
	}

	if (CurrentCraftingStation.IsValid() && CurrentCraftingStation == ThisActor)
	{
		if (ThisActor->Implements<UInv_CraftingInterface>())
		{
			IInv_CraftingInterface::Execute_OnInteract(ThisActor.Get(), this);
			return;
		}
	}

	UInv_ItemComponent* ItemComp = ThisActor->FindComponentByClass<UInv_ItemComponent>();
	if (!IsValid(ItemComp) || !InventoryComponent.IsValid()) return;

	InventoryComponent->TryAddItem(ItemComp);
}

void AInv_PlayerController::Server_Interact_Implementation(AActor* TargetActor)
{
	if (!TargetActor) return;

	if (TargetActor->Implements<UInv_Interface_Primary>())
	{
		IInv_Interface_Primary::Execute_ExecuteInteract(TargetActor, this);
	}
}

void AInv_PlayerController::CreateHUDWidget()
{
	if (!IsLocalController()) return;

	if (!HUDWidgetClass)
	{
#if INV_DEBUG_PLAYER
		UE_LOG(LogTemp, Warning, TEXT("[Inv_PlayerController] HUDWidgetClass가 설정되지 않음"));
#endif
		return;
	}

	if (!HUDWidget)
	{
		HUDWidget = CreateWidget<UInv_HUDWidget>(this, HUDWidgetClass);
		if (IsValid(HUDWidget))
		{
			HUDWidget->AddToViewport();
#if INV_DEBUG_PLAYER
			UE_LOG(LogTemp, Warning, TEXT("[Inv_PlayerController] HUD 위젯 생성됨"));
#endif
		}
	}
}

void AInv_PlayerController::TraceForInteractables()
{
	if (!IsValid(GEngine) || !IsValid(GEngine->GameViewport)) return;
	FVector2D ViewportSize;
	GEngine->GameViewport->GetViewportSize(ViewportSize);
	const FVector2D ViewportCenter = ViewportSize / 2.f;

	FVector TraceStart;
	FVector Forward;
	if (!UGameplayStatics::DeprojectScreenToWorld(this, ViewportCenter, TraceStart, Forward)) return;

	const FVector TraceEnd = TraceStart + (Forward * TraceLength);
	FHitResult HitResult;
	GetWorld()->LineTraceSingleByChannel(HitResult, TraceStart, TraceEnd, ItemTraceChannel);

	LastActor = ThisActor;
	ThisActor = HitResult.GetActor();

	bool bIsCraftingStation = false;
	if (ThisActor.IsValid() && ThisActor->Implements<UInv_CraftingInterface>())
	{
		CurrentCraftingStation = ThisActor;
		bIsCraftingStation = true;
	}
	else
	{
		CurrentCraftingStation = nullptr;
	}

	if (!ThisActor.IsValid())
	{
		if (IsValid(HUDWidget))
		{
			HUDWidget->HidePickupMessage();
		}
		return;
	}

	if (ThisActor == LastActor) return;

	if (ThisActor.IsValid())
	{
		if (UActorComponent* Highlightable = ThisActor->FindComponentByInterface(UInv_Highlightable::StaticClass()); IsValid(Highlightable))
		{
			IInv_Highlightable::Execute_Highlight(Highlightable);
		}

		if (IsValid(HUDWidget))
		{
			if (bIsCraftingStation)
			{
				AInv_CraftingStation* CraftingStation = Cast<AInv_CraftingStation>(ThisActor.Get());
				if (IsValid(CraftingStation))
				{
					HUDWidget->ShowPickupMessage(CraftingStation->GetPickupMessage());
				}
			}
			else
			{
				UInv_ItemComponent* ItemComponent = ThisActor->FindComponentByClass<UInv_ItemComponent>();
				if (IsValid(ItemComponent))
				{
					HUDWidget->ShowPickupMessage(ItemComponent->GetPickupMessage());
				}
			}
		}
	}

	if (LastActor.IsValid())
	{
		if (UActorComponent* Highlightable = LastActor->FindComponentByInterface(UInv_Highlightable::StaticClass()); IsValid(Highlightable))
		{
			IInv_Highlightable::Execute_UnHighlight(Highlightable);
		}
	}
}

void AInv_PlayerController::HandlePrimaryWeapon()
{
	if (EquipmentComponent.IsValid())
	{
		EquipmentComponent->HandlePrimaryWeaponInput();
	}
}

void AInv_PlayerController::HandleSecondaryWeapon()
{
	if (EquipmentComponent.IsValid())
	{
		EquipmentComponent->HandleSecondaryWeaponInput();
	}
}

// ============================================
// 📌 인벤토리 저장/로드용 함수 (Phase 3)
// ============================================

/**
 * 현재 클라이언트 UI의 인벤토리 Grid 상태를 수집
 */
TArray<FInv_SavedItemData> AInv_PlayerController::CollectInventoryGridState()
{
	TArray<FInv_SavedItemData> Result;

#if INV_DEBUG_PLAYER
	UE_LOG(LogTemp, Warning, TEXT(""));
	UE_LOG(LogTemp, Warning, TEXT("╔══════════════════════════════════════════════════════════════════════════════╗"));
	UE_LOG(LogTemp, Warning, TEXT("║          [Phase 3] CollectInventoryGridState() - 인벤토리 상태 수집           ║"));
	UE_LOG(LogTemp, Warning, TEXT("╠══════════════════════════════════════════════════════════════════════════════╣"));
	UE_LOG(LogTemp, Warning, TEXT("║ 📍 호출 위치: 클라이언트                                                      ║"));
	UE_LOG(LogTemp, Warning, TEXT("║ 📍 목적: UI의 Grid 상태를 수집하여 서버로 전송할 데이터 생성                   ║"));
	UE_LOG(LogTemp, Warning, TEXT("╚══════════════════════════════════════════════════════════════════════════════╝"));
#endif

	// ============================================
	// Step 1: InventoryComponent 유효성 검사
	// ============================================
#if INV_DEBUG_PLAYER
	UE_LOG(LogTemp, Warning, TEXT(""));
	UE_LOG(LogTemp, Warning, TEXT("▶ [Step 1] InventoryComponent 확인"));
#endif

	if (!InventoryComponent.IsValid())
	{
#if INV_DEBUG_PLAYER
		UE_LOG(LogTemp, Error, TEXT("  ❌ InventoryComponent가 nullptr입니다!"));
		UE_LOG(LogTemp, Error, TEXT("     → PlayerController에 InventoryComponent가 없거나 초기화 안 됨"));
#endif
		return Result;
	}
#if INV_DEBUG_PLAYER
	UE_LOG(LogTemp, Warning, TEXT("  ✅ InventoryComponent 유효함"));
#endif

	// ============================================
	// Step 2: InventoryMenu 가져오기
	// ============================================
#if INV_DEBUG_PLAYER
	UE_LOG(LogTemp, Warning, TEXT(""));
	UE_LOG(LogTemp, Warning, TEXT("▶ [Step 2] InventoryMenu(위젯) 가져오기"));
#endif

	UInv_InventoryBase* InventoryMenu = InventoryComponent->GetInventoryMenu();
	if (!IsValid(InventoryMenu))
	{
#if INV_DEBUG_PLAYER
		UE_LOG(LogTemp, Error, TEXT("  ❌ InventoryMenu가 nullptr입니다!"));
		UE_LOG(LogTemp, Error, TEXT("     → 인벤토리 위젯이 생성되지 않았거나 파괴됨"));
		UE_LOG(LogTemp, Error, TEXT("     → InventoryComponent::BeginPlay()에서 위젯 생성 확인 필요"));
#endif
		return Result;
	}
#if INV_DEBUG_PLAYER
	UE_LOG(LogTemp, Warning, TEXT("  ✅ InventoryMenu 유효함: %s"), *InventoryMenu->GetName());
#endif

	// ============================================
	// Step 3: SpatialInventory로 캐스트
	// ============================================
#if INV_DEBUG_PLAYER
	UE_LOG(LogTemp, Warning, TEXT(""));
	UE_LOG(LogTemp, Warning, TEXT("▶ [Step 3] SpatialInventory로 캐스트"));
#endif

	UInv_SpatialInventory* SpatialInventory = Cast<UInv_SpatialInventory>(InventoryMenu);
	if (!IsValid(SpatialInventory))
	{
#if INV_DEBUG_PLAYER
		UE_LOG(LogTemp, Error, TEXT("  ❌ SpatialInventory로 캐스트 실패!"));
		UE_LOG(LogTemp, Error, TEXT("     → InventoryMenu 클래스: %s"), *InventoryMenu->GetClass()->GetName());
		UE_LOG(LogTemp, Error, TEXT("     → UInv_SpatialInventory 상속 확인 필요"));
#endif
		return Result;
	}
#if INV_DEBUG_PLAYER
	UE_LOG(LogTemp, Warning, TEXT("  ✅ SpatialInventory 캐스트 성공"));
#endif

	// ============================================
	// Step 4: 3개 Grid 접근 및 상태 수집
	// ============================================
#if INV_DEBUG_PLAYER
	UE_LOG(LogTemp, Warning, TEXT(""));
	UE_LOG(LogTemp, Warning, TEXT("▶ [Step 4] 3개 Grid에서 아이템 수집"));
	UE_LOG(LogTemp, Warning, TEXT("  ┌─────────────────────────────────────────────────────────────┐"));
#endif

	// Grid 배열 구성
	struct FGridInfo
	{
		UInv_InventoryGrid* Grid;
		const TCHAR* Name;
		uint8 Category;
	};

	FGridInfo Grids[] = {
		{ SpatialInventory->GetGrid_Equippables(),  TEXT("Grid_Equippables (장비)"),   0 },
		{ SpatialInventory->GetGrid_Consumables(), TEXT("Grid_Consumables (소모품)"), 1 },
		{ SpatialInventory->GetGrid_Craftables(),  TEXT("Grid_Craftables (재료)"),    2 }
	};

	int32 TotalCollected = 0;

	for (const FGridInfo& GridInfo : Grids)
	{
#if INV_DEBUG_PLAYER
		UE_LOG(LogTemp, Warning, TEXT("  │"));
		UE_LOG(LogTemp, Warning, TEXT("  ├─ [Grid %d] %s"), GridInfo.Category, GridInfo.Name);
#endif

		if (!IsValid(GridInfo.Grid))
		{
#if INV_DEBUG_PLAYER
			UE_LOG(LogTemp, Warning, TEXT("  │    ⚠️ Grid가 nullptr! 건너뜀"));
#endif
			continue;
		}

		// 각 Grid의 상태 수집
		TArray<FInv_SavedItemData> GridItems = GridInfo.Grid->CollectGridState();

#if INV_DEBUG_PLAYER
		UE_LOG(LogTemp, Warning, TEXT("  │    📦 수집된 아이템: %d개"), GridItems.Num());

		for (int32 i = 0; i < GridItems.Num(); ++i)
		{
			const FInv_SavedItemData& Item = GridItems[i];
			UE_LOG(LogTemp, Warning, TEXT("  │      [%d] %s x%d @ Pos(%d,%d)"),
				i, *Item.ItemType.ToString(), Item.StackCount,
				Item.GridPosition.X, Item.GridPosition.Y);
		}
#endif

		TotalCollected += GridItems.Num();
		Result.Append(GridItems);
	}

#if INV_DEBUG_PLAYER
	UE_LOG(LogTemp, Warning, TEXT("  │"));
	UE_LOG(LogTemp, Warning, TEXT("  └─────────────────────────────────────────────────────────────┘"));
#endif

	// ============================================
	// Step 5: 장착 슬롯(EquippedGridSlots) 상태 디버깅
	// ============================================
#if INV_DEBUG_PLAYER
	UE_LOG(LogTemp, Warning, TEXT(""));
	UE_LOG(LogTemp, Warning, TEXT("▶ [Step 5] 장착 슬롯(EquippedGridSlots) 상태 확인"));
	UE_LOG(LogTemp, Warning, TEXT("  ┌─────────────────────────────────────────────────────────────┐"));
	UE_LOG(LogTemp, Warning, TEXT("  │ ⚔️ 장착 슬롯 디버깅 (Phase 6 준비)                          │"));
	UE_LOG(LogTemp, Warning, TEXT("  ├─────────────────────────────────────────────────────────────┤"));
#endif

	const TArray<TObjectPtr<UInv_EquippedGridSlot>>& EquippedSlots = SpatialInventory->GetEquippedGridSlots();
#if INV_DEBUG_PLAYER
	UE_LOG(LogTemp, Warning, TEXT("  │ 총 장착 슬롯 개수: %d                                       │"), EquippedSlots.Num());
	UE_LOG(LogTemp, Warning, TEXT("  ├─────────────────────────────────────────────────────────────┤"));
#endif

	int32 EquippedItemCount = 0;
	for (int32 i = 0; i < EquippedSlots.Num(); ++i)
	{
		UInv_EquippedGridSlot* Slot = EquippedSlots[i].Get();
		if (!IsValid(Slot))
		{
#if INV_DEBUG_PLAYER
			UE_LOG(LogTemp, Warning, TEXT("  │ [%d] ❌ 슬롯 nullptr                                       │"), i);
#endif
			continue;
		}

		int32 WeaponSlotIndex = Slot->GetWeaponSlotIndex();
		UInv_InventoryItem* EquippedItem = Slot->GetInventoryItem().Get();

		if (IsValid(EquippedItem))
		{
			FGameplayTag ItemType = EquippedItem->GetItemManifest().GetItemType();
#if INV_DEBUG_PLAYER
			UE_LOG(LogTemp, Warning, TEXT("  │ [%d] ✅ WeaponSlot=%d │ %s"),
				i, WeaponSlotIndex, *ItemType.ToString());
#endif
			EquippedItemCount++;

			// ============================================
			// 🆕 Phase 6: 장착 아이템을 Result에 추가
			// ============================================
			FInv_SavedItemData EquippedData(
				EquippedItem->GetItemManifest().GetItemType(),
				1,  // 장비는 스택 1
				Slot->GetWeaponSlotIndex()
			);
			Result.Add(EquippedData);
#if INV_DEBUG_PLAYER
			UE_LOG(LogTemp, Warning, TEXT("  │      → ✅ Result에 추가됨!"));
#endif
		}
		else
		{
#if INV_DEBUG_PLAYER
			UE_LOG(LogTemp, Warning, TEXT("  │ [%d] ⬜ WeaponSlot=%d │ (비어있음)                        │"),
				i, WeaponSlotIndex);
#endif
		}
	}

#if INV_DEBUG_PLAYER
	UE_LOG(LogTemp, Warning, TEXT("  ├─────────────────────────────────────────────────────────────┤"));
	UE_LOG(LogTemp, Warning, TEXT("  │ 📊 장착된 아이템: %d개 → Result에 추가됨!                    │"), EquippedItemCount);
	UE_LOG(LogTemp, Warning, TEXT("  │ ✅ Phase 6 완료: 장착 아이템 저장 로직 구현됨                 │"));
	UE_LOG(LogTemp, Warning, TEXT("  └─────────────────────────────────────────────────────────────┘"));
#endif

	// ============================================
	// 최종 결과 출력
	// ============================================
#if INV_DEBUG_PLAYER
	UE_LOG(LogTemp, Warning, TEXT(""));
	UE_LOG(LogTemp, Warning, TEXT("╔══════════════════════════════════════════════════════════════════════════════╗"));
	UE_LOG(LogTemp, Warning, TEXT("║                        📊 수집 결과 요약                                      ║"));
	UE_LOG(LogTemp, Warning, TEXT("╠══════════════════════════════════════════════════════════════════════════════╣"));
	UE_LOG(LogTemp, Warning, TEXT("║ 총 수집된 아이템: %d개                                                        "), Result.Num());
	UE_LOG(LogTemp, Warning, TEXT("╠══════════════════════════════════════════════════════════════════════════════╣"));
#endif

	// 카테고리별 통계
	int32 EquipCount = 0, ConsumeCount = 0, CraftCount = 0;
	for (const FInv_SavedItemData& Item : Result)
	{
		switch (Item.GridCategory)
		{
			case 0: EquipCount++; break;
			case 1: ConsumeCount++; break;
			case 2: CraftCount++; break;
		}
	}

#if INV_DEBUG_PLAYER
	UE_LOG(LogTemp, Warning, TEXT("║ 장비(0):   %d개                                                               "), EquipCount);
	UE_LOG(LogTemp, Warning, TEXT("║ 소모품(1): %d개                                                               "), ConsumeCount);
	UE_LOG(LogTemp, Warning, TEXT("║ 재료(2):   %d개                                                               "), CraftCount);
	UE_LOG(LogTemp, Warning, TEXT("╠══════════════════════════════════════════════════════════════════════════════╣"));

	// 전체 아이템 목록
	for (int32 i = 0; i < Result.Num(); ++i)
	{
		const FInv_SavedItemData& Item = Result[i];
		UE_LOG(LogTemp, Warning, TEXT("║ [%02d] %s"), i, *Item.ToString());
	}

	UE_LOG(LogTemp, Warning, TEXT("╚══════════════════════════════════════════════════════════════════════════════╝"));
	UE_LOG(LogTemp, Warning, TEXT(""));
#endif

	return Result;
}

/**
 * 저장된 상태로 인벤토리 Grid 복원
 */
void AInv_PlayerController::RestoreInventoryFromState(const TArray<FInv_SavedItemData>& SavedItems)
{
#if INV_DEBUG_PLAYER
	UE_LOG(LogTemp, Warning, TEXT(""));
	UE_LOG(LogTemp, Warning, TEXT("╔══════════════════════════════════════════════════════════════════════════════╗"));
	UE_LOG(LogTemp, Warning, TEXT("║          [Phase 5] RestoreInventoryFromState() - 인벤토리 상태 복원           ║"));
	UE_LOG(LogTemp, Warning, TEXT("╠══════════════════════════════════════════════════════════════════════════════╣"));
	UE_LOG(LogTemp, Warning, TEXT("║ 📍 호출 위치: 클라이언트                                                      ║"));
	UE_LOG(LogTemp, Warning, TEXT("║ 📍 목적: 저장된 Grid 위치로 아이템 배치 복원                                   ║"));
	UE_LOG(LogTemp, Warning, TEXT("╚══════════════════════════════════════════════════════════════════════════════╝"));
#endif

	if (SavedItems.Num() == 0)
	{
#if INV_DEBUG_PLAYER
		UE_LOG(LogTemp, Warning, TEXT(""));
		UE_LOG(LogTemp, Warning, TEXT("⚠️ 복원할 아이템이 없습니다."));
#endif
		return;
	}

#if INV_DEBUG_PLAYER
	UE_LOG(LogTemp, Warning, TEXT(""));
	UE_LOG(LogTemp, Warning, TEXT("▶ 복원할 아이템 목록 (%d개):"), SavedItems.Num());
	UE_LOG(LogTemp, Warning, TEXT("  ┌─────────────────────────────────────────────────────────────┐"));

	for (int32 i = 0; i < SavedItems.Num(); ++i)
	{
		const FInv_SavedItemData& Item = SavedItems[i];
		UE_LOG(LogTemp, Warning, TEXT("  │ [%02d] %s"), i, *Item.ToString());
	}

	UE_LOG(LogTemp, Warning, TEXT("  └─────────────────────────────────────────────────────────────┘"));
#endif

	// ============================================
	// Step 1: InventoryComponent 접근
	// ============================================
#if INV_DEBUG_PLAYER
	UE_LOG(LogTemp, Warning, TEXT(""));
	UE_LOG(LogTemp, Warning, TEXT("▶ [Step 1] InventoryComponent 접근"));
#endif

	if (!InventoryComponent.IsValid())
	{
#if INV_DEBUG_PLAYER
		UE_LOG(LogTemp, Error, TEXT("   ❌ [실패] InventoryComponent가 유효하지 않습니다!"));
		UE_LOG(LogTemp, Error, TEXT("         Controller: %s"), *GetName());
		UE_LOG(LogTemp, Error, TEXT("         → BeginPlay()에서 InventoryComponent 초기화 확인"));
		UE_LOG(LogTemp, Error, TEXT("         → PlayerController BP에 Component 추가 확인"));
#endif
		return;
	}
#if INV_DEBUG_PLAYER
	UE_LOG(LogTemp, Warning, TEXT("   ✅ InventoryComponent 유효함"));
#endif

	// ============================================
	// Step 2: SpatialInventory 접근
	// ============================================
#if INV_DEBUG_PLAYER
	UE_LOG(LogTemp, Warning, TEXT(""));
	UE_LOG(LogTemp, Warning, TEXT("▶ [Step 2] SpatialInventory 접근"));
#endif

	UInv_InventoryBase* InventoryMenu = InventoryComponent->GetInventoryMenu();
	if (!IsValid(InventoryMenu))
	{
#if INV_DEBUG_PLAYER
		UE_LOG(LogTemp, Error, TEXT("   ❌ [실패] InventoryMenu가 nullptr!"));
		UE_LOG(LogTemp, Error, TEXT("         → InventoryComponent::BeginPlay()에서 위젯 생성 확인"));
		UE_LOG(LogTemp, Error, TEXT("         → InventoryMenuClass가 설정되어 있는지 확인"));
#endif
		return;
	}

	UInv_SpatialInventory* SpatialInventory = Cast<UInv_SpatialInventory>(InventoryMenu);
	if (!IsValid(SpatialInventory))
	{
#if INV_DEBUG_PLAYER
		UE_LOG(LogTemp, Error, TEXT("   ❌ [실패] SpatialInventory로 캐스트 실패!"));
		UE_LOG(LogTemp, Error, TEXT("         InventoryMenu 클래스: %s"), *InventoryMenu->GetClass()->GetName());
		UE_LOG(LogTemp, Error, TEXT("         → InventoryMenu가 UInv_SpatialInventory 상속 확인"));
#endif
		return;
	}

#if INV_DEBUG_PLAYER
	UE_LOG(LogTemp, Warning, TEXT("   ✅ SpatialInventory 접근 성공"));
#endif

	// ============================================
	// Step 3: 각 Grid에 위치 복원 요청
	// ============================================
#if INV_DEBUG_PLAYER
	UE_LOG(LogTemp, Warning, TEXT(""));
	UE_LOG(LogTemp, Warning, TEXT("▶ [Step 3] 각 Grid에 위치 복원 요청"));
#endif

	int32 TotalRestored = 0;

	// Grid 배열 구성
	struct FGridRestoreInfo
	{
		UInv_InventoryGrid* Grid;
		const TCHAR* Name;
	};

	FGridRestoreInfo Grids[] = {
		{ SpatialInventory->GetGrid_Equippables(),  TEXT("Grid_Equippables (장비)") },
		{ SpatialInventory->GetGrid_Consumables(), TEXT("Grid_Consumables (소모품)") },
		{ SpatialInventory->GetGrid_Craftables(),  TEXT("Grid_Craftables (재료)") }
	};

	for (const FGridRestoreInfo& GridInfo : Grids)
	{
#if INV_DEBUG_PLAYER
		UE_LOG(LogTemp, Warning, TEXT(""));
		UE_LOG(LogTemp, Warning, TEXT("   📦 %s"), GridInfo.Name);
#endif

		if (!IsValid(GridInfo.Grid))
		{
#if INV_DEBUG_PLAYER
			UE_LOG(LogTemp, Warning, TEXT("      ⚠️ Grid가 nullptr! 건너뜀"));
#endif
			continue;
		}

		int32 RestoredInGrid = GridInfo.Grid->RestoreItemPositions(SavedItems);
		TotalRestored += RestoredInGrid;

#if INV_DEBUG_PLAYER
		UE_LOG(LogTemp, Warning, TEXT("      → %d개 복원됨"), RestoredInGrid);
#endif
	}

	// ============================================
	// 최종 결과 요약
	// ============================================
#if INV_DEBUG_PLAYER
	UE_LOG(LogTemp, Warning, TEXT(""));
	UE_LOG(LogTemp, Warning, TEXT("  ┌─────────────────────────────────────────────────────────────┐"));
	UE_LOG(LogTemp, Warning, TEXT("  │ 📊 [Phase 5] Grid 위치 복원 결과                            │"));
	UE_LOG(LogTemp, Warning, TEXT("  ├─────────────────────────────────────────────────────────────┤"));
	UE_LOG(LogTemp, Warning, TEXT("  │ 요청: %3d개 아이템                                          │"), SavedItems.Num());
	UE_LOG(LogTemp, Warning, TEXT("  │ 복원: %3d개 성공 ✅                                         │"), TotalRestored);
#endif

	// ============================================
	// 🆕 [Phase 6] 장착 아이템 복원
	// ============================================
#if INV_DEBUG_PLAYER
	UE_LOG(LogTemp, Warning, TEXT("  ├─────────────────────────────────────────────────────────────┤"));
	UE_LOG(LogTemp, Warning, TEXT("  │ ⚔️ [Phase 6] 장착 아이템 복원 시작...                        │"));

	// 🔍 디버깅: 전체 SavedItems에서 bEquipped 상태 확인
	UE_LOG(LogTemp, Warning, TEXT("  │                                                              │"));
	UE_LOG(LogTemp, Warning, TEXT("  │ 🔍 [디버깅] SavedItems bEquipped 상태:                       │"));
#endif
	int32 EquippedCount = 0;
	for (int32 i = 0; i < SavedItems.Num(); ++i)
	{
		const FInv_SavedItemData& Item = SavedItems[i];
#if INV_DEBUG_PLAYER
		UE_LOG(LogTemp, Warning, TEXT("  │   [%d] %s"), i, *Item.ItemType.ToString());
		UE_LOG(LogTemp, Warning, TEXT("  │       bEquipped=%s, WeaponSlotIndex=%d, GridPos=(%d,%d)"),
			Item.bEquipped ? TEXT("TRUE ✅") : TEXT("false"),
			Item.WeaponSlotIndex,
			Item.GridPosition.X, Item.GridPosition.Y);
#endif
		if (Item.bEquipped) EquippedCount++;
	}
#if INV_DEBUG_PLAYER
	UE_LOG(LogTemp, Warning, TEXT("  │ 🔍 bEquipped=true 아이템: %d개                              │"), EquippedCount);
	UE_LOG(LogTemp, Warning, TEXT("  │                                                              │"));
#endif

	int32 EquippedRestored = 0;
	TSet<UInv_InventoryItem*> ProcessedEquipItems;  // 🆕 이미 장착 처리한 아이템 추적

	// SpatialInventory에서 장착 슬롯 가져오기
	// SpatialInventory는 이미 위에서 선언됨 - 유효성만 체크
	if (IsValid(SpatialInventory))
	{
		// 🆕 [Phase 7] EquippedGridSlots가 비어있으면 강제 수집
		SpatialInventory->CollectEquippedGridSlots();

		const TArray<TObjectPtr<UInv_EquippedGridSlot>>& EquippedSlots = SpatialInventory->GetEquippedGridSlots();
#if INV_DEBUG_PLAYER
		UE_LOG(LogTemp, Warning, TEXT("  │ 🔍 EquippedSlots 개수: %d                                   │"), EquippedSlots.Num());
#endif

		// 🆕 [Phase 7] 디버깅: EquippedSlots가 비어있으면 경고
		if (EquippedSlots.Num() == 0)
		{
#if INV_DEBUG_PLAYER
			UE_LOG(LogTemp, Error, TEXT("  │ ❌❌❌ [Phase 7] EquippedSlots가 비어있음! 위젯 초기화 문제! ❌❌❌ │"));
#endif
		}

		for (const FInv_SavedItemData& ItemData : SavedItems)
		{
			// 🔍 디버깅: 각 아이템의 bEquipped 체크
#if INV_DEBUG_PLAYER
			UE_LOG(LogTemp, Warning, TEXT("  │                                                              │"));
			UE_LOG(LogTemp, Warning, TEXT("  │   📌 처리 중: %s"), *ItemData.ItemType.ToString());
			UE_LOG(LogTemp, Warning, TEXT("  │       bEquipped=%s, WeaponSlotIndex=%d"),
				ItemData.bEquipped ? TEXT("TRUE") : TEXT("FALSE"), ItemData.WeaponSlotIndex);
#endif

			if (!ItemData.bEquipped)
			{
#if INV_DEBUG_PLAYER
				UE_LOG(LogTemp, Warning, TEXT("  │       → 건너뜀 (Grid 아이템)"));
#endif
				continue;  // Grid 아이템 건너뛰기
			}
			if (ItemData.WeaponSlotIndex < 0)
			{
#if INV_DEBUG_PLAYER
				UE_LOG(LogTemp, Warning, TEXT("  │       → 건너뜀 (유효하지 않은 슬롯 인덱스)"));
#endif
				continue;  // 유효하지 않은 슬롯
			}

#if INV_DEBUG_PLAYER
			UE_LOG(LogTemp, Warning, TEXT("  │   → ⚔️ 장착 복원 시도: %s (슬롯 %d)"),
				*ItemData.ItemType.ToString(), ItemData.WeaponSlotIndex);
#endif

			// 해당 WeaponSlotIndex를 가진 장착 슬롯 찾기
			UInv_EquippedGridSlot* TargetSlot = nullptr;
			for (const TObjectPtr<UInv_EquippedGridSlot>& Slot : EquippedSlots)
			{
				if (IsValid(Slot) && Slot->GetWeaponSlotIndex() == ItemData.WeaponSlotIndex)
				{
					TargetSlot = Slot.Get();
					break;
				}
			}

			if (!IsValid(TargetSlot))
			{
#if INV_DEBUG_PLAYER
				UE_LOG(LogTemp, Warning, TEXT("  │   ❌ WeaponSlot %d를 찾을 수 없음!"), ItemData.WeaponSlotIndex);
#endif
				continue;
			}
#if INV_DEBUG_PLAYER
			UE_LOG(LogTemp, Warning, TEXT("  │       ✅ TargetSlot 찾음: %s"), *TargetSlot->GetName());
#endif

			// InventoryComponent에서 해당 ItemType 아이템 찾기
#if INV_DEBUG_PLAYER
			UE_LOG(LogTemp, Warning, TEXT("  │       🔍 FindItemByType 호출: %s"), *ItemData.ItemType.ToString());
#endif
			UInv_InventoryItem* FoundItem = InventoryComponent->FindItemByTypeExcluding(ItemData.ItemType, ProcessedEquipItems);
			if (!IsValid(FoundItem))
			{
#if INV_DEBUG_PLAYER
				UE_LOG(LogTemp, Warning, TEXT("  │   ❌ 아이템을 찾을 수 없음: %s"), *ItemData.ItemType.ToString());
#endif
				continue;
			}
#if INV_DEBUG_PLAYER
			UE_LOG(LogTemp, Warning, TEXT("  │       ✅ FoundItem: %s"), *FoundItem->GetItemManifest().GetItemType().ToString());
#endif

			// 🆕 [Phase 6] SpatialInventory의 RestoreEquippedItem 사용 (델리게이트 바인딩 포함)
#if INV_DEBUG_PLAYER
			UE_LOG(LogTemp, Warning, TEXT("  │       🔧 RestoreEquippedItem 호출 (델리게이트 바인딩 포함)"));
#endif
			UInv_EquippedSlottedItem* EquippedSlottedItem = SpatialInventory->RestoreEquippedItem(TargetSlot, FoundItem);
			if (IsValid(EquippedSlottedItem))
			{
				EquippedRestored++;
#if INV_DEBUG_PLAYER
				UE_LOG(LogTemp, Warning, TEXT("  │   ✅ 장착 복원 성공: %s → 슬롯 %d"),
					*ItemData.ItemType.ToString(), ItemData.WeaponSlotIndex);
#endif

				// 🆕 [Phase 6] Grid에서 장착된 아이템 제거 (중복 저장 방지)
				UInv_InventoryGrid* EquipGrid = SpatialInventory->GetGrid_Equippables();
				if (IsValid(EquipGrid))
				{
					bool bRemoved = EquipGrid->RemoveSlottedItemByPointer(FoundItem);
					if (bRemoved)
					{
#if INV_DEBUG_PLAYER
						UE_LOG(LogTemp, Warning, TEXT("  │       🗑️ Grid에서 장착 아이템 제거 완료"));
#endif
					}
					else
					{
#if INV_DEBUG_PLAYER
						UE_LOG(LogTemp, Warning, TEXT("  │       ⚠️ Grid에서 아이템 제거 실패 (이미 없음?)"));
#endif
					}
				}

				// 장착 델리게이트 브로드캐스트 (무기 Actor 스폰용)
#if INV_DEBUG_PLAYER
				UE_LOG(LogTemp, Warning, TEXT("  │       📡 OnItemEquipped 브로드캐스트 (WeaponSlotIndex=%d)"), ItemData.WeaponSlotIndex);
#endif
				InventoryComponent->OnItemEquipped.Broadcast(FoundItem, ItemData.WeaponSlotIndex);

				// 🆕 이미 처리한 아이템으로 표시 (같은 타입 다중 장착 시 다른 아이템 찾기 위함)
				ProcessedEquipItems.Add(FoundItem);
			}
			else
			{
#if INV_DEBUG_PLAYER
				UE_LOG(LogTemp, Warning, TEXT("  │   ❌ OnItemEquipped 실패 (EquippedSlottedItem=nullptr)"));
#endif
			}
		}
	}
	else
	{
#if INV_DEBUG_PLAYER
		UE_LOG(LogTemp, Warning, TEXT("  │   ⚠️ SpatialInventory를 찾을 수 없음!"));
#endif
	}

#if INV_DEBUG_PLAYER
	UE_LOG(LogTemp, Warning, TEXT("  │                                                              │"));
	UE_LOG(LogTemp, Warning, TEXT("  │ ⚔️ 장착 아이템 복원 완료: %d개 (예상: %d개)                    │"), EquippedRestored, EquippedCount);
	UE_LOG(LogTemp, Warning, TEXT("  │ 실패: %3d개 ❌                                              │"), SavedItems.Num() - TotalRestored);
	UE_LOG(LogTemp, Warning, TEXT("  └─────────────────────────────────────────────────────────────┘"));
	UE_LOG(LogTemp, Warning, TEXT(""));
#endif
}

// ============================================
// 📌 인벤토리 저장 RPC 구현 (Phase 4)
// ============================================

/**
 * [서버 → 클라이언트] 인벤토리 상태 요청
 */
void AInv_PlayerController::Client_RequestInventoryState_Implementation()
{
#if INV_DEBUG_PLAYER
	UE_LOG(LogTemp, Warning, TEXT(""));
	UE_LOG(LogTemp, Warning, TEXT("╔══════════════════════════════════════════════════════════════════════════════╗"));
	UE_LOG(LogTemp, Warning, TEXT("║      [Phase 4] Client_RequestInventoryState - 서버로부터 요청 수신           ║"));
	UE_LOG(LogTemp, Warning, TEXT("╠══════════════════════════════════════════════════════════════════════════════╣"));
	UE_LOG(LogTemp, Warning, TEXT("║ 📍 실행 위치: 클라이언트                                                      ║"));
	UE_LOG(LogTemp, Warning, TEXT("║ 📍 요청자: 서버 (자동저장/로그아웃)                                           ║"));
	UE_LOG(LogTemp, Warning, TEXT("╚══════════════════════════════════════════════════════════════════════════════╝"));
#endif

	// Step 1: 인벤토리 상태 수집
#if INV_DEBUG_PLAYER
	UE_LOG(LogTemp, Warning, TEXT(""));
	UE_LOG(LogTemp, Warning, TEXT("▶ [Step 1] CollectInventoryGridState() 호출하여 UI 상태 수집..."));
#endif

	TArray<FInv_SavedItemData> CollectedData = CollectInventoryGridState();

	// Step 2: 서버로 전송
#if INV_DEBUG_PLAYER
	UE_LOG(LogTemp, Warning, TEXT(""));
	UE_LOG(LogTemp, Warning, TEXT("▶ [Step 2] Server_ReceiveInventoryState() RPC로 서버에 전송..."));
	UE_LOG(LogTemp, Warning, TEXT("   전송할 아이템: %d개"), CollectedData.Num());
#endif

	Server_ReceiveInventoryState(CollectedData);

#if INV_DEBUG_PLAYER
	UE_LOG(LogTemp, Warning, TEXT(""));
	UE_LOG(LogTemp, Warning, TEXT("✅ 클라이언트 → 서버 전송 완료!"));
	UE_LOG(LogTemp, Warning, TEXT("════════════════════════════════════════════════════════════════════════════════"));
#endif
}

/**
 * [클라이언트 → 서버] 수집된 인벤토리 상태 전송
 */
void AInv_PlayerController::Server_ReceiveInventoryState_Implementation(const TArray<FInv_SavedItemData>& SavedItems)
{
#if INV_DEBUG_PLAYER
	UE_LOG(LogTemp, Warning, TEXT(""));
	UE_LOG(LogTemp, Warning, TEXT("╔══════════════════════════════════════════════════════════════════════════════╗"));
	UE_LOG(LogTemp, Warning, TEXT("║      [Phase 4] Server_ReceiveInventoryState - 클라이언트로부터 수신          ║"));
	UE_LOG(LogTemp, Warning, TEXT("╠══════════════════════════════════════════════════════════════════════════════╣"));
	UE_LOG(LogTemp, Warning, TEXT("║ 📍 실행 위치: 서버                                                            ║"));
	UE_LOG(LogTemp, Warning, TEXT("║ 📍 전송자: 클라이언트 (%s)                                                    "), *GetName());
	UE_LOG(LogTemp, Warning, TEXT("╚══════════════════════════════════════════════════════════════════════════════╝"));

	UE_LOG(LogTemp, Warning, TEXT(""));
	UE_LOG(LogTemp, Warning, TEXT("▶ 수신된 아이템: %d개"), SavedItems.Num());
	UE_LOG(LogTemp, Warning, TEXT("  ┌─────────────────────────────────────────────────────────────┐"));

	for (int32 i = 0; i < SavedItems.Num(); ++i)
	{
		const FInv_SavedItemData& Item = SavedItems[i];
		UE_LOG(LogTemp, Warning, TEXT("  │ [%02d] %s"), i, *Item.ToString());
	}

	UE_LOG(LogTemp, Warning, TEXT("  └─────────────────────────────────────────────────────────────┘"));
#endif

	// 델리게이트 브로드캐스트 (GameMode에서 바인딩하여 저장 처리)
#if INV_DEBUG_PLAYER
	UE_LOG(LogTemp, Warning, TEXT(""));
	UE_LOG(LogTemp, Warning, TEXT("▶ OnInventoryStateReceived 델리게이트 브로드캐스트..."));
#endif

	if (OnInventoryStateReceived.IsBound())
	{
#if INV_DEBUG_PLAYER
		UE_LOG(LogTemp, Warning, TEXT("   ✅ 델리게이트 바인딩됨! 브로드캐스트 실행"));
#endif
		OnInventoryStateReceived.Broadcast(this, SavedItems);
	}
	else
	{
#if INV_DEBUG_PLAYER
		UE_LOG(LogTemp, Warning, TEXT("   ⚠️ 델리게이트 바인딩 안 됨! (GameMode에서 바인딩 필요)"));
#endif
	}

#if INV_DEBUG_PLAYER
	UE_LOG(LogTemp, Warning, TEXT(""));
	UE_LOG(LogTemp, Warning, TEXT("✅ 서버 수신 처리 완료!"));
	UE_LOG(LogTemp, Warning, TEXT("════════════════════════════════════════════════════════════════════════════════"));
#endif
}

// ============================================
// 📌 인벤토리 로드 RPC 구현 (Phase 5)
// ============================================

/**
 * [서버 → 클라이언트] 저장된 인벤토리 데이터 수신
 */
void AInv_PlayerController::Client_ReceiveInventoryData_Implementation(const TArray<FInv_SavedItemData>& SavedItems)
{
#if INV_DEBUG_PLAYER
	UE_LOG(LogTemp, Warning, TEXT(""));
	UE_LOG(LogTemp, Warning, TEXT("╔══════════════════════════════════════════════════════════════════════════════╗"));
	UE_LOG(LogTemp, Warning, TEXT("║      [Phase 5] Client_ReceiveInventoryData - 서버로부터 인벤토리 데이터 수신  ║"));
	UE_LOG(LogTemp, Warning, TEXT("╠══════════════════════════════════════════════════════════════════════════════╣"));
	UE_LOG(LogTemp, Warning, TEXT("║ 📍 실행 위치: 클라이언트                                                      ║"));
	UE_LOG(LogTemp, Warning, TEXT("║ 📍 수신된 아이템: %d개                                                        "), SavedItems.Num());
	UE_LOG(LogTemp, Warning, TEXT("╚══════════════════════════════════════════════════════════════════════════════╝"));
#endif

	if (SavedItems.Num() == 0)
	{
#if INV_DEBUG_PLAYER
		UE_LOG(LogTemp, Warning, TEXT(""));
		UE_LOG(LogTemp, Warning, TEXT("⚠️ 저장된 인벤토리 데이터가 없습니다. (신규 플레이어?)"));
		UE_LOG(LogTemp, Warning, TEXT("════════════════════════════════════════════════════════════════════════════════"));
#endif
		return;
	}

	// 수신된 아이템 목록 출력
#if INV_DEBUG_PLAYER
	UE_LOG(LogTemp, Warning, TEXT(""));
	UE_LOG(LogTemp, Warning, TEXT("▶ 수신된 아이템 목록:"));
	UE_LOG(LogTemp, Warning, TEXT("  ┌─────────────────────────────────────────────────────────────┐"));

	for (int32 i = 0; i < SavedItems.Num(); ++i)
	{
		const FInv_SavedItemData& Item = SavedItems[i];
		UE_LOG(LogTemp, Warning, TEXT("  │ [%02d] %s"), i, *Item.ToString());
	}

	UE_LOG(LogTemp, Warning, TEXT("  └─────────────────────────────────────────────────────────────┘"));
#endif

	// ============================================
	// FastArray 리플리케이션 완료 대기 후 Grid 위치 복원
	// ============================================
	// 서버에서 아이템이 추가되면 FastArray가 클라이언트로 리플리케이트됨
	// 리플리케이션 완료 후 Grid 위치를 복원해야 하므로 딜레이 필요
#if INV_DEBUG_PLAYER
	UE_LOG(LogTemp, Warning, TEXT(""));
	UE_LOG(LogTemp, Warning, TEXT("▶ 0.5초 후 Grid 위치 복원 예약..."));
	UE_LOG(LogTemp, Warning, TEXT("   (FastArray 리플리케이션 완료 대기)"));
#endif

	// SavedItems 복사본 생성 (타이머 람다에서 사용)
	TArray<FInv_SavedItemData> SavedItemsCopy = SavedItems;

	FTimerHandle TimerHandle;
	GetWorldTimerManager().SetTimer(TimerHandle, [this, SavedItemsCopy]()
	{
		DelayedRestoreGridPositions(SavedItemsCopy);
	}, 0.5f, false);

#if INV_DEBUG_PLAYER
	UE_LOG(LogTemp, Warning, TEXT("════════════════════════════════════════════════════════════════════════════════"));
#endif
}

/**
 * FastArray 리플리케이션 완료 후 Grid 위치 복원
 */
void AInv_PlayerController::DelayedRestoreGridPositions(const TArray<FInv_SavedItemData>& SavedItems)
{
#if INV_DEBUG_PLAYER
	UE_LOG(LogTemp, Warning, TEXT(""));
	UE_LOG(LogTemp, Warning, TEXT("╔══════════════════════════════════════════════════════════════════════════════╗"));
	UE_LOG(LogTemp, Warning, TEXT("║      [Phase 5] DelayedRestoreGridPositions - Grid 위치 복원 시작             ║"));
	UE_LOG(LogTemp, Warning, TEXT("╠══════════════════════════════════════════════════════════════════════════════╣"));
	UE_LOG(LogTemp, Warning, TEXT("║ 📍 실행 위치: 클라이언트                                                      ║"));
	UE_LOG(LogTemp, Warning, TEXT("║ 📍 복원할 아이템: %d개                                                        "), SavedItems.Num());
	UE_LOG(LogTemp, Warning, TEXT("╚══════════════════════════════════════════════════════════════════════════════╝"));
#endif

	// RestoreInventoryFromState 호출하여 Grid 위치 복원
	RestoreInventoryFromState(SavedItems);

#if INV_DEBUG_PLAYER
	UE_LOG(LogTemp, Warning, TEXT(""));
	UE_LOG(LogTemp, Warning, TEXT("🎉 [Phase 5] 인벤토리 로드 완료!"));
	UE_LOG(LogTemp, Warning, TEXT("════════════════════════════════════════════════════════════════════════════════"));
#endif
}

// ════════════════════════════════════════════════════════════════
// 🆕 [Phase 7.5] 현재 활성 무기의 EquipActor 반환
// ════════════════════════════════════════════════════════════════
// [2026-02-18] 작업자: 김기현
// ────────────────────────────────────────────────────────────────
// EquipmentComponent가 유효한 경우에만 GetActiveWeaponActor()를 호출.
// EquipmentComponent는 BeginPlay에서 FindComponentByClass로 캐싱됨.
//
// 반환값:
//   - 현재 손에 든 무기의 AInv_EquipActor* (소음기/스코프/레이저 효과 프로퍼티 포함)
//   - EquipmentComponent가 없거나 맨손이면 nullptr
// ════════════════════════════════════════════════════════════════
AInv_EquipActor* AInv_PlayerController::GetCurrentEquipActor() const
{
	if (EquipmentComponent.IsValid())
	{
		return EquipmentComponent->GetActiveWeaponActor();
	}
	return nullptr;
}
