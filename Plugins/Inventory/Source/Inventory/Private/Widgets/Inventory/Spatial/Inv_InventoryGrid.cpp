// Gihyeon's Inventory Project
#include "Widgets/Inventory/Spatial/Inv_InventoryGrid.h"

#include "Inventory.h"
#include "Blueprint/WidgetLayoutLibrary.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "InventoryManagement/Components/Inv_InventoryComponent.h"
#include "InventoryManagement/Utils/Inv_InventoryStatics.h"
#include "Items/Inv_InventoryItem.h"
#include "Items/Components/Inv_ItemComponent.h"
#include "Items/Fragments/Inv_FragmentTags.h"
#include "Items/Fragments/Inv_ItemFragment.h"
#include "Player/Inv_PlayerController.h"
#include "Widgets/Inventory/GridSlots/Inv_GridSlot.h"
#include "Widgets/Utils/Inv_WidgetUtils.h"
#include "Items/Manifest/Inv_ItemManifest.h"
#include "Widgets/Inventory/HoverItem/Inv_HoverItem.h"
#include "Widgets/Inventory/SlottedItems/Inv_SlottedItem.h"
#include "Widgets/ItemPopUp/Inv_ItemPopUp.h"

// 인벤토리 바인딩 메뉴
void UInv_InventoryGrid::NativeOnInitialized()
{
	Super::NativeOnInitialized();

	ConstructGrid();

	InventoryComponent = UInv_InventoryStatics::GetInventoryComponent(GetOwningPlayer()); // 플레이어의 인벤토리 컴포넌트를 가져온다.
	InventoryComponent->OnItemAdded.AddDynamic(this, &ThisClass::AddItem); // 델리게이트 바인딩 
	InventoryComponent->OnStackChange.AddDynamic(this, &ThisClass::AddStacks); // 스택 변경 델리게이트 바인딩
	InventoryComponent->OnInventoryMenuToggled.AddDynamic(this, &ThisClass::OnInventoryMenuToggled);
	InventoryComponent->OnItemRemoved.AddDynamic(this, &ThisClass::RemoveItem); // 아이템 제거 델리게이트 바인딩
	InventoryComponent->OnMaterialStacksChanged.AddDynamic(this, &ThisClass::UpdateMaterialStacksByTag); // Building 재료 업데이트 바인딩
}

// 매 프레임마다 호출되는 틱 함수 (마우스 Hover에 사용)
void UInv_InventoryGrid::NativeTick(const FGeometry& MyGeometry, float InDeltaTime)
{
	Super::NativeTick(MyGeometry, InDeltaTime);

	//캔버스가 시작하는 왼쪽 모서리 점을 알아보자.
	const FVector2D CanvasPosition = UInv_WidgetUtils::GetWidgetPosition(CanvasPanel); // 캔2버스 패널의 위치 가져오기
	const FVector2D MousePosition = UWidgetLayoutLibrary::GetMousePositionOnViewport(GetOwningPlayer()); // 뷰포트에서 마우스 위치 가져오기

	//캔버스 패널 바깥으로 벗어났는지 여부 확인 (매 틱마다 확인해줌)
	if (CursorExitedCanvas(CanvasPosition, UInv_WidgetUtils::GetWidgetSize(CanvasPanel), MousePosition))
	{
		return; // 캔버스 패널을 벗어났다면 반환
	}

	UpdateTileParameters(CanvasPosition, MousePosition); // 타일 매개변수 업데이트
}

// 마우스 위치에 따라 타일 매개변수를 업데이트하는 함수
void UInv_InventoryGrid::UpdateTileParameters(const FVector2D& CanvasPosition, const FVector2D& MousePosition)
{
	//마우스가 캔버스 패널에 없으면 아무것도 전달하지 않는다.
	//if mouse not in canvas panel, return.
	if (!bMouseWithinCanvas) return;

	// Calculate the tile quadrant, tile index, and coordinates
	const FIntPoint HoveredTileCoordinates = CalculateHoveredCoordinates(CanvasPosition, MousePosition);

	LastTileParameters = TileParameters;// 이전 타일 매개변수를 저장
	TileParameters.TileCoordinats = HoveredTileCoordinates; // 현재 타일 좌표 설정
	TileParameters.TileIndex = UInv_WidgetUtils::GetIndexFromPosition(HoveredTileCoordinates, Columns); // 타일 인덱스 계산
	TileParameters.TileQuadrant = CalculateTileQuadrant(CanvasPosition, MousePosition); // 타일 사분면 계산

	// 그리드 슬롯 하이라이트를 처리하거나 해제하는 것. <- 마우스 위치에 따라 계산하는 함수를 만들 예정.
	// Handle highlight/unhighlight of the grid slots
	OnTileParametersUpdated(TileParameters);

}

void UInv_InventoryGrid::OnTileParametersUpdated(const FInv_TileParameters& Parameters)
{
	if (!IsValid(HoverItem)) return;

	// Get Hover Item's dimensions
	// 호버 아이템의 치수 가져오기
	const FIntPoint Dimensions = HoverItem->GetGridDimensions();
	// Calculate the starting coordinate for highlighting
	// 하이라이팅을 시작하는 좌표를 검색한다
	const FIntPoint StartingCoordinate = CalculateStartingCoordinate(Parameters.TileCoordinats, Dimensions, Parameters.TileQuadrant);
	ItemDropIndex = UInv_WidgetUtils::GetIndexFromPosition(StartingCoordinate, Columns); // 아이템 드롭 인덱스 계산
	
	CurrentQueryResult = CheckHoverPosition(StartingCoordinate, Dimensions); // 호버 위치 확인

	if (CurrentQueryResult.bHasSpace)
	{
		HighlightSlots(ItemDropIndex, Dimensions); // 슬롯 강조 표시
		return;
	}
	UnHighlightSlots(LastHighlightedIndex, LastHighlightedDimensions); // 마지막 강조 표시된 슬롯 강조 해제

	if (CurrentQueryResult.ValidItem.IsValid() && GridSlots.IsValidIndex(CurrentQueryResult.UpperLeftIndex)) // 검색 결과 확인하고
	{
		const FInv_GridFragment* GridFragment = GetFragment<FInv_GridFragment>(CurrentQueryResult.ValidItem.Get(), FragmentTags::GridFragment);
		if (!GridFragment) return;

		ChangeHoverType(CurrentQueryResult.UpperLeftIndex, GridFragment->GetGridSize(), EInv_GridSlotState::GrayedOut); // 호버 타입 변경
	}
}

FInv_SpaceQueryResult UInv_InventoryGrid::CheckHoverPosition(const FIntPoint& Position, const FIntPoint& Dimensions)
{
	// check hover position
	// 호버 위치 확인
	FInv_SpaceQueryResult Result;

	// in the grid bounds?
	// 그리드 경계 내에 있는지?
	if (!IsInGridBounds(UInv_WidgetUtils::GetIndexFromPosition(Position, Columns), Dimensions)) return Result; // 그리드 경계 내에 없으면 빈 결과 반환

	Result.bHasSpace = true; // 공간이 있다고 설정

	// If more than one of the indices is occupied with the same item, we nneed to see if they all have the same upper left index.
	// 여러 인덱스가 동일한 항목으로 점유된 경우, 모두 동일한 왼쪽 위 인덱스를 가지고 있는지 확인해야 합니다.
	TSet<int32> OccupiedUpperLeftIndices;
	UInv_InventoryStatics::ForEach2D(GridSlots, UInv_WidgetUtils::GetIndexFromPosition(Position, Columns), Dimensions, Columns, [&](const UInv_GridSlot* GridSlot)
		{
			if (GridSlot->GetInventoryItem().IsValid())
			{
				//서로 다른 항목이 몇 개 있는지 알고 싶음.
				OccupiedUpperLeftIndices.Add(GridSlot->GetUpperLeftIndex());
				Result.bHasSpace = false; // 공간이 없다고 설정
			}
		});
	
	// if so, is there only one item in the way?
	// 그렇다면, 장애물이 하나뿐인가? (바꿀 수 있을까?)
	if (OccupiedUpperLeftIndices.Num() == 1) // single item at position - it's valid for swapping/combining
	{
		const int32 Index = *OccupiedUpperLeftIndices.CreateConstIterator();
		Result.ValidItem = GridSlots[Index]->GetInventoryItem(); // 격자 슬롯에 배치
		Result.UpperLeftIndex = GridSlots[Index]->GetUpperLeftIndex(); // 왼쪽 위 인덱스 설정
	}
	return Result;
}

bool UInv_InventoryGrid::CursorExitedCanvas(const FVector2D& BoundaryPos, const FVector2D& BoundarySize, const FVector2D& Location) // 커서가 캔버스를 벗어났는지 확인
{
	bLastMouseWithinCanvas = bMouseWithinCanvas;
	bMouseWithinCanvas = UInv_WidgetUtils::IsWithinBounds(BoundaryPos, BoundarySize, Location);
	
	// 마우스가 캔버스 패널에 벗어나게 되면?
	if (!bMouseWithinCanvas && bLastMouseWithinCanvas)
	{
		UnHighlightSlots(LastHighlightedIndex, LastHighlightedDimensions); // 마지막 강조 표시된 슬롯 강조 해제
		return true;
	}
	return false;
}

// 슬롯 강조 표시 함수
void UInv_InventoryGrid::HighlightSlots(const int32 Index, const FIntPoint& Dimensions)
{
	if (!bMouseWithinCanvas) return;
	UnHighlightSlots(LastHighlightedIndex, LastHighlightedDimensions);
	UInv_InventoryStatics::ForEach2D(GridSlots, Index, Dimensions, Columns, [&](UInv_GridSlot* GridSlot)
		{
			GridSlot->SetOccupiedTexture();
		});
	LastHighlightedDimensions = Dimensions;
	LastHighlightedIndex = Index;
}

// 슬롯 강조 해제 함수
void UInv_InventoryGrid::UnHighlightSlots(const int32 Index, const FIntPoint& Dimensions)
{
	UInv_InventoryStatics::ForEach2D(GridSlots, Index, Dimensions, Columns, [&](UInv_GridSlot* GridSlot)
		{
			// 점유된 텍스처에 맞게 설정
			if (GridSlot->IsAvailable())
			{
				GridSlot->SetUnoccupiedTexture(); // 비점유 텍스처 설정
			}
			else
			{
				GridSlot->SetOccupiedTexture(); // 점유 텍스처 설정
			}
		});
}

void UInv_InventoryGrid::ChangeHoverType(const int32 Index, const FIntPoint& Dimensions, EInv_GridSlotState GridSlotState) // 호버 타입 변경
{
	UnHighlightSlots(LastHighlightedIndex, LastHighlightedDimensions);
	UInv_InventoryStatics::ForEach2D(GridSlots, Index, Dimensions, Columns, [State = GridSlotState](UInv_GridSlot* GridSlot)
		{
			switch (State)
			{
			case EInv_GridSlotState::Occupied:
				GridSlot->SetOccupiedTexture();
				break;
			case EInv_GridSlotState::Unoccupied:
				GridSlot->SetUnoccupiedTexture();
				break;
			case EInv_GridSlotState::GrayedOut:
				GridSlot->SetGrayedOutTexture();
				break;
			case EInv_GridSlotState::Selected:
				GridSlot->SetSelectedTexture();
				break;
			}
		});
	LastHighlightedIndex = Index;
	LastHighlightedDimensions = Dimensions;
}

// 수평 및 수직 너비와 관련하여 그것이 있는지 보는 것 (격자가 어느정도 넘어가야 할지 계산해야 하는 것을 만들자.)
FIntPoint UInv_InventoryGrid::CalculateStartingCoordinate(const FIntPoint& Coordinate, const FIntPoint& Dimensions, const EInv_TileQuadrant Quadrant) const
{
	const int32 HasEvenWidth = Dimensions.X % 2 == 0 ? 1 : 0; // 짝수 너비인지 확인
	const int32 HasEvenHeight = Dimensions.Y % 2 == 0 ? 1 : 0; // 짝수 높이인지 확인

	// 이동할 때 사각 좌표 계산을 해보자. -> 당연히 반칸씩 만큼 움직여야겠지?
	FIntPoint StartingCoord;
	switch (Quadrant)
	{
		case EInv_TileQuadrant::TopLeft:
			StartingCoord.X = Coordinate.X - FMath::FloorToInt(0.5f * Dimensions.X); // 격자 차원의 절반만 뺴는 것.
			StartingCoord.Y = Coordinate.Y - FMath::FloorToInt(0.5f * Dimensions.Y); // 격자 차원의 절반만 뺴는 것.
			break;

		case EInv_TileQuadrant::TopRight:
			StartingCoord.X = Coordinate.X - FMath::FloorToInt(0.5f * Dimensions.X) + HasEvenWidth; // 격자 차원의 절반만 뺴는 것.
			StartingCoord.Y = Coordinate.Y - FMath::FloorToInt(0.5f * Dimensions.Y); // 격자 차원의 절반만 뺴는 것.
			break;

		case EInv_TileQuadrant::BottomLeft:
			StartingCoord.X = Coordinate.X - FMath::FloorToInt(0.5f * Dimensions.X); // 격자 차원의 절반만 뺴는 것.
			StartingCoord.Y = Coordinate.Y - FMath::FloorToInt(0.5f * Dimensions.Y) + HasEvenHeight; // 격자 차원의 절반만 뺴는 것.
			break;

		case EInv_TileQuadrant::BottomRight:
			StartingCoord.X = Coordinate.X - FMath::FloorToInt(0.5f * Dimensions.X) + HasEvenWidth; // 격자 차원의 절반만 뺴는 것.
			StartingCoord.Y = Coordinate.Y - FMath::FloorToInt(0.5f * Dimensions.Y) + HasEvenHeight; // 격자 차원의 절반만 뺴는 것.
			break;

		default: // 아무것도 선택하지 않았을 때
			UE_LOG(LogInventory, Error, TEXT("Invalid Quadrant."))
			return FIntPoint(-1, -1);
	}
	return StartingCoord; // 시작 좌표 반환
}

FIntPoint UInv_InventoryGrid::CalculateHoveredCoordinates(const FVector2D& CanvasPosition, const FVector2D& MousePosition) const
{
	// 타일 사분면, 타일 인덱스와 좌표를 계산하기
	// Calculate the tile quadrant, tile index, and coordinates
	return FIntPoint // 와 이런 것도 가능하다고? ㅋㅋ 근데 왜 굳이 이렇게 짜지?
	{
		static_cast<int32>(FMath::FloorToInt((MousePosition.X - CanvasPosition.X) / TileSize)),
		static_cast<int32>(FMath::FloorToInt((MousePosition.Y - CanvasPosition.Y) / TileSize))
	};
}

// 타일 사분면 계산
EInv_TileQuadrant UInv_InventoryGrid::CalculateTileQuadrant(const FVector2D& CanvasPosition, const FVector2D& MousePosition) const
{
	//현재 타일 내에서의 상대 위치를 계산하는 곳.
	//Calculate the relative position within the current tile.
	const float TileLocalX = FMath::Fmod(MousePosition.X - CanvasPosition.X, TileSize); // Fmod가 뭐지?
	const float TileLocalY = FMath::Fmod(MousePosition.Y - CanvasPosition.Y, TileSize); // 

	// 마우스가 어느 사분면에 있는지 결정하는 부분.
	// Determine which quadrant the mouse is in.
	const bool bIsTop = TileLocalY < TileSize / 2.f; // Top if Y is in the upper half
	const bool bIsLeft = TileLocalX < TileSize / 2.f; // Left if X is in the left half

	// 사분면이 어디에 위치했는지 bool값을 정해주는 것.
	EInv_TileQuadrant HoveredTileQuadrant{ EInv_TileQuadrant::None }; // 사분면 변수 선언
	if (bIsTop && bIsLeft) 	{
		HoveredTileQuadrant = EInv_TileQuadrant::TopLeft;
	}
	else if (bIsTop && !bIsLeft)
	{
		HoveredTileQuadrant = EInv_TileQuadrant::TopRight;
	}
	else if (!bIsTop && bIsLeft)
	{
		HoveredTileQuadrant = EInv_TileQuadrant::BottomLeft;
	}
	else // if (!bIsTop && !bIsLeft)
	{
		HoveredTileQuadrant = EInv_TileQuadrant::BottomRight;
	}

	return HoveredTileQuadrant;
}

FInv_SlotAvailabilityResult UInv_InventoryGrid::HasRoomForItem(const UInv_ItemComponent* ItemComponent)
{
	return HasRoomForItem(ItemComponent->GetItemManifest());
}

FInv_SlotAvailabilityResult UInv_InventoryGrid::HasRoomForItem(const UInv_InventoryItem* Item, const int32 StackAmountOverride)
{
	return HasRoomForItem(Item->GetItemManifest(), StackAmountOverride);
}

FInv_SlotAvailabilityResult UInv_InventoryGrid::HasRoomForItem(const FInv_ItemManifest& Manifest, const int32 StackAmountOverride)
{
	FInv_SlotAvailabilityResult Result; // GridTypes.h에서 참고해야할 구조체.
	
	// 아이템을 쌓을 수 있는지 판단하기.
	// Determine if the item is stackable.
	const FInv_StackableFragment* StackableFragment = Manifest.GetFragmentOfType<FInv_StackableFragment>();
	Result.bStackable = StackableFragment != nullptr; // nullptr가 아니라면 쌓을 수 있다!

	// 얼마나 쌓을 수 있는지 판단하는 부분 만들기.
	// Determine how many stacks to add.
	const int32 MaxStackSize = StackableFragment ? StackableFragment->GetMaxStackSize() : 1; // 스택 최대 크기 얻기
	int32 AmountToFill = StackableFragment ? StackableFragment->GetStackCount() : 1; // 널포인트가 아니면 스택을 쌓아준다. 다만 이쪽은 변경 가능하게. 채울 양을 업데이트 해야하니.
	if (StackAmountOverride != -1 && Result.bStackable)
	{
		AmountToFill = StackAmountOverride;
	}
	
	TSet<int32> CheckedIndices; // 이미 확인한 인덱스 집합
	//그리드 슬롯을 반복하여서 확인하기.
	// For each Grid Slot:
	for (const auto& GridSlot : GridSlots)
	{
		// ➡️ 더 이상 채울 아이템이 없다면, (루프를) 일찍 빠져나옵니다.
		// If we don't have anymore to fill, break int32 AmountToFill = Stackut of the early
		if (AmountToFill == 0) break;

		// 이 인덱스가 이미 점유되어있는지 확인하기
		// Is this Index claimed yet?
		if (IsIndexClaimed(CheckedIndices, GridSlot->GetIndex())) continue; // 이미 점유되어 있다면 다음으로 넘어간다. bool값으로 확인.

		// Is the item in grid bounds?
		if (!IsInGridBounds(GridSlot->GetIndex(), GetItemDimensions(Manifest))) continue; // 그리드 경계 내에 있는지 확인 (넣어도 되거나 안 되는 항목 체크 부분)

		// ➡️ 아이템이 여기에 들어갈 수 있습니까? (예: 그리드 경계를 벗어나지 않는지?)
		// Can the item fit here? (i.e. is it out of grid bounds?)
		TSet<int32> TentativelyClaimed;
		if (!HasRoomAtIndex(GridSlot, GetItemDimensions(Manifest), CheckedIndices, TentativelyClaimed, Manifest.GetItemType(), MaxStackSize))
		{
			continue;// 공간이 없다면 다음으로 넘어간다.
		}

		// 얼마나 채워야해?
		// How much to fill?
		const int32 AmountToFillInSlot = DetermineFillAmountForSlot(Result.bStackable, MaxStackSize, AmountToFill, GridSlot); // 슬롯에 채워야 할 양 결정
		if (AmountToFillInSlot == 0) continue; // 채울 양이 0이라면 넘어가면?

		CheckedIndices.Append(TentativelyClaimed); // 확인된 인덱스에 임시로 점유된 인덱스 추가 

		// 채워야 할 남은 양을 업데이트합니다.
		// Update the amount left to fill.
		Result.TotalRoomToFill += AmountToFillInSlot; // 총 채울 수 있는 공간 업데이트
		Result.SlotAvailabilities.Emplace(
			FInv_SlotAvailability{
				HasValidItem(GridSlot) ? GridSlot->GetUpperLeftIndex() : GridSlot->GetIndex(), // 인덱스 설정
				Result.bStackable ? AmountToFillInSlot : 0,
				HasValidItem(GridSlot) // 슬롯에 아이템이 있는지 여부 설정
			}
		); // 복사본을 만들지 않고 여기에 넣는 최적화 기술

		//해당 공간을 할당할 때 마다 얼마나 더 채워야 하는지 업데이트.
		AmountToFill -= AmountToFillInSlot;

		// 모두 반복한 후 나머지는 얼마나 있나요?
		// How much is the Remainder?
		Result.Remainder = AmountToFill;

		if (AmountToFill == 0) return Result; // 다 채웠다면 결과 반환
	}

	return Result;
}

//2차원 범위 내 각 정사각형의 슬롯 제약 조건을 검사하는 부분들.
//게임 플레이 태그로 구분할 것이다.
bool UInv_InventoryGrid::HasRoomAtIndex(const UInv_GridSlot* GridSlot,
										const FIntPoint& Dimensions,
										const TSet<int32>& CheckedIndices,
										TSet<int32>& OutTentativelyClaimed,
										const FGameplayTag& ItemType,
										const int32 MaxStackSize)
{
	// ➡️ 이 인덱스에 공간이 있습니까? (예: 다른 아이템이 길을 막고 있지 않은지?)
	// Is there room at this index? (i.e are there other items in the way?)
	bool bHasRoomAtIndex = true;
	UInv_InventoryStatics::ForEach2D(GridSlots, GridSlot->GetIndex(), Dimensions, Columns, [&](const UInv_GridSlot* SubGridSlot) 
	{	
		if (CheckSlotConstraints(GridSlot, SubGridSlot, CheckedIndices, OutTentativelyClaimed, ItemType, MaxStackSize))
		{
			OutTentativelyClaimed.Add(SubGridSlot->GetIndex());
		}
		else
		{
			bHasRoomAtIndex = false;
		}
	});

	return bHasRoomAtIndex; 
}

//이 제약조건을 다 확인해야 인벤토리에 공간이 있는지 확인해주는 것이다.
bool UInv_InventoryGrid::CheckSlotConstraints(const UInv_GridSlot* GridSlot,
	const UInv_GridSlot* SubGridSlot,
	const TSet<int32>& CheckedIndices,
	TSet<int32>& OutTentativelyClaimed,
	const FGameplayTag& ItemType,
	const int32 MaxStackSize) const
{		
	// Index claimed? 
	// 점유되어 있는지 확인한다.
	if (IsIndexClaimed(CheckedIndices, SubGridSlot->GetIndex())) return false; // 인덱스가 이미 사용하면 false를 반환하는 부분.

	// 유효한 항목이 있습니까?
	// Has valid item? 
	if (!HasValidItem(SubGridSlot))
	{
		OutTentativelyClaimed.Add(SubGridSlot->GetIndex());
		return true;
	}

	// 이 격자가 왼쪽으로 가는 것이 맞을까요?
	// Is this Grid Slot an upper left slot?
	if (!IsUpperLeftSlot(GridSlot, SubGridSlot)) return false; // 왼쪽 위 슬롯이 아니라면 false 반환.

	// ➡️ [!] (항목이 있다면) 스택 가능한 아이템입니까?
	// If so, it this a stackable item?
	const UInv_InventoryItem* SubItem = SubGridSlot->GetInventoryItem().Get();
	if (!SubItem->IsStackable()) return false;

	// 이 아이템이 우리가 추가하려는 아이템과 동일한 유형인가?
	// Is this item the same type as item we're trying to add?
	if (!DoesItemTypeMatch(SubItem, ItemType)) return false;

	// ➡️ [!] 스택 가능하다면, 이 슬롯은 이미 최대 스택 크기입니까?
	// If Stackable, is this slot at the max stack size already?
	if (GridSlot->GetStackCount() >= MaxStackSize) return false;
	 
	return true;
}

FIntPoint UInv_InventoryGrid::GetItemDimensions(const FInv_ItemManifest& Manifest) const
{
	const FInv_GridFragment* GridFragment = Manifest.GetFragmentOfType<FInv_GridFragment>();
	return GridFragment ? GridFragment->GetGridSize() : FIntPoint(1, 1); 
}

bool UInv_InventoryGrid::HasValidItem(const UInv_GridSlot* GridSlot) const
{
	return GridSlot->GetInventoryItem().IsValid();
}

bool UInv_InventoryGrid::IsUpperLeftSlot(const UInv_GridSlot* GridSlot, const UInv_GridSlot* SubGridSlot) const
{
	return SubGridSlot->GetUpperLeftIndex() == GridSlot->GetIndex();
}

bool UInv_InventoryGrid::DoesItemTypeMatch(const UInv_InventoryItem* SubItem, const FGameplayTag& ItemType) const
{
	return SubItem->GetItemManifest().GetItemType().MatchesTagExact(ItemType);
}

bool UInv_InventoryGrid::IsInGridBounds(const int32 StartIndex, const FIntPoint& ItemDimensions) const
{
	if (StartIndex < 0 || StartIndex >= GridSlots.Num()) return false;
	const int32 EndColumn = (StartIndex % Columns) + ItemDimensions.X;
	const int32 EndRow = (StartIndex / Columns) + ItemDimensions.Y;
	return EndColumn <= Columns && EndRow <= Rows;
}

int32 UInv_InventoryGrid::DetermineFillAmountForSlot(const bool bStackable, const int32 MaxStackSize, const int32 AmountToFill, const UInv_GridSlot* GridSlot) const
{
	// calculate room in the slot
	// 슬롯에 남은 공간 계산
	const int32 RoomInSlot = MaxStackSize - GetStackAmount(GridSlot); 

	// if stackable, need the minium between AmountToFill and RommInSlot.
	// 스택 가능하다면, 채워야 할 양과 슬롯의 남은 공간 중 최소값을 반환. 아니라면 1 반환
	return bStackable ? FMath::Min(AmountToFill, RoomInSlot) : 1;
}


int32 UInv_InventoryGrid::GetStackAmount(const UInv_GridSlot* GridSlot) const
{
	int32 CurrentSlotStackCount = GridSlot->GetStackCount();
	// 스택이 없을 경우 개수를 세어 실제 스택 개수를 파악하는 함수
	// If we are at a slot that dosen't hold the stack count. we must get the actual stack count.
	if (const int32 UpperLeftIndex = GridSlot->GetUpperLeftIndex(); UpperLeftIndex != INDEX_NONE)
	{
		UInv_GridSlot* UpperLeftGridSlot = GridSlots[UpperLeftIndex];
		CurrentSlotStackCount = UpperLeftGridSlot->GetStackCount();
	}
	return CurrentSlotStackCount;
}

bool UInv_InventoryGrid::IsRightClick(const FPointerEvent& MouseEvent) const // 마우스 오른쪽 클릭인지 확인
{
	return MouseEvent.GetEffectingButton() == EKeys::RightMouseButton;
}

bool UInv_InventoryGrid::IsLeftClick(const FPointerEvent& MouseEvent) const // 마우스 왼쪽 클릭인지 확인
{
	return MouseEvent.GetEffectingButton() == EKeys::LeftMouseButton;
}

void UInv_InventoryGrid::PickUp(UInv_InventoryItem* ClickedInventoryItem, const int32 GridIndex) //집었을 때 개수까지 알아와주기
{
	// Assign the hover item
	// 아이템을 집었을 때 호버 아이템으로 할당하는 부분
	AssignHoverItem(ClickedInventoryItem, GridIndex, GridIndex);

	// Remove Clicked Item from the grid
	RemoveItemFromGrid(ClickedInventoryItem, GridIndex);
}

// 호버 아이템 할당 부분
void UInv_InventoryGrid::AssignHoverItem(UInv_InventoryItem* InventoryItem, const int32 GridIndex, const int32 PreviousGridIndex)
{
	// ⭐ Nullptr 체크 (EXCEPTION_ACCESS_VIOLATION 방지)
	if (!IsValid(InventoryItem))
	{
		UE_LOG(LogTemp, Error, TEXT("[AssignHoverItem] ❌ InventoryItem이 nullptr입니다! GridIndex=%d"), GridIndex);
		return;
	}

	AssignHoverItem(InventoryItem);

	HoverItem->SetPreviousGridIndex(PreviousGridIndex);
	HoverItem->UpdateStackCount(InventoryItem->IsStackable() ? GridSlots[GridIndex]->GetStackCount() : 0);
}

void UInv_InventoryGrid::RemoveItemFromGrid(UInv_InventoryItem* InventoryItem, const int32 GridIndex) // 아이템을 Hover 한 뒤로.
{
	UE_LOG(LogTemp, Warning, TEXT("[RemoveItemFromGrid] 호출됨: GridIndex=%d, Item=%s"),
		GridIndex, IsValid(InventoryItem) ? *InventoryItem->GetItemManifest().GetItemType().ToString() : TEXT("NULL"));

	const FInv_GridFragment* GridFragment = GetFragment<FInv_GridFragment>(InventoryItem, FragmentTags::GridFragment);
	if (!GridFragment) return;

	UInv_InventoryStatics::ForEach2D(GridSlots, GridIndex, GridFragment->GetGridSize(), Columns, [&](UInv_GridSlot* GridSlot)
		{
			//인벤토리 아이템 옮기기인데. 기존 있던 것을 0으로 두고 새로운 곳으로 인덱스를 둔다. (람다 함수 부분)
			GridSlot->SetInventoryItem(nullptr);
			GridSlot->SetUpperLeftIndex(INDEX_NONE);
			GridSlot->SetUnoccupiedTexture();
			GridSlot->SetAvailable(true);
			GridSlot->SetStackCount(0);
		});

	if (SlottedItems.Contains(GridIndex))
	{
		TObjectPtr<UInv_SlottedItem> FoundSlottedItem;
		SlottedItems.RemoveAndCopyValue(GridIndex, FoundSlottedItem);

		UE_LOG(LogTemp, Log, TEXT("[RemoveItemFromGrid] SlottedItem 삭제: GridIndex=%d"), GridIndex);

		FoundSlottedItem->RemoveFromParent();
	}
	else
	{
		UE_LOG(LogTemp, Warning, TEXT("[RemoveItemFromGrid] GridIndex=%d는 SlottedItems에 없음"), GridIndex);
	}
}


void UInv_InventoryGrid::AssignHoverItem(UInv_InventoryItem* InventoryItem) // 이걸 참조하면 나중에 그걸 만들 수 있겠지? 창고
{
	// ⭐ Nullptr 체크 (EXCEPTION_ACCESS_VIOLATION 방지)
	if (!IsValid(InventoryItem))
	{
		UE_LOG(LogTemp, Error, TEXT("[AssignHoverItem] ❌ InventoryItem이 nullptr입니다!"));
		HoverItem->SetVisibility(ESlateVisibility::Hidden);
		return;
	}
	if (!IsValid(HoverItem))
	{
		HoverItem = CreateWidget<UInv_HoverItem>(GetOwningPlayer(), HoverItemClass);
	}

	const FInv_GridFragment* GridFragment = GetFragment<FInv_GridFragment>(InventoryItem, FragmentTags::GridFragment);
	const FInv_ImageFragment* ImageFragment = GetFragment<FInv_ImageFragment>(InventoryItem, FragmentTags::IconFragment);
	if (!GridFragment || !ImageFragment) return;

	// 이미지 불러오는 것들.
	const FVector2D DrawSize = GetDrawSize(GridFragment);

	FSlateBrush IconBrush;
	IconBrush.SetResourceObject(ImageFragment->GetIcon());
	IconBrush.DrawAs = ESlateBrushDrawType::Image;
	IconBrush.ImageSize = DrawSize * UWidgetLayoutLibrary::GetViewportScale(this); // 뷰포트 스케일로 곱해주기. (왜 뷰포트로 곱해줄까?)

	HoverItem->SetImageBrush(IconBrush);
	HoverItem->SetGridDimensions(GridFragment->GetGridSize());
	HoverItem->SetInventoryItem(InventoryItem);
	HoverItem->SetIsStackable(InventoryItem->IsStackable());

	GetOwningPlayer()->SetMouseCursorWidget(EMouseCursor::Default, HoverItem); // 마우스 커서 위젯 설정
}

void UInv_InventoryGrid::OnHide()
{
	PutHoverItemBack();
}


// 같은 아이템이면 수량 쌓기
void UInv_InventoryGrid::AddStacks(const FInv_SlotAvailabilityResult& Result) 
{
	if (!MatchesCategory(Result.Item.Get())) return;

	// SlotAvailabilities가 비어있으면 Item으로 슬롯을 직접 찾아서 업데이트
	if (Result.SlotAvailabilities.Num() == 0 && Result.Item.IsValid())
	{
		UE_LOG(LogTemp, Warning, TEXT("=== AddStacks 호출 (Split 대응) ==="));
		UE_LOG(LogTemp, Warning, TEXT("📥 [클라이언트] Item포인터: %p, ItemType: %s, 서버 총량: %d"), 
			Result.Item.Get(), *Result.Item->GetItemManifest().GetItemType().ToString(), Result.TotalRoomToFill);
		
		// 🔍 디버깅: UI 상태 확인 (업데이트 전)
		UE_LOG(LogTemp, Warning, TEXT("🔍 [클라이언트] UI 슬롯 상태 (업데이트 전):"));
		int32 UITotalBefore = 0;
		for (const auto& [Index, SlottedItem] : SlottedItems)
		{
			if (!GridSlots.IsValidIndex(Index)) continue;
			UInv_InventoryItem* GridSlotItem = GridSlots[Index]->GetInventoryItem().Get();
			// ⭐ Phase 7 롤백: 포인터 비교로 복원 (태그 매칭은 다른 Entry의 슬롯까지 영향을 줌)
			if (GridSlotItem == Result.Item)
			{
				int32 SlotCount = GridSlots[Index]->GetStackCount();
				UITotalBefore += SlotCount;
				UE_LOG(LogTemp, Warning, TEXT("  슬롯[%d]: Item포인터=%p, StackCount=%d"), 
					Index, GridSlotItem, SlotCount);
			}
		}
		UE_LOG(LogTemp, Warning, TEXT("🔍 [클라이언트] UI 총량 (업데이트 전): %d"), UITotalBefore);
		UE_LOG(LogTemp, Warning, TEXT("🔍 [클라이언트] 차감량: %d - %d = %d"), 
			UITotalBefore, Result.TotalRoomToFill, UITotalBefore - Result.TotalRoomToFill);
		
		// ⭐ 1단계: 같은 Item을 가진 모든 슬롯 찾기 (Split 대응!)
		TArray<int32> MatchedIndices;
		int32 TotalUICount = 0;
		
		for (const auto& [Index, SlottedItem] : SlottedItems)
		{
			UInv_InventoryItem* GridSlotItem = GridSlots[Index]->GetInventoryItem().Get();
			
			// ⭐ Phase 7 롤백: 포인터 비교로 복원
			if (GridSlots.IsValidIndex(Index) && GridSlotItem == Result.Item)
			{
				MatchedIndices.Add(Index);
				TotalUICount += GridSlots[Index]->GetStackCount();
				UE_LOG(LogTemp, Warning, TEXT("📌 [클라이언트] 매칭된 슬롯 발견: Index=%d, CurrentCount=%d, Item포인터=%p"), 
					Index, GridSlots[Index]->GetStackCount(), GridSlotItem);
			}
		}
		
		if (MatchedIndices.Num() == 0)
		{
			UE_LOG(LogTemp, Error, TEXT("❌ [클라이언트] AddStacks: Item에 해당하는 슬롯을 찾지 못함! Item포인터: %p"), Result.Item.Get());
			return;
		}
		
		UE_LOG(LogTemp, Warning, TEXT("📊 [클라이언트] 총 %d개 슬롯 매칭됨, UI 총량: %d → 서버 총량: %d"), 
			MatchedIndices.Num(), TotalUICount, Result.TotalRoomToFill);
		
		// ⭐ 1.5단계: 큰 스택부터 처리하도록 정렬 (안정적인 분배를 위해)
		MatchedIndices.Sort([this](int32 A, int32 B) {
			return GridSlots[A]->GetStackCount() > GridSlots[B]->GetStackCount();
		});
		
		UE_LOG(LogTemp, Warning, TEXT("🔀 [클라이언트] 슬롯 정렬 완료 (큰 스택부터):"));
		for (int32 Index : MatchedIndices)
		{
			UE_LOG(LogTemp, Warning, TEXT("  슬롯[%d]: StackCount=%d"), Index, GridSlots[Index]->GetStackCount());
		}
		
		// ⭐ 2단계: 서버 총량을 슬롯들에 분배
		int32 RemainingToDistribute = Result.TotalRoomToFill;
		TArray<int32> IndicesToRemove;
		
		UE_LOG(LogTemp, Warning, TEXT("🔧 [클라이언트] 슬롯별 분배 시작 (RemainingToDistribute=%d):"), RemainingToDistribute);
		
		for (int32 Index : MatchedIndices)
		{
			int32 OldCount = GridSlots[Index]->GetStackCount();
			int32 NewCount = FMath::Min(OldCount, RemainingToDistribute);
			RemainingToDistribute -= NewCount;
			
			UE_LOG(LogTemp, Warning, TEXT("  슬롯[%d]: OldCount=%d, NewCount=%d, Remaining=%d → %d"), 
				Index, OldCount, NewCount, RemainingToDistribute + NewCount, RemainingToDistribute);
			
			if (NewCount <= 0)
			{
				// 이 슬롯은 제거해야 함
				IndicesToRemove.Add(Index);
				UE_LOG(LogTemp, Warning, TEXT("  ❌ 슬롯[%d]: 제거 예약 (%d → 0)"), Index, OldCount);
			}
			else
			{
				// 개수 업데이트
				GridSlots[Index]->SetStackCount(NewCount);
				if (SlottedItems.Contains(Index) && IsValid(SlottedItems[Index]))
				{
					SlottedItems[Index]->UpdateStackCount(NewCount);
				}
				UE_LOG(LogTemp, Warning, TEXT("  ✅ 슬롯[%d]: UI 업데이트 (%d → %d)"), Index, OldCount, NewCount);
			}
		}
		
		// ⭐ 3단계: 제거할 슬롯들 제거
		UE_LOG(LogTemp, Warning, TEXT("🗑️ [클라이언트] 제거할 슬롯 개수: %d"), IndicesToRemove.Num());
		for (int32 IndexToRemove : IndicesToRemove)
		{
			if (GridSlots.IsValidIndex(IndexToRemove))
			{
				UInv_InventoryItem* ItemToRemove = GridSlots[IndexToRemove]->GetInventoryItem().Get();
				if (IsValid(ItemToRemove))
				{
					RemoveItemFromGrid(ItemToRemove, IndexToRemove);
					UE_LOG(LogTemp, Warning, TEXT("  ✅ 슬롯[%d]: UI에서 제거 완료 (Item포인터=%p)"), IndexToRemove, ItemToRemove);
				}
			}
		}
		
		// 🔍 디버깅: UI 상태 확인 (업데이트 후)
		UE_LOG(LogTemp, Warning, TEXT("🔍 [클라이언트] UI 슬롯 상태 (업데이트 후):"));
		int32 UITotalAfter = 0;
		for (const auto& [Index, SlottedItem] : SlottedItems)
		{
			if (!GridSlots.IsValidIndex(Index)) continue;
			UInv_InventoryItem* GridSlotItem = GridSlots[Index]->GetInventoryItem().Get();
			// ⭐ Phase 7 롤백: 포인터 비교로 복원
			if (GridSlotItem == Result.Item)
			{
				int32 SlotCount = GridSlots[Index]->GetStackCount();
				UITotalAfter += SlotCount;
				UE_LOG(LogTemp, Warning, TEXT("  슬롯[%d]: Item포인터=%p, StackCount=%d"), 
					Index, GridSlotItem, SlotCount);
			}
		}
		UE_LOG(LogTemp, Warning, TEXT("🔍 [클라이언트] UI 총량 (업데이트 후): %d (예상: %d)"), UITotalAfter, Result.TotalRoomToFill);
		
		if (UITotalAfter != Result.TotalRoomToFill)
		{
			UE_LOG(LogTemp, Error, TEXT("⚠️ [클라이언트] UI 총량이 서버 총량과 일치하지 않습니다! UI=%d, 서버=%d"), 
				UITotalAfter, Result.TotalRoomToFill);
		}
		
		UE_LOG(LogTemp, Warning, TEXT("=== AddStacks 완료 ==="));
		return;
	}

	// 기존 로직: SlotAvailabilities가 있을 때
	for (const auto& Availability : Result.SlotAvailabilities)
	{
		if (Availability.bItemAtIndex) // 해당 인덱스에 아이템이 있는 경우
		{
			const auto& GridSlot = GridSlots[Availability.Index];
			const auto& SlottedItem = SlottedItems.FindChecked(Availability.Index);
			SlottedItem->UpdateStackCount(GridSlot->GetStackCount() + Availability.AmountToFill); // 스택 수 업데이트
			GridSlot->SetStackCount(GridSlot->GetStackCount() + Availability.AmountToFill); // 그리드 슬롯에도 스택 수 업데이트
		}
		else // 해당 인덱스에 아이템이 없는 경우
		{
			AddItemAtIndex(Result.Item.Get(), Availability.Index, Result.bStackable, Availability.AmountToFill, Result.EntryIndex); // 인덱스에 아이템 추가
			UpdateGridSlots(Result.Item.Get(), Availability.Index, Result.bStackable, Availability.AmountToFill); // 그리드 슬롯 업데이트
		}
	}
}

// 슬롯에 있는 아이템을 마우스로 클릭했을 때 
void UInv_InventoryGrid::OnSlottedItemClicked(int32 GridIndex, const FPointerEvent& MouseEvent)
{
	// 마우스를 가장자리 넘을 때 언호버 처리 해서 자연스러운 아이템 Detail칸 열게 하기
	UInv_InventoryStatics::ItemUnhovered(GetOwningPlayer()); // 아이템 언호버 처리
	
	//UE_LOG(LogTemp, Warning, TEXT("Clicked on item at index %d"), GridIndex); // 아이템 클릭 디버깅입니다.
	check(GridSlots.IsValidIndex(GridIndex)); // 유효한 인덱스인지 확인
	UInv_InventoryItem* ClickedInventoryItem = GridSlots[GridIndex]->GetInventoryItem().Get(); // 클릭한 아이템 가져오기
	
	// ⭐ nullptr 체크 추가 (MoveItemByCurrentIndex 후 원래 위치 클릭 시 크래시 방지)
	if (!IsValid(ClickedInventoryItem))
	{
		UE_LOG(LogTemp, Warning, TEXT("[OnSlottedItemClicked] ⚠️ GridIndex=%d에 InventoryItem 없음, 무시"), GridIndex);
		return;
	}

	//좌클릭을 눌렀을 때 실행되는 호버 부분 실행 부분
	if (!IsValid(HoverItem) && IsLeftClick(MouseEvent))
	{
		// 호버 항목을 지정하고 그리드에서 슬롯이 있는 항목을 제거하는 부분을 구현하자.
		// Assign the hover item, and remove the slotted item from the grid.
		PickUp(ClickedInventoryItem, GridIndex);
		return;
	}
	
	if (IsRightClick(MouseEvent)) // 우클릭을 눌렀을 때 실행되는 팝업 부분 실행 부분
	{
		CreateItemPopUp(GridIndex); // 팝업 생성 함수 호출
		return;
	}
	
	
	// 호버아이템이 넘어간다면?
	// 호버 된 항목과 (클릭된) 인벤토리 항목이 유형을 공유하고, 쌓을 수 있는가?
	// Do the hovered item and the clicked inventory item share a type, and are they Stackable?
	if (IsSameStackable(ClickedInventoryItem))
	{
		// 호버 아이템의 스택을 소모해야 하는가? 선택한 스롯에 여우 공간이 없으면
		// Should we consume the hover item's stacks? (Room in the clicked slot == 0 && HoveredStackCound < MaxStackSize)
		const int32 ClickedStackCount = GridSlots[GridIndex]->GetStackCount(); // 클릭된 슬롯의 스택 수
		const FInv_StackableFragment* StackableFragment = ClickedInventoryItem->GetItemManifest().GetFragmentOfType<FInv_StackableFragment>(); // 그리드의 최대스택 쌓을 수 있는지 얻기 위해
		const int32 MaxStackSize = StackableFragment->GetMaxStackSize(); // 최대 쌓기 스택을 얻기 위한 것
		const int32 RoomInClickedSlot = MaxStackSize - ClickedStackCount; // 클릭된 슬롯의 남은 공간 계산
		const int32 HoveredStackCount = HoverItem->GetStackCount(); // 호버된 아이템의 스택 수
		
		// Should we swap their stack counts? (Room in the clicked slot == 0 && HoveredStackCount < MaxStackSize)
		if (ShouldSwapStackCounts(RoomInClickedSlot, HoveredStackCount, MaxStackSize)) // 스택 수를 교체할 수 있는지 확인하는 것  
		{
			// TODO: Swap Stack Counts
			// 스택 교체 부분
			SwapStackCounts(ClickedStackCount, HoveredStackCount, GridIndex); // 스택 수 교체 함수
			return;
		}
		
		// Should we consume the hover item's stacks? (Room in the clicked slot >= HoveredStackCount)
		// 호버 아이템의 스택을 소모해야 하는 것일까? (아이템을 소모하는 부분)
		if (ShouldConsumeHoverItemStacks(HoveredStackCount, RoomInClickedSlot))
		{
			// TODO: ConsumeHoverItemSatcks
			ConsumeHoverItemStacks(ClickedStackCount, HoveredStackCount, GridIndex); // 호버 아이템 스택 소모 함수
			return;
		}
		
		// 클릭된 아이템의 스택을 채워야 하는가? (그리고 호버 아이템은 소모하지 않는가?)
		// Should we fill in the stacks of the clicked item? (and not consume the hover item)
		if (ShouldFillInStack(RoomInClickedSlot, HoveredStackCount)) // 스택을 채울 수 있는지 확인하는 것
		{
			// 그리드 슬롯을 가져오고 스택 카운트를 세는 것. 클릭하는 항목에 여유가 있을 때 발생하는 부분
			FillInStack(RoomInClickedSlot, HoveredStackCount - RoomInClickedSlot, GridIndex); // 스택 채우기 함수
			return;
		}
		
		// Clicked Slot is already full - do nothing
		// 클릭된 슬롯이 이미 가득 찼습니다 - 아무 것도 하지 않습니다
		if (RoomInClickedSlot == 0)
		{
			// 슬롯이 가득 찼을 때 아무 것도 하지 않는 부분
			return;
		}
	}
	
	// Make sure we can swap with a valid item
	// 유효한 항목과 교체할 수 있는지 확인
	if (CurrentQueryResult.ValidItem.IsValid())
	{
		// 호버 아이템과 교체(Swap)하기
		// Swap with the hover item.
		SwapWithHoverItem(ClickedInventoryItem, GridIndex);
	}
	

}

//우클릭 팝업을 생성하는 함수를 만드는 부분. (아이템 디테일 부분들) 
void UInv_InventoryGrid::CreateItemPopUp(const int32 GridIndex)
{
	UInv_InventoryItem* RightClickedItem = GridSlots[GridIndex]->GetInventoryItem().Get();
	if (!IsValid(RightClickedItem)) return; //오른쪽 클릭을 확인했을 때.
	if (IsValid(GridSlots[GridIndex]->GetItemPopUp())) return; // 이미 팝업이 있다면 리턴
	
	ItemPopUp = CreateWidget<UInv_ItemPopUp>(this, ItemPopUpClass); // 팝업 위젯 생성
	GridSlots[GridIndex]->SetItemPopUp(ItemPopUp);
	
	//마우스 올려진 곳에 툴팁 같은 것이 등장하는 부분인가?
	OwningCanvasPanel->AddChild(ItemPopUp);
	UCanvasPanelSlot* CanvasSlot = UWidgetLayoutLibrary::SlotAsCanvasSlot(ItemPopUp);
	const FVector2D MousePosition = UWidgetLayoutLibrary::GetMousePositionOnViewport(GetOwningPlayer());
	CanvasSlot->SetPosition(MousePosition - ItemPopUpOffset); // 마우스 위치에 팝업 위치 설정
	CanvasSlot->SetSize(ItemPopUp->GetBoxSize());
	
	const int32 SliderMax = GridSlots[GridIndex]->GetStackCount() - 1; // 슬라이더 최대값 설정
	if (RightClickedItem->IsStackable() && SliderMax > 0)
	{
		ItemPopUp->OnSplit.BindDynamic(this, &ThisClass::OnPopUpMenuSplit); // 분할 바인딩
		ItemPopUp->SetSliderParams(SliderMax, FMath::Max(1, GridSlots[GridIndex] -> GetStackCount() / 2)); // 슬라이더 파라미터 설정
	}
	else
	{
		ItemPopUp->CollapseSplitButton(); // 분할 버튼 숨기기
	}
	
	ItemPopUp->OnDrop.BindDynamic(this, &ThisClass::OnPopUpMenuDrop); // 드롭 바인딩 (바인딩은 키를 등록하는 부분이었나)
	
	if (RightClickedItem->IsConsumable())
	{
		ItemPopUp->OnConsume.BindDynamic(this, &ThisClass::OnPopUpMenuConsume);
	}
	else
	{
		ItemPopUp->CollapseConsumeButton();
	}
}

void UInv_InventoryGrid::PutHoverItemBack()
{
	if (!IsValid(HoverItem)) return;
	
	FInv_SlotAvailabilityResult Result = HasRoomForItem(HoverItem->GetInventoryItem(), HoverItem->GetStackCount());
	Result.Item = HoverItem->GetInventoryItem();
	
	AddStacks(Result);
	ClearHoverItem();
}

void UInv_InventoryGrid::DropItem()
{
	//위젯 쪽에서 먼저 처리하게 하기
	if (!IsValid(HoverItem)) return;
	if (!IsValid(HoverItem->GetInventoryItem())) return;
	
	// TODO : Tell the server to actually drop the item
	// TODO : 서버에서 실제로 아이템을 떨어뜨리도록 지시하는 일
	InventoryComponent->Server_DropItem(HoverItem->GetInventoryItem(), HoverItem->GetStackCount()); // 서버에 아이템 드롭 요청
	
	ClearHoverItem();
	ShowCursor();
}

bool UInv_InventoryGrid::HasHoverItem() const // 호버 아이템이 있는지 확인
{
	return IsValid(HoverItem); // 호버 아이템이 유효한지 확인
}

// 호버 아이템 반환 <- 장비 슬롯
UInv_HoverItem* UInv_InventoryGrid::GetHoverItem() const 
{
	return HoverItem;
}

// 인벤토리 스택 쌓는 부분.
void UInv_InventoryGrid::AddItem(UInv_InventoryItem* Item, int32 EntryIndex)
{
	//아이템 그리드 체크 부분?
	if (!MatchesCategory(Item))
	{
		UE_LOG(LogTemp, Log, TEXT("[AddItem] 카테고리 불일치: %s, Grid=%d"),
			*Item->GetItemManifest().GetItemType().ToString(), (int32)ItemCategory);
		return;
	}

	UE_LOG(LogTemp, Warning, TEXT("========== [AddItem] 아이템 추가/업데이트 시작 =========="));
	UE_LOG(LogTemp, Warning, TEXT("[AddItem] Item=%s, EntryIndex=%d, Grid=%d"),
		*Item->GetItemManifest().GetItemType().ToString(), EntryIndex, (int32)ItemCategory);
	UE_LOG(LogTemp, Warning, TEXT("[AddItem] 찾을 Item포인터: %p, TotalStackCount: %d"), Item, Item->GetTotalStackCount());

	// 🔍 디버깅: 현재 Grid 슬롯 상태 출력
	UE_LOG(LogTemp, Warning, TEXT("[AddItem] 🔍 현재 Grid 슬롯 상태 (SlottedItems 개수: %d):"), SlottedItems.Num());
	for (const auto& [Index, SlottedItem] : SlottedItems)
	{
		if (!GridSlots.IsValidIndex(Index)) continue;
		UInv_InventoryItem* GridSlotItem = GridSlots[Index]->GetInventoryItem().Get();
		if (GridSlotItem)
		{
			UE_LOG(LogTemp, Warning, TEXT("  슬롯[%d]: Item포인터=%p, StackCount=%d, Type=%s"),
				Index, GridSlotItem, GridSlots[Index]->GetStackCount(),
				*GridSlotItem->GetItemManifest().GetItemType().ToString());
		}
	}

	// ⭐⭐⭐ 1단계: EntryIndex로 우선 검색 (더 정확함!)
	UE_LOG(LogTemp, Warning, TEXT("[AddItem] 🔍 1단계: EntryIndex=%d 로 검색 시작..."), EntryIndex);

	for (const auto& [Index, SlottedItem] : SlottedItems)
	{
		if (!IsValid(SlottedItem)) continue;

		// 🔍 디버깅: 각 슬롯의 EntryIndex 상태 확인
		int32 SlotEntryIndex = SlottedItem->GetEntryIndex();
		UE_LOG(LogTemp, Warning, TEXT("[AddItem] 슬롯[%d] 검사: SlottedItem.EntryIndex=%d, 찾는값=%d, 매칭=%s"),
			Index, SlotEntryIndex, EntryIndex, (SlotEntryIndex == EntryIndex) ? TEXT("YES") : TEXT("NO"));

		// ⚠️ EntryIndex가 -1이면 아직 설정되지 않은 상태
		if (SlotEntryIndex == -1 || SlotEntryIndex == 0)
		{
			UE_LOG(LogTemp, Warning, TEXT("[AddItem] ⚠️ 슬롯[%d]의 EntryIndex가 초기값(%d)! SetEntryIndex() 호출 누락 의심"), Index, SlotEntryIndex);
		}

		// ⭐ EntryIndex로 매칭!
		if (SlotEntryIndex == EntryIndex)
		{
			// ✅ EntryIndex 매칭 발견! 스택 카운트만 업데이트
			int32 NewStackCount = Item->GetTotalStackCount();
			int32 OldStackCount = GridSlots[Index]->GetStackCount();

			UE_LOG(LogTemp, Warning, TEXT("[AddItem] ⭐ EntryIndex 매칭! GridIndex=%d, EntryIndex=%d"), Index, EntryIndex);
			UE_LOG(LogTemp, Warning, TEXT("[AddItem]   스택 업데이트: %d → %d"), OldStackCount, NewStackCount);

			// Item 포인터도 업데이트 (리플리케이션 후 포인터가 바뀔 수 있음)
			GridSlots[Index]->SetInventoryItem(Item);
			GridSlots[Index]->SetStackCount(NewStackCount);
			if (IsValid(SlottedItem))
			{
				SlottedItem->UpdateStackCount(NewStackCount);
			}

			UE_LOG(LogTemp, Warning, TEXT("========== [AddItem] 스택 업데이트 완료! (EntryIndex 매칭) =========="));
			return; // ⭐ 새 슬롯 추가 필요 없음!
		}
	}

	// ⚠️ 1단계 검색 실패!
	UE_LOG(LogTemp, Warning, TEXT("[AddItem] ❌ 1단계 실패: EntryIndex=%d 를 가진 SlottedItem을 찾지 못함! 2단계(포인터 검색)으로 진행..."), EntryIndex);

	// ⭐⭐⭐ 2단계: Item 포인터로 검색 (Fallback)
	UE_LOG(LogTemp, Warning, TEXT("[AddItem] 🔍 2단계: Item 포인터=%p 로 검색 시작..."), Item);
	for (const auto& [Index, SlottedItem] : SlottedItems)
	{
		if (!GridSlots.IsValidIndex(Index)) continue;

		UInv_InventoryItem* GridSlotItem = GridSlots[Index]->GetInventoryItem().Get();
		if (GridSlotItem == Item) // ⭐ 포인터 비교로 같은 아이템인지 확인
		{
			// ✅ 기존 아이템 발견! 스택 카운트만 업데이트
			int32 NewStackCount = Item->GetTotalStackCount();
			int32 OldStackCount = GridSlots[Index]->GetStackCount();

			UE_LOG(LogTemp, Warning, TEXT("[AddItem] ⭐ Item 포인터 매칭! GridIndex=%d"), Index);
			UE_LOG(LogTemp, Warning, TEXT("[AddItem]   스택 업데이트: %d → %d"), OldStackCount, NewStackCount);

			GridSlots[Index]->SetStackCount(NewStackCount);
			if (IsValid(SlottedItem))
			{
				SlottedItem->UpdateStackCount(NewStackCount);
			}

			UE_LOG(LogTemp, Warning, TEXT("========== [AddItem] 스택 업데이트 완료! (Item 포인터 매칭) =========="));
			return; // ⭐ 새 슬롯 추가 필요 없음!
		}
	}

	UE_LOG(LogTemp, Warning, TEXT("[AddItem] ❌ 기존 슬롯 못 찾음 (EntryIndex/포인터 모두 실패), 새 슬롯 생성..."));

	// ⭐ [Phase 5 Fix] 2.4단계: GridIndex 체크 (로드 시 저장된 위치로 배치!)
	// Entry에 GridIndex가 설정되어 있고 카테고리가 일치하면 해당 위치에 배치
	if (InventoryComponent.IsValid())
	{
		FInv_InventoryFastArray& InventoryList = InventoryComponent->GetInventoryList();
		if (InventoryList.Entries.IsValidIndex(EntryIndex))
		{
			int32 SavedGridIndex = InventoryList.Entries[EntryIndex].GridIndex;
			uint8 SavedGridCategory = InventoryList.Entries[EntryIndex].GridCategory;
			
			// 이 Grid의 카테고리와 일치하고, GridIndex가 유효한 경우
			if (SavedGridCategory == static_cast<uint8>(ItemCategory) && 
				SavedGridIndex >= 0 && 
				GridSlots.IsValidIndex(SavedGridIndex) && 
				!SlottedItems.Contains(SavedGridIndex))
			{
				UE_LOG(LogTemp, Warning, TEXT("[AddItem] [Phase 5 Fix] Entry[%d] GridIndex=%d (saved pos), placing..."), 
					EntryIndex, SavedGridIndex);
				
				const int32 ActualStackCount = Item->GetTotalStackCount();
				const bool bStackable = Item->IsStackable();
				AddItemAtIndex(Item, SavedGridIndex, bStackable, ActualStackCount, EntryIndex);
				
				// ⭐ [Phase 5 Fix] 로드 시에는 Server_UpdateItemGridPosition RPC 스킵!
				bSuppressServerSync = true;
				UpdateGridSlots(Item, SavedGridIndex, bStackable, ActualStackCount);
				bSuppressServerSync = false;
				
				UE_LOG(LogTemp, Warning, TEXT("[AddItem] [Phase 5 Fix] Placed at saved GridIndex=%d (no RPC)"), SavedGridIndex);
				UE_LOG(LogTemp, Warning, TEXT("========== [AddItem] 아이템 추가 완료 =========="));
				return;
			}
		}
	}

	// ⭐⭐⭐ 2.5단계: TargetGridIndex 체크 (Split 아이템의 마우스 위치 배치!)
	// Entry에 TargetGridIndex가 설정되어 있으면 해당 위치에 직접 배치
	UE_LOG(LogTemp, Warning, TEXT("[AddItem] 🔍 2.5단계 조건 체크 시작"));
	UE_LOG(LogTemp, Warning, TEXT("[AddItem]   InventoryComponent.IsValid()=%s"), InventoryComponent.IsValid() ? TEXT("true") : TEXT("false"));

	if (InventoryComponent.IsValid())
	{
		FInv_InventoryFastArray& InventoryList = InventoryComponent->GetInventoryList();
		UE_LOG(LogTemp, Warning, TEXT("[AddItem]   Entries.Num()=%d, EntryIndex=%d, IsValidIndex=%s"), 
			InventoryList.Entries.Num(), EntryIndex, 
			InventoryList.Entries.IsValidIndex(EntryIndex) ? TEXT("true") : TEXT("false"));

		if (InventoryList.Entries.IsValidIndex(EntryIndex))
		{
			int32 TargetGridIndex = InventoryList.Entries[EntryIndex].TargetGridIndex;
			UE_LOG(LogTemp, Warning, TEXT("[AddItem] 🔍 2.5단계: Entry[%d]의 TargetGridIndex=%d 체크"), EntryIndex, TargetGridIndex);

			// TargetGridIndex가 유효하고, 해당 슬롯이 비어있으면 직접 배치
			if (TargetGridIndex != INDEX_NONE && GridSlots.IsValidIndex(TargetGridIndex) && !SlottedItems.Contains(TargetGridIndex))
			{
				UE_LOG(LogTemp, Warning, TEXT("[AddItem] ✅ TargetGridIndex=%d가 유효하고 비어있음! 직접 배치 진행"), TargetGridIndex);

				// 직접 배치
				const int32 ActualStackCount = Item->GetTotalStackCount();
				const bool bStackable = Item->IsStackable();
				AddItemAtIndex(Item, TargetGridIndex, bStackable, ActualStackCount, EntryIndex);
				UpdateGridSlots(Item, TargetGridIndex, bStackable, ActualStackCount);

				// ⭐ 배치 후 TargetGridIndex 초기화 (재사용 방지)
				InventoryList.Entries[EntryIndex].TargetGridIndex = INDEX_NONE;

				UE_LOG(LogTemp, Warning, TEXT("[AddItem] ✅ TargetGridIndex=%d에 배치 완료! (Split 아이템 마우스 위치)"), TargetGridIndex);
				UE_LOG(LogTemp, Warning, TEXT("========== [AddItem] 아이템 추가 완료 =========="));
				return;
			}
			else if (TargetGridIndex != INDEX_NONE)
			{
				UE_LOG(LogTemp, Warning, TEXT("[AddItem] ⚠️ TargetGridIndex=%d가 유효하지 않거나 이미 차지됨, 기본 로직으로 진행"), TargetGridIndex);
			}
		}
	}

	// ⭐⭐⭐ 3단계: 빈 슬롯 찾기 (PostReplicatedAdd/Change 순서 문제 해결!)
	// PostReplicatedAdd가 먼저 실행되어 Grid[0]을 차지한 경우,
	// PostReplicatedChange가 Grid[0]을 찾지 못해 중복 배치되는 문제 방지
	UE_LOG(LogTemp, Warning, TEXT("[AddItem] 🔍 3단계: 빈 슬롯 검색 시작 (HasRoomForItem 재사용)"));

	//공간이 있다고 부르는 부분.
	FInv_SlotAvailabilityResult Result = HasRoomForItem(Item);
	Result.EntryIndex = EntryIndex; // ⭐ Entry Index 저장

	UE_LOG(LogTemp, Warning, TEXT("[AddItem] Result.EntryIndex=%d로 설정됨"), Result.EntryIndex);
	UE_LOG(LogTemp, Warning, TEXT("[AddItem] HasRoomForItem 반환: %d개 슬롯"), Result.SlotAvailabilities.Num());

	// ⭐⭐⭐ 필터링: 이미 차지된 슬롯 제외! (중복 배치 방지)
	TArray<FInv_SlotAvailability> ActuallyEmptySlots;
	for (const FInv_SlotAvailability& SlotAvail : Result.SlotAvailabilities)
	{
		if (!SlottedItems.Contains(SlotAvail.Index))
		{
			ActuallyEmptySlots.Add(SlotAvail);
			UE_LOG(LogTemp, Warning, TEXT("[AddItem]   슬롯[%d]: 실제로 비어있음 ✅"), SlotAvail.Index);
		}
		else
		{
			UE_LOG(LogTemp, Warning, TEXT("[AddItem]   슬롯[%d]: 이미 차지됨, 건너뜀 ⚠️"), SlotAvail.Index);
		}
	}

	if (ActuallyEmptySlots.Num() > 0)
	{
		UE_LOG(LogTemp, Warning, TEXT("[AddItem] ✅ 실제 빈 슬롯 발견! 개수: %d"), ActuallyEmptySlots.Num());

		// 실제로 비어있는 슬롯만 사용
		Result.SlotAvailabilities = ActuallyEmptySlots;

		// Create a widget to show the item icon and add it to the correct spot on the grid.
		// 아이콘을 보여주고 그리드의 올바른 위치에 추가하는 위젯을 만듭니다.
		AddItemToIndices(Result, Item);

		UE_LOG(LogTemp, Warning, TEXT("[AddItem] ✅ 빈 슬롯에 배치 완료! EntryIndex=%d"), EntryIndex);
		UE_LOG(LogTemp, Warning, TEXT("========== [AddItem] 아이템 추가 완료 =========="));
	}
	else
	{
		UE_LOG(LogTemp, Warning, TEXT("[AddItem] ⚠️ 필터링 후 빈 슬롯 없음 (기존 스택 제외)"));
		UE_LOG(LogTemp, Warning, TEXT("[AddItem] 🔍 4단계: 완전히 새로운 빈 슬롯 재검색 (스택 무시)"));
		
		// ⭐⭐⭐ 4단계: 스택 가능 여부 무시하고 완전히 빈 슬롯 찾기
		// HasRoomForItem은 스택 가능 아이템의 경우 기존 스택을 우선 반환하므로,
		// 기존 스택이 모두 차지된 경우 새로운 빈 슬롯을 찾지 못할 수 있음.
		
		TArray<FInv_SlotAvailability> CompletelyEmptySlots;
		
		// Grid 전체를 순회하며 완전히 비어있는 슬롯 찾기
		for (int32 Index = 0; Index < GridSlots.Num(); ++Index)
		{
			// 이미 SlottedItems에 등록된 슬롯은 건너뜀
			if (SlottedItems.Contains(Index))
			{
				continue;
			}
			
			// GridSlot도 비어있는지 확인
			if (GridSlots.IsValidIndex(Index) && IsValid(GridSlots[Index]))
			{
				if (!GridSlots[Index]->GetInventoryItem().IsValid())
				{
					// 완전히 비어있는 슬롯 발견!
					FInv_SlotAvailability NewSlot(Index, Item->GetTotalStackCount(), false);
					CompletelyEmptySlots.Add(NewSlot);
					UE_LOG(LogTemp, Warning, TEXT("[AddItem]   슬롯[%d]: 완전히 비어있음 ✅"), Index);
					break; // 하나만 찾으면 충분 (1x1 아이템)
				}
			}
		}
		
		if (CompletelyEmptySlots.Num() > 0)
		{
			UE_LOG(LogTemp, Warning, TEXT("[AddItem] ✅ 완전히 빈 슬롯 발견! 개수: %d"), CompletelyEmptySlots.Num());
			
			Result.SlotAvailabilities = CompletelyEmptySlots;
			AddItemToIndices(Result, Item);
			
			UE_LOG(LogTemp, Warning, TEXT("[AddItem] ✅ 새 빈 슬롯에 배치 완료! EntryIndex=%d"), EntryIndex);
			UE_LOG(LogTemp, Warning, TEXT("========== [AddItem] 아이템 추가 완료 =========="));
		}
		else
		{
			UE_LOG(LogTemp, Error, TEXT("[AddItem] ❌ 완전히 빈 슬롯도 찾지 못했습니다! 인벤토리가 가득 참!"));
			UE_LOG(LogTemp, Warning, TEXT("========== [AddItem] 아이템 추가 실패 =========="));
		}
	}

	UE_LOG(LogTemp, Warning, TEXT("========== [AddItem] 아이템 추가 완료 =========="));
}

// 인벤토리에서 아이템 제거 시 UI에서도 삭제
// ⭐ 핵심 변경: EntryIndex는 로그용으로만 사용, 실제 매칭은 포인터 + ItemManifest로!
void UInv_InventoryGrid::RemoveItem(UInv_InventoryItem* Item, int32 EntryIndex)
{
	if (!IsValid(Item))
	{
		UE_LOG(LogTemp, Warning, TEXT("[RemoveItem] Item is invalid!"));
		return;
	}

	UE_LOG(LogTemp, Warning, TEXT("[RemoveItem] ========== 제거 요청 시작 =========="));
	UE_LOG(LogTemp, Warning, TEXT("[RemoveItem] ItemType=%s, EntryIndex=%d, Grid=%d"),
		*Item->GetItemManifest().GetItemType().ToString(), EntryIndex, (int32)ItemCategory);

	// ⭐ 핵심 변경: EntryIndex는 로그용으로만 사용, 실제 매칭은 포인터 + ItemManifest로!
	int32 FoundIndex = INDEX_NONE;

	for (const auto& [GridIndex, SlottedItem] : SlottedItems)
	{
		if (!IsValid(SlottedItem)) continue;

		UInv_InventoryItem* GridSlotItem = SlottedItem->GetInventoryItem();
		if (!IsValid(GridSlotItem)) continue;

		// 1차 검증: 포인터 비교
		if (GridSlotItem == Item)
		{
			// ⭐ Phase 7: EntryIndex로 정확한 슬롯 매칭 (Split 후 포인터 공유 문제 해결)
			if (SlottedItem->GetEntryIndex() != EntryIndex)
			{
				UE_LOG(LogTemp, Warning, TEXT("[RemoveItem] ⚠️ 포인터 일치하지만 EntryIndex 불일치: SlotEntry=%d, 찾는Entry=%d, 스킵!"), 
					SlottedItem->GetEntryIndex(), EntryIndex);
				continue;
			}
			// 2차 검증: ItemManifest 비교 (안전장치)
			if (GridSlotItem->GetItemManifest().GetItemType().MatchesTagExact(
				Item->GetItemManifest().GetItemType()))
			{
				FoundIndex = GridIndex;
				UE_LOG(LogTemp, Warning, TEXT("[RemoveItem] ✅ 슬롯 찾음! GridIndex=%d (포인터 일치 + Manifest 일치)"), GridIndex);
				break;
			}
		}
	}

	if (FoundIndex == INDEX_NONE)
	{
		UE_LOG(LogTemp, Warning, TEXT("[RemoveItem] ❌ Item을 찾지 못함 (다른 그리드에 있을 수 있음)"));
		UE_LOG(LogTemp, Warning, TEXT("[RemoveItem] ========== 제거 요청 종료 (실패) =========="));
		return;
	}

	RemoveItemFromGrid(Item, FoundIndex);
	UE_LOG(LogTemp, Warning, TEXT("[RemoveItem] ✅ 제거 완료! GridIndex=%d"), FoundIndex);
	UE_LOG(LogTemp, Warning, TEXT("[RemoveItem] ========== 제거 요청 종료 (성공) =========="));
}

// 🆕 [Phase 6] 포인터만으로 아이템 제거 (장착 복원 시 Grid에서 제거용)
bool UInv_InventoryGrid::RemoveSlottedItemByPointer(UInv_InventoryItem* Item)
{
	if (!IsValid(Item))
	{
		UE_LOG(LogTemp, Warning, TEXT("[RemoveSlottedItemByPointer] Item is invalid!"));
		return false;
	}

	UE_LOG(LogTemp, Warning, TEXT("[RemoveSlottedItemByPointer] 🔍 포인터로 아이템 검색: %s"), 
		*Item->GetItemManifest().GetItemType().ToString());

	// SlottedItems를 순회해서 같은 포인터를 가진 슬롯 찾기
	int32 FoundGridIndex = INDEX_NONE;
	for (const auto& [GridIndex, SlottedItem] : SlottedItems)
	{
		if (!IsValid(SlottedItem)) continue;

		UInv_InventoryItem* GridSlotItem = SlottedItem->GetInventoryItem();
		if (GridSlotItem == Item)
		{
			FoundGridIndex = GridIndex;
			UE_LOG(LogTemp, Warning, TEXT("[RemoveSlottedItemByPointer] ✅ 찾음! GridIndex=%d"), GridIndex);
			break;
		}
	}

	if (FoundGridIndex == INDEX_NONE)
	{
		UE_LOG(LogTemp, Warning, TEXT("[RemoveSlottedItemByPointer] ❌ 해당 아이템을 Grid에서 찾을 수 없음"));
		return false;
	}

	// Grid에서 제거
	RemoveItemFromGrid(Item, FoundGridIndex);
	UE_LOG(LogTemp, Warning, TEXT("[RemoveSlottedItemByPointer] ✅ Grid에서 제거 완료! GridIndex=%d"), FoundGridIndex);
	return true;
}

// GameplayTag로 모든 스택을 찾아서 업데이트 (Building 시스템용 - Split된 스택 처리)
void UInv_InventoryGrid::UpdateMaterialStacksByTag(const FGameplayTag& MaterialTag)
{
	if (!MaterialTag.IsValid())
	{
		UE_LOG(LogTemp, Error, TEXT("UpdateMaterialStacksByTag: Invalid MaterialTag!"));
		return;
	}

	if (!InventoryComponent.IsValid())
	{
		UE_LOG(LogTemp, Error, TEXT("UpdateMaterialStacksByTag: InventoryComponent is invalid!"));
		return;
	}

	UE_LOG(LogTemp, Warning, TEXT("=== UpdateMaterialStacksByTag 호출 ==="));
	UE_LOG(LogTemp, Warning, TEXT("MaterialTag: %s"), *MaterialTag.ToString());

	// 1단계: InventoryList에서 실제 총량 계산
	const FInv_InventoryFastArray& InventoryList = InventoryComponent->GetInventoryList();
	TArray<UInv_InventoryItem*> AllItems = InventoryList.GetAllItems();
	
	int32 TotalCountInInventory = 0;
	for (UInv_InventoryItem* Item : AllItems)
	{
		if (IsValid(Item) && Item->GetItemManifest().GetItemType().MatchesTagExact(MaterialTag))
		{
			TotalCountInInventory += Item->GetTotalStackCount();
		}
	}
	
	UE_LOG(LogTemp, Warning, TEXT("InventoryList 총량: %d"), TotalCountInInventory);

	// 2단계: UI의 같은 타입 모든 슬롯 찾기
	TArray<TPair<int32, int32>> SlotsWithCounts; // <Index, CurrentCount>
	
	for (const auto& [Index, SlottedItem] : SlottedItems)
	{
		if (!GridSlots.IsValidIndex(Index)) continue;
		
		UInv_InventoryItem* GridItem = GridSlots[Index]->GetInventoryItem().Get();
		if (!IsValid(GridItem)) continue;
		
		if (GridItem->GetItemManifest().GetItemType().MatchesTagExact(MaterialTag))
		{
			int32 CurrentCount = GridSlots[Index]->GetStackCount();
			SlotsWithCounts.Add(TPair<int32, int32>(Index, CurrentCount));
			UE_LOG(LogTemp, Warning, TEXT("슬롯 %d 발견: 현재 개수 %d"), Index, CurrentCount);
		}
	}
	
	UE_LOG(LogTemp, Warning, TEXT("발견된 슬롯 개수: %d"), SlotsWithCounts.Num());
	
	// 3단계: 총량을 슬롯에 분배 (순차적으로 채우기)
	int32 RemainingTotal = TotalCountInInventory;
	TArray<int32> IndicesToRemove;
	
	for (const auto& [Index, OldCount] : SlotsWithCounts)
	{
		if (RemainingTotal <= 0)
		{
			// 남은 총량이 0 → 이 슬롯은 삭제
			IndicesToRemove.Add(Index);
			UE_LOG(LogTemp, Warning, TEXT("슬롯 %d: 총량 소진, 제거 예약"), Index);
		}
		else
		{
			// 이 슬롯에 최대한 채우기
			int32 NewCount = FMath::Min(OldCount, RemainingTotal);
			RemainingTotal -= NewCount;
			
			if (NewCount <= 0)
			{
				// 0개가 됨 → 삭제
				IndicesToRemove.Add(Index);
				UE_LOG(LogTemp, Warning, TEXT("슬롯 %d: 0개 됨, 제거 예약 (%d → 0)"), Index, OldCount);
			}
			else if (NewCount != OldCount)
			{
				// 개수 변경 → 업데이트
				GridSlots[Index]->SetStackCount(NewCount);
				SlottedItems[Index]->UpdateStackCount(NewCount);
				UE_LOG(LogTemp, Warning, TEXT("슬롯 %d: UI 업데이트 (%d → %d)"), Index, OldCount, NewCount);
			}
			else
			{
				// 변경 없음
				UE_LOG(LogTemp, Log, TEXT("슬롯 %d: 변경 없음 (유지: %d)"), Index, NewCount);
			}
		}
	}
	
	// 4단계: 제거 예약된 슬롯들 실제 제거
	for (int32 IndexToRemove : IndicesToRemove)
	{
		if (!GridSlots.IsValidIndex(IndexToRemove)) continue;
		
		UInv_InventoryItem* ItemToRemove = GridSlots[IndexToRemove]->GetInventoryItem().Get();
		if (IsValid(ItemToRemove))
		{
			RemoveItemFromGrid(ItemToRemove, IndexToRemove);
			UE_LOG(LogTemp, Warning, TEXT("슬롯 %d: UI에서 제거 완료"), IndexToRemove);
		}
	}
	
	UE_LOG(LogTemp, Warning, TEXT("=== UpdateMaterialStacksByTag 완료 ==="));
}

// GridSlot을 직접 순회하며 재료 차감 (Split된 스택 처리)
void UInv_InventoryGrid::ConsumeItemsByTag(const FGameplayTag& MaterialTag, int32 AmountToConsume)
{
	if (!MaterialTag.IsValid() || AmountToConsume <= 0)
	{
		UE_LOG(LogTemp, Error, TEXT("ConsumeItemsByTag: Invalid MaterialTag or Amount!"));
		return;
	}

	UE_LOG(LogTemp, Warning, TEXT("=== ConsumeItemsByTag 시작 ==="));
	UE_LOG(LogTemp, Warning, TEXT("MaterialTag: %s, AmountToConsume: %d"), *MaterialTag.ToString(), AmountToConsume);

	int32 RemainingToConsume = AmountToConsume;
	TArray<int32> IndicesToRemove; // 제거할 슬롯 인덱스 목록

	// ⭐ GridSlot을 인덱스 순서대로 정렬하여 순회 (일관성 있는 차감)
	TArray<int32> SortedIndices;
	for (const auto& [Index, SlottedItem] : SlottedItems)
	{
		SortedIndices.Add(Index);
	}
	SortedIndices.Sort(); // 인덱스 정렬

	// GridSlot 순회하며 차감
	for (int32 Index : SortedIndices)
	{
		if (RemainingToConsume <= 0)
		{
			UE_LOG(LogTemp, Warning, TEXT("✅ 차감 완료! RemainingToConsume이 0 이하입니다. 순회 종료."));
			break; // 다 차감했으면 종료
		}

		if (!GridSlots.IsValidIndex(Index)) continue;

		UInv_InventoryItem* GridItem = GridSlots[Index]->GetInventoryItem().Get();
		if (!IsValid(GridItem)) continue;

		// 같은 타입인지 확인
		if (!GridItem->GetItemManifest().GetItemType().MatchesTagExact(MaterialTag)) continue;

		// 현재 슬롯의 개수
		int32 CurrentCount = GridSlots[Index]->GetStackCount();
		
		// 이 슬롯에서 차감할 개수 (최대 CurrentCount까지만)
		int32 ToConsume = FMath::Min(CurrentCount, RemainingToConsume);
		
		UE_LOG(LogTemp, Warning, TEXT("슬롯 %d: CurrentCount=%d, ToConsume=%d, RemainingBefore=%d"), Index, CurrentCount, ToConsume, RemainingToConsume);

		RemainingToConsume -= ToConsume; // ⭐ 차감!
		int32 NewCount = CurrentCount - ToConsume;

		UE_LOG(LogTemp, Warning, TEXT("슬롯 %d: 차감 후 RemainingToConsume=%d, NewCount=%d"), Index, RemainingToConsume, NewCount);

		if (NewCount <= 0)
		{
			// 이 슬롯을 전부 소비 → 제거 예약
			IndicesToRemove.Add(Index);
			UE_LOG(LogTemp, Warning, TEXT("슬롯 %d: 전부 소비! 제거 예약 (%d → 0)"), Index, CurrentCount);
		}
		else
		{
			// 슬롯 개수만 감소
			GridSlots[Index]->SetStackCount(NewCount);
			
			if (SlottedItems.Contains(Index) && IsValid(SlottedItems[Index]))
			{
				SlottedItems[Index]->UpdateStackCount(NewCount);
			}
			
			UE_LOG(LogTemp, Warning, TEXT("슬롯 %d: UI 업데이트 (%d → %d)"), Index, CurrentCount, NewCount);
		}
	}

	// 제거 예약된 슬롯들 실제 제거
	for (int32 IndexToRemove : IndicesToRemove)
	{
		if (!GridSlots.IsValidIndex(IndexToRemove)) continue;

		UInv_InventoryItem* ItemToRemove = GridSlots[IndexToRemove]->GetInventoryItem().Get();
		if (IsValid(ItemToRemove))
		{
			RemoveItemFromGrid(ItemToRemove, IndexToRemove);
			UE_LOG(LogTemp, Warning, TEXT("슬롯 %d: UI에서 제거 완료"), IndexToRemove);
		}
	}

	if (RemainingToConsume > 0)
	{
		UE_LOG(LogTemp, Error, TEXT("❌ 재료가 부족합니다! 남은 차감량: %d"), RemainingToConsume);
	}
	else
	{
		UE_LOG(LogTemp, Warning, TEXT("✅ 재료 차감 완료! MaterialTag: %s, Amount: %d"), *MaterialTag.ToString(), AmountToConsume);
	}

	UE_LOG(LogTemp, Warning, TEXT("=== ConsumeItemsByTag 완료 ==="));
}



void UInv_InventoryGrid::AddItemToIndices(const FInv_SlotAvailabilityResult& Result, UInv_InventoryItem* NewItem)
{
	for (const auto& Availability : Result.SlotAvailabilities)
	{
		// ⭐ 빈 슬롯에 새 아이템 배치 시: 실제 TotalStackCount 사용
		// Availability.AmountToFill은 HasRoomForItem에서 MaxStackSize로 제한된 값이라 사용 불가
		// 리플리케이션된 아이템의 실제 스택 수를 그대로 반영해야 함
		const int32 ActualStackCount = NewItem->GetTotalStackCount();

		UE_LOG(LogTemp, Warning, TEXT("[AddItemToIndices] 빈 슬롯 배치: Index=%d, AmountToFill=%d (무시), ActualStackCount=%d (사용)"),
			Availability.Index, Availability.AmountToFill, ActualStackCount);

		AddItemAtIndex(NewItem, Availability.Index, Result.bStackable, ActualStackCount, Result.EntryIndex);
		UpdateGridSlots(NewItem, Availability.Index, Result.bStackable, ActualStackCount);
	}
}

FVector2D UInv_InventoryGrid::GetDrawSize(const FInv_GridFragment* GridFragment) const
{
	const float IconTileWidth = TileSize - GridFragment->GetGridPadding() * 2; // 아이콘 타일 너비 계산

	return GridFragment->GetGridSize() * IconTileWidth; // 아이콘 크기 반환
}

void UInv_InventoryGrid::SetSlottedItemImage(const UInv_SlottedItem* SlottedItem, const FInv_GridFragment* GridFragment, const FInv_ImageFragment* ImageFragment) const
{
	//슬레이트 브러시?
	FSlateBrush Brush;
	Brush.SetResourceObject(ImageFragment->GetIcon()); // 아이콘 설정
	Brush.DrawAs = ESlateBrushDrawType::Image; // 이미지로 그리기
	Brush.ImageSize = GetDrawSize(GridFragment); // 아이콘 크기 설정
	SlottedItem->SetImageBrush(Brush); // 슬로티드 아이템에 브러시 설정
}

void UInv_InventoryGrid::AddItemAtIndex(UInv_InventoryItem* Item, const int32 Index, const bool bStackable, const int32 StackAmount, const int32 EntryIndex)
{
	UE_LOG(LogTemp, Log, TEXT("[AddItemAtIndex] GridIndex=%d, Item=%s"),
		Index, *Item->GetItemManifest().GetItemType().ToString());

	//격자의 크기를 얻어오자. 게임플레이 태그로 말야
	// Get Grid Fragment so we know how many grid spaces the item takes.
	// 텍스처와 아이콘도 여기서 얻어온다는건가?
	// Get Image Fragment so we have the icon to display
	const FInv_GridFragment* GridFragment = GetFragment<FInv_GridFragment>(Item, FragmentTags::GridFragment);
	const FInv_ImageFragment* ImageFragment = GetFragment<FInv_ImageFragment>(Item, FragmentTags::IconFragment);
	if (!GridFragment || !ImageFragment) return; // 둘 중 하나라도 없으면 리턴

	// Add the slotted item to the canvas panel.
	// 슬롯 아이템을 캔버스 패널에 그려주는 곳. 또한 그리드 슬롯을 관리해주는 곳.
	UInv_SlottedItem* SlottedItem = CreateSlottedItem(Item, bStackable, StackAmount, GridFragment, ImageFragment, Index, EntryIndex);

	AddSlottedItemToCanvas(Index, GridFragment, SlottedItem); // 캔버스에 슬로티드 아이템 추가하는 부분

	// 삭제 소비 파괴 했을 때 이곳에.
	// Store the new widget in a container.
	SlottedItems.Add(Index, SlottedItem); // 인덱스와 슬로티드 아이템 매핑
}

UInv_SlottedItem* UInv_InventoryGrid::CreateSlottedItem(UInv_InventoryItem* Item, const bool bStackable, const int32 StackAmount, const FInv_GridFragment* GridFragment, const FInv_ImageFragment* ImageFragment, const int32 Index, const int32 EntryIndex)
{
	// Create a widget to add to the grid
	UInv_SlottedItem* SlottedItem = CreateWidget<UInv_SlottedItem>(GetOwningPlayer(), SlottedItemClass);
	SlottedItem->SetInventoryItem(Item);
	SetSlottedItemImage(SlottedItem, GridFragment, ImageFragment);
	SlottedItem->SetGridIndex(Index);
	SlottedItem->SetEntryIndex(EntryIndex); // ⭐ EntryIndex 설정!
	SlottedItem->SetIsStackable(bStackable);
	const int32 StackUpdateAmount = bStackable ? StackAmount : 0;
	SlottedItem->UpdateStackCount(StackUpdateAmount);
	SlottedItem->OnSlottedItemClicked.AddDynamic(this, &ThisClass::OnSlottedItemClicked); // 마우스 아이템 클릭 델리게이트 바인딩

	return SlottedItem;
}

void UInv_InventoryGrid::AddSlottedItemToCanvas(const int32 Index, const FInv_GridFragment* GridFragment, UInv_SlottedItem* SlottedItem) const
{
	CanvasPanel->AddChild(SlottedItem);
	UCanvasPanelSlot* CanvasSlot = UWidgetLayoutLibrary::SlotAsCanvasSlot(SlottedItem);
	CanvasSlot->SetSize(GetDrawSize(GridFragment));
	const FVector2D DrawPos = UInv_WidgetUtils::GetPositionFromIndex(Index, Columns)* TileSize; // 정사각형 위치를 의미하는 것?
	const FVector2D DrawPosWithPadding = DrawPos + FVector2D(GridFragment->GetGridPadding()); // 패딩 적용된 위치
	CanvasSlot->SetPosition(DrawPosWithPadding);
}

void UInv_InventoryGrid::UpdateGridSlots(UInv_InventoryItem* NewItem, const int32 Index, bool bStackableItem, const int32 StackAmount)
{
	check(GridSlots.IsValidIndex(Index)); // 인덱스 유효성 검사
	
	//쌓을 수 있는 아이템인지 확인해볼까? (Stackable이 가능한지)
	if (bStackableItem)
	{
		GridSlots[Index]->SetStackCount(StackAmount); // 그리드 슬롯에 인벤토리 아이템 설정
	}

	const FInv_GridFragment* GridFragment = GetFragment<FInv_GridFragment>(NewItem, FragmentTags::GridFragment); // 그리드 조각 가져오기
	const FIntPoint Dimensions = GridFragment ? GridFragment->GetGridSize() : FIntPoint(1, 1); // 그리드 크기 가져오기

	// ⭐ 아이템의 Grid 위치 저장! (서버→클라이언트 동기화!)
	FIntPoint GridPos = UInv_WidgetUtils::GetPositionFromIndex(Index, Columns);
	NewItem->SetGridPosition(GridPos);
	
	// ⭐ [Phase 4 방법2] 서버에 Grid 위치 동기화
	// ⭐ [Phase 4 Fix] 로드 중에는 RPC 스킵 (잘못된 위치로 덮어쓰기 방지)
	if (InventoryComponent.IsValid() && !bSuppressServerSync)
	{
		uint8 GridCategoryValue = static_cast<uint8>(ItemCategory);
		InventoryComponent->Server_UpdateItemGridPosition(NewItem, Index, GridCategoryValue);
	}
	
	UE_LOG(LogTemp, Log, TEXT("[UpdateGridSlots] 아이템 %s를 Grid[%d,%d]에 배치 (Index=%d, Category=%d)"), 
		*NewItem->GetItemManifest().GetItemType().ToString(), GridPos.X, GridPos.Y, Index, static_cast<int32>(ItemCategory));

	//2D 격자 순회하면서 그리드 슬롯 업데이트
	UInv_InventoryStatics::ForEach2D(GridSlots, Index, Dimensions, Columns, [&](UInv_GridSlot* GridSlot) // [&] 이건 뭔데?
	{
		GridSlot->SetInventoryItem(NewItem); // 그리드 슬롯에 인벤토리 아이템 설정
		GridSlot->SetUpperLeftIndex(Index); // 그리드 슬롯에 왼쪽 위 인덱스 설정
		GridSlot->SetOccupiedTexture(); // 그리드 슬롯을 점유된 텍스처로 설정 (그니까 아이템을 격자칸들 수만큼 공간으로 채운다는 것)
		GridSlot->SetAvailable(false); // 그리드 슬롯을 사용 불가능으로 설정
	}); //람다함수 부분들
}

bool UInv_InventoryGrid::IsIndexClaimed(const TSet<int32>& CheckedIndices, const int32 Index) const
{
	return CheckedIndices.Contains(Index);
}


//2차원 격자 생성 아이템칸 만든다는 뜻.
void UInv_InventoryGrid::ConstructGrid()
{
	GridSlots.Reserve(Rows * Columns); // Tarray 지정 하는 건 알겠는데 GridSlot이거 어디서?

	for (int32 j = 0; j < Rows; ++j)
	{
		for (int32 i = 0; i < Columns; ++i)
		{
			UInv_GridSlot* GridSlot = CreateWidget<UInv_GridSlot>(this, GridSlotClass);
			CanvasPanel->AddChild(GridSlot);

			const FIntPoint TilePosition(i, j);
			GridSlot->SetTileIndex(UInv_WidgetUtils::GetIndexFromPosition(TilePosition, Columns));

			UCanvasPanelSlot* GridCPS = UWidgetLayoutLibrary::SlotAsCanvasSlot(GridSlot); // 슬롯 사용한다는 건가?
			GridCPS->SetSize(FVector2D(TileSize)); // 사이즈 조정
			GridCPS->SetPosition(TilePosition * TileSize); // 위치 조정
			
			GridSlots.Add(GridSlot);
			GridSlot->GridSlotClicked.AddDynamic(this, &ThisClass::OnGridSlotClicked); // 그리드 슬롯 클릭 델리게이트 바인딩
			GridSlot->GridSlotHovered.AddDynamic(this, &ThisClass::OnGridSlotHovered); // 그리드 슬롯 호버 델리게이트 바인딩
			GridSlot->GridSlotUnhovered.AddDynamic(this, &ThisClass::OnGridSlotUnhovered); // 그리드 슬롯 언호버 델리게이트 바인딩
		}
	}
}

// 그리드 클릭되었을 때 작동하게 만드려는 델리게이트 대비 함수.
void UInv_InventoryGrid::OnGridSlotClicked(int32 GridIndex, const FPointerEvent& MouseEvent)
{
	if (!IsValid(HoverItem)) return; // 호버 아이템이 유효하다면 리턴
	if (!GridSlots.IsValidIndex(ItemDropIndex)) return; // 아이템 드롭 인덱스가 유효하지 않다면 리턴


	if (CurrentQueryResult.ValidItem.IsValid() && GridSlots.IsValidIndex(CurrentQueryResult.UpperLeftIndex)) // 이미 있는 아이템의 슬롯도 참조를 해주는 함수.
	{
		OnSlottedItemClicked(CurrentQueryResult.UpperLeftIndex, MouseEvent); // 유효한 인덱스를 확인한 후 픽업 실행.
		return;
	}
	
	
	if (!IsInGridBounds(ItemDropIndex, HoverItem->GetGridDimensions())) return; // 그리드 경계 내에 있는지 확인
	auto GridSlot = GridSlots[ItemDropIndex];
	if (!GridSlot->GetInventoryItem().IsValid()) // 그리드 슬롯에 아이템이 없다면
	{
		// 아이템을 내려놓을 시 일어나는 이벤트.
		PutDownOnIndex(ItemDropIndex);
	}
}



void UInv_InventoryGrid::PutDownOnIndex(const int32 Index)
{
    UInv_InventoryItem* ItemToPutDown = HoverItem->GetInventoryItem();
    const bool bIsStackable = HoverItem->IsStackable();
    const int32 StackCount = HoverItem->GetStackCount();
    const int32 EntryIndex = HoverItem->GetEntryIndex();

    // Phase 8.1: Split 아이템이면 UI 배치 건너뛰기, 서버 RPC로 처리
    if (HoverItem->IsSplitItem())
    {
        UInv_InventoryItem* OriginalItem = HoverItem->GetOriginalSplitItem();
        if (IsValid(OriginalItem) && InventoryComponent.IsValid())
        {
            int32 OriginalNewStackCount = OriginalItem->GetTotalStackCount() - StackCount;
            UE_LOG(LogTemp, Warning, TEXT("[Phase 8.1] Split PutDown - UI 배치 스킵, 서버 RPC만 호출"));
            UE_LOG(LogTemp, Warning, TEXT("  원본 TotalStackCount: %d, 새 개수: %d, Split 개수: %d, 목표 Index: %d"),
                OriginalItem->GetTotalStackCount(), OriginalNewStackCount, StackCount, Index);
            // ⭐ Index(마우스 위치)를 서버에 전달하여 해당 위치에 배치되도록 함
            InventoryComponent.Get()->Server_SplitItemEntry(OriginalItem, OriginalNewStackCount, StackCount, Index);
        }
        ClearHoverItem();
        return;
    }

    AddItemAtIndex(ItemToPutDown, Index, bIsStackable, StackCount, EntryIndex);
    UpdateGridSlots(ItemToPutDown, Index, bIsStackable, StackCount);
    UE_LOG(LogTemp, Verbose, TEXT("PutDown: Index=%d, StackCount=%d"), Index, StackCount);
    ClearHoverItem();
}

void UInv_InventoryGrid::ClearHoverItem() // 호버(잡는모션) 아이템 초기화
{
	if (!IsValid(HoverItem)) return;

	HoverItem->SetInventoryItem(nullptr); // 호버 아이템의 인벤토리 아이템 초기화
	HoverItem->SetIsStackable(false); // 호버 아이템의 스택 가능 여부 초기화
	HoverItem->SetPreviousGridIndex(INDEX_NONE); // 이전 그리드 인덱스 초기화
	HoverItem->UpdateStackCount(0); // 스택 수 초기화
	
	// ⭐ Phase 8: Split 플래그 초기화
	HoverItem->SetIsSplitItem(false);
	HoverItem->SetOriginalSplitItem(nullptr);
	HoverItem->SetImageBrush(FSlateNoResource()); // 이미지 브러시 초기화 FSlateNoResource <- 모든 것을 지운다고 하네

	
	HoverItem->RemoveFromParent(); // 호버 아이템을 부모에서 제거
	HoverItem = nullptr; // 호버 아이템 포인터 초기화

	// 마우스 커서 보이게 하기
	ShowCursor();
}

UUserWidget* UInv_InventoryGrid::GetVisibleCursorWidget()
{
	if (!IsValid(GetOwningPlayer())) return nullptr;
	if (!IsValid(VisibleCursorWidget)) // 유효한 커서 위젯이 아닐 시
	{ 
		VisibleCursorWidget = CreateWidget<UUserWidget>(GetOwningPlayer(), VisibleCursorWidgetClass); // 컨트롤러 플레이어에 의해 활성화 될 것.
	}
	return VisibleCursorWidget;
}

UUserWidget* UInv_InventoryGrid::GetHiddenCursorWidget()
{
	if (!IsValid(GetOwningPlayer())) return nullptr;
	if (!IsValid(HiddenCursorWidget)) // 유효한 커서 위젯이 아닐 시
	{ 
		HiddenCursorWidget = CreateWidget<UUserWidget>(GetOwningPlayer(), HiddenCursorWidgetClass); // 컨트롤러 플레이어에 의해 활성화 될 것.
	}
	return HiddenCursorWidget;
}

bool UInv_InventoryGrid::IsSameStackable(const UInv_InventoryItem* ClickedInventoryItem) const
{
	// ⭐ nullptr 체크 추가 (크래시 방지!)
	if (!IsValid(ClickedInventoryItem) || !IsValid(HoverItem) || !IsValid(HoverItem->GetInventoryItem()))
	{
		return false;
	}
	
	// ⭐ 포인터 비교 제거! 같은 종류의 아이템이면 합칠 수 있도록 태그로만 비교
	// 기존: const bool bIsSameItem = ClickedInventoryItem == HoverItem->GetInventoryItem();
	// 문제: Split으로 나눈 아이템들은 같은 종류여도 다른 인스턴스라서 합쳐지지 않음
	const bool bIsSameType = HoverItem->GetItemType().MatchesTagExact(ClickedInventoryItem->GetItemManifest().GetItemType());
	const bool bIsStackable = ClickedInventoryItem->IsStackable();
	return bIsSameType && bIsStackable;
}

void UInv_InventoryGrid::SwapWithHoverItem(UInv_InventoryItem* ClickedInventoryItem, const int32 GridIndex)
{
	if (!IsValid(HoverItem)) return; // 호버 아이템이 유효하다면 리턴

	// 임시로 저장해서 할당하는 이유가 뭘까?
	UInv_InventoryItem* TempInventoryItem = HoverItem->GetInventoryItem(); // 호버 아이템 임시 저장
	const int32 TempStackCount = HoverItem->GetStackCount(); // 호버 아이템 스택 수 임시 저장
	const bool bTempIsStackable = HoverItem->IsStackable(); // 호버 아이템 스택 가능 여부 임시 저장
	const int32 TempEntryIndex = HoverItem->GetEntryIndex(); // ⭐ 호버 아이템 EntryIndex 임시 저장

	// 이전 격자 인덱스를 유지시켜야 하는 부분.
	// Keep the same previous grid index.
	AssignHoverItem(ClickedInventoryItem, GridIndex, HoverItem->GetPreviousGridIndex()); // 클릭된 아이템을 호버 아이템으로 할당
	RemoveItemFromGrid(ClickedInventoryItem, GridIndex); // 그리드에서 클릭된 아이템 제거
	AddItemAtIndex(TempInventoryItem, ItemDropIndex, bTempIsStackable, TempStackCount, TempEntryIndex); // 임시 저장된 아이템을 인덱스에 추가
	UpdateGridSlots(TempInventoryItem, ItemDropIndex, bTempIsStackable, TempStackCount); // 그리드 슬롯 업데이트
}

bool UInv_InventoryGrid::ShouldSwapStackCounts(const int32 RoomInClickedSlot, const int32 HoveredStackCount, const int32 MaxStackSize) const
{
	return RoomInClickedSlot == 0 && HoveredStackCount < MaxStackSize; // 스택 개수가 최대 스택 크기보다 작으면?
}

void UInv_InventoryGrid::SwapStackCounts(const int32 ClickedStackCount, const int32 HoveredStackCount, const int32 Index)
{
	UInv_GridSlot* GridSlot = GridSlots[Index]; // 그리드 슬롯 가져오기
	GridSlot->SetStackCount(HoveredStackCount);
	
	UInv_SlottedItem* ClickedSlottedItem = SlottedItems.FindChecked(Index); // 클릭된 슬로티드 아이템 가져오기
	ClickedSlottedItem->UpdateStackCount(HoveredStackCount); // 클릭된 슬로티드 아이템 스택 수 업데이트
	
	HoverItem->UpdateStackCount(ClickedStackCount); // 호버 아이템 스택 수 업데이트
}

bool UInv_InventoryGrid::ShouldConsumeHoverItemStacks(const int32 HoveredStackCount, const int32 RoomInClickedSlot) const
{
	// 클릭된 슬롯의 남은 공간이 호버된 스택 수보다 크거나 같으면?
	return RoomInClickedSlot >= HoveredStackCount; 
}

// 스택을 어떻게 채울지에 대한 구현 부분?
void UInv_InventoryGrid::ConsumeHoverItemStacks(const int32 ClickedStackCount, const int32 HoveredStackCount, const int32 Index)
{
	const int32 AmountToTransfer = HoveredStackCount;
	const int32 NewClickedStackCount = ClickedStackCount + AmountToTransfer;
	
	// UI 업데이트
	GridSlots[Index]->SetStackCount(NewClickedStackCount); // 그리드 슬롯 스택 수 업데이트
	SlottedItems.FindChecked(Index)->UpdateStackCount(NewClickedStackCount); // 슬로티드 아이템 스택 수 업데이트
	
	// 서버에 스택 변경 알림
	if (InventoryComponent.IsValid())
	{
		UInv_InventoryItem* ClickedItem = GridSlots[Index]->GetInventoryItem().Get();
		if (IsValid(ClickedItem))
		{
			if (GetOwningPlayer()->HasAuthority())
			{
				// ListenServer: 직접 설정
				ClickedItem->SetTotalStackCount(NewClickedStackCount);
				UE_LOG(LogTemp, Warning, TEXT("ConsumeHoverStacks (Authority): 스택 %d로 설정"), NewClickedStackCount);
			}
			// Client는 리플리케이션으로 자동 업데이트됨
			// (HoverItem을 소비했으므로 추가 RPC 불필요)
		}
	}
	
	ClearHoverItem(); // 호버 아이템 초기화
	ShowCursor(); // 마우스 커서 보이게 하기
	
	//그리드 조각 일부를 얻을 수 있는 정보
	const FInv_GridFragment* GridFragment = GridSlots[Index]->GetInventoryItem()->GetItemManifest().GetFragmentOfType<FInv_GridFragment>();
	const FIntPoint Dimensions = GridFragment ? GridFragment->GetGridSize() : FIntPoint(1, 1); // 그리드 크기 가져오기
	HighlightSlots(Index, Dimensions); // 슬롯 강조 표시 이제 더이상 회색이 아니야!
}

// 스택을 채워야 하는가?
bool UInv_InventoryGrid::ShouldFillInStack(const int32 RoomInClickedSlot, const int32 HoveredStackCount) const 
{
	return RoomInClickedSlot < HoveredStackCount; // 클릭된 슬롯의 남은 공간이 호버된 스택 수보다 작으면? 채운다.
}

void UInv_InventoryGrid::FillInStack(const int32 FillAmount, const int32 Remainder, const int32 Index)
{
	UInv_GridSlot* GridSlot = GridSlots[Index]; // 그리드 슬롯 가져오기
	const int32 NewStackCount = GridSlot->GetStackCount() + FillAmount; // 새로운 스택 수 계산 -> 합칠 때 스택 개수를 어떻게 할지
	
	GridSlot->SetStackCount(NewStackCount); // 그리드 슬롯 스택 수 업데이트
	
	UInv_SlottedItem* ClickedSlottedItem = SlottedItems.FindChecked(Index); // 클릭된 슬로티드 아이템 가져오기
	ClickedSlottedItem->UpdateStackCount(NewStackCount); // 클릭된 슬로티드 아이템 스택 수 업데이트
	
	HoverItem->UpdateStackCount(Remainder); // 호버 아이템 스택 수 업데이트
	
	// 서버에 스택 변경 알림
	if (InventoryComponent.IsValid())
	{
		UInv_InventoryItem* ClickedItem = GridSlot->GetInventoryItem().Get();
		if (IsValid(ClickedItem))
		{
			if (GetOwningPlayer()->HasAuthority())
			{
				// ListenServer: 직접 설정
				ClickedItem->SetTotalStackCount(NewStackCount);
				UE_LOG(LogTemp, Warning, TEXT("FillInStack (Authority): 스택 %d로 설정"), NewStackCount);
			}
			// Client는 리플리케이션으로 자동 업데이트
		}
	}
}

// 마우스 커서 켜기 끄기 함수들
void UInv_InventoryGrid::ShowCursor()
{
	if (!IsValid(GetOwningPlayer())) return;
	GetOwningPlayer()->SetMouseCursorWidget(EMouseCursor::Default, GetVisibleCursorWidget()); // 마우스 커서 위젯 설정
}

void UInv_InventoryGrid::HideCursor()
{
	if (!IsValid(GetOwningPlayer())) return;
	GetOwningPlayer()->SetMouseCursorWidget(EMouseCursor::Default, GetHiddenCursorWidget()); // 마우스 커서 위젯 설정
}

//장비 툴팁 부분 캔버스 패널
void UInv_InventoryGrid::SetOwningCanvas(UCanvasPanel* OwningCanvas)
{
	OwningCanvasPanel = OwningCanvas;
}

void UInv_InventoryGrid::OnGridSlotHovered(int32 GridIndex, const FPointerEvent& MouseEvent)
{
	if (IsValid(HoverItem)) return; // 호버 아이템이 유효하다면 리턴

	UInv_GridSlot* GridSlot = GridSlots[GridIndex]; // 그리드 슬롯 가져오기
	if (GridSlot->IsAvailable()) // 그리드 슬롯이 사용 가능하다면
	{
		GridSlot->SetOccupiedTexture(); // 점유된 텍스처로 설정
	}
}

void UInv_InventoryGrid::OnGridSlotUnhovered(int32 GridIndex, const FPointerEvent& MouseEvent)
{
	if (IsValid(HoverItem)) return; // 호버 아이템이 유효하다면 리턴

	UInv_GridSlot* GridSlot = GridSlots[GridIndex]; // 그리드 슬롯 가져오기
	if (GridSlot->IsAvailable()) // 그리드 슬롯이 사용 가능하다면
	{
		GridSlot->SetUnoccupiedTexture(); // 비점유된 텍스처로 설정
	}
}

void UInv_InventoryGrid::OnPopUpMenuSplit(int32 SplitAmount, int32 Index) // 아이템 분할 함수
{
	// 오른쪽 마우스 우클릭 창 불러오는 곳
	UInv_InventoryItem* RightClickedItem = GridSlots[Index]->GetInventoryItem().Get(); // 오른쪽 클릭한 아이템 가져오기
	if (!IsValid(RightClickedItem)) return; // 유효한 아이템인지 확인
	if (!RightClickedItem -> IsStackable()) return; // 스택 가능한 아이템인지 확인
	
	const int32 UpperLeftIndex = GridSlots[Index]->GetUpperLeftIndex(); // 그리드 슬롯의 왼쪽 위 인덱스 가져오기
	UInv_GridSlot* UpperLeftGridSlot = GridSlots[UpperLeftIndex]; // 왼쪽 위 그리드 슬롯 가져오기
	const int32 OriginalStackCount = UpperLeftGridSlot->GetStackCount(); // 원본 스택 수 가져오기
	const int32 NewStackCount = OriginalStackCount - SplitAmount; // 새로운 스택 수 계산 <- 분할된 양을 빼주는 것
	
	UE_LOG(LogTemp, Warning, TEXT("🔀 Split 시작: 원본 %d개 → 원본 슬롯 %d개 + 새 Entry %d개"), 
		OriginalStackCount, NewStackCount, SplitAmount);
	
	// 1단계: UI 업데이트 (빠른 반응성)
	UpperLeftGridSlot->SetStackCount(NewStackCount); // 그리드 슬롯 스택 수 업데이트
	SlottedItems.FindChecked(UpperLeftIndex)->UpdateStackCount(NewStackCount); // 슬로티드 아이템 스택 수 업데이트
	
	AssignHoverItem(RightClickedItem, UpperLeftIndex, UpperLeftIndex); // 호버 아이템 할당
	HoverItem->UpdateStackCount(SplitAmount); // 호버 아이템 스택 수 업데이트
	
	// ⭐ Phase 8: Split 플래그 설정 (PutDown 시 서버에 새 Entry 생성 필요)
	HoverItem->SetIsSplitItem(true);
	HoverItem->SetOriginalSplitItem(RightClickedItem); // 원본 아이템 참조 저장
	
	// ⭐⭐⭐ 2단계: 서버의 TotalStackCount는 원본 그대로 유지!
	// 핵심 개념:
	// - UI: 9개(슬롯) + 11개(HoverItem) = 2개로 나눠짐
	// - 서버 InventoryList: 여전히 1개 Entry, TotalStackCount=20 (변경 없음!)
	// - GetTotalMaterialCount(): InventoryList 합산 → 20개 (정확!)
	//
	// Split은 "UI 전용 작업"이므로 서버 데이터는 변경하지 않음!
	// PutDown 시에도 서버 TotalStackCount는 그대로 유지됨
	
	UE_LOG(LogTemp, Warning, TEXT("✅ Split 완료: 서버 TotalStackCount는 %d로 유지됨 (UI만 %d + %d로 나눠짐)"), 
		OriginalStackCount, NewStackCount, SplitAmount);
}

void UInv_InventoryGrid::OnPopUpMenuDrop(int32 Index) // 아이템 버리기 함수
{
	//어느 서버에서도 통신을 할 수 있게 만드는 부분 (리슨서버, 호스트 서버, 데디서버 등)
	UInv_InventoryItem* RightClickedItem = GridSlots[Index]->GetInventoryItem().Get();
	if (!IsValid(RightClickedItem)) return; // 유효한 아이템인지 확인
	
	PickUp(RightClickedItem, Index); // 아이템 집기
	DropItem(); // 아이템 버리기
}

// 아이템 소비 상호작용 부분
void UInv_InventoryGrid::OnPopUpMenuConsume(int32 Index) 
{
	UInv_InventoryItem* RightClickedItem = GridSlots[Index]->GetInventoryItem().Get(); // 오른쪽 클릭한 아이템 가져오기
	if (!IsValid(RightClickedItem)) return; // 유효한 아이템인지 확인
	
	const int32 UpperLeftIndex = GridSlots[Index]->GetUpperLeftIndex(); // 그리드 슬롯의 왼쪽 위 인덱스 가져오기
	UInv_GridSlot* UpperLeftGridSlot = GridSlots[UpperLeftIndex]; // 왼쪽 위 그리드 슬롯 가져오기
	const int32 StackCount = UpperLeftGridSlot -> GetStackCount(); // 스택 수 가져오기
	const int32 NewStackCount = StackCount - 1; // 새로운 스택 수 계산 <- 1개 소비하는 것
	
	UpperLeftGridSlot->SetStackCount(NewStackCount); // 그리드 슬롯 스택 수 업데이트
	SlottedItems.FindChecked(UpperLeftIndex)->UpdateStackCount(NewStackCount); // 슬로티드 아이템 스택 수 업데이트
	
	// 서버에서 내가 소모되는 것을 서버에게 알리는 부분.
	InventoryComponent->Server_ConsumeItem(RightClickedItem);
	
	if (NewStackCount <= 0)
	{
		RemoveItemFromGrid(RightClickedItem, Index); // 그리드에서 아이템 제거
	}
}

// 아이템을 들고 있을 때 다른 UI를 건드리지 못하게 하는 것.
void UInv_InventoryGrid::OnInventoryMenuToggled(bool bOpen)
{
	if (!bOpen)
	{
		PutHoverItemBack();
	}
}

bool UInv_InventoryGrid::MatchesCategory(const UInv_InventoryItem* Item) const
{
	return Item->GetItemManifest().GetItemCategory() == ItemCategory; // 아이템 카테고리 비교
}

// ⭐ UI GridSlots 기반 재료 개수 세기 (Split 대응!)
int32 UInv_InventoryGrid::GetTotalMaterialCountFromSlots(const FGameplayTag& MaterialTag) const
{
	if (!MaterialTag.IsValid()) return 0;

	int32 TotalCount = 0;
	TSet<int32> CountedUpperLeftIndices; // 중복 카운트 방지

	// 모든 GridSlot 순회
	for (const auto& GridSlot : GridSlots)
	{
		if (!IsValid(GridSlot)) continue;
		if (!GridSlot->GetInventoryItem().IsValid()) continue;

		// 이미 카운트한 아이템인지 확인 (같은 아이템이 여러 슬롯에 걸쳐있을 수 있음)
		const int32 UpperLeftIndex = GridSlot->GetUpperLeftIndex();
		if (CountedUpperLeftIndices.Contains(UpperLeftIndex)) continue;

		// 아이템 타입 확인
		UInv_InventoryItem* Item = GridSlot->GetInventoryItem().Get();
		if (Item->GetItemManifest().GetItemType().MatchesTagExact(MaterialTag))
		{
			// ⭐ UI의 StackCount 읽기 (Split된 스택 반영!)
			const int32 StackCount = GridSlots[UpperLeftIndex]->GetStackCount();
			TotalCount += StackCount;

			// 중복 카운트 방지
			CountedUpperLeftIndices.Add(UpperLeftIndex);

			UE_LOG(LogTemp, Verbose, TEXT("GridSlot[%d]: %s x %d (누적: %d)"), 
				UpperLeftIndex, *MaterialTag.ToString(), StackCount, TotalCount);
		}
	}

	UE_LOG(LogTemp, Log, TEXT("GetTotalMaterialCountFromSlots(%s) = %d"), *MaterialTag.ToString(), TotalCount);
	return TotalCount;
}

// ⭐ 실제 UI Grid 상태 확인 (크래프팅 공간 체크용)
bool UInv_InventoryGrid::HasRoomInActualGrid(const FInv_ItemManifest& Manifest) const
{
	// ⭐ GridSlots를 직접 참조하여 실제 UI 상태 기반 공간 체크!
	// HasRoomForItem은 const가 아니므로 직접 구현

	const FInv_GridFragment* GridFragment = Manifest.GetFragmentOfType<FInv_GridFragment>();
	if (!GridFragment) return false;

	FIntPoint ItemSize = GridFragment->GetGridSize();

	UE_LOG(LogTemp, Warning, TEXT("[ACTUAL GRID CHECK] 아이템 크기: %dx%d"), ItemSize.X, ItemSize.Y);
	UE_LOG(LogTemp, Warning, TEXT("[ACTUAL GRID CHECK] Grid 크기: %dx%d"), Columns, Rows);

	// 실제 GridSlots 순회 (UI의 정확한 상태!)
	for (int32 StartIndex = 0; StartIndex < GridSlots.Num(); ++StartIndex)
	{
		// 그리드 범위 체크
		int32 StartX = StartIndex % Columns;
		int32 StartY = StartIndex / Columns;

		// 아이템이 그리드를 벗어나는지 확인
		if (StartX + ItemSize.X > Columns || StartY + ItemSize.Y > Rows)
		{
			continue; // 범위 밖이면 스킵
		}

		// 해당 위치에서 ItemSize 크기만큼의 공간이 비어있는지 체크
		bool bCanFit = true;

		for (int32 Y = 0; Y < ItemSize.Y; ++Y)
		{
			for (int32 X = 0; X < ItemSize.X; ++X)
			{
				int32 CheckIndex = (StartY + Y) * Columns + (StartX + X);

				if (CheckIndex >= GridSlots.Num())
				{
					bCanFit = false;
					break;
				}

				UInv_GridSlot* CheckSlot = GridSlots[CheckIndex];
				if (!IsValid(CheckSlot) || CheckSlot->GetInventoryItem().IsValid())
				{
					// 슬롯이 유효하지 않거나 이미 아이템이 있으면 실패
					bCanFit = false;
					break;
				}
			}

			if (!bCanFit) break;
		}

		if (bCanFit)
		{
			UE_LOG(LogTemp, Warning, TEXT("[ACTUAL GRID CHECK] ✅ 공간 발견! [%d, %d]부터 %dx%d"),
				StartX, StartY, ItemSize.X, ItemSize.Y);
			return true; // 공간 발견!
		}
	}

	UE_LOG(LogTemp, Warning, TEXT("[ACTUAL GRID CHECK] ❌ 공간 없음!"));
	return false; // 공간 없음
}

// ============================================
// 📌 Grid 상태 수집 (저장용) - Phase 3
// ============================================
/**
 * Grid의 모든 아이템 상태를 수집
 * Split된 스택도 개별 항목으로 수집됨
 * 
 * 📌 수집 과정:
 * 1. SlottedItems 맵 순회 (GridIndex → SlottedItem)
 * 2. GridSlot에서 StackCount 읽기 (⭐ Split 반영!)
 * 3. GridIndex → GridPosition 변환
 * 4. FInv_SavedItemData 생성
 */
TArray<FInv_SavedItemData> UInv_InventoryGrid::CollectGridState() const
{
	TArray<FInv_SavedItemData> Result;

	// 카테고리 이름 변환
	const TCHAR* GridCategoryNames[] = { TEXT("장비"), TEXT("소모품"), TEXT("재료") };
	const int32 CategoryIndex = static_cast<int32>(ItemCategory);
	const TCHAR* GridCategoryStr = (CategoryIndex >= 0 && CategoryIndex < 3) ? GridCategoryNames[CategoryIndex] : TEXT("???");

	UE_LOG(LogTemp, Warning, TEXT(""));
	UE_LOG(LogTemp, Warning, TEXT("    ┌─── [CollectGridState] Grid %d (%s) ───┐"), CategoryIndex, GridCategoryStr);
	UE_LOG(LogTemp, Warning, TEXT("    │ Grid 크기: %d x %d (총 %d 슬롯)"), Columns, Rows, Columns * Rows);
	UE_LOG(LogTemp, Warning, TEXT("    │ SlottedItems 개수: %d"), SlottedItems.Num());

	if (SlottedItems.Num() == 0)
	{
		UE_LOG(LogTemp, Warning, TEXT("    │ → 수집할 아이템 없음 (빈 Grid)"));
		UE_LOG(LogTemp, Warning, TEXT("    └────────────────────────────────────────┘"));
		return Result;
	}

	UE_LOG(LogTemp, Warning, TEXT("    │"));
	UE_LOG(LogTemp, Warning, TEXT("    │ ▶ SlottedItems 순회 시작:"));

	// SlottedItems 순회 (각 GridIndex에 있는 아이템)
	int32 ItemIndex = 0;
	for (const auto& Pair : SlottedItems)
	{
		const int32 GridIndex = Pair.Key;
		const UInv_SlottedItem* SlottedItem = Pair.Value;

		UE_LOG(LogTemp, Warning, TEXT("    │"));
		UE_LOG(LogTemp, Warning, TEXT("    │   [%d] GridIndex=%d"), ItemIndex, GridIndex);

		// SlottedItem 유효성 검사
		if (!IsValid(SlottedItem))
		{
			UE_LOG(LogTemp, Warning, TEXT("    │       ⚠️ SlottedItem이 nullptr! 건너뜀"));
			continue;
		}

		// InventoryItem 가져오기
		UInv_InventoryItem* Item = SlottedItem->GetInventoryItem();
		if (!IsValid(Item))
		{
			UE_LOG(LogTemp, Warning, TEXT("    │       ⚠️ InventoryItem이 nullptr! 건너뜀"));
			continue;
		}

		// GridSlot 유효성 검사
		if (!GridSlots.IsValidIndex(GridIndex))
		{
			UE_LOG(LogTemp, Warning, TEXT("    │       ⚠️ GridIndex(%d)가 범위 밖! (GridSlots.Num=%d) 건너뜀"), 
				GridIndex, GridSlots.Num());
			continue;
		}

		// ============================================
		// ⭐ 핵심: GridSlot에서 StackCount 읽기
		// ============================================
		// 서버의 TotalStackCount가 아니라 UI 슬롯의 개별 스택 수량!
		// Split 시: 서버(20개) → UI슬롯1(9개) + UI슬롯2(11개)
		const int32 StackCount = GridSlots[GridIndex]->GetStackCount();
		const int32 ServerStackCount = Item->GetTotalStackCount();

		UE_LOG(LogTemp, Warning, TEXT("    │       ItemType: %s"), *Item->GetItemManifest().GetItemType().ToString());
		UE_LOG(LogTemp, Warning, TEXT("    │       UI StackCount: %d (⭐ 저장할 값)"), StackCount);
		UE_LOG(LogTemp, Warning, TEXT("    │       서버 TotalStackCount: %d (참고용)"), ServerStackCount);

		// Split 감지 로그
		if (StackCount != ServerStackCount && ServerStackCount > 0)
		{
			UE_LOG(LogTemp, Warning, TEXT("    │       🔀 Split 감지! UI(%d) ≠ 서버(%d)"), StackCount, ServerStackCount);
		}

		// ============================================
		// GridIndex → GridPosition 변환
		// ============================================
		const FIntPoint GridPosition = UInv_WidgetUtils::GetPositionFromIndex(GridIndex, Columns);
		UE_LOG(LogTemp, Warning, TEXT("    │       GridIndex(%d) → Position(%d, %d)"), 
			GridIndex, GridPosition.X, GridPosition.Y);

		// ============================================
		// 저장 데이터 생성
		// ============================================
		FInv_SavedItemData SavedData;
		SavedData.ItemType = Item->GetItemManifest().GetItemType();
		SavedData.StackCount = StackCount > 0 ? StackCount : 1;  // Non-stackable은 1
		SavedData.GridPosition = GridPosition;
		SavedData.GridCategory = static_cast<uint8>(ItemCategory);

		Result.Add(SavedData);

		UE_LOG(LogTemp, Warning, TEXT("    │       ✅ 수집 완료: %s"), *SavedData.ToString());

		ItemIndex++;
	}

	UE_LOG(LogTemp, Warning, TEXT("    │"));
	UE_LOG(LogTemp, Warning, TEXT("    │ 📦 Grid %d 수집 결과: %d개 아이템"), CategoryIndex, Result.Num());
	UE_LOG(LogTemp, Warning, TEXT("    └────────────────────────────────────────┘"));

	return Result;
}

// ============================================
// 📦 [Phase 5] Grid 위치 복원 함수
// ============================================

int32 UInv_InventoryGrid::RestoreItemPositions(const TArray<FInv_SavedItemData>& SavedItems)
{
	const int32 CategoryIndex = static_cast<int32>(ItemCategory);
	const TCHAR* GridCategoryNames[] = { TEXT("장비"), TEXT("소모품"), TEXT("재료") };
	const TCHAR* GridCategoryStr = (CategoryIndex >= 0 && CategoryIndex < 3) ? GridCategoryNames[CategoryIndex] : TEXT("???");

	UE_LOG(LogTemp, Warning, TEXT(""));
	UE_LOG(LogTemp, Warning, TEXT("    ┌─── [RestoreItemPositions] Grid %d (%s) ───┐"), CategoryIndex, GridCategoryStr);
	UE_LOG(LogTemp, Warning, TEXT("    │ 복원할 아이템: %d개"), SavedItems.Num());

	// ============================================
	// 🔧 [핵심 수정] 순서 기반 이동으로 변경
	// ============================================
	// 문제: ItemType + StackCount로 찾으면 같은 타입/수량의 아이템이 
	//       여러 개 있을 때 첫 번째 것만 계속 찾음
	// 해결: 저장된 순서와 현재 SlottedItems 순서를 1:1 매칭
	// ============================================

	// 1. 이 Grid 카테고리에 해당하는 저장 데이터만 필터링
	TArray<FInv_SavedItemData> FilteredSavedItems;
	for (const FInv_SavedItemData& SavedItem : SavedItems)
	{
		if (SavedItem.GridCategory == static_cast<uint8>(ItemCategory))
		{
			FilteredSavedItems.Add(SavedItem);
		}
	}

	UE_LOG(LogTemp, Warning, TEXT("    │ 이 Grid 카테고리 아이템: %d개"), FilteredSavedItems.Num());

	// 2. 현재 SlottedItems의 키(GridIndex)를 정렬하여 배열로 만듦
	TArray<int32> SortedKeys;
	SlottedItems.GetKeys(SortedKeys);
	SortedKeys.Sort();  // 오름차순 정렬 (저장 시 순회 순서와 동일)

	UE_LOG(LogTemp, Warning, TEXT("    │ 현재 SlottedItems 개수: %d"), SortedKeys.Num());

	// 3. 1:1 매칭하여 이동
	int32 RestoredCount = 0;
	const int32 MatchCount = FMath::Min(SortedKeys.Num(), FilteredSavedItems.Num());

	for (int32 i = 0; i < MatchCount; i++)
	{
		const int32 CurrentGridIndex = SortedKeys[i];
		const FInv_SavedItemData& SavedItem = FilteredSavedItems[i];

		UE_LOG(LogTemp, Warning, TEXT("    │"));
		UE_LOG(LogTemp, Warning, TEXT("    │ [%d] %s x%d → Pos(%d,%d)"),
			i, *SavedItem.ItemType.ToString(), SavedItem.StackCount,
			SavedItem.GridPosition.X, SavedItem.GridPosition.Y);

		// 현재 GridIndex의 아이템을 저장된 위치로 이동
		// ⭐ Phase 5: SavedItem.StackCount를 세 번째 파라미터로 전달!
		if (MoveItemByCurrentIndex(CurrentGridIndex, SavedItem.GridPosition, SavedItem.StackCount))
		{
			UE_LOG(LogTemp, Warning, TEXT("    │     ✅ 복원 성공!"));
			RestoredCount++;
		}
		else
		{
			UE_LOG(LogTemp, Warning, TEXT("    │     ⚠️ 복원 실패"));
		}
	}

	UE_LOG(LogTemp, Warning, TEXT("    │"));
	UE_LOG(LogTemp, Warning, TEXT("    │ 📦 복원 결과: %d개 성공"), RestoredCount);
	UE_LOG(LogTemp, Warning, TEXT("    └────────────────────────────────────────┘"));

	return RestoredCount;
}

// ============================================
// ⭐ [Phase 4 Fix] 복원 완료 후 서버에 올바른 위치 전송
// ============================================
void UInv_InventoryGrid::SendAllItemPositionsToServer()
{
	if (!InventoryComponent.IsValid())
	{
		UE_LOG(LogTemp, Warning, TEXT("[SendAllItemPositionsToServer] InventoryComponent 없음, 스킵"));
		return;
	}

	const int32 CategoryIndex = static_cast<int32>(ItemCategory);
	const uint8 GridCategoryValue = static_cast<uint8>(ItemCategory);
	
	int32 SentCount = 0;
	
	for (const auto& Pair : SlottedItems)
	{
		const int32 GridIndex = Pair.Key;
		UInv_SlottedItem* SlottedItem = Pair.Value;
		
		if (!IsValid(SlottedItem)) continue;
		
		UInv_InventoryItem* Item = SlottedItem->GetInventoryItem();
		if (!IsValid(Item)) continue;
		
		// 서버에 올바른 위치 전송
		InventoryComponent->Server_UpdateItemGridPosition(Item, GridIndex, GridCategoryValue);
		SentCount++;
		
		UE_LOG(LogTemp, Log, TEXT("[SendAllItemPositionsToServer] Grid%d: %s → Index=%d"), 
			CategoryIndex, *Item->GetItemManifest().GetItemType().ToString(), GridIndex);
	}
	
	UE_LOG(LogTemp, Warning, TEXT("[SendAllItemPositionsToServer] Grid%d: %d개 아이템 위치 전송 완료"), 
		CategoryIndex, SentCount);
}

bool UInv_InventoryGrid::MoveItemToPosition(const FGameplayTag& ItemType, const FIntPoint& TargetPosition, int32 StackCount)
{
	// ============================================
	// 📦 [Phase 5] Grid 위치 복원 - 완전한 이동 로직
	// ============================================
	// 
	// 이동 순서:
	// 1. ItemType + StackCount로 SlottedItem 찾기
	// 2. 현재 위치가 목표 위치면 스킵
	// 3. 목표 위치가 비어있는지 확인
	// 4. 원래 위치의 GridSlots 해제
	// 5. SlottedItems 맵 키 변경
	// 6. 새 위치의 GridSlots 점유
	// 7. 위젯 위치 업데이트
	// ============================================

	const int32 TargetIndex = UInv_WidgetUtils::GetIndexFromPosition(TargetPosition, Columns);

	// ============================================
	// Step 1: ItemType + StackCount로 SlottedItem 찾기
	// ============================================
	UInv_SlottedItem* FoundSlottedItem = nullptr;
	int32 CurrentIndex = INDEX_NONE;

	for (const auto& Pair : SlottedItems)
	{
		UInv_SlottedItem* SlottedItem = Pair.Value;
		if (!IsValid(SlottedItem)) continue;

		UInv_InventoryItem* Item = SlottedItem->GetInventoryItem();
		if (!Item) continue;

		// ItemType 매칭
		if (Item->GetItemManifest().GetItemType() != ItemType) continue;

		// StackCount 매칭 (TotalStackCount 사용)
		if (Item->GetTotalStackCount() != StackCount) continue;

		// 첫 번째 매칭 선택
		FoundSlottedItem = SlottedItem;
		CurrentIndex = Pair.Key;
		break;
	}

	if (!FoundSlottedItem || CurrentIndex == INDEX_NONE)
	{
		UE_LOG(LogTemp, Warning, TEXT("    │     ⚠️ [MoveItemToPosition] 매칭되는 아이템 없음"));
		UE_LOG(LogTemp, Warning, TEXT("    │         ItemType: %s, StackCount: %d"), *ItemType.ToString(), StackCount);
		return false;
	}

	// ============================================
	// Step 2: 현재 위치가 목표 위치면 스킵
	// ============================================
	if (CurrentIndex == TargetIndex)
	{
		UE_LOG(LogTemp, Warning, TEXT("    │     ✅ [MoveItemToPosition] 이미 목표 위치에 있음 (Index=%d)"), CurrentIndex);
		return true;
	}

	// ============================================
	// Step 3: 목표 위치가 비어있는지 확인
	// ============================================
	if (!GridSlots.IsValidIndex(TargetIndex))
	{
		UE_LOG(LogTemp, Warning, TEXT("    │     ⚠️ [MoveItemToPosition] 유효하지 않은 목표 Index: %d"), TargetIndex);
		return false;
	}

	// 목표 슬롯이 이미 점유되어 있는지 확인
	if (GridSlots[TargetIndex]->GetInventoryItem().IsValid())
	{
		UE_LOG(LogTemp, Warning, TEXT("    │     ⚠️ [MoveItemToPosition] 목표 위치가 이미 점유됨 (Index=%d)"), TargetIndex);
		return false;
	}

	UE_LOG(LogTemp, Warning, TEXT("    │     🔄 [MoveItemToPosition] 이동 시작: Index %d → %d"), CurrentIndex, TargetIndex);

	// ============================================
	// Step 4: 원래 위치의 GridSlots 해제
	// ============================================
	UInv_InventoryItem* InventoryItem = FoundSlottedItem->GetInventoryItem();
	const FInv_GridFragment* GridFragment = GetFragment<FInv_GridFragment>(InventoryItem, FragmentTags::GridFragment);
	FIntPoint Dimensions = FIntPoint(1, 1);
	if (GridFragment)
	{
		Dimensions = GridFragment->GetGridSize();
	}

	// 원래 위치의 모든 GridSlot 해제 (다차원 아이템 지원)
	UInv_InventoryStatics::ForEach2D(GridSlots, CurrentIndex, Dimensions, Columns, [&](UInv_GridSlot* GridSlot)
	{
		if (GridSlot)
		{
			GridSlot->SetInventoryItem(nullptr);
		}
	});

	// ============================================
	// Step 5: SlottedItems 맵 키 변경
	// ============================================
	SlottedItems.Remove(CurrentIndex);
	SlottedItems.Add(TargetIndex, FoundSlottedItem);

	// ============================================
	// Step 6: 새 위치의 GridSlots 점유
	// ============================================
	UInv_InventoryStatics::ForEach2D(GridSlots, TargetIndex, Dimensions, Columns, [&](UInv_GridSlot* GridSlot)
	{
		if (GridSlot)
		{
			GridSlot->SetInventoryItem(InventoryItem);
			GridSlot->SetUpperLeftIndex(TargetIndex);
			GridSlot->SetOccupiedTexture();
		}
	});

	// ============================================
	// Step 7: 위젯 위치 업데이트
	// ============================================
	UCanvasPanelSlot* CanvasSlot = Cast<UCanvasPanelSlot>(FoundSlottedItem->Slot);
	if (CanvasSlot)
	{
		// 기존 AddItemAtIndex 로직과 동일한 방식으로 위치 계산
		const FVector2D DrawPos = FVector2D(TargetPosition.X * TileSize, TargetPosition.Y * TileSize);
		float ItemPadding = 0.0f;
		if (GridFragment)
		{
			ItemPadding = GridFragment->GetGridPadding();
		}
		const FVector2D DrawPosWithPadding = DrawPos + FVector2D(ItemPadding);
		CanvasSlot->SetPosition(DrawPosWithPadding);
	}

	UE_LOG(LogTemp, Warning, TEXT("    │     ✅ [MoveItemToPosition] 이동 완료!"));
	UE_LOG(LogTemp, Warning, TEXT("    │         %s x%d: Index %d → %d, Pos(%d,%d)"),
		*ItemType.ToString(), StackCount, CurrentIndex, TargetIndex, TargetPosition.X, TargetPosition.Y);

	return true;
}

// ============================================
// 📦 [Phase 5] Grid Index 기반 위치 이동 함수
// ============================================
// MoveItemToPosition의 문제점:
//   - ItemType + StackCount로 찾으면 같은 타입/수량의 아이템이
//     여러 개 있을 때 첫 번째 것만 계속 찾음
// 해결:
//   - 현재 GridIndex를 직접 지정하여 정확한 아이템 이동
// ============================================

bool UInv_InventoryGrid::MoveItemByCurrentIndex(int32 CurrentIndex, const FIntPoint& TargetPosition, int32 SavedStackCount)
{
	const int32 TargetIndex = UInv_WidgetUtils::GetIndexFromPosition(TargetPosition, Columns);

	// ============================================
	// Step 1: 현재 위치가 목표 위치면 스킵
	// ============================================
	if (CurrentIndex == TargetIndex)
	{
		UE_LOG(LogTemp, Warning, TEXT("    │     ✅ [MoveItemByCurrentIndex] 이미 목표 위치에 있음 (Index=%d)"), CurrentIndex);
		return true;
	}

	// ============================================
	// Step 2: CurrentIndex에 SlottedItem이 있는지 확인
	// ============================================
	UInv_SlottedItem* FoundSlottedItem = SlottedItems.FindRef(CurrentIndex);
	if (!IsValid(FoundSlottedItem))
	{
		UE_LOG(LogTemp, Warning, TEXT("    │     ⚠️ [MoveItemByCurrentIndex] CurrentIndex=%d에 SlottedItem 없음"), CurrentIndex);
		return false;
	}

	UInv_InventoryItem* InventoryItem = FoundSlottedItem->GetInventoryItem();
	if (!InventoryItem)
	{
		UE_LOG(LogTemp, Warning, TEXT("    │     ⚠️ [MoveItemByCurrentIndex] InventoryItem이 nullptr"));
		return false;
	}

	// ============================================
	// Step 3: 목표 위치가 비어있는지 확인
	// ============================================
	if (!GridSlots.IsValidIndex(TargetIndex))
	{
		UE_LOG(LogTemp, Warning, TEXT("    │     ⚠️ [MoveItemByCurrentIndex] 유효하지 않은 목표 Index: %d"), TargetIndex);
		return false;
	}

	if (GridSlots[TargetIndex]->GetInventoryItem().IsValid())
	{
		UE_LOG(LogTemp, Warning, TEXT("    │     ⚠️ [MoveItemByCurrentIndex] 목표 위치가 이미 점유됨 (Index=%d)"), TargetIndex);
		return false;
	}

	UE_LOG(LogTemp, Warning, TEXT("    │     🔄 [MoveItemByCurrentIndex] 이동 시작: Index %d → %d"), CurrentIndex, TargetIndex);

	// ============================================
	// Step 4: 아이템 크기 정보 가져오기
	// ============================================
	const FInv_GridFragment* GridFragment = GetFragment<FInv_GridFragment>(InventoryItem, FragmentTags::GridFragment);
	FIntPoint Dimensions = FIntPoint(1, 1);
	if (GridFragment)
	{
		Dimensions = GridFragment->GetGridSize();
	}

	// ============================================
	// ⭐ Step 4.5: 기존 위치의 StackCount 저장 (핵심 수정!)
	// ============================================
	// ⭐ Phase 5: SavedStackCount가 전달되면 그 값을 사용, 아니면 현재 슬롯의 StackCount 사용
	const int32 OriginalStackCount = (SavedStackCount > 0) ? SavedStackCount : GridSlots[CurrentIndex]->GetStackCount();
	UE_LOG(LogTemp, Warning, TEXT("    │     📦 기존 StackCount: %d (SavedStackCount=%d)"), OriginalStackCount, SavedStackCount);

	// ============================================
	// Step 5: 원래 위치의 GridSlots 해제 (+ 텍스처/상태 복원!)
	// ============================================
	UInv_InventoryStatics::ForEach2D(GridSlots, CurrentIndex, Dimensions, Columns, [&](UInv_GridSlot* GridSlot)
	{
		if (GridSlot)
		{
			GridSlot->SetInventoryItem(nullptr);
			GridSlot->SetUpperLeftIndex(INDEX_NONE);  // ⭐ UpperLeftIndex도 초기화
			GridSlot->SetStackCount(0);  // ⭐ StackCount 초기화
			GridSlot->SetAvailable(true);  // ⭐ 핵심 수정: 슬롯을 사용 가능으로 설정! (Hover 애니메이션 복원)
			GridSlot->SetUnoccupiedTexture();
		}
	});

	// ============================================
	// Step 6: SlottedItems 맵 키 변경
	// ============================================
	SlottedItems.Remove(CurrentIndex);
	SlottedItems.Add(TargetIndex, FoundSlottedItem);

	// ============================================
	// Step 7: 새 위치의 GridSlots 점유
	// ============================================
	bool bIsFirstSlot = true;
	UInv_InventoryStatics::ForEach2D(GridSlots, TargetIndex, Dimensions, Columns, [&](UInv_GridSlot* GridSlot)
	{
		if (GridSlot)
		{
			GridSlot->SetInventoryItem(InventoryItem);
			GridSlot->SetUpperLeftIndex(TargetIndex);
			GridSlot->SetOccupiedTexture();
			GridSlot->SetAvailable(false);  // ⭐ 핵심 수정: 슬롯을 사용 불가능으로 설정!
			
			// ⭐ 핵심 수정: 첫 번째 슬롯(UpperLeft)에만 StackCount 설정
			if (bIsFirstSlot)
			{
				GridSlot->SetStackCount(OriginalStackCount);
				bIsFirstSlot = false;
				UE_LOG(LogTemp, Warning, TEXT("    │     📦 새 위치에 StackCount=%d 설정"), OriginalStackCount);
			}
		}
	});

	// ============================================
	// ⭐ Step 7.5: SlottedItem 위젯의 GridIndex 업데이트 (핵심 수정!)
	// ============================================
	// 문제: SlottedItem 클릭 시 저장된 GridIndex를 Broadcast함
	// 해결: 새 위치의 GridIndex로 업데이트해야 클릭이 정상 동작
	FoundSlottedItem->SetGridIndex(TargetIndex);
	// ⭐ Phase 5: SlottedItem UI 텍스트도 업데이트 (로드 후 "1"로 표시되는 버그 수정)
	FoundSlottedItem->UpdateStackCount(OriginalStackCount);
	UE_LOG(LogTemp, Warning, TEXT("    │     🔧 SlottedItem.GridIndex=%d로 업데이트, UI StackCount=%d"), TargetIndex, OriginalStackCount);

	// ============================================
	// Step 8: 위젯 위치 업데이트
	// ============================================
	UCanvasPanelSlot* CanvasSlot = Cast<UCanvasPanelSlot>(FoundSlottedItem->Slot);
	if (CanvasSlot)
	{
		const FVector2D DrawPos = FVector2D(TargetPosition.X * TileSize, TargetPosition.Y * TileSize);
		float ItemPadding = 0.0f;
		if (GridFragment)
		{
			ItemPadding = GridFragment->GetGridPadding();
		}
		const FVector2D DrawPosWithPadding = DrawPos + FVector2D(ItemPadding);
		CanvasSlot->SetPosition(DrawPosWithPadding);
	}

	UE_LOG(LogTemp, Warning, TEXT("    │     ✅ [MoveItemByCurrentIndex] 이동 완료!"));
	UE_LOG(LogTemp, Warning, TEXT("    │         %s: Index %d → %d, Pos(%d,%d)"),
		*InventoryItem->GetItemManifest().GetItemType().ToString(),
		CurrentIndex, TargetIndex, TargetPosition.X, TargetPosition.Y);

	return true;
}


