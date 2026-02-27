// File: Plugins/Inventory/Source/Inventory/Private/Widgets/Inventory/Container/Inv_ContainerWidget.cpp
// ════════════════════════════════════════════════════════════════════════════════
// UInv_ContainerWidget 구현
// ════════════════════════════════════════════════════════════════════════════════

#include "Widgets/Inventory/Container/Inv_ContainerWidget.h"

#include "InventoryManagement/Components/Inv_InventoryComponent.h"
#include "InventoryManagement/Components/Inv_LootContainerComponent.h"
#include "Widgets/Inventory/Spatial/Inv_InventoryGrid.h"
#include "Components/TextBlock.h"
#include "Components/Button.h"

void UInv_ContainerWidget::NativeConstruct()
{
	Super::NativeConstruct();

	// TakeAll 버튼 바인딩
	if (IsValid(Button_TakeAll))
	{
		Button_TakeAll->OnClicked.AddUniqueDynamic(this, &UInv_ContainerWidget::OnTakeAllClicked); // B6: 중복 바인딩 방지
	}
}

void UInv_ContainerWidget::InitializePanels(
	UInv_LootContainerComponent* InContainerComp,
	UInv_InventoryComponent* InPlayerComp)
{
	if (!IsValid(InContainerComp) || !IsValid(InPlayerComp))
	{
		UE_LOG(LogTemp, Error, TEXT("[ContainerWidget] InitializePanels: 유효하지 않은 컴포넌트"));
		return;
	}

	CachedContainerComp = InContainerComp;
	CachedPlayerComp = InPlayerComp;

	// 컨테이너 이름 표시
	if (IsValid(Text_ContainerName))
	{
		Text_ContainerName->SetText(InContainerComp->ContainerDisplayName);
	}

	// ═══════════════════════════════════════════
	// 왼쪽: 컨테이너 Grid 설정
	// ═══════════════════════════════════════════
	if (IsValid(ContainerGrid))
	{
		ContainerGrid->SetSkipAutoInit(true);
		ContainerGrid->SetOwnerType(EGridOwnerType::Container);
		ContainerGrid->SetContainerComponent(InContainerComp);

		// 컨테이너의 FastArray에서 델리게이트 바인딩
		InContainerComp->OnContainerItemAdded.AddDynamic(ContainerGrid, &UInv_InventoryGrid::AddItem);
		InContainerComp->OnContainerItemRemoved.AddDynamic(ContainerGrid, &UInv_InventoryGrid::RemoveItem);

		// [B1+B2 Fix] RPC 호출용으로만 InvComp 설정 (델리게이트 바인딩 X, 플레이어 아이템 동기화 X)
		ContainerGrid->SetInventoryComponentForRPC(InPlayerComp);

		// 컨테이너의 기존 아이템을 Grid에 동기화
		ContainerGrid->SyncContainerItems(InContainerComp);
	}

	// ═══════════════════════════════════════════
	// 오른쪽: 플레이어 Grid 설정
	// ═══════════════════════════════════════════
	if (IsValid(PlayerGrid))
	{
		PlayerGrid->SetSkipAutoInit(true);
		PlayerGrid->SetOwnerType(EGridOwnerType::Player);
		PlayerGrid->SetInventoryComponent(InPlayerComp);
	}

	// ═══════════════════════════════════════════
	// 크로스 Grid 연결 (양방향)
	// ═══════════════════════════════════════════
	if (IsValid(ContainerGrid) && IsValid(PlayerGrid))
	{
		ContainerGrid->SetLinkedContainerGrid(PlayerGrid);
		PlayerGrid->SetLinkedContainerGrid(ContainerGrid);
	}

	UE_LOG(LogTemp, Log, TEXT("[ContainerWidget] InitializePanels 완료: %s"),
		*InContainerComp->ContainerDisplayName.ToString());
}

void UInv_ContainerWidget::CleanupPanels()
{
	// 델리게이트 해제
	if (CachedContainerComp.IsValid())
	{
		if (IsValid(ContainerGrid))
		{
			CachedContainerComp->OnContainerItemAdded.RemoveDynamic(ContainerGrid, &UInv_InventoryGrid::AddItem);
			CachedContainerComp->OnContainerItemRemoved.RemoveDynamic(ContainerGrid, &UInv_InventoryGrid::RemoveItem);
		}
	}

	// Grid 연결 해제
	if (IsValid(ContainerGrid))
	{
		ContainerGrid->SetLinkedContainerGrid(nullptr);
		ContainerGrid->SetContainerComponent(nullptr);
		ContainerGrid->OnHide();
	}

	if (IsValid(PlayerGrid))
	{
		PlayerGrid->SetLinkedContainerGrid(nullptr);
		PlayerGrid->OnHide();
	}

	CachedContainerComp.Reset();
	CachedPlayerComp.Reset();

	UE_LOG(LogTemp, Log, TEXT("[ContainerWidget] CleanupPanels 완료"));
}

void UInv_ContainerWidget::OnTakeAllClicked()
{
	if (!CachedContainerComp.IsValid() || !CachedPlayerComp.IsValid())
	{
		return;
	}

	CachedPlayerComp->Server_TakeAllFromContainer(CachedContainerComp.Get());
}
