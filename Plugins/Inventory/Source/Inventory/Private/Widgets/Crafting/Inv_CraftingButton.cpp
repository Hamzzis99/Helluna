// Gihyeon's Inventory Project

#include "Widgets/Crafting/Inv_CraftingButton.h"
#include "Components/Button.h"
#include "Components/Image.h"
#include "Components/TextBlock.h"
#include "Components/HorizontalBox.h"
#include "InventoryManagement/Components/Inv_InventoryComponent.h"
#include "InventoryManagement/Utils/Inv_InventoryStatics.h"
#include "Items/Inv_InventoryItem.h"
#include "Items/Components/Inv_ItemComponent.h"
#include "Items/Fragments/Inv_ItemFragment.h"

void UInv_CraftingButton::NativeOnInitialized()
{
	Super::NativeOnInitialized();

	if (IsValid(Button_Main))
	{
		Button_Main->OnClicked.AddDynamic(this, &ThisClass::OnButtonClicked);
	}
}

void UInv_CraftingButton::NativeConstruct()
{
	Super::NativeConstruct();

	// UI 초기 설정
	if (IsValid(Text_ItemName))
	{
		Text_ItemName->SetText(ItemName);
	}

	if (IsValid(Image_ItemIcon) && IsValid(ItemIcon))
	{
		Image_ItemIcon->SetBrushFromTexture(ItemIcon);
	}

	// 재료 아이콘 초기 설정 (Blueprint에서 설정한 값 사용)
	if (IsValid(Image_Material1) && IsValid(MaterialIcon1))
	{
		Image_Material1->SetBrushFromTexture(MaterialIcon1);
	}

	if (IsValid(Image_Material2) && IsValid(MaterialIcon2))
	{
		Image_Material2->SetBrushFromTexture(MaterialIcon2);
	}

	if (IsValid(Image_Material3) && IsValid(MaterialIcon3))
	{
		Image_Material3->SetBrushFromTexture(MaterialIcon3);
	}

	// 인벤토리 델리게이트 바인딩
	BindInventoryDelegates();

	// 재료 UI 업데이트 (이미지 표시/숨김)
	UpdateMaterialUI();

	// 초기 버튼 상태 업데이트
	UpdateButtonState();
}

void UInv_CraftingButton::NativeDestruct()
{
	Super::NativeDestruct();

	// 델리게이트 언바인딩
	UnbindInventoryDelegates();
}

void UInv_CraftingButton::NativeTick(const FGeometry& MyGeometry, float InDeltaTime)
{
	Super::NativeTick(MyGeometry, InDeltaTime);
}

void UInv_CraftingButton::SetCraftingInfo(const FText& Name, UTexture2D* Icon, TSubclassOf<AActor> ItemActorClassParam)
{
	ItemName = Name;
	ItemIcon = Icon;
	ItemActorClass = ItemActorClassParam;

	// UI 즉시 업데이트
	if (IsValid(Text_ItemName))
	{
		Text_ItemName->SetText(ItemName);
	}

	if (IsValid(Image_ItemIcon) && IsValid(ItemIcon))
	{
		Image_ItemIcon->SetBrushFromTexture(ItemIcon);
	}
}

void UInv_CraftingButton::OnButtonClicked()
{
	// 쿨다운 체크 (연타 방지)
	const float CurrentTime = GetWorld()->GetTimeSeconds();
	if (CurrentTime - LastCraftTime < CraftingCooldown)
	{
		UE_LOG(LogTemp, Warning, TEXT("⏱️ 제작 쿨다운 중! 남은 시간: %.2f초"), CraftingCooldown - (CurrentTime - LastCraftTime));
		return;
	}

	if (!HasRequiredMaterials())
	{
		UE_LOG(LogTemp, Warning, TEXT("재료가 부족합니다!"));
		return;
	}

	// 쿨다운 시간 기록
	LastCraftTime = CurrentTime;

	UE_LOG(LogTemp, Warning, TEXT("=== 아이템 제작 시작! ==="));
	UE_LOG(LogTemp, Warning, TEXT("아이템: %s"), *ItemName.ToString());

	// 재료 소비
	ConsumeMaterials();

	// 제작 완료 후 인벤토리에 아이템 추가
	AddCraftedItemToInventory();

	// 즉시 버튼 비활성화 (서버 응답 기다리지 않고)
	if (IsValid(Button_Main))
	{
		Button_Main->SetIsEnabled(false);
		UE_LOG(LogTemp, Log, TEXT("제작 버튼 즉시 비활성화 (중복 클릭 방지)"));
	}

	// 0.5초 후 강제로 UI 업데이트 (서버 동기화 대기)
	FTimerHandle UpdateTimerHandle;
	GetWorld()->GetTimerManager().SetTimer(UpdateTimerHandle, [this]()
	{
		UE_LOG(LogTemp, Warning, TEXT("타이머: 강제 UI 업데이트 실행!"));
		UpdateMaterialUI();
		UpdateButtonState();
	}, 0.5f, false);

	UE_LOG(LogTemp, Warning, TEXT("제작 완료!"));
}

bool UInv_CraftingButton::HasRequiredMaterials()
{
	UInv_InventoryComponent* InvComp = UInv_InventoryStatics::GetInventoryComponent(GetOwningPlayer());
	if (!IsValid(InvComp)) return false;

	// 재료 1 체크
	if (RequiredMaterialTag.IsValid() && RequiredAmount > 0)
	{
		int32 TotalCount = 0;
		const auto& AllItems = InvComp->GetInventoryList().GetAllItems();
		
		for (UInv_InventoryItem* Item : AllItems)
		{
			if (!IsValid(Item)) continue;
			if (!Item->GetItemManifest().GetItemType().MatchesTagExact(RequiredMaterialTag)) continue;
			
			TotalCount += Item->GetTotalStackCount();
		}

		if (TotalCount < RequiredAmount)
		{
			UE_LOG(LogTemp, Log, TEXT("재료1 부족: %d/%d (%s)"), TotalCount, RequiredAmount, *RequiredMaterialTag.ToString());
			return false;
		}
	}

	// 재료 2 체크
	if (RequiredMaterialTag2.IsValid() && RequiredAmount2 > 0)
	{
		int32 TotalCount = 0;
		const auto& AllItems = InvComp->GetInventoryList().GetAllItems();
		
		for (UInv_InventoryItem* Item : AllItems)
		{
			if (!IsValid(Item)) continue;
			if (!Item->GetItemManifest().GetItemType().MatchesTagExact(RequiredMaterialTag2)) continue;
			
			TotalCount += Item->GetTotalStackCount();
		}

		if (TotalCount < RequiredAmount2)
		{
			UE_LOG(LogTemp, Log, TEXT("재료2 부족: %d/%d (%s)"), TotalCount, RequiredAmount2, *RequiredMaterialTag2.ToString());
			return false;
		}
	}

	// 재료 3 체크
	if (RequiredMaterialTag3.IsValid() && RequiredAmount3 > 0)
	{
		int32 TotalCount = 0;
		const auto& AllItems = InvComp->GetInventoryList().GetAllItems();
		
		for (UInv_InventoryItem* Item : AllItems)
		{
			if (!IsValid(Item)) continue;
			if (!Item->GetItemManifest().GetItemType().MatchesTagExact(RequiredMaterialTag3)) continue;
			
			TotalCount += Item->GetTotalStackCount();
		}

		if (TotalCount < RequiredAmount3)
		{
			UE_LOG(LogTemp, Log, TEXT("재료3 부족: %d/%d (%s)"), TotalCount, RequiredAmount3, *RequiredMaterialTag3.ToString());
			return false;
		}
	}

	return true;
}

void UInv_CraftingButton::UpdateButtonState()
{
	if (!IsValid(Button_Main)) return;

	const bool bHasMaterials = HasRequiredMaterials();
	Button_Main->SetIsEnabled(bHasMaterials);

	UE_LOG(LogTemp, Log, TEXT("CraftingButton: 버튼 상태 업데이트 - %s"), bHasMaterials ? TEXT("활성화") : TEXT("비활성화"));
}

void UInv_CraftingButton::UpdateMaterialUI()
{
	UInv_InventoryComponent* InvComp = UInv_InventoryStatics::GetInventoryComponent(GetOwningPlayer());

	// === 재료 1 UI 업데이트 ===
	if (RequiredMaterialTag.IsValid() && RequiredAmount > 0)
	{
		// Material Tag가 있으면 컨테이너 Visible
		if (IsValid(HorizontalBox_Material1))
		{
			HorizontalBox_Material1->SetVisibility(ESlateVisibility::Visible);
		}
		
		// 재료 아이콘은 NativeConstruct에서 이미 설정했으므로 여기선 건드리지 않음!
		// (Blueprint에서 설정한 MaterialIcon1이 유지됨)

		// 개수 텍스트 업데이트 (실시간!)
		if (IsValid(Text_Material1Amount))
		{
			int32 CurrentAmount = 0;
			
			// 인벤토리에서 재료 개수 세기
			if (IsValid(InvComp))
			{
				const auto& AllItems = InvComp->GetInventoryList().GetAllItems();
				for (UInv_InventoryItem* Item : AllItems)
				{
					if (!IsValid(Item)) continue;
					if (!Item->GetItemManifest().GetItemType().MatchesTagExact(RequiredMaterialTag)) continue;
					CurrentAmount += Item->GetTotalStackCount();
				}
			}
			
			// 아이템이 없으면 CurrentAmount = 0 (위에서 초기화됨)
			FString AmountText = FString::Printf(TEXT("%d/%d"), CurrentAmount, RequiredAmount);
			Text_Material1Amount->SetText(FText::FromString(AmountText));
			UE_LOG(LogTemp, Log, TEXT("재료1 UI 업데이트: %s"), *AmountText);
		}
	}
	else
	{
		// Material Tag가 없으면 컨테이너 Hidden
		if (IsValid(HorizontalBox_Material1))
		{
			HorizontalBox_Material1->SetVisibility(ESlateVisibility::Hidden);
		}
	}

	// === 재료 2 UI 업데이트 ===
	if (RequiredMaterialTag2.IsValid() && RequiredAmount2 > 0)
	{
		// Material Tag2가 있으면 컨테이너 Visible
		if (IsValid(HorizontalBox_Material2))
		{
			HorizontalBox_Material2->SetVisibility(ESlateVisibility::Visible);
		}
		
		// 재료 아이콘은 NativeConstruct에서 이미 설정했으므로 여기선 건드리지 않음!

		// 개수 텍스트 업데이트 (실시간!)
		if (IsValid(Text_Material2Amount))
		{
			int32 CurrentAmount = 0;
			
			// 인벤토리에서 재료 개수 세기
			if (IsValid(InvComp))
			{
				const auto& AllItems = InvComp->GetInventoryList().GetAllItems();
				for (UInv_InventoryItem* Item : AllItems)
				{
					if (!IsValid(Item)) continue;
					if (!Item->GetItemManifest().GetItemType().MatchesTagExact(RequiredMaterialTag2)) continue;
					CurrentAmount += Item->GetTotalStackCount();
				}
			}
			
			// 아이템이 없으면 CurrentAmount = 0
			FString AmountText = FString::Printf(TEXT("%d/%d"), CurrentAmount, RequiredAmount2);
			Text_Material2Amount->SetText(FText::FromString(AmountText));
		}
	}
	else
	{
		// Material Tag2가 없으면 컨테이너 Hidden
		if (IsValid(HorizontalBox_Material2))
		{
			HorizontalBox_Material2->SetVisibility(ESlateVisibility::Hidden);
		}
	}

	// === 재료 3 UI 업데이트 ===
	if (RequiredMaterialTag3.IsValid() && RequiredAmount3 > 0)
	{
		// Material Tag3가 있으면 컨테이너 Visible
		if (IsValid(HorizontalBox_Material3))
		{
			HorizontalBox_Material3->SetVisibility(ESlateVisibility::Visible);
		}
		
		// 재료 아이콘은 NativeConstruct에서 이미 설정했으므로 여기선 건드리지 않음!

		// 개수 텍스트 업데이트 (실시간!)
		if (IsValid(Text_Material3Amount))
		{
			int32 CurrentAmount = 0;
			
			// 인벤토리에서 재료 개수 세기
			if (IsValid(InvComp))
			{
				const auto& AllItems = InvComp->GetInventoryList().GetAllItems();
				for (UInv_InventoryItem* Item : AllItems)
				{
					if (!IsValid(Item)) continue;
					if (!Item->GetItemManifest().GetItemType().MatchesTagExact(RequiredMaterialTag3)) continue;
					CurrentAmount += Item->GetTotalStackCount();
				}
			}
			
			// 아이템이 없으면 CurrentAmount = 0
			FString AmountText = FString::Printf(TEXT("%d/%d"), CurrentAmount, RequiredAmount3);
			Text_Material3Amount->SetText(FText::FromString(AmountText));
		}
	}
	else
	{
		// Material Tag3가 없으면 컨테이너 Hidden
		if (IsValid(HorizontalBox_Material3))
		{
			HorizontalBox_Material3->SetVisibility(ESlateVisibility::Hidden);
		}
	}

	UE_LOG(LogTemp, Log, TEXT("CraftingButton: 재료 UI 업데이트 완료"));
}

void UInv_CraftingButton::BindInventoryDelegates()
{
	UInv_InventoryComponent* InvComp = UInv_InventoryStatics::GetInventoryComponent(GetOwningPlayer());
	if (!IsValid(InvComp)) return;

	if (!InvComp->OnItemAdded.IsAlreadyBound(this, &ThisClass::OnInventoryItemAdded))
	{
		InvComp->OnItemAdded.AddDynamic(this, &ThisClass::OnInventoryItemAdded);
	}

	if (!InvComp->OnItemRemoved.IsAlreadyBound(this, &ThisClass::OnInventoryItemRemoved))
	{
		InvComp->OnItemRemoved.AddDynamic(this, &ThisClass::OnInventoryItemRemoved);
	}

	if (!InvComp->OnStackChange.IsAlreadyBound(this, &ThisClass::OnInventoryStackChanged))
	{
		InvComp->OnStackChange.AddDynamic(this, &ThisClass::OnInventoryStackChanged);
	}

	UE_LOG(LogTemp, Log, TEXT("CraftingButton: 인벤토리 델리게이트 바인딩 완료"));
}

void UInv_CraftingButton::UnbindInventoryDelegates()
{
	UInv_InventoryComponent* InvComp = UInv_InventoryStatics::GetInventoryComponent(GetOwningPlayer());
	if (!IsValid(InvComp)) return;

	InvComp->OnItemAdded.RemoveDynamic(this, &ThisClass::OnInventoryItemAdded);
	InvComp->OnItemRemoved.RemoveDynamic(this, &ThisClass::OnInventoryItemRemoved);
	InvComp->OnStackChange.RemoveDynamic(this, &ThisClass::OnInventoryStackChanged);
}

void UInv_CraftingButton::OnInventoryItemAdded(UInv_InventoryItem* Item)
{
	UE_LOG(LogTemp, Log, TEXT("CraftingButton: 아이템 추가됨! 버튼 상태 재계산..."));
	UpdateMaterialUI(); // 재료 UI 업데이트
	UpdateButtonState();
}

void UInv_CraftingButton::OnInventoryItemRemoved(UInv_InventoryItem* Item)
{
	UE_LOG(LogTemp, Warning, TEXT("=== CraftingButton: 아이템 제거됨! ==="));
	if (IsValid(Item))
	{
		UE_LOG(LogTemp, Warning, TEXT("제거된 아이템: %s"), *Item->GetItemManifest().GetItemType().ToString());
	}
	
	UpdateMaterialUI(); // 재료 UI 업데이트
	UpdateButtonState();
}

void UInv_CraftingButton::OnInventoryStackChanged(const FInv_SlotAvailabilityResult& Result)
{
	UE_LOG(LogTemp, Log, TEXT("CraftingButton: 스택 변경됨! 버튼 상태 재계산..."));
	UpdateMaterialUI(); // 재료 UI 업데이트
	UpdateButtonState();
}

void UInv_CraftingButton::ConsumeMaterials()
{
	UE_LOG(LogTemp, Warning, TEXT("=== ConsumeMaterials 호출됨 ==="));

	UInv_InventoryComponent* InvComp = UInv_InventoryStatics::GetInventoryComponent(GetOwningPlayer());
	if (!IsValid(InvComp))
	{
		UE_LOG(LogTemp, Error, TEXT("ConsumeMaterials: InventoryComponent not found!"));
		return;
	}

	// 재료 1 차감
	if (RequiredMaterialTag.IsValid() && RequiredAmount > 0)
	{
		UE_LOG(LogTemp, Warning, TEXT("재료1 차감: %s x %d"), *RequiredMaterialTag.ToString(), RequiredAmount);
		InvComp->Server_ConsumeMaterialsMultiStack(RequiredMaterialTag, RequiredAmount);
	}

	// 재료 2 차감
	if (RequiredMaterialTag2.IsValid() && RequiredAmount2 > 0)
	{
		UE_LOG(LogTemp, Warning, TEXT("재료2 차감: %s x %d"), *RequiredMaterialTag2.ToString(), RequiredAmount2);
		InvComp->Server_ConsumeMaterialsMultiStack(RequiredMaterialTag2, RequiredAmount2);
	}

	// 재료 3 차감
	if (RequiredMaterialTag3.IsValid() && RequiredAmount3 > 0)
	{
		UE_LOG(LogTemp, Warning, TEXT("재료3 차감: %s x %d"), *RequiredMaterialTag3.ToString(), RequiredAmount3);
		InvComp->Server_ConsumeMaterialsMultiStack(RequiredMaterialTag3, RequiredAmount3);
	}

	UE_LOG(LogTemp, Warning, TEXT("=== ConsumeMaterials 완료 ==="));
}

void UInv_CraftingButton::AddCraftedItemToInventory()
{
	UE_LOG(LogTemp, Warning, TEXT("=== [CLIENT] AddCraftedItemToInventory 시작 ==="));

	// ItemActorClass 확인
	if (!ItemActorClass)
	{
		UE_LOG(LogTemp, Error, TEXT("[CLIENT] ItemActorClass가 설정되지 않았습니다! Blueprint에서 제작할 아이템을 설정하세요."));
		return;
	}

	// InventoryComponent 가져오기
	APlayerController* PC = GetOwningPlayer();
	if (!IsValid(PC))
	{
		UE_LOG(LogTemp, Error, TEXT("[CLIENT] PlayerController를 찾을 수 없습니다!"));
		return;
	}

	UInv_InventoryComponent* InvComp = PC->FindComponentByClass<UInv_InventoryComponent>();
	if (!IsValid(InvComp))
	{
		UE_LOG(LogTemp, Error, TEXT("[CLIENT] InventoryComponent를 찾을 수 없습니다!"));
		return;
	}

	// 디버깅: Blueprint 정보 출력
	UE_LOG(LogTemp, Warning, TEXT("[CLIENT] 제작할 아이템 Blueprint: %s"), *ItemActorClass->GetName());

	// 서버 RPC 호출 (서버에서 안전하게 스폰)
	InvComp->Server_CraftItem(ItemActorClass);

	UE_LOG(LogTemp, Warning, TEXT("=== [CLIENT] 서버에 제작 요청 전송 완료 ==="));
}



