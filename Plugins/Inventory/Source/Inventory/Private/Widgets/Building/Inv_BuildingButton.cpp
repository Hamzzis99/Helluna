// Gihyeon's Inventory Project

#include "Widgets/Building/Inv_BuildingButton.h"
#include "Inventory.h"
#include "Components/Button.h"
#include "Components/Image.h"
#include "Components/TextBlock.h"
#include "Components/HorizontalBox.h"
#include "Components/StaticMeshComponent.h"
#include "Building/Components/Inv_BuildingComponent.h"
#include "InventoryManagement/Components/Inv_InventoryComponent.h"
#include "Items/Inv_InventoryItem.h"
#include "Items/Fragments/Inv_ItemFragment.h"
#include "GameFramework/PlayerController.h"
#include "Engine/StaticMesh.h"

// ════════════════════════════════════════════════════════════════
// CDO 캐시 헬퍼
// ════════════════════════════════════════════════════════════════
const AInv_BuildableActor* UInv_BuildingButton::GetCDO() const
{
	if (!BuildableActorClass) return nullptr;
	return BuildableActorClass->GetDefaultObject<AInv_BuildableActor>();
}

// ════════════════════════════════════════════════════════════════
// Getter 함수들 — CDO에서 데이터 읽기
// ════════════════════════════════════════════════════════════════
EBuildCategory UInv_BuildingButton::GetBuildCategory() const
{
	const AInv_BuildableActor* CDO = GetCDO();
	return CDO ? CDO->BuildCategory : EBuildCategory::Construction;
}

FText UInv_BuildingButton::GetBuildingName() const
{
	const AInv_BuildableActor* CDO = GetCDO();
	return CDO ? CDO->BuildingDisplayName : FText::GetEmpty();
}

FText UInv_BuildingButton::GetBuildingDescription() const
{
	const AInv_BuildableActor* CDO = GetCDO();
	return CDO ? CDO->BuildingDescription : FText::GetEmpty();
}

UStaticMesh* UInv_BuildingButton::GetPreviewMesh() const
{
	const AInv_BuildableActor* CDO = GetCDO();
	return CDO ? CDO->GetEffectivePreviewMesh() : nullptr;
}

FRotator UInv_BuildingButton::GetPreviewRotationOffset() const
{
	const AInv_BuildableActor* CDO = GetCDO();
	return CDO ? CDO->PreviewRotationOffset : FRotator::ZeroRotator;
}

float UInv_BuildingButton::GetPreviewCameraDistance() const
{
	const AInv_BuildableActor* CDO = GetCDO();
	return CDO ? CDO->PreviewCameraDistance : 0.f;
}

TSubclassOf<AActor> UInv_BuildingButton::GetGhostActorClass() const
{
	const AInv_BuildableActor* CDO = GetCDO();
	if (!CDO) return nullptr;
	// GhostActorClass가 None이면 자기 자신의 클래스를 고스트로 사용
	return CDO->GhostActorClass ? CDO->GhostActorClass : BuildableActorClass;
}

int32 UInv_BuildingButton::GetBuildingID() const
{
	const AInv_BuildableActor* CDO = GetCDO();
	return CDO ? CDO->BuildingID : 0;
}

FGameplayTag UInv_BuildingButton::GetRequiredMaterialTag() const
{
	const AInv_BuildableActor* CDO = GetCDO();
	return CDO ? CDO->RequiredMaterialTag1 : FGameplayTag();
}

int32 UInv_BuildingButton::GetRequiredAmount() const
{
	const AInv_BuildableActor* CDO = GetCDO();
	return CDO ? CDO->RequiredAmount1 : 0;
}

UTexture2D* UInv_BuildingButton::GetMaterialIcon1() const
{
	const AInv_BuildableActor* CDO = GetCDO();
	return CDO ? CDO->MaterialIcon1 : nullptr;
}

FGameplayTag UInv_BuildingButton::GetRequiredMaterialTag2() const
{
	const AInv_BuildableActor* CDO = GetCDO();
	return CDO ? CDO->RequiredMaterialTag2 : FGameplayTag();
}

int32 UInv_BuildingButton::GetRequiredAmount2() const
{
	const AInv_BuildableActor* CDO = GetCDO();
	return CDO ? CDO->RequiredAmount2 : 0;
}

UTexture2D* UInv_BuildingButton::GetMaterialIcon2() const
{
	const AInv_BuildableActor* CDO = GetCDO();
	return CDO ? CDO->MaterialIcon2 : nullptr;
}

FGameplayTag UInv_BuildingButton::GetRequiredMaterialTag3() const
{
	const AInv_BuildableActor* CDO = GetCDO();
	return CDO ? CDO->RequiredMaterialTag3 : FGameplayTag();
}

int32 UInv_BuildingButton::GetRequiredAmount3() const
{
	const AInv_BuildableActor* CDO = GetCDO();
	return CDO ? CDO->RequiredAmount3 : 0;
}

UTexture2D* UInv_BuildingButton::GetMaterialIcon3() const
{
	const AInv_BuildableActor* CDO = GetCDO();
	return CDO ? CDO->MaterialIcon3 : nullptr;
}

// ════════════════════════════════════════════════════════════════
// 초기화
// ════════════════════════════════════════════════════════════════

void UInv_BuildingButton::NativeOnInitialized()
{
	Super::NativeOnInitialized();

	if (IsValid(Button_Main))
	{
		Button_Main->OnClicked.AddUniqueDynamic(this, &ThisClass::OnButtonClicked);
	}
	else
	{
#if INV_DEBUG_BUILD
		UE_LOG(LogTemp, Error, TEXT("Inv_BuildingButton: Button_Main is not bound!"));
#endif
	}
}

void UInv_BuildingButton::NativeConstruct()
{
	Super::NativeConstruct();

	const AInv_BuildableActor* CDO = GetCDO();
	if (!CDO)
	{
		UE_LOG(LogTemp, Warning, TEXT("[BuildingButton] CDO 로드 실패: BuildableActorClass=NULL"));
		return;
	}

	// UI 채우기 (CDO에서 자동 로드)
	if (IsValid(Text_BuildingName))
	{
		Text_BuildingName->SetText(CDO->BuildingDisplayName);
	}

	if (IsValid(Image_Icon) && IsValid(CDO->BuildingIcon))
	{
		Image_Icon->SetBrushFromTexture(CDO->BuildingIcon);
	}

	// 재료 아이콘 설정
	if (IsValid(Image_Material1) && IsValid(CDO->MaterialIcon1))
	{
		Image_Material1->SetBrushFromTexture(CDO->MaterialIcon1);
	}
	if (IsValid(Image_Material2) && IsValid(CDO->MaterialIcon2))
	{
		Image_Material2->SetBrushFromTexture(CDO->MaterialIcon2);
	}
	if (IsValid(Image_Material3) && IsValid(CDO->MaterialIcon3))
	{
		Image_Material3->SetBrushFromTexture(CDO->MaterialIcon3);
	}

	UE_LOG(LogTemp, Warning, TEXT("[BuildingButton] CDO 로드: %s (Category=%d, Mesh=%s)"),
		*CDO->BuildingDisplayName.ToString(),
		static_cast<uint8>(CDO->BuildCategory),
		CDO->GetEffectivePreviewMesh() ? *CDO->GetEffectivePreviewMesh()->GetName() : TEXT("NULL"));

	// 델리게이트 바인딩
	BindInventoryDelegates();

	// 재료 UI + 버튼 상태 업데이트
	UpdateMaterialUI();
	UpdateButtonState();
}

void UInv_BuildingButton::OnButtonClicked()
{
	UE_LOG(LogTemp, Warning, TEXT("[BuildingButton] 카드 클릭 → 디테일 패널 열기 요청: %s"),
		*GetBuildingName().ToString());

	OnCardClicked.Broadcast(this);
}

void UInv_BuildingButton::ExecuteBuild()
{
	const AInv_BuildableActor* CDO = GetCDO();
	if (!CDO)
	{
#if INV_DEBUG_BUILD
		UE_LOG(LogTemp, Error, TEXT("ExecuteBuild: BuildableActorClass CDO is null!"));
#endif
		return;
	}

#if INV_DEBUG_BUILD
	UE_LOG(LogTemp, Warning, TEXT("=== BUILDING EXECUTE ==="));
	UE_LOG(LogTemp, Warning, TEXT("Building Name: %s"), *CDO->BuildingDisplayName.ToString());
	UE_LOG(LogTemp, Warning, TEXT("Building ID: %d"), CDO->BuildingID);
#endif

	if (!HasRequiredMaterials())
	{
#if INV_DEBUG_BUILD
		UE_LOG(LogTemp, Warning, TEXT("=== 재료가 부족합니다! ==="));
#endif
		return;
	}

	APlayerController* PC = GetOwningPlayer();
	if (!IsValid(PC))
	{
#if INV_DEBUG_BUILD
		UE_LOG(LogTemp, Error, TEXT("Inv_BuildingButton: Owning Player is invalid!"));
#endif
		return;
	}

	UInv_BuildingComponent* BuildingComp = PC->FindComponentByClass<UInv_BuildingComponent>();
	if (!IsValid(BuildingComp))
	{
#if INV_DEBUG_BUILD
		UE_LOG(LogTemp, Error, TEXT("Inv_BuildingButton: BuildingComponent not found!"));
#endif
		return;
	}

	// CDO에서 데이터 추출하여 구조체로 전달
	FInv_BuildingSelectionInfo Info;
	Info.BuildingName = CDO->BuildingDisplayName;
	Info.BuildingIcon = CDO->BuildingIcon;
	Info.GhostClass = CDO->GhostActorClass ? CDO->GhostActorClass : (CDO->ActualBuildingClass ? CDO->ActualBuildingClass : BuildableActorClass);
	Info.ActualBuildingClass = CDO->ActualBuildingClass ? CDO->ActualBuildingClass : BuildableActorClass;
	Info.BuildingID = CDO->BuildingID;
	Info.MaterialTag1 = CDO->RequiredMaterialTag1;
	Info.MaterialAmount1 = CDO->RequiredAmount1;
	Info.MaterialIcon1 = CDO->MaterialIcon1;
	Info.MaterialTag2 = CDO->RequiredMaterialTag2;
	Info.MaterialAmount2 = CDO->RequiredAmount2;
	Info.MaterialIcon2 = CDO->MaterialIcon2;
	Info.MaterialTag3 = CDO->RequiredMaterialTag3;
	Info.MaterialAmount3 = CDO->RequiredAmount3;
	Info.MaterialIcon3 = CDO->MaterialIcon3;

	BuildingComp->OnBuildingSelectedFromWidget(Info);
}

void UInv_BuildingButton::NativeTick(const FGeometry& MyGeometry, float InDeltaTime)
{
	Super::NativeTick(MyGeometry, InDeltaTime);
}

bool UInv_BuildingButton::HasRequiredMaterials()
{
	APlayerController* PC = GetOwningPlayer();
	if (!IsValid(PC)) return false;

	UInv_InventoryComponent* InvComp = PC->FindComponentByClass<UInv_InventoryComponent>();
	if (!IsValid(InvComp)) return false;

	const AInv_BuildableActor* CDO = GetCDO();
	if (!CDO) return false;

	if (CDO->RequiredMaterialTag1.IsValid() && CDO->RequiredAmount1 > 0)
	{
		if (InvComp->GetTotalMaterialCount(CDO->RequiredMaterialTag1) < CDO->RequiredAmount1)
			return false;
	}

	if (CDO->RequiredMaterialTag2.IsValid() && CDO->RequiredAmount2 > 0)
	{
		if (InvComp->GetTotalMaterialCount(CDO->RequiredMaterialTag2) < CDO->RequiredAmount2)
			return false;
	}

	if (CDO->RequiredMaterialTag3.IsValid() && CDO->RequiredAmount3 > 0)
	{
		if (InvComp->GetTotalMaterialCount(CDO->RequiredMaterialTag3) < CDO->RequiredAmount3)
			return false;
	}

	return true;
}

void UInv_BuildingButton::UpdateButtonState()
{
	if (!IsValid(Button_Main)) return;

	const bool bHasMaterials = HasRequiredMaterials();
	Button_Main->SetIsEnabled(bHasMaterials);

	if (IsValid(Text_BuildingName))
	{
		FLinearColor TextColor = bHasMaterials ? FLinearColor::White : FLinearColor::Red;
		Text_BuildingName->SetColorAndOpacity(FSlateColor(TextColor));
	}
}

void UInv_BuildingButton::NativeDestruct()
{
	UnbindInventoryDelegates();
	Super::NativeDestruct();
}

void UInv_BuildingButton::BindInventoryDelegates()
{
	APlayerController* PC = GetOwningPlayer();
	if (!IsValid(PC)) return;

	UInv_InventoryComponent* InvComp = PC->FindComponentByClass<UInv_InventoryComponent>();
	if (!IsValid(InvComp)) return;

	if (!InvComp->OnItemAdded.IsAlreadyBound(this, &ThisClass::OnInventoryItemAdded))
		InvComp->OnItemAdded.AddUniqueDynamic(this, &ThisClass::OnInventoryItemAdded);
	if (!InvComp->OnItemRemoved.IsAlreadyBound(this, &ThisClass::OnInventoryItemRemoved))
		InvComp->OnItemRemoved.AddUniqueDynamic(this, &ThisClass::OnInventoryItemRemoved);
	if (!InvComp->OnStackChange.IsAlreadyBound(this, &ThisClass::OnInventoryStackChanged))
		InvComp->OnStackChange.AddUniqueDynamic(this, &ThisClass::OnInventoryStackChanged);
}

void UInv_BuildingButton::UnbindInventoryDelegates()
{
	APlayerController* PC = GetOwningPlayer();
	if (!IsValid(PC)) return;

	UInv_InventoryComponent* InvComp = PC->FindComponentByClass<UInv_InventoryComponent>();
	if (!IsValid(InvComp)) return;

	if (InvComp->OnItemAdded.IsAlreadyBound(this, &ThisClass::OnInventoryItemAdded))
		InvComp->OnItemAdded.RemoveDynamic(this, &ThisClass::OnInventoryItemAdded);
	if (InvComp->OnItemRemoved.IsAlreadyBound(this, &ThisClass::OnInventoryItemRemoved))
		InvComp->OnItemRemoved.RemoveDynamic(this, &ThisClass::OnInventoryItemRemoved);
	if (InvComp->OnStackChange.IsAlreadyBound(this, &ThisClass::OnInventoryStackChanged))
		InvComp->OnStackChange.RemoveDynamic(this, &ThisClass::OnInventoryStackChanged);
}

void UInv_BuildingButton::OnInventoryItemAdded(UInv_InventoryItem* Item, int32 EntryIndex)
{
	UpdateMaterialUI();
	UpdateButtonState();
}

void UInv_BuildingButton::OnInventoryItemRemoved(UInv_InventoryItem* Item, int32 EntryIndex)
{
	UpdateMaterialUI();
	UpdateButtonState();
}

void UInv_BuildingButton::OnInventoryStackChanged(const FInv_SlotAvailabilityResult& Result)
{
	UpdateMaterialUI();
	UpdateButtonState();
}

void UInv_BuildingButton::UpdateMaterialUI()
{
	APlayerController* PC = GetOwningPlayer();
	if (!IsValid(PC)) return;

	UInv_InventoryComponent* InvComp = PC->FindComponentByClass<UInv_InventoryComponent>();
	const AInv_BuildableActor* CDO = GetCDO();

	// 헬퍼 람다: 재료 행 UI 업데이트
	auto UpdateMaterialRow = [&InvComp](const FGameplayTag& Tag, int32 Required,
		UHorizontalBox* Container, UTextBlock* AmountText)
	{
		if (Tag.IsValid() && Required > 0)
		{
			if (IsValid(Container))
				Container->SetVisibility(ESlateVisibility::Visible);

			if (IsValid(AmountText) && IsValid(InvComp))
			{
				int32 CurrentAmount = 0;
				const auto& AllItems = InvComp->GetInventoryList().GetAllItems();
				for (UInv_InventoryItem* Item : AllItems)
				{
					if (!IsValid(Item)) continue;
					if (!Item->GetItemManifest().GetItemType().MatchesTagExact(Tag)) continue;
					CurrentAmount += Item->GetTotalStackCount();
				}
				AmountText->SetText(FText::FromString(FString::Printf(TEXT("%d/%d"), CurrentAmount, Required)));
			}
		}
		else
		{
			if (IsValid(Container))
				Container->SetVisibility(ESlateVisibility::Hidden);
		}
	};

	if (CDO)
	{
		UpdateMaterialRow(CDO->RequiredMaterialTag1, CDO->RequiredAmount1, HorizontalBox_Material1, Text_Material1Amount);
		UpdateMaterialRow(CDO->RequiredMaterialTag2, CDO->RequiredAmount2, HorizontalBox_Material2, Text_Material2Amount);
		UpdateMaterialRow(CDO->RequiredMaterialTag3, CDO->RequiredAmount3, HorizontalBox_Material3, Text_Material3Amount);
	}
	else
	{
		if (IsValid(HorizontalBox_Material1)) HorizontalBox_Material1->SetVisibility(ESlateVisibility::Hidden);
		if (IsValid(HorizontalBox_Material2)) HorizontalBox_Material2->SetVisibility(ESlateVisibility::Hidden);
		if (IsValid(HorizontalBox_Material3)) HorizontalBox_Material3->SetVisibility(ESlateVisibility::Hidden);
	}
}
