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
	AssignHoverItem(InventoryItem);

	HoverItem->SetPreviousGridIndex(PreviousGridIndex);
	HoverItem->UpdateStackCount(InventoryItem->IsStackable() ? GridSlots[GridIndex]->GetStackCount() : 0);
}

void UInv_InventoryGrid::RemoveItemFromGrid(UInv_InventoryItem* InventoryItem, const int32 GridIndex) // 아이템을 Hover 한 뒤로.
{
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
		FoundSlottedItem->RemoveFromParent();
	}
}


void UInv_InventoryGrid::AssignHoverItem(UInv_InventoryItem* InventoryItem) // 이걸 참조하면 나중에 그걸 만들 수 있겠지? 창고
{
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
			AddItemAtIndex(Result.Item.Get(), Availability.Index, Result.bStackable, Availability.AmountToFill); // 인덱스에 아이템 추가
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
void UInv_InventoryGrid::AddItem(UInv_InventoryItem* Item)
{
	//아이템 그리드 체크 부분?
	if (!MatchesCategory(Item)) return;

	//공간이 있다고 부르는 부분.
	FInv_SlotAvailabilityResult Result = HasRoomForItem(Item);

	// Create a widget to show the item icon and add it to the correct spot on the grid.
	// 아이콘을 보여주고 그리드의 올바른 위치에 추가하는 위젯을 만듭니다.
	AddItemToIndices(Result, Item);

}

void UInv_InventoryGrid::AddItemToIndices(const FInv_SlotAvailabilityResult& Result, UInv_InventoryItem* NewItem) 
{
	for (const auto& Availability : Result.SlotAvailabilities)
	{
		AddItemAtIndex(NewItem, Availability.Index, Result.bStackable, Availability.AmountToFill);// 인덱스에 아이템 추가
		UpdateGridSlots(NewItem, Availability.Index, Result.bStackable, Availability.AmountToFill);//그리드 슬롯 업데이트 부분
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

void UInv_InventoryGrid::AddItemAtIndex(UInv_InventoryItem* Item, const int32 Index, const bool bStackable, const int32 StackAmount)
{
	//격자의 크기를 얻어오자. 게임플레이 태그로 말야
	// Get Grid Fragment so we know how many grid spaces the item takes.
	// 텍스처와 아이콘도 여기서 얻어온다는건가?
	// Get Image Fragment so we have the icon to display
	const FInv_GridFragment* GridFragment = GetFragment<FInv_GridFragment>(Item, FragmentTags::GridFragment);
	const FInv_ImageFragment* ImageFragment = GetFragment<FInv_ImageFragment>(Item, FragmentTags::IconFragment);
	if (!GridFragment || !ImageFragment) return; // 둘 중 하나라도 없으면 리턴

	// Add the slotted item to the canvas panel.
	// 슬롯 아이템을 캔버스 패널에 그려주는 곳. 또한 그리드 슬롯을 관리해주는 곳.
	UInv_SlottedItem* SlottedItem = CreateSlottedItem(Item, bStackable, StackAmount, GridFragment, ImageFragment, Index);
	AddSlottedItemToCanvas(Index, GridFragment, SlottedItem); // 캔버스에 슬로티드 아이템 추가하는 부분

	// 삭제 소비 파괴 했을 때 이곳에.
	// Store the new widget in a container.
	SlottedItems.Add(Index, SlottedItem); // 인덱스와 슬로티드 아 이템 매핑
}

UInv_SlottedItem* UInv_InventoryGrid::CreateSlottedItem(UInv_InventoryItem* Item, const bool bStackable, const int32 StackAmount, const FInv_GridFragment* GridFragment, const FInv_ImageFragment* ImageFragment, const int32 Index)
{
	// Create a widget to add to the grid
	UInv_SlottedItem* SlottedItem = CreateWidget<UInv_SlottedItem>(GetOwningPlayer(), SlottedItemClass);
	SlottedItem->SetInventoryItem(Item);
	SetSlottedItemImage(SlottedItem, GridFragment, ImageFragment);
	SlottedItem->SetGridIndex(Index);
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



void UInv_InventoryGrid::PutDownOnIndex(const int32 Index) // 집은 아이템을 다시 내려놓을 때 사용되는 함수.
{
	AddItemAtIndex(HoverItem->GetInventoryItem(), Index, HoverItem->IsStackable(), HoverItem->GetStackCount()); // 인덱스에 아이템 추가
	UpdateGridSlots(HoverItem->GetInventoryItem(), Index, HoverItem->IsStackable(), HoverItem->GetStackCount()); // 그리드 슬롯 업데이트
	ClearHoverItem();
}

void UInv_InventoryGrid::ClearHoverItem() // 호버(잡는모션) 아이템 초기화
{
	if (!IsValid(HoverItem)) return;

	HoverItem->SetInventoryItem(nullptr); // 호버 아이템의 인벤토리 아이템 초기화
	HoverItem->SetIsStackable(false); // 호버 아이템의 스택 가능 여부 초기화
	HoverItem->SetPreviousGridIndex(INDEX_NONE); // 이전 그리드 인덱스 초기화
	HoverItem->UpdateStackCount(0); // 스택 수 초기화
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
	const bool bIsSameItem = ClickedInventoryItem == HoverItem->GetInventoryItem();
	const bool bIsStackable = ClickedInventoryItem->IsStackable();
	return bIsSameItem && bIsStackable && HoverItem->GetItemType().MatchesTagExact(ClickedInventoryItem->GetItemManifest().GetItemType());
}

void UInv_InventoryGrid::SwapWithHoverItem(UInv_InventoryItem* ClickedInventoryItem, const int32 GridIndex)
{
	if (!IsValid(HoverItem)) return; // 호버 아이템이 유효하다면 리턴
	
	// 임시로 저장해서 할당하는 이유가 뭘까?
	UInv_InventoryItem* TempInventoryItem = HoverItem->GetInventoryItem(); // 호버 아이템 임시 저장
	const int32 TempStackCount = HoverItem->GetStackCount(); // 호버 아이템 스택 수 임시 저장
	const bool bTempIsStackable = HoverItem->IsStackable(); // 호버 아이템 스택 가능 여부 임시 저장
	
	// 이전 격자 인덱스를 유지시켜야 하는 부분.
	// Keep the same previous grid index.
	AssignHoverItem(ClickedInventoryItem, GridIndex, HoverItem->GetPreviousGridIndex()); // 클릭된 아이템을 호버 아이템으로 할당
	RemoveItemFromGrid(ClickedInventoryItem, GridIndex); // 그리드에서 클릭된 아이템 제거
	AddItemAtIndex(TempInventoryItem, ItemDropIndex, bTempIsStackable, TempStackCount); // 임시 저장된 아이템을 인덱스에 추가
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
	
	//인덱스 격자 슬롯의 스택 수 확인
	GridSlots[Index]->SetStackCount(NewClickedStackCount); // 그리드 슬롯 스택 수 업데이트
	SlottedItems.FindChecked(Index)->UpdateStackCount(NewClickedStackCount); // 슬로티드 아이템 스택 수 업데이트
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
	const int32 StackCount = UpperLeftGridSlot -> GetStackCount(); // 스택 수 가져오기
	const int32 NewStackCount = StackCount - SplitAmount; // 새로운 스택 수 계산 <- 분할된 양을 빼주는 것
	
	UpperLeftGridSlot->SetStackCount(NewStackCount); // 그리드 슬롯 스택 수 업데이트
	SlottedItems.FindChecked(UpperLeftIndex)->UpdateStackCount(NewStackCount); // 슬로티드 아이템 스택 수 업데이트
	
	AssignHoverItem(RightClickedItem, UpperLeftIndex, UpperLeftIndex); // 호버 아이템 할당
	HoverItem->UpdateStackCount(SplitAmount); // 호버 아이템 스택 수 업데이트
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

