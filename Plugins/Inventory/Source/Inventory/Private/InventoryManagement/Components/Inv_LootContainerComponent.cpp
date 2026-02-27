// File: Plugins/Inventory/Source/Inventory/Private/InventoryManagement/Components/Inv_LootContainerComponent.cpp
// ════════════════════════════════════════════════════════════════════════════════
// UInv_LootContainerComponent 구현
// ════════════════════════════════════════════════════════════════════════════════

#include "InventoryManagement/Components/Inv_LootContainerComponent.h"

#include "Inventory.h"
#include "Items/Inv_InventoryItem.h"
#include "Items/Components/Inv_ItemComponent.h"
#include "Items/Fragments/Inv_ItemFragment.h"
#include "Player/Inv_PlayerController.h"
#include "Net/UnrealNetwork.h"
#include "Components/PrimitiveComponent.h"

UInv_LootContainerComponent::UInv_LootContainerComponent()
	: ContainerInventoryList(this)
{
	// 리플리케이션 활성화
	SetIsReplicatedByDefault(true);

	// 기본 비활성 (사체용 — 사망 시 Activate() 호출)
	// 상자용은 BP에서 bActivated=true 설정
	bActivated = true;
	CurrentUser = nullptr;

	// 기본 표시 이름
	ContainerDisplayName = FText::FromString(TEXT("Container"));
}

void UInv_LootContainerComponent::BeginPlay()
{
	Super::BeginPlay();

	// 서버에서만 초기 아이템 생성
	if (GetOwner() && GetOwner()->HasAuthority() && bActivated)
	{
		GenerateInitialItems();
	}
}

void UInv_LootContainerComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	DOREPLIFETIME(UInv_LootContainerComponent, ContainerInventoryList);
	DOREPLIFETIME(UInv_LootContainerComponent, CurrentUser);
	DOREPLIFETIME(UInv_LootContainerComponent, bActivated);
	DOREPLIFETIME(UInv_LootContainerComponent, ContainerDisplayName);
}

// ════════════════════════════════════════════════════════════════
// IInv_Highlightable 구현
// ════════════════════════════════════════════════════════════════

void UInv_LootContainerComponent::Highlight_Implementation()
{
	// Owner 액터의 모든 PrimitiveComponent에 커스텀 뎁스 활성화
	AActor* Owner = GetOwner();
	if (!IsValid(Owner)) return;

	TArray<UPrimitiveComponent*> PrimComps;
	Owner->GetComponents<UPrimitiveComponent>(PrimComps);
	for (UPrimitiveComponent* Comp : PrimComps)
	{
		if (IsValid(Comp))
		{
			Comp->SetRenderCustomDepth(true);
		}
	}
}

void UInv_LootContainerComponent::UnHighlight_Implementation()
{
	AActor* Owner = GetOwner();
	if (!IsValid(Owner)) return;

	TArray<UPrimitiveComponent*> PrimComps;
	Owner->GetComponents<UPrimitiveComponent>(PrimComps);
	for (UPrimitiveComponent* Comp : PrimComps)
	{
		if (IsValid(Comp))
		{
			Comp->SetRenderCustomDepth(false);
		}
	}
}

// ════════════════════════════════════════════════════════════════
// 컨테이너 활성화
// ════════════════════════════════════════════════════════════════

void UInv_LootContainerComponent::Activate()
{
	bActivated = true;
}

// ════════════════════════════════════════════════════════════════
// 외부 아이템으로 초기화 (사체: 죽은 플레이어 아이템 복사)
// ════════════════════════════════════════════════════════════════

void UInv_LootContainerComponent::InitializeWithItems(
	const TArray<FInv_SavedItemData>& Items,
	const FInv_ItemTemplateResolver& Resolver)
{
	AActor* Owner = GetOwner();
	if (!IsValid(Owner) || !Owner->HasAuthority()) return;

	// 기존 아이템 제거
	ContainerInventoryList.ClearAllEntries();

	for (const FInv_SavedItemData& SavedItem : Items)
	{
		// 유효성 검사: ItemType + StackCount
		if (!SavedItem.ItemType.IsValid() || SavedItem.StackCount <= 0)
		{
			continue;
		}

		// Resolver로 ItemComponent CDO 획득
		UInv_ItemComponent* TemplateCDO = nullptr;
		if (Resolver.IsBound())
		{
			TemplateCDO = Resolver.Execute(SavedItem.ItemType);
		}

		if (!IsValid(TemplateCDO))
		{
			UE_LOG(LogTemp, Warning,
				TEXT("[LootContainer] InitializeWithItems: ItemType=%s 리졸브 실패 — 스킵"),
				*SavedItem.ItemType.ToString());
			continue;
		}

		// FastArray에 아이템 추가
		UInv_InventoryItem* NewItem = ContainerInventoryList.AddEntry(TemplateCDO);
		if (IsValid(NewItem))
		{
			// 스택 수량 설정
			NewItem->SetTotalStackCount(SavedItem.StackCount);

			// 리플리케이션 서브오브젝트 등록
			if (IsUsingRegisteredSubObjectList() && IsReadyForReplication())
			{
				AddReplicatedSubObject(NewItem);
			}
		}
	}

	UE_LOG(LogTemp, Log, TEXT("[LootContainer] InitializeWithItems: %d개 아이템 중 %d개 로드됨 (Owner=%s)"),
		Items.Num(), ContainerInventoryList.GetAllItems().Num(),
		*Owner->GetName());
}

// ════════════════════════════════════════════════════════════════
// ItemComponent 직접 추가
// ════════════════════════════════════════════════════════════════

UInv_InventoryItem* UInv_LootContainerComponent::AddItem(UInv_ItemComponent* ItemComponent)
{
	AActor* Owner = GetOwner();
	if (!IsValid(Owner) || !Owner->HasAuthority() || !IsValid(ItemComponent))
	{
		return nullptr;
	}

	UInv_InventoryItem* NewItem = ContainerInventoryList.AddEntry(ItemComponent);
	if (IsValid(NewItem))
	{
		// 리플리케이션 서브오브젝트 등록
		if (IsUsingRegisteredSubObjectList() && IsReadyForReplication())
		{
			AddReplicatedSubObject(NewItem);
		}
	}

	return NewItem;
}

// ════════════════════════════════════════════════════════════════
// 상태 조회
// ════════════════════════════════════════════════════════════════

bool UInv_LootContainerComponent::IsEmpty() const
{
	return ContainerInventoryList.GetAllItems().Num() == 0;
}

void UInv_LootContainerComponent::SetCurrentUser(APlayerController* PC)
{
	CurrentUser = PC;
}

void UInv_LootContainerComponent::ClearCurrentUser()
{
	CurrentUser = nullptr;
}

// ════════════════════════════════════════════════════════════════
// 초기 아이템 생성
// ════════════════════════════════════════════════════════════════

void UInv_LootContainerComponent::GenerateInitialItems()
{
	// 고정 아이템 생성
	if (PresetItems.Num() > 0)
	{
		GeneratePresetItems();
	}

	// 랜덤 루트 생성
	if (bRandomizeLootOnSpawn && LootTable.Num() > 0)
	{
		GenerateRandomLoot();
	}
}

void UInv_LootContainerComponent::GeneratePresetItems()
{
	for (const FInv_PresetContainerItem& Preset : PresetItems)
	{
		if (!Preset.ItemClass || Preset.StackCount <= 0)
		{
			continue;
		}

		UInv_ItemComponent* ItemComp = GetItemComponentFromClass(Preset.ItemClass);
		if (!IsValid(ItemComp))
		{
			UE_LOG(LogTemp, Warning,
				TEXT("[LootContainer] GeneratePresetItems: %s에서 ItemComponent를 찾을 수 없음"),
				*Preset.ItemClass->GetName());
			continue;
		}

		UInv_InventoryItem* NewItem = ContainerInventoryList.AddEntry(ItemComp);
		if (IsValid(NewItem))
		{
			NewItem->SetTotalStackCount(Preset.StackCount);

			if (IsUsingRegisteredSubObjectList() && IsReadyForReplication())
			{
				AddReplicatedSubObject(NewItem);
			}
		}
	}

	UE_LOG(LogTemp, Log, TEXT("[LootContainer] GeneratePresetItems: %d개 프리셋 아이템 생성 완료"),
		PresetItems.Num());
}

void UInv_LootContainerComponent::GenerateRandomLoot()
{
	if (LootTable.Num() == 0) return;

	// MinItems ~ MaxItems 범위에서 랜덤 개수
	const int32 ItemCount = FMath::RandRange(MinItems, MaxItems);

	for (int32 i = 0; i < ItemCount; ++i)
	{
		// 랜덤 선택
		const int32 RandIndex = FMath::RandRange(0, LootTable.Num() - 1);
		TSubclassOf<AActor> ItemClass = LootTable[RandIndex];

		if (!ItemClass)
		{
			continue;
		}

		UInv_ItemComponent* ItemComp = GetItemComponentFromClass(ItemClass);
		if (!IsValid(ItemComp))
		{
			UE_LOG(LogTemp, Warning,
				TEXT("[LootContainer] GenerateRandomLoot: %s에서 ItemComponent를 찾을 수 없음"),
				*ItemClass->GetName());
			continue;
		}

		UInv_InventoryItem* NewItem = ContainerInventoryList.AddEntry(ItemComp);
		if (IsValid(NewItem))
		{
			// 비스택 아이템은 1, 스택 아이템은 기본 CDO 수량 사용
			if (!NewItem->IsStackable())
			{
				NewItem->SetTotalStackCount(1);
			}

			if (IsUsingRegisteredSubObjectList() && IsReadyForReplication())
			{
				AddReplicatedSubObject(NewItem);
			}
		}
	}

	UE_LOG(LogTemp, Log, TEXT("[LootContainer] GenerateRandomLoot: %d개 랜덤 아이템 생성 (테이블 크기: %d)"),
		ItemCount, LootTable.Num());
}

UInv_ItemComponent* UInv_LootContainerComponent::GetItemComponentFromClass(TSubclassOf<AActor> ItemClass) const
{
	if (!ItemClass) return nullptr;

	// CDO에서 ItemComponent 검색
	AActor* CDO = ItemClass->GetDefaultObject<AActor>();
	if (!IsValid(CDO)) return nullptr;

	return CDO->FindComponentByClass<UInv_ItemComponent>();
}
