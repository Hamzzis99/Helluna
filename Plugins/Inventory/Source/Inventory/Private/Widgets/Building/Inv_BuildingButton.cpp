// Gihyeon's Inventory Project

#include "Widgets/Building/Inv_BuildingButton.h"
#include "Components/Button.h"
#include "Components/Image.h"
#include "Components/TextBlock.h"
#include "Building/Components/Inv_BuildingComponent.h"
#include "InventoryManagement/Components/Inv_InventoryComponent.h"
#include "InventoryManagement/FastArray/Inv_FastArray.h"
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
		UE_LOG(LogTemp, Error, TEXT("Inv_BuildingButton: Button_Main is not bound! Make sure the widget has a Button named 'Button_Main'."));
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

	// 델리게이트 바인딩 (인벤토리 변경 감지)
	BindInventoryDelegates();

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
	UE_LOG(LogTemp, Warning, TEXT("=== BUILDING BUTTON CLICKED ==="));
	UE_LOG(LogTemp, Warning, TEXT("Building Name: %s"), *BuildingName.ToString());
	UE_LOG(LogTemp, Warning, TEXT("Building ID: %d"), BuildingID);

	// 재료 체크
	if (!HasRequiredMaterials())
	{
		UE_LOG(LogTemp, Warning, TEXT("=== 재료가 부족합니다! ==="));
		UE_LOG(LogTemp, Warning, TEXT("필요한 재료: %s x %d"), *RequiredMaterialTag.ToString(), RequiredAmount);
		// TODO: UI 팝업 표시
		return;
	}

	// PlayerController 가져오기
	APlayerController* PC = GetOwningPlayer();
	if (!IsValid(PC))
	{
		UE_LOG(LogTemp, Error, TEXT("Inv_BuildingButton: Owning Player is invalid!"));
		return;
	}

	// BuildingComponent 가져오기
	UInv_BuildingComponent* BuildingComp = PC->FindComponentByClass<UInv_BuildingComponent>();
	if (!IsValid(BuildingComp))
	{
		UE_LOG(LogTemp, Error, TEXT("Inv_BuildingButton: BuildingComponent not found on Player Controller!"));
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
			UE_LOG(LogTemp, Warning, TEXT("재료1 부족: %d/%d (%s)"), TotalAmount1, RequiredAmount, *RequiredMaterialTag.ToString());
			return false; // 재료1 개수 부족
		}
	}

	// === 재료 2 체크 (모든 스택 합산!) ===
	if (RequiredMaterialTag2.IsValid() && RequiredAmount2 > 0)
	{
		int32 TotalAmount2 = InvComp->GetTotalMaterialCount(RequiredMaterialTag2);
		if (TotalAmount2 < RequiredAmount2)
		{
			UE_LOG(LogTemp, Warning, TEXT("재료2 부족: %d/%d (%s)"), TotalAmount2, RequiredAmount2, *RequiredMaterialTag2.ToString());
			return false; // 재료2 개수 부족
		}
	}

	// 모든 재료 충족!
	UE_LOG(LogTemp, Log, TEXT("모든 재료 충족!"));
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
		UE_LOG(LogTemp, Log, TEXT("BuildingButton: OnItemAdded 델리게이트 바인딩 완료"));
	}

	if (!InvComp->OnItemRemoved.IsAlreadyBound(this, &ThisClass::OnInventoryItemRemoved))
	{
		InvComp->OnItemRemoved.AddDynamic(this, &ThisClass::OnInventoryItemRemoved);
		UE_LOG(LogTemp, Log, TEXT("BuildingButton: OnItemRemoved 델리게이트 바인딩 완료"));
	}

	if (!InvComp->OnStackChange.IsAlreadyBound(this, &ThisClass::OnInventoryStackChanged))
	{
		InvComp->OnStackChange.AddDynamic(this, &ThisClass::OnInventoryStackChanged);
		UE_LOG(LogTemp, Log, TEXT("BuildingButton: OnStackChange 델리게이트 바인딩 완료"));
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

	UE_LOG(LogTemp, Log, TEXT("BuildingButton: 모든 델리게이트 언바인딩 완료"));
}

void UInv_BuildingButton::OnInventoryItemAdded(UInv_InventoryItem* Item)
{
	// 아이템이 추가되었을 때 (픽업, PutDown 포함)
	UE_LOG(LogTemp, Log, TEXT("BuildingButton: 아이템 추가됨! 버튼 상태 재계산..."));
	UpdateButtonState();
}

void UInv_BuildingButton::OnInventoryItemRemoved(UInv_InventoryItem* Item)
{
	// 아이템이 제거되었을 때 (드롭, 소비 포함)
	UE_LOG(LogTemp, Log, TEXT("BuildingButton: 아이템 제거됨! 버튼 상태 재계산..."));
	UpdateButtonState();
}

void UInv_BuildingButton::OnInventoryStackChanged(const FInv_SlotAvailabilityResult& Result)
{
	// 아이템 스택이 변경되었을 때 (Split, Combine 포함)
	UE_LOG(LogTemp, Log, TEXT("BuildingButton: 스택 변경됨! 버튼 상태 재계산..."));
	UpdateButtonState();
}


