// Gihyeon's Inventory Project

#include "Widgets/Building/Inv_BuildingButton.h"
#include "Inventory.h"
#include "Components/Button.h"
#include "Components/Image.h"
#include "Components/TextBlock.h"
#include "Components/HorizontalBox.h"
#include "Building/Components/Inv_BuildingComponent.h"
#include "InventoryManagement/Components/Inv_InventoryComponent.h"
#include "Items/Inv_InventoryItem.h"
#include "Items/Fragments/Inv_ItemFragment.h"
#include "GameFramework/PlayerController.h"

void UInv_BuildingButton::NativeOnInitialized()
{
	Super::NativeOnInitialized();

	// 버튼 클릭 이벤트 바인딩
	if (IsValid(Button_Main))
	{
		Button_Main->OnClicked.AddDynamic(this, &ThisClass::OnButtonClicked);
	}
	else
	{
#if INV_DEBUG_BUILD
		UE_LOG(LogTemp, Error, TEXT("Inv_BuildingButton: Button_Main is not bound! Make sure the widget has a Button named 'Button_Main'."));
#endif
	}
}

void UInv_BuildingButton::NativeConstruct()
{
	Super::NativeConstruct();

	// 위젯이 생성될 때 UI 업데이트
	if (IsValid(Text_BuildingName))
	{
		Text_BuildingName->SetText(BuildingName);
	}

	if (IsValid(Image_Icon) && IsValid(BuildingIcon))
	{
		Image_Icon->SetBrushFromTexture(BuildingIcon);
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

	// 델리게이트 바인딩 (인벤토리 변경 감지)
	BindInventoryDelegates();

	// 재료 UI 업데이트 (이미지 표시/숨김)
	UpdateMaterialUI();

	// 초기 버튼 상태 업데이트
	UpdateButtonState();
}

void UInv_BuildingButton::SetBuildingInfo(const FText& Name, UTexture2D* Icon, TSubclassOf<AActor> GhostClass, TSubclassOf<AActor> BuildingClass, int32 ID)
{
	BuildingName = Name;
	BuildingIcon = Icon;
	GhostActorClass = GhostClass;
	ActualBuildingClass = BuildingClass;
	BuildingID = ID;

	// UI 업데이트
	if (IsValid(Text_BuildingName))
	{
		Text_BuildingName->SetText(BuildingName);
	}

	if (IsValid(Image_Icon) && IsValid(BuildingIcon))
	{
		Image_Icon->SetBrushFromTexture(BuildingIcon);
	}
}

void UInv_BuildingButton::OnButtonClicked()
{
#if INV_DEBUG_BUILD
	UE_LOG(LogTemp, Warning, TEXT("=== BUILDING BUTTON CLICKED ==="));
	UE_LOG(LogTemp, Warning, TEXT("Building Name: %s"), *BuildingName.ToString());
	UE_LOG(LogTemp, Warning, TEXT("Building ID: %d"), BuildingID);
#endif

	// 재료 체크
	if (!HasRequiredMaterials())
	{
#if INV_DEBUG_BUILD
		UE_LOG(LogTemp, Warning, TEXT("=== 재료가 부족합니다! ==="));
		UE_LOG(LogTemp, Warning, TEXT("필요한 재료: %s x %d"), *RequiredMaterialTag.ToString(), RequiredAmount);
#endif
		// TODO: UI 팝업 표시
		return;
	}

	// PlayerController 가져오기
	APlayerController* PC = GetOwningPlayer();
	if (!IsValid(PC))
	{
#if INV_DEBUG_BUILD
		UE_LOG(LogTemp, Error, TEXT("Inv_BuildingButton: Owning Player is invalid!"));
#endif
		return;
	}

	// BuildingComponent 가져오기
	UInv_BuildingComponent* BuildingComp = PC->FindComponentByClass<UInv_BuildingComponent>();
	if (!IsValid(BuildingComp))
	{
#if INV_DEBUG_BUILD
		UE_LOG(LogTemp, Error, TEXT("Inv_BuildingButton: BuildingComponent not found on Player Controller!"));
#endif
		return;
	}

	// Component에 건물 선택 알림 (재료 정보 포함!)
	BuildingComp->OnBuildingSelectedFromWidget(
		GhostActorClass, 
		ActualBuildingClass, 
		BuildingID, 
		RequiredMaterialTag, 
		RequiredAmount,
		RequiredMaterialTag2,
		RequiredAmount2
	);
}

void UInv_BuildingButton::NativeTick(const FGeometry& MyGeometry, float InDeltaTime)
{
	Super::NativeTick(MyGeometry, InDeltaTime);

	// 매 프레임 업데이트 제거 → 델리게이트로 대체
	// 성능 최적화: 인벤토리 변경 시에만 UpdateButtonState() 호출됨
}

bool UInv_BuildingButton::HasRequiredMaterials()
{
	// PlayerController 가져오기
	APlayerController* PC = GetOwningPlayer();
	if (!IsValid(PC)) return false;

	// InventoryComponent 가져오기
	UInv_InventoryComponent* InvComp = PC->FindComponentByClass<UInv_InventoryComponent>();
	if (!IsValid(InvComp)) return false;

	// === 재료 1 체크 (모든 스택 합산!) ===
	if (RequiredMaterialTag.IsValid() && RequiredAmount > 0)
	{
		int32 TotalAmount1 = InvComp->GetTotalMaterialCount(RequiredMaterialTag);
		if (TotalAmount1 < RequiredAmount)
		{
#if INV_DEBUG_BUILD
			UE_LOG(LogTemp, Warning, TEXT("재료1 부족: %d/%d (%s)"), TotalAmount1, RequiredAmount, *RequiredMaterialTag.ToString());
#endif
			return false; // 재료1 개수 부족
		}
	}

	// === 재료 2 체크 (모든 스택 합산!) ===
	if (RequiredMaterialTag2.IsValid() && RequiredAmount2 > 0)
	{
		int32 TotalAmount2 = InvComp->GetTotalMaterialCount(RequiredMaterialTag2);
		if (TotalAmount2 < RequiredAmount2)
		{
#if INV_DEBUG_BUILD
			UE_LOG(LogTemp, Warning, TEXT("재료2 부족: %d/%d (%s)"), TotalAmount2, RequiredAmount2, *RequiredMaterialTag2.ToString());
#endif
			return false; // 재료2 개수 부족
		}
	}

	// === 재료 3 체크 (모든 스택 합산!) ===
	if (RequiredMaterialTag3.IsValid() && RequiredAmount3 > 0)
	{
		int32 TotalAmount3 = InvComp->GetTotalMaterialCount(RequiredMaterialTag3);
		if (TotalAmount3 < RequiredAmount3)
		{
#if INV_DEBUG_BUILD
			UE_LOG(LogTemp, Warning, TEXT("재료3 부족: %d/%d (%s)"), TotalAmount3, RequiredAmount3, *RequiredMaterialTag3.ToString());
#endif
			return false; // 재료3 개수 부족
		}
	}

	// 모든 재료 충족!
#if INV_DEBUG_BUILD
	UE_LOG(LogTemp, Log, TEXT("모든 재료 충족!"));
#endif
	return true;
}

void UInv_BuildingButton::UpdateButtonState()
{
	if (!IsValid(Button_Main)) return;

	bool bHasMaterials = HasRequiredMaterials();

	// 버튼 활성화/비활성화
	Button_Main->SetIsEnabled(bHasMaterials);

	// 텍스트 색상 변경 (재료 부족 시 빨간색)
	if (IsValid(Text_BuildingName))
	{
		FLinearColor TextColor = bHasMaterials ? FLinearColor::White : FLinearColor::Red;
		Text_BuildingName->SetColorAndOpacity(FSlateColor(TextColor));
	}
}

void UInv_BuildingButton::NativeDestruct()
{
	// 델리게이트 언바인딩 (메모리 누수 방지)
	UnbindInventoryDelegates();

	Super::NativeDestruct();
}

void UInv_BuildingButton::BindInventoryDelegates()
{
	// PlayerController 가져오기
	APlayerController* PC = GetOwningPlayer();
	if (!IsValid(PC)) return;

	// InventoryComponent 가져오기
	UInv_InventoryComponent* InvComp = PC->FindComponentByClass<UInv_InventoryComponent>();
	if (!IsValid(InvComp)) return;

	// 델리게이트 바인딩 (이미 바인딩되어 있으면 건너뜀)
	if (!InvComp->OnItemAdded.IsAlreadyBound(this, &ThisClass::OnInventoryItemAdded))
	{
		InvComp->OnItemAdded.AddDynamic(this, &ThisClass::OnInventoryItemAdded);
#if INV_DEBUG_BUILD
		UE_LOG(LogTemp, Log, TEXT("BuildingButton: OnItemAdded 델리게이트 바인딩 완료"));
#endif
	}

	if (!InvComp->OnItemRemoved.IsAlreadyBound(this, &ThisClass::OnInventoryItemRemoved))
	{
		InvComp->OnItemRemoved.AddDynamic(this, &ThisClass::OnInventoryItemRemoved);
#if INV_DEBUG_BUILD
		UE_LOG(LogTemp, Log, TEXT("BuildingButton: OnItemRemoved 델리게이트 바인딩 완료"));
#endif
	}

	if (!InvComp->OnStackChange.IsAlreadyBound(this, &ThisClass::OnInventoryStackChanged))
	{
		InvComp->OnStackChange.AddDynamic(this, &ThisClass::OnInventoryStackChanged);
#if INV_DEBUG_BUILD
		UE_LOG(LogTemp, Log, TEXT("BuildingButton: OnStackChange 델리게이트 바인딩 완료"));
#endif
	}
}

void UInv_BuildingButton::UnbindInventoryDelegates()
{
	// PlayerController 가져오기
	APlayerController* PC = GetOwningPlayer();
	if (!IsValid(PC)) return;

	// InventoryComponent 가져오기
	UInv_InventoryComponent* InvComp = PC->FindComponentByClass<UInv_InventoryComponent>();
	if (!IsValid(InvComp)) return;

	// 델리게이트 언바인딩
	if (InvComp->OnItemAdded.IsAlreadyBound(this, &ThisClass::OnInventoryItemAdded))
	{
		InvComp->OnItemAdded.RemoveDynamic(this, &ThisClass::OnInventoryItemAdded);
	}

	if (InvComp->OnItemRemoved.IsAlreadyBound(this, &ThisClass::OnInventoryItemRemoved))
	{
		InvComp->OnItemRemoved.RemoveDynamic(this, &ThisClass::OnInventoryItemRemoved);
	}

	if (InvComp->OnStackChange.IsAlreadyBound(this, &ThisClass::OnInventoryStackChanged))
	{
		InvComp->OnStackChange.RemoveDynamic(this, &ThisClass::OnInventoryStackChanged);
	}

#if INV_DEBUG_BUILD
	UE_LOG(LogTemp, Log, TEXT("BuildingButton: 모든 델리게이트 언바인딩 완료"));
#endif
}

void UInv_BuildingButton::OnInventoryItemAdded(UInv_InventoryItem* Item, int32 EntryIndex)
{
	// 아이템이 추가되었을 때 (픽업, PutDown 포함)
#if INV_DEBUG_BUILD
	UE_LOG(LogTemp, Log, TEXT("BuildingButton: 아이템 추가됨! EntryIndex=%d, 버튼 상태 재계산..."), EntryIndex);
#endif
	UpdateMaterialUI(); // 재료 UI 업데이트
	UpdateButtonState();
}

void UInv_BuildingButton::OnInventoryItemRemoved(UInv_InventoryItem* Item, int32 EntryIndex)
{
	// 아이템이 제거되었을 때 (드롭, 소비 포함)
#if INV_DEBUG_BUILD
	UE_LOG(LogTemp, Log, TEXT("BuildingButton: 아이템 제거됨! EntryIndex=%d, 버튼 상태 재계산..."), EntryIndex);
#endif
	UpdateMaterialUI(); // 재료 UI 업데이트
	UpdateButtonState();
}

void UInv_BuildingButton::OnInventoryStackChanged(const FInv_SlotAvailabilityResult& Result)
{
	// 아이템 스택이 변경되었을 때 (Split, Combine 포함)
#if INV_DEBUG_BUILD
	UE_LOG(LogTemp, Log, TEXT("BuildingButton: 스택 변경됨! 버튼 상태 재계산..."));
#endif
	UpdateMaterialUI(); // 재료 UI 업데이트
	UpdateButtonState();
}

void UInv_BuildingButton::UpdateMaterialUI()
{
	APlayerController* PC = GetOwningPlayer();
	if (!IsValid(PC)) return;

	UInv_InventoryComponent* InvComp = PC->FindComponentByClass<UInv_InventoryComponent>();

	// === 재료 1 UI 업데이트 ===
	if (RequiredMaterialTag.IsValid() && RequiredAmount > 0)
	{
		// 컨테이너 Visible
		if (IsValid(HorizontalBox_Material1))
		{
			HorizontalBox_Material1->SetVisibility(ESlateVisibility::Visible);
		}

		// 개수 텍스트 업데이트 (실시간!)
		if (IsValid(Text_Material1Amount))
		{
			int32 CurrentAmount = 0;
			
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
			
			FString AmountText = FString::Printf(TEXT("%d/%d"), CurrentAmount, RequiredAmount);
			Text_Material1Amount->SetText(FText::FromString(AmountText));
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
		// 컨테이너 Visible
		if (IsValid(HorizontalBox_Material2))
		{
			HorizontalBox_Material2->SetVisibility(ESlateVisibility::Visible);
		}

		// 개수 텍스트 업데이트
		if (IsValid(Text_Material2Amount))
		{
			int32 CurrentAmount = 0;
			
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
		// 컨테이너 Visible
		if (IsValid(HorizontalBox_Material3))
		{
			HorizontalBox_Material3->SetVisibility(ESlateVisibility::Visible);
		}

		// 개수 텍스트 업데이트
		if (IsValid(Text_Material3Amount))
		{
			int32 CurrentAmount = 0;
			
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

#if INV_DEBUG_BUILD
	UE_LOG(LogTemp, Log, TEXT("BuildingButton: 재료 UI 업데이트 완료"));
#endif
}


