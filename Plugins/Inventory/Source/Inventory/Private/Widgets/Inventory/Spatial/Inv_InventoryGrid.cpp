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
#include "Widgets/Inventory/AttachmentSlots/Inv_AttachmentPanel.h"
#include "Items/Fragments/Inv_AttachmentFragments.h"
#include "InventoryManagement/Components/Inv_LootContainerComponent.h"
#include "Framework/Application/SlateApplication.h"
#include "Components/Image.h" // R키 회전: SlottedItem 이미지 RenderTransform용

// 인벤토리 바인딩 메뉴
void UInv_InventoryGrid::NativeOnInitialized()
{
	Super::NativeOnInitialized();

	// ⭐ [Phase 4 Lobby] bSkipAutoInit=true이면 ConstructGrid + 자동 바인딩 모두 스킵
	// 로비 듀얼 Grid에서는 SetInventoryComponent()에서 ConstructGrid를 지연 호출
	// 이유: 로비에서는 CanvasPanel(BindWidget)이 NativeOnInitialized 시점에 아직 nullptr일 수 있음
	//       → ConstructGrid()가 CanvasPanel->AddChild()에서 크래시 발생
	if (bSkipAutoInit)
	{
		UE_LOG(LogTemp, Log, TEXT("[InventoryGrid] bSkipAutoInit=true → ConstructGrid + 자동 바인딩 스킵 (SetInventoryComponent 대기)"));
		return;
	}

	ConstructGrid();

	InventoryComponent = UInv_InventoryStatics::GetInventoryComponent(GetOwningPlayer()); // 플레이어의 인벤토리 컴포넌트를 가져온다.
	// U2: InventoryComponent null 체크 (타이밍에 따라 아직 준비 안 될 수 있음)
	if (!InventoryComponent.IsValid())
	{
		UE_LOG(LogTemp, Error, TEXT("[InventoryGrid] NativeOnInitialized: InventoryComponent를 찾을 수 없음!"));
		return;
	}
	InventoryComponent->OnItemAdded.AddDynamic(this, &ThisClass::AddItem); // 델리게이트 바인딩
	InventoryComponent->OnStackChange.AddDynamic(this, &ThisClass::AddStacks); // 스택 변경 델리게이트 바인딩
	InventoryComponent->OnInventoryMenuToggled.AddDynamic(this, &ThisClass::OnInventoryMenuToggled);
	InventoryComponent->OnItemRemoved.AddDynamic(this, &ThisClass::RemoveItem); // 아이템 제거 델리게이트 바인딩
	InventoryComponent->OnMaterialStacksChanged.AddDynamic(this, &ThisClass::UpdateMaterialStacksByTag); // Building 재료 업데이트 바인딩
}

// ════════════════════════════════════════════════════════════════
// U19: NativeDestruct — 위젯 파괴 시 InvComp 델리게이트 해제
// ════════════════════════════════════════════════════════════════
void UInv_InventoryGrid::NativeDestruct()
{
	if (InventoryComponent.IsValid())
	{
		InventoryComponent->OnItemAdded.RemoveDynamic(this, &ThisClass::AddItem);
		InventoryComponent->OnStackChange.RemoveDynamic(this, &ThisClass::AddStacks);
		InventoryComponent->OnInventoryMenuToggled.RemoveDynamic(this, &ThisClass::OnInventoryMenuToggled);
		InventoryComponent->OnItemRemoved.RemoveDynamic(this, &ThisClass::RemoveItem);
		InventoryComponent->OnMaterialStacksChanged.RemoveDynamic(this, &ThisClass::UpdateMaterialStacksByTag);
	}
	Super::NativeDestruct();
}

// ════════════════════════════════════════════════════════════════
// 📌 [Phase 4 Lobby] 외부 InvComp 수동 바인딩
// ════════════════════════════════════════════════════════════════
void UInv_InventoryGrid::SetInventoryComponent(UInv_InventoryComponent* InComp)
{
	if (!InComp)
	{
		UE_LOG(LogTemp, Warning, TEXT("[InventoryGrid] SetInventoryComponent: InComp is nullptr!"));
		return;
	}

	// ⭐ [Phase 4 Lobby] bSkipAutoInit=true로 지연된 ConstructGrid 실행
	// NativeOnInitialized에서 스킵된 Grid 구성을 여기서 수행
	// GridSlots이 비어있으면 아직 ConstructGrid가 호출되지 않은 것
	if (GridSlots.Num() == 0 && CanvasPanel)
	{
		UE_LOG(LogTemp, Log, TEXT("[InventoryGrid] SetInventoryComponent → 지연된 ConstructGrid 실행 (bSkipAutoInit 경로)"));
		ConstructGrid();
	}
	else if (GridSlots.Num() == 0 && !CanvasPanel)
	{
		UE_LOG(LogTemp, Error, TEXT("[InventoryGrid] SetInventoryComponent: CanvasPanel이 nullptr! Grid 구성 불가"));
		UE_LOG(LogTemp, Error, TEXT("[InventoryGrid]   → WBP에서 Grid 위젯 내부에 'CanvasPanel' 이름의 CanvasPanel을 추가하세요"));
	}

	// 이전 바인딩이 있다면 해제
	if (InventoryComponent.IsValid())
	{
		InventoryComponent->OnItemAdded.RemoveDynamic(this, &ThisClass::AddItem);
		InventoryComponent->OnStackChange.RemoveDynamic(this, &ThisClass::AddStacks);
		InventoryComponent->OnInventoryMenuToggled.RemoveDynamic(this, &ThisClass::OnInventoryMenuToggled);
		InventoryComponent->OnItemRemoved.RemoveDynamic(this, &ThisClass::RemoveItem);
		InventoryComponent->OnMaterialStacksChanged.RemoveDynamic(this, &ThisClass::UpdateMaterialStacksByTag);
		UE_LOG(LogTemp, Log, TEXT("[InventoryGrid] 이전 InvComp 바인딩 해제 완료"));
	}

	// 새 컴포넌트 바인딩
	InventoryComponent = InComp;
	InventoryComponent->OnItemAdded.AddDynamic(this, &ThisClass::AddItem);
	InventoryComponent->OnStackChange.AddDynamic(this, &ThisClass::AddStacks);
	InventoryComponent->OnInventoryMenuToggled.AddDynamic(this, &ThisClass::OnInventoryMenuToggled);
	InventoryComponent->OnItemRemoved.AddDynamic(this, &ThisClass::RemoveItem);
	InventoryComponent->OnMaterialStacksChanged.AddDynamic(this, &ThisClass::UpdateMaterialStacksByTag);

	UE_LOG(LogTemp, Log, TEXT("[InventoryGrid] SetInventoryComponent 완료 → InvComp=%s, Category=%d"),
		*InComp->GetName(), (int32)ItemCategory);

	// [Phase 4 Fix] 이미 InvComp에 존재하는 아이템을 Grid에 동기화
	// SetInventoryComponent 호출 시점에 이미 아이템이 있으면 OnItemAdded가 안 날아가므로
	// 수동으로 기존 아이템을 Grid에 추가
	SyncExistingItems();
}

// ════════════════════════════════════════════════════════════════════════════════
// [Phase 4 Fix] SyncExistingItems — 기존 아이템을 Grid에 동기화
// ════════════════════════════════════════════════════════════════════════════════
//
// 📌 문제: SetInventoryComponent() 호출 시점에 InvComp에 이미 아이템이 있으면
//    OnItemAdded 델리게이트가 발동하지 않아 Grid에 아이템이 표시되지 않음
//
// 📌 해결: InvComp의 Entries를 순회하며 유효한 아이템을 AddItem()으로 수동 추가
//    - bIsEquipped=true → 스킵 (장착 아이템은 Grid 밖)
//    - bIsAttachedToWeapon=true → 스킵 (부착물은 무기에 귀속)
//    - Item==nullptr → 스킵 (빈 엔트리)
//
// ════════════════════════════════════════════════════════════════════════════════
void UInv_InventoryGrid::SyncExistingItems()
{
	if (!InventoryComponent.IsValid())
	{
		return;
	}

	FInv_InventoryFastArray& InvList = InventoryComponent->GetInventoryList();
	const TArray<FInv_InventoryEntry>& Entries = InvList.Entries;

	int32 SyncCount = 0;

	for (int32 i = 0; i < Entries.Num(); ++i)
	{
		const FInv_InventoryEntry& Entry = Entries[i];

		// 빈 엔트리 스킵
		if (!IsValid(Entry.Item))
		{
			continue;
		}

		// 장착된 아이템은 Grid 밖
		if (Entry.bIsEquipped)
		{
			continue;
		}

		// 무기에 부착된 부착물은 Grid 밖
		if (Entry.bIsAttachedToWeapon)
		{
			continue;
		}

		// 카테고리 불일치 스킵 (AddItem 내부에서도 체크하지만 로그 줄이기 위해 미리 필터)
		if (!MatchesCategory(Entry.Item))
		{
			continue;
		}

		// 이미 이 Grid에 표시 중인 아이템인지 확인 (중복 방지)
		bool bAlreadySlotted = false;
		for (const auto& [SlotIdx, Slotted] : SlottedItems)
		{
			if (IsValid(Slotted) && Slotted->GetInventoryItem() == Entry.Item)
			{
				bAlreadySlotted = true;
				break;
			}
		}
		if (bAlreadySlotted)
		{
			continue;
		}

		AddItem(Entry.Item, i);
		++SyncCount;
	}

	if (SyncCount > 0)
	{
		UE_LOG(LogTemp, Log, TEXT("[InventoryGrid] SyncExistingItems: %d개 동기화 완료 (Category=%d)"),
			SyncCount, (int32)ItemCategory);
	}
}

// [Fix21] HoverItem 브러시를 TargetTileSize에 맞게 리사이즈 (크로스 Grid 드래그 시 크기 동적 조절)
void UInv_InventoryGrid::RefreshHoverItemBrushSize(float TargetTileSize)
{
	if (!IsValid(HoverItem)) return;
	if (FMath::IsNearlyEqual(HoverItemCurrentTileSize, TargetTileSize)) return;

	UInv_InventoryItem* Item = HoverItem->GetInventoryItem();
	if (!IsValid(Item)) return;

	const FInv_GridFragment* GridFragment = GetFragment<FInv_GridFragment>(Item, FragmentTags::GridFragment);
	const FInv_ImageFragment* ImageFragment = GetFragment<FInv_ImageFragment>(Item, FragmentTags::IconFragment);
	if (!GridFragment || !ImageFragment) return;

	const float IconTileWidth = TargetTileSize - GridFragment->GetGridPadding() * 2;
	// R키 회전 시 회전된 dimensions로 DrawSize 계산
	const FIntPoint EffDim = GetEffectiveDimensions(GridFragment, HoverItem->IsRotated());
	const FVector2D DrawSize = FVector2D(EffDim) * IconTileWidth;

	FSlateBrush IconBrush;
	IconBrush.SetResourceObject(ImageFragment->GetIcon());
	IconBrush.DrawAs = ESlateBrushDrawType::Image;
	IconBrush.ImageSize = DrawSize * UWidgetLayoutLibrary::GetViewportScale(this);

	HoverItem->SetImageBrush(IconBrush);
	HoverItemCurrentTileSize = TargetTileSize;

	// R키 회전: RenderTransform 유지 (크로스 Grid 이동 시에도 회전 시각 유지)
	UImage* HoverImage = HoverItem->GetImageIcon();
	if (IsValid(HoverImage))
	{
		HoverImage->SetRenderTransformPivot(FVector2D(0.5f, 0.5f));
		HoverImage->SetRenderTransformAngle(HoverItem->IsRotated() ? 90.f : 0.f);
	}
}

// R키 회전: 회전 적용된 실효 크기
FIntPoint UInv_InventoryGrid::GetEffectiveDimensions(const FInv_GridFragment* GridFragment, bool bRotated)
{
	FIntPoint Size = GridFragment ? GridFragment->GetGridSize() : FIntPoint(1, 1);
	return bRotated ? FIntPoint(Size.Y, Size.X) : Size;
}

// R키 회전: 회전 상태에 따른 DrawSize 계산
FVector2D UInv_InventoryGrid::GetDrawSizeRotated(const FInv_GridFragment* GridFragment, bool bRotated) const
{
	const float IconTileWidth = TileSize - GridFragment->GetGridPadding() * 2;
	const FIntPoint EffDim = GetEffectiveDimensions(GridFragment, bRotated);
	return FVector2D(EffDim) * IconTileWidth;
}

// 매 프레임마다 호출되는 틱 함수 (마우스 Hover에 사용)
void UInv_InventoryGrid::NativeTick(const FGeometry& MyGeometry, float InDeltaTime)
{
	Super::NativeTick(MyGeometry, InDeltaTime);

	// [최적화] HoverItem을 들고 있지 않으면 마우스 추적 스킵
	// [Phase 9] LinkedGrid에 HoverItem이 있으면 크로스 Grid 하이라이트를 위해 Tick 실행
	if (!bShouldTickForHover && !HasLinkedHoverItem()) return;

	// U7: CanvasPanel null 체크 (bSkipAutoInit 경로에서 아직 미초기화 상태일 수 있음)
	if (!IsValid(CanvasPanel)) return;

	//캔버스가 시작하는 왼쪽 모서리 점을 알아보자.
	const FVector2D CanvasPosition = UInv_WidgetUtils::GetWidgetPosition(CanvasPanel); // 캔2버스 패널의 위치 가져오기
	const FVector2D MousePosition = UWidgetLayoutLibrary::GetMousePositionOnViewport(GetOwningPlayer()); // 뷰포트에서 마우스 위치 가져오기

	//캔버스 패널 바깥으로 벗어났는지 여부 확인 (매 틱마다 확인해줌)
	if (CursorExitedCanvas(CanvasPosition, UInv_WidgetUtils::GetWidgetSize(CanvasPanel), MousePosition))
	{
		// [Fix21] 커서가 이 Grid를 벗어남 → 대상 Grid의 TileSize로 HoverItem 리사이즈
		if (IsValid(HoverItem))
		{
			if (LobbyTargetGrid.IsValid())
			{
				RefreshHoverItemBrushSize(LobbyTargetGrid->GetTileSize());
			}
			else if (LinkedContainerGrid.IsValid())
			{
				RefreshHoverItemBrushSize(LinkedContainerGrid->GetTileSize());
			}
		}
		return; // 캔버스 패널을 벗어났다면 반환
	}

	// [Fix21] 커서가 이 Grid 안에 있음 → 이 Grid의 TileSize로 HoverItem 리사이즈
	if (bMouseWithinCanvas && IsValid(HoverItem))
	{
		RefreshHoverItemBrushSize(TileSize);
	}

	UpdateTileParameters(CanvasPosition, MousePosition); // 타일 매개변수 업데이트
}

// R키 아이템 회전 핸들러
FReply UInv_InventoryGrid::NativeOnKeyDown(const FGeometry& MyGeometry, const FKeyEvent& InKeyEvent)
{
	if (InKeyEvent.GetKey() != EKeys::R)
	{
		return Super::NativeOnKeyDown(MyGeometry, InKeyEvent);
	}

	// 드래그 중이 아니면 무시
	if (!bShouldTickForHover || !IsValid(HoverItem))
	{
		return Super::NativeOnKeyDown(MyGeometry, InKeyEvent);
	}

	// Split 아이템은 회전 비활성화 (서버에서 새 Entry 생성 시 위치만 전달)
	if (HoverItem->IsSplitItem())
	{
		return FReply::Handled();
	}

	const FIntPoint CurrentDim = HoverItem->GetGridDimensions();

	// 1x1 또는 정사각형은 회전 무의미
	if (CurrentDim.X == CurrentDim.Y)
	{
		return FReply::Handled();
	}

	// 토글 회전
	const bool bNewRotated = !HoverItem->IsRotated();
	HoverItem->SetRotated(bNewRotated);

	// GridDimensions XY 교환
	HoverItem->SetGridDimensions(FIntPoint(CurrentDim.Y, CurrentDim.X));

	// 브러시 재계산 (회전된 크기로)
	UInv_InventoryItem* Item = HoverItem->GetInventoryItem();
	if (IsValid(Item))
	{
		const FInv_GridFragment* GridFragment = GetFragment<FInv_GridFragment>(Item, FragmentTags::GridFragment);
		const FInv_ImageFragment* ImageFragment = GetFragment<FInv_ImageFragment>(Item, FragmentTags::IconFragment);
		if (GridFragment && ImageFragment)
		{
			// 브러시 크기는 회전된 dimensions (캔버스 풋프린트)
			const FVector2D DrawSize = GetDrawSizeRotated(GridFragment, bNewRotated);

			FSlateBrush IconBrush;
			IconBrush.SetResourceObject(ImageFragment->GetIcon());
			IconBrush.DrawAs = ESlateBrushDrawType::Image;
			IconBrush.ImageSize = DrawSize * UWidgetLayoutLibrary::GetViewportScale(this);

			HoverItem->SetImageBrush(IconBrush);
			HoverItemCurrentTileSize = TileSize;

			// 이미지 RenderTransform으로 시각적 회전
			UImage* HoverImage = HoverItem->GetImageIcon();
			if (IsValid(HoverImage))
			{
				HoverImage->SetRenderTransformPivot(FVector2D(0.5f, 0.5f));
				HoverImage->SetRenderTransformAngle(bNewRotated ? 90.f : 0.f);
			}
		}
	}

	// 하이라이트 갱신 — OnTileParametersUpdated 재호출
	if (bMouseWithinCanvas)
	{
		UnHighlightSlots(LastHighlightedIndex, LastHighlightedDimensions);
		OnTileParametersUpdated(TileParameters);
	}

#if INV_DEBUG_WIDGET
	UE_LOG(LogTemp, Warning, TEXT("[R키 회전] bRotated=%s, Dim=(%d,%d)"),
		bNewRotated ? TEXT("true") : TEXT("false"),
		HoverItem->GetGridDimensions().X, HoverItem->GetGridDimensions().Y);
#endif

	return FReply::Handled();
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

	const int32 StartIndex = UInv_WidgetUtils::GetIndexFromPosition(Position, Columns);

	// in the grid bounds?
	// 그리드 경계 내에 있는지?
	if (!IsInGridBounds(StartIndex, Dimensions)) return Result; // 그리드 경계 내에 없으면 빈 결과 반환

	// ⭐ [최적화 #5] 비트마스크 빠른 검사: 영역이 완전히 비어있으면 즉시 반환
	if (IsAreaFree(StartIndex, Dimensions))
	{
		Result.bHasSpace = true;
		return Result;
	}

	Result.bHasSpace = true; // 공간이 있다고 설정

	// If more than one of the indices is occupied with the same item, we nneed to see if they all have the same upper left index.
	// 여러 인덱스가 동일한 항목으로 점유된 경우, 모두 동일한 왼쪽 위 인덱스를 가지고 있는지 확인해야 합니다.
	TSet<int32> OccupiedUpperLeftIndices;
	UInv_InventoryStatics::ForEach2D(GridSlots, StartIndex, Dimensions, Columns, [&](const UInv_GridSlot* GridSlot)
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
	// U9-b: null 체크
	if (!IsValid(ItemComponent)) return FInv_SlotAvailabilityResult();
	return HasRoomForItem(ItemComponent->GetItemManifest());
}

FInv_SlotAvailabilityResult UInv_InventoryGrid::HasRoomForItem(const UInv_InventoryItem* Item, const int32 StackAmountOverride)
{
	// U9-b: null 체크
	if (!IsValid(Item)) return FInv_SlotAvailabilityResult();
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
	// U5: UpperLeftIndex 범위 체크 추가
	if (const int32 UpperLeftIndex = GridSlot->GetUpperLeftIndex(); UpperLeftIndex != INDEX_NONE && GridSlots.IsValidIndex(UpperLeftIndex))
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
	// 기존 SlottedItem의 회전 상태 읽기
	bool bWasRotated = false;
	{
		UInv_SlottedItem* FoundSlotted = SlottedItems.FindRef(GridIndex);
		if (IsValid(FoundSlotted))
		{
			bWasRotated = FoundSlotted->IsRotated();
		}
	}

	// Assign the hover item
	// 아이템을 집었을 때 호버 아이템으로 할당하는 부분
	AssignHoverItem(ClickedInventoryItem, GridIndex, GridIndex);

	// 회전 상태 복원 (AssignHoverItem 이후에 설정)
	if (bWasRotated && IsValid(HoverItem))
	{
		HoverItem->SetRotated(true);
		const FInv_GridFragment* GridFrag = GetFragment<FInv_GridFragment>(ClickedInventoryItem, FragmentTags::GridFragment);
		if (GridFrag)
		{
			// GridDimensions XY 교환
			const FIntPoint OrigDim = GridFrag->GetGridSize();
			HoverItem->SetGridDimensions(FIntPoint(OrigDim.Y, OrigDim.X));

			// 브러시 재계산
			const FInv_ImageFragment* ImgFrag = GetFragment<FInv_ImageFragment>(ClickedInventoryItem, FragmentTags::IconFragment);
			if (ImgFrag)
			{
				const FVector2D DrawSize = GetDrawSizeRotated(GridFrag, true);
				FSlateBrush IconBrush;
				IconBrush.SetResourceObject(ImgFrag->GetIcon());
				IconBrush.DrawAs = ESlateBrushDrawType::Image;
				IconBrush.ImageSize = DrawSize * UWidgetLayoutLibrary::GetViewportScale(this);
				HoverItem->SetImageBrush(IconBrush);

				// 이미지 RenderTransform 회전 적용
				UImage* HoverImage = HoverItem->GetImageIcon();
				if (IsValid(HoverImage))
				{
					HoverImage->SetRenderTransformPivot(FVector2D(0.5f, 0.5f));
					HoverImage->SetRenderTransformAngle(90.f);
				}
			}
		}
	}

	// Remove Clicked Item from the grid (회전된 크기로 제거)
	RemoveItemFromGrid(ClickedInventoryItem, GridIndex);
}

// 호버 아이템 할당 부분
void UInv_InventoryGrid::AssignHoverItem(UInv_InventoryItem* InventoryItem, const int32 GridIndex, const int32 PreviousGridIndex)
{
	// ⭐ Nullptr 체크 (EXCEPTION_ACCESS_VIOLATION 방지)
	if (!IsValid(InventoryItem))
	{
#if INV_DEBUG_WIDGET
		UE_LOG(LogTemp, Error, TEXT("[AssignHoverItem] ❌ InventoryItem이 nullptr입니다! GridIndex=%d"), GridIndex);
#endif
		return;
	}

	AssignHoverItem(InventoryItem);

	HoverItem->SetPreviousGridIndex(PreviousGridIndex);
	HoverItem->UpdateStackCount(InventoryItem->IsStackable() ? GridSlots[GridIndex]->GetStackCount() : 0);
}

void UInv_InventoryGrid::RemoveItemFromGrid(UInv_InventoryItem* InventoryItem, const int32 GridIndex) // 아이템을 Hover 한 뒤로.
{
#if INV_DEBUG_WIDGET
	UE_LOG(LogTemp, Warning, TEXT("[RemoveItemFromGrid] 호출됨: GridIndex=%d, Item=%s"),
		GridIndex, IsValid(InventoryItem) ? *InventoryItem->GetItemManifest().GetItemType().ToString() : TEXT("NULL"));
#endif

	const FInv_GridFragment* GridFragment = GetFragment<FInv_GridFragment>(InventoryItem, FragmentTags::GridFragment);
	if (!GridFragment) return;

	// 회전 상태 확인: SlottedItem이 있으면 회전 여부 체크, 없으면 HoverItem에서 확인
	bool bItemRotated = false;
	{
		UInv_SlottedItem* FoundSlotted = SlottedItems.FindRef(GridIndex);
		if (IsValid(FoundSlotted))
		{
			bItemRotated = FoundSlotted->IsRotated();
		}
		else if (IsValid(HoverItem) && HoverItem->GetInventoryItem() == InventoryItem)
		{
			bItemRotated = HoverItem->IsRotated();
		}
	}

	const FIntPoint GridSize = GetEffectiveDimensions(GridFragment, bItemRotated);
	UInv_InventoryStatics::ForEach2D(GridSlots, GridIndex, GridSize, Columns, [&](UInv_GridSlot* GridSlot)
		{
			//인벤토리 아이템 옮기기인데. 기존 있던 것을 0으로 두고 새로운 곳으로 인덱스를 둔다. (람다 함수 부분)
			GridSlot->SetInventoryItem(nullptr);
			GridSlot->SetUpperLeftIndex(INDEX_NONE);
			GridSlot->SetUnoccupiedTexture();
			GridSlot->SetAvailable(true);
			GridSlot->SetStackCount(0);
		});
	SetOccupiedBits(GridIndex, GridSize, false); // ⭐ [최적화 #5] 비트마스크 비점유 설정

	if (SlottedItems.Contains(GridIndex))
	{
		TObjectPtr<UInv_SlottedItem> FoundSlottedItem;
		SlottedItems.RemoveAndCopyValue(GridIndex, FoundSlottedItem);

#if INV_DEBUG_WIDGET
		UE_LOG(LogTemp, Log, TEXT("[RemoveItemFromGrid] SlottedItem 삭제: GridIndex=%d"), GridIndex);
#endif

		ReleaseSlottedItem(FoundSlottedItem); // ⭐ [최적화 #6] 풀에 반환
	}
	else
	{
#if INV_DEBUG_WIDGET
		UE_LOG(LogTemp, Warning, TEXT("[RemoveItemFromGrid] GridIndex=%d는 SlottedItems에 없음"), GridIndex);
#endif
	}
}


void UInv_InventoryGrid::AssignHoverItem(UInv_InventoryItem* InventoryItem) // 이걸 참조하면 나중에 그걸 만들 수 있겠지? 창고
{
	// ⭐ Nullptr 체크 (EXCEPTION_ACCESS_VIOLATION 방지)
	if (!IsValid(InventoryItem))
	{
#if INV_DEBUG_WIDGET
		UE_LOG(LogTemp, Error, TEXT("[AssignHoverItem] ❌ InventoryItem이 nullptr입니다!"));
#endif
		// U1: HoverItem이 아직 생성되지 않았을 수 있으므로 null 체크
		if (IsValid(HoverItem))
		{
			HoverItem->SetVisibility(ESlateVisibility::Hidden);
		}
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
	bShouldTickForHover = true; // [최적화] Tick 활성화
	HoverItemCurrentTileSize = TileSize; // [Fix21] 크로스 Grid 리사이즈 추적
}

void UInv_InventoryGrid::OnHide()
{
	PutHoverItemBack();
	bShouldTickForHover = false; // [최적화] 인벤토리 닫힘 → Tick 확실히 비활성화

	// ⭐ [최적화 #6] 풀 트리밍 — 너무 많이 쌓이면 메모리 절약
	const int32 MaxPoolSize = 16;
	while (SlottedItemPool.Num() > MaxPoolSize)
	{
		SlottedItemPool.Pop();
	}
}

// ⭐ [최적화 #6] 풀에서 SlottedItem 획득 (없으면 새로 생성)
UInv_SlottedItem* UInv_InventoryGrid::AcquireSlottedItem()
{
	if (SlottedItemPool.Num() > 0)
	{
		UInv_SlottedItem* Pooled = SlottedItemPool.Pop();
		if (IsValid(Pooled))
		{
			Pooled->SetVisibility(ESlateVisibility::Visible);
			return Pooled;
		}
	}
	// 풀이 비어있으면 새로 생성
	return CreateWidget<UInv_SlottedItem>(GetOwningPlayer(), SlottedItemClass);
}

// ⭐ [최적화 #6] SlottedItem을 풀에 반환
void UInv_InventoryGrid::ReleaseSlottedItem(UInv_SlottedItem* SlottedItem)
{
	if (!IsValid(SlottedItem)) return;

	SlottedItem->RemoveFromParent();
	SlottedItem->SetVisibility(ESlateVisibility::Collapsed);
	SlottedItem->SetInventoryItem(nullptr);
	SlottedItem->SetRotated(false); // 회전 상태 리셋
	SlottedItemPool.Add(SlottedItem);
}

// ⭐ [최적화 #5] 비트마스크 점유 상태 일괄 설정
void UInv_InventoryGrid::SetOccupiedBits(int32 StartIndex, const FIntPoint& Dimensions, bool bOccupied)
{
	const FIntPoint StartPos = UInv_WidgetUtils::GetPositionFromIndex(StartIndex, Columns);
	for (int32 j = 0; j < Dimensions.Y; ++j)
	{
		for (int32 i = 0; i < Dimensions.X; ++i)
		{
			const FIntPoint Coord = StartPos + FIntPoint(i, j);
			const int32 Idx = UInv_WidgetUtils::GetIndexFromPosition(Coord, Columns);
			if (Idx >= 0 && Idx < OccupiedMask.Num())
			{
				OccupiedMask[Idx] = bOccupied;
			}
		}
	}
}

// ⭐ [최적화 #5] 영역이 비어있는지 비트마스크로 빠르게 확인
bool UInv_InventoryGrid::IsAreaFree(int32 StartIndex, const FIntPoint& Dimensions) const
{
	const FIntPoint StartPos = UInv_WidgetUtils::GetPositionFromIndex(StartIndex, Columns);
	for (int32 j = 0; j < Dimensions.Y; ++j)
	{
		for (int32 i = 0; i < Dimensions.X; ++i)
		{
			const FIntPoint Coord = StartPos + FIntPoint(i, j);
			// 그리드 범위 밖이면 자유 아님
			if (Coord.X < 0 || Coord.X >= Columns || Coord.Y < 0 || Coord.Y >= Rows)
			{
				return false;
			}
			const int32 Idx = UInv_WidgetUtils::GetIndexFromPosition(Coord, Columns);
			if (Idx < 0 || Idx >= OccupiedMask.Num() || OccupiedMask[Idx])
			{
				return false;
			}
		}
	}
	return true;
}

// 같은 아이템이면 수량 쌓기
void UInv_InventoryGrid::AddStacks(const FInv_SlotAvailabilityResult& Result) 
{
	if (!MatchesCategory(Result.Item.Get())) return;

	// SlotAvailabilities가 비어있으면 Item으로 슬롯을 직접 찾아서 업데이트
	if (Result.SlotAvailabilities.Num() == 0 && Result.Item.IsValid())
	{
#if INV_DEBUG_WIDGET
		UE_LOG(LogTemp, Warning, TEXT("=== AddStacks 호출 (Split 대응) ==="));
		UE_LOG(LogTemp, Warning, TEXT("📥 [클라이언트] Item포인터: %p, ItemType: %s, 서버 총량: %d"),
			Result.Item.Get(), *Result.Item->GetItemManifest().GetItemType().ToString(), Result.TotalRoomToFill);

		// 🔍 디버깅: UI 상태 확인 (업데이트 전)
		UE_LOG(LogTemp, Warning, TEXT("🔍 [클라이언트] UI 슬롯 상태 (업데이트 전):"));
#endif
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
#if INV_DEBUG_WIDGET
				UE_LOG(LogTemp, Warning, TEXT("  슬롯[%d]: Item포인터=%p, StackCount=%d"),
					Index, GridSlotItem, SlotCount);
#endif
			}
		}
#if INV_DEBUG_WIDGET
		UE_LOG(LogTemp, Warning, TEXT("🔍 [클라이언트] UI 총량 (업데이트 전): %d"), UITotalBefore);
		UE_LOG(LogTemp, Warning, TEXT("🔍 [클라이언트] 차감량: %d - %d = %d"),
			UITotalBefore, Result.TotalRoomToFill, UITotalBefore - Result.TotalRoomToFill);
#endif
		
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
#if INV_DEBUG_WIDGET
				UE_LOG(LogTemp, Warning, TEXT("📌 [클라이언트] 매칭된 슬롯 발견: Index=%d, CurrentCount=%d, Item포인터=%p"),
					Index, GridSlots[Index]->GetStackCount(), GridSlotItem);
#endif
			}
		}

		if (MatchedIndices.Num() == 0)
		{
#if INV_DEBUG_WIDGET
			UE_LOG(LogTemp, Error, TEXT("❌ [클라이언트] AddStacks: Item에 해당하는 슬롯을 찾지 못함! Item포인터: %p"), Result.Item.Get());
#endif
			return;
		}

#if INV_DEBUG_WIDGET
		UE_LOG(LogTemp, Warning, TEXT("📊 [클라이언트] 총 %d개 슬롯 매칭됨, UI 총량: %d → 서버 총량: %d"),
			MatchedIndices.Num(), TotalUICount, Result.TotalRoomToFill);
#endif
		
		// ⭐ 1.5단계: 큰 스택부터 처리하도록 정렬 (안정적인 분배를 위해)
		MatchedIndices.Sort([this](int32 A, int32 B) {
			return GridSlots[A]->GetStackCount() > GridSlots[B]->GetStackCount();
		});

#if INV_DEBUG_WIDGET
		UE_LOG(LogTemp, Warning, TEXT("🔀 [클라이언트] 슬롯 정렬 완료 (큰 스택부터):"));
		for (int32 Index : MatchedIndices)
		{
			UE_LOG(LogTemp, Warning, TEXT("  슬롯[%d]: StackCount=%d"), Index, GridSlots[Index]->GetStackCount());
		}
#endif

		// ⭐ 2단계: 서버 총량을 슬롯들에 분배
		int32 RemainingToDistribute = Result.TotalRoomToFill;
		TArray<int32> IndicesToRemove;

#if INV_DEBUG_WIDGET
		UE_LOG(LogTemp, Warning, TEXT("🔧 [클라이언트] 슬롯별 분배 시작 (RemainingToDistribute=%d):"), RemainingToDistribute);
#endif
		
		for (int32 Index : MatchedIndices)
		{
			int32 OldCount = GridSlots[Index]->GetStackCount();
			int32 NewCount = FMath::Min(OldCount, RemainingToDistribute);
			RemainingToDistribute -= NewCount;

#if INV_DEBUG_WIDGET
			UE_LOG(LogTemp, Warning, TEXT("  슬롯[%d]: OldCount=%d, NewCount=%d, Remaining=%d → %d"),
				Index, OldCount, NewCount, RemainingToDistribute + NewCount, RemainingToDistribute);
#endif

			if (NewCount <= 0)
			{
				// 이 슬롯은 제거해야 함
				IndicesToRemove.Add(Index);
#if INV_DEBUG_WIDGET
				UE_LOG(LogTemp, Warning, TEXT("  ❌ 슬롯[%d]: 제거 예약 (%d → 0)"), Index, OldCount);
#endif
			}
			else
			{
				// 개수 업데이트
				GridSlots[Index]->SetStackCount(NewCount);
				if (SlottedItems.Contains(Index) && IsValid(SlottedItems[Index]))
				{
					SlottedItems[Index]->UpdateStackCount(NewCount);
				}
#if INV_DEBUG_WIDGET
				UE_LOG(LogTemp, Warning, TEXT("  ✅ 슬롯[%d]: UI 업데이트 (%d → %d)"), Index, OldCount, NewCount);
#endif
			}
		}
		
		// ⭐ 3단계: 제거할 슬롯들 제거
#if INV_DEBUG_WIDGET
		UE_LOG(LogTemp, Warning, TEXT("🗑️ [클라이언트] 제거할 슬롯 개수: %d"), IndicesToRemove.Num());
#endif
		for (int32 IndexToRemove : IndicesToRemove)
		{
			if (GridSlots.IsValidIndex(IndexToRemove))
			{
				UInv_InventoryItem* ItemToRemove = GridSlots[IndexToRemove]->GetInventoryItem().Get();
				if (IsValid(ItemToRemove))
				{
					RemoveItemFromGrid(ItemToRemove, IndexToRemove);
#if INV_DEBUG_WIDGET
					UE_LOG(LogTemp, Warning, TEXT("  ✅ 슬롯[%d]: UI에서 제거 완료 (Item포인터=%p)"), IndexToRemove, ItemToRemove);
#endif
				}
			}
		}

#if INV_DEBUG_WIDGET
		// 🔍 디버깅: UI 상태 확인 (업데이트 후)
		UE_LOG(LogTemp, Warning, TEXT("🔍 [클라이언트] UI 슬롯 상태 (업데이트 후):"));
#endif
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
#if INV_DEBUG_WIDGET
				UE_LOG(LogTemp, Warning, TEXT("  슬롯[%d]: Item포인터=%p, StackCount=%d"),
					Index, GridSlotItem, SlotCount);
#endif
			}
		}
#if INV_DEBUG_WIDGET
		UE_LOG(LogTemp, Warning, TEXT("🔍 [클라이언트] UI 총량 (업데이트 후): %d (예상: %d)"), UITotalAfter, Result.TotalRoomToFill);

		if (UITotalAfter != Result.TotalRoomToFill)
		{
			UE_LOG(LogTemp, Error, TEXT("⚠️ [클라이언트] UI 총량이 서버 총량과 일치하지 않습니다! UI=%d, 서버=%d"),
				UITotalAfter, Result.TotalRoomToFill);
		}

		UE_LOG(LogTemp, Warning, TEXT("=== AddStacks 완료 ==="));
#endif
		return;
	}

	// 기존 로직: SlotAvailabilities가 있을 때
	for (const auto& Availability : Result.SlotAvailabilities)
	{
		if (Availability.bItemAtIndex) // 해당 인덱스에 아이템이 있는 경우
		{
			const auto& GridSlot = GridSlots[Availability.Index];
			UInv_SlottedItem* SlottedItem = SlottedItems.FindRef(Availability.Index); // U26: FindRef null 안전
			if (SlottedItem) SlottedItem->UpdateStackCount(GridSlot->GetStackCount() + Availability.AmountToFill); // 스택 수 업데이트
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
	// U3: check() → 안전한 early return (데디서버에서 check 실패 시 전체 크래시)
	if (!GridSlots.IsValidIndex(GridIndex)) return;
	UInv_InventoryItem* ClickedInventoryItem = GridSlots[GridIndex]->GetInventoryItem().Get(); // 클릭한 아이템 가져오기
	
	// ⭐ nullptr 체크 추가 (MoveItemByCurrentIndex 후 원래 위치 클릭 시 크래시 방지)
	if (!IsValid(ClickedInventoryItem))
	{
#if INV_DEBUG_WIDGET
		UE_LOG(LogTemp, Warning, TEXT("[OnSlottedItemClicked] ⚠️ GridIndex=%d에 InventoryItem 없음, 무시"), GridIndex);
#endif
		return;
	}

	//좌클릭을 눌렀을 때 실행되는 호버 부분 실행 부분
	if (!IsValid(HoverItem) && IsLeftClick(MouseEvent))
	{
		// [Phase 11] 타르코프 스타일 단축키 — modifier 키 우선 체크
		if (MouseEvent.IsControlDown())
		{
			HandleQuickTransfer(GridIndex);
			return;
		}
		if (MouseEvent.IsAltDown())
		{
			HandleQuickEquip(GridIndex);
			return;
		}
		if (MouseEvent.IsShiftDown())
		{
			HandleQuickSplit(GridIndex);
			return;
		}

		// 호버 항목을 지정하고 그리드에서 슬롯이 있는 항목을 제거하는 부분을 구현하자.
		// Assign the hover item, and remove the slotted item from the grid.
		PickUp(ClickedInventoryItem, GridIndex);
		return;
	}
	
	if (IsRightClick(MouseEvent)) // 우클릭을 눌렀을 때 실행되는 팝업 부분 실행 부분
	{
		// [Phase 9] Shift+RMB → 컨테이너 빠른 전송 (LinkedContainerGrid가 설정된 경우)
		if (LinkedContainerGrid.IsValid() && MouseEvent.IsShiftDown())
		{
			// 클릭된 아이템의 EntryIndex 추출
			int32 LookupIndex = GridIndex;
			if (GridSlots.IsValidIndex(GridIndex) && GridSlots[GridIndex])
			{
				const int32 UpperLeft = GridSlots[GridIndex]->GetUpperLeftIndex();
				if (UpperLeft >= 0) LookupIndex = UpperLeft;
			}

			UInv_SlottedItem* Slotted = SlottedItems.FindRef(LookupIndex);
			if (IsValid(Slotted) && InventoryComponent.IsValid())
			{
				UInv_InventoryItem* SlottedItem = Slotted->GetInventoryItem();
				if (IsValid(SlottedItem))
				{
					// Entry 인덱스 찾기
					int32 EntryIdx = INDEX_NONE;
					const TArray<FInv_InventoryEntry>& Entries =
						(OwnerType == EGridOwnerType::Container && ContainerComp.IsValid())
						? ContainerComp->ContainerInventoryList.Entries
						: InventoryComponent->GetInventoryList().Entries;

					for (int32 i = 0; i < Entries.Num(); ++i)
					{
						if (Entries[i].Item == SlottedItem)
						{
							EntryIdx = i;
							break;
						}
					}

					if (EntryIdx != INDEX_NONE)
					{
						UInv_LootContainerComponent* LinkedCC = LinkedContainerGrid->GetContainerComponent();
						UInv_LootContainerComponent* MyCC = GetContainerComponent();

						if (OwnerType == EGridOwnerType::Container && IsValid(MyCC))
						{
							// 컨테이너 → 플레이어
							InventoryComponent->Server_TakeItemFromContainer(MyCC, EntryIdx, -1);
						}
						else if (OwnerType == EGridOwnerType::Player && IsValid(LinkedCC))
						{
							// 플레이어 → 컨테이너
							InventoryComponent->Server_PutItemInContainer(LinkedCC, EntryIdx, -1);
						}
					}
				}
			}
			return;
		}

		// Shift+RMB → 로비 빠른 전송 (기존 우클릭 전송 로직)
		if (bLobbyTransferMode && MouseEvent.IsShiftDown())
		{
			// 큰 아이템(2x6 등)은 SlottedItems에 왼쪽 상단 셀 인덱스로만 등록됨
			// 클릭된 셀의 UpperLeftIndex를 먼저 구해서 올바른 SlottedItem을 찾음
			int32 LookupIndex = GridIndex;
			if (GridSlots.IsValidIndex(GridIndex) && GridSlots[GridIndex])
			{
				const int32 UpperLeft = GridSlots[GridIndex]->GetUpperLeftIndex();
				if (UpperLeft >= 0)
				{
					LookupIndex = UpperLeft;
				}
			}

			UInv_SlottedItem* Slotted = SlottedItems.FindRef(LookupIndex);
			if (Slotted)
			{
				// ReplicationID 기반 전송 — 배열 인덱스 대신 안정적 식별자 사용
				int32 RepID = INDEX_NONE;
				if (InventoryComponent.IsValid())
				{
					UInv_InventoryItem* SlottedItem = Slotted->GetInventoryItem();
					if (IsValid(SlottedItem))
					{
						const TArray<FInv_InventoryEntry>& Entries = InventoryComponent->GetInventoryList().Entries;
						for (const FInv_InventoryEntry& Entry : Entries)
						{
							if (Entry.Item == SlottedItem)
							{
								RepID = Entry.ReplicationID;
								break;
							}
						}
					}
				}

				UE_LOG(LogTemp, Log, TEXT("[InventoryGrid] 로비 빠른 전송(Shift+RMB) → RepID=%d, GridIndex=%d (UpperLeft=%d)"), RepID, GridIndex, LookupIndex);
				if (RepID != INDEX_NONE)
				{
					// [Fix19] 대상 Grid 용량 사전 체크 - 공간 없으면 전송 차단
					if (LobbyTargetGrid.IsValid())
					{
						UInv_InventoryItem* ItemToTransfer = Slotted->GetInventoryItem();
						if (IsValid(ItemToTransfer))
						{
							const FInv_ItemManifest& Manifest = ItemToTransfer->GetItemManifest();
							if (!LobbyTargetGrid->HasRoomInActualGrid(Manifest))
							{
								UE_LOG(LogTemp, Warning, TEXT("[InventoryGrid] Fix19 - Transfer blocked (Shift+RMB): no room in target Grid (RepID=%d)"), RepID);
								return;
							}
						}
					}
					else
					{
						// [W2] LobbyTargetGrid 미설정 경고 — HellunaLobbyStashWidget::InitializePanels에서 SetLobbyTargetGrid 필요
						UE_LOG(LogTemp, Warning, TEXT("[InventoryGrid] W2 - LobbyTargetGrid 미설정 (Shift+RMB 전송). 용량 체크 없이 전송 진행"));
					}

					OnLobbyTransferRequested.Broadcast(RepID);
				}
				else
				{
					UE_LOG(LogTemp, Error, TEXT("[InventoryGrid] 로비 전송 실패 → ReplicationID 미발견 (Item 없음 또는 InvComp 없음)"));
				}
			}
			else
			{
				UE_LOG(LogTemp, Warning, TEXT("[InventoryGrid] 로비 전송 실패 → GridIndex=%d, LookupIndex=%d에 SlottedItem 없음"), GridIndex, LookupIndex);
			}
			return;
		}

		// RMB (Shift 없음) → 항상 PopupMenu 표시
		CreateItemPopUp(GridIndex);
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
	if (!GridSlots.IsValidIndex(GridIndex)) return;

	// B9: 대형 아이템의 서브셀 클릭 시 UpperLeft 기준으로 보정 (중복 팝업 방지)
	const int32 UpperLeft = GridSlots[GridIndex]->GetUpperLeftIndex();
	const int32 LookupIndex = (UpperLeft >= 0 && GridSlots.IsValidIndex(UpperLeft)) ? UpperLeft : GridIndex;
	UInv_InventoryItem* RightClickedItem = GridSlots[LookupIndex]->GetInventoryItem().Get();
	if (!IsValid(RightClickedItem)) return; //오른쪽 클릭을 확인했을 때.
	if (IsValid(GridSlots[LookupIndex]->GetItemPopUp())) return; // 이미 팝업이 있다면 리턴

	// 기존 팝업이 있으면 정리 (다른 아이템의 팝업)
	if (IsValid(ItemPopUp))
	{
		ItemPopUp->RemoveFromParent();
	}

	// ItemPopUpClass 미설정 방어
	if (!ItemPopUpClass)
	{
		UE_LOG(LogTemp, Error, TEXT("[CreateItemPopUp] ❌ ItemPopUpClass가 설정되지 않음! BP에서 'Item Pop Up Class' 프로퍼티를 설정하세요."));
		return;
	}

	ItemPopUp = CreateWidget<UInv_ItemPopUp>(this, ItemPopUpClass); // 팝업 위젯 생성
	GridSlots[LookupIndex]->SetItemPopUp(ItemPopUp); // B9: UpperLeft 슬롯에 팝업 설정

	// 팝업을 캔버스에 추가 — OwningCanvasPanel이 없으면 자체 CanvasPanel 사용 (로비 듀얼 Grid 대응)
	UCanvasPanel* TargetCanvas = OwningCanvasPanel.IsValid() ? OwningCanvasPanel.Get() : CanvasPanel.Get();
	if (!IsValid(TargetCanvas)) return;
	TargetCanvas->AddChild(ItemPopUp);
	UCanvasPanelSlot* CanvasSlot = UWidgetLayoutLibrary::SlotAsCanvasSlot(ItemPopUp);

	// 뷰포트 마우스 좌표를 캔버스 로컬 좌표로 변환 (캔버스가 화면 중앙/우측에 있어도 정확한 위치에 팝업 표시)
	const FVector2D AbsoluteMousePos = FSlateApplication::Get().GetCursorPos();
	const FVector2D LocalMousePos = TargetCanvas->GetCachedGeometry().AbsoluteToLocal(AbsoluteMousePos);
	CanvasSlot->SetPosition(LocalMousePos - ItemPopUpOffset); // 캔버스 로컬 좌표에 팝업 위치 설정
	CanvasSlot->SetSize(ItemPopUp->GetBoxSize());
	
	const int32 SliderMax = GridSlots[LookupIndex]->GetStackCount() - 1; // B9: UpperLeft 기준 스택 수
	if (RightClickedItem->IsStackable() && SliderMax > 0)
	{
		ItemPopUp->OnSplit.BindDynamic(this, &ThisClass::OnPopUpMenuSplit); // 분할 바인딩
		ItemPopUp->SetSliderParams(SliderMax, FMath::Max(1, GridSlots[LookupIndex]->GetStackCount() / 2)); // 슬라이더 파라미터 설정
	}
	else
	{
		ItemPopUp->CollapseSplitButton(); // 분할 버튼 숨기기
	}
	
	// ════════════════════════════════════════════════════════════════
	// 로비 전송 모드: Transfer 버튼 표시, Drop/Consume 숨기기
	// 게임 모드: Transfer 숨기기, Drop/Consume 표시
	// ════════════════════════════════════════════════════════════════
	if (bLobbyTransferMode)
	{
		// 로비: Transfer 버튼 활성화
		ItemPopUp->OnTransfer.BindDynamic(this, &ThisClass::OnPopUpMenuTransfer);

		// 로비에는 3D 월드가 없으므로 Drop 불필요
		ItemPopUp->CollapseDropButton();
		// 로비에서 소비 불가
		ItemPopUp->CollapseConsumeButton();
	}
	else
	{
		// 게임: Transfer 버튼 숨기기
		ItemPopUp->CollapseTransferButton();

		// 게임: Drop 바인딩
		ItemPopUp->OnDrop.BindDynamic(this, &ThisClass::OnPopUpMenuDrop);

		// 게임: Consume 바인딩 (소비 가능한 아이템만)
		if (RightClickedItem->IsConsumable())
		{
			ItemPopUp->OnConsume.BindDynamic(this, &ThisClass::OnPopUpMenuConsume);
		}
		else
		{
			ItemPopUp->CollapseConsumeButton();
		}
	}

	// ════════════════════════════════════════════════════════════════
	// 📌 [부착물 시스템 Phase 3] 부착물 관리 버튼
	// 부착물 슬롯이 있는 호스트 아이템(무기)에만 표시
	// ════════════════════════════════════════════════════════════════
	if (RightClickedItem->HasAttachmentSlots())
	{
		ItemPopUp->OnAttachment.BindDynamic(this, &ThisClass::OnPopUpMenuAttachment);
	}
	else
	{
		ItemPopUp->CollapseAttachmentButton();
	}
}

void UInv_InventoryGrid::PutHoverItemBack()
{
#if INV_DEBUG_WIDGET
	// [Swap버그추적] PutHoverItemBack 호출
	UE_LOG(LogTemp, Error, TEXT("===== [PutHoverItemBack] ====="));
	UE_LOG(LogTemp, Error, TEXT("  HoverItem 유효=%s"),
		IsValid(HoverItem) ? TEXT("Y") : TEXT("N"));
	if (IsValid(HoverItem))
	{
		UE_LOG(LogTemp, Error, TEXT("  HoverItem 아이템: %s, StackCount=%d, PrevGridIndex=%d, Rotated=%s"),
			IsValid(HoverItem->GetInventoryItem())
				? *HoverItem->GetInventoryItem()->GetItemManifest().GetItemType().ToString() : TEXT("NULL"),
			HoverItem->GetStackCount(),
			HoverItem->GetPreviousGridIndex(),
			HoverItem->IsRotated() ? TEXT("Y") : TEXT("N"));
	}
#endif

	if (!IsValid(HoverItem)) return;

	// 회전 상태를 리셋하여 기본 방향으로 복원
	// HasRoomForItem은 Manifest의 원본 GridSize를 사용하므로, 회전 상태를 기본값으로 맞춤
	if (HoverItem->IsRotated())
	{
		const FInv_GridFragment* GridFrag = GetFragment<FInv_GridFragment>(
			HoverItem->GetInventoryItem(), FragmentTags::GridFragment);
		if (GridFrag)
		{
			HoverItem->SetGridDimensions(GridFrag->GetGridSize()); // 원본 크기로 복원
		}
		HoverItem->SetRotated(false);
	}

	FInv_SlotAvailabilityResult Result = HasRoomForItem(HoverItem->GetInventoryItem(), HoverItem->GetStackCount());
	Result.Item = HoverItem->GetInventoryItem();

	AddStacks(Result);
	ClearHoverItem();
}

// [Fix20] 상대 Grid의 HoverItem을 이쪽으로 전송 (패널 간 드래그 앤 드롭)
bool UInv_InventoryGrid::TryTransferFromTargetGrid()
{
	if (!bLobbyTransferMode) return false;
	if (!LobbyTargetGrid.IsValid()) return false;
	if (!LobbyTargetGrid->HasHoverItem()) return false;

	UInv_HoverItem* SourceHover = LobbyTargetGrid->GetHoverItem();
	if (!IsValid(SourceHover)) return false;

	UInv_InventoryItem* ItemToTransfer = SourceHover->GetInventoryItem();
	if (!IsValid(ItemToTransfer)) return false;

	// ReplicationID 찾기
	if (!LobbyTargetGrid->InventoryComponent.IsValid()) return false;
	const TArray<FInv_InventoryEntry>& Entries = LobbyTargetGrid->InventoryComponent->GetInventoryList().Entries;
	int32 RepID = INDEX_NONE;
	for (const FInv_InventoryEntry& Entry : Entries)
	{
		if (Entry.Item == ItemToTransfer)
		{
			RepID = Entry.ReplicationID;
			break;
		}
	}
	if (RepID == INDEX_NONE) return false;

	// 용량 체크 (Fix19 패턴)
	const FInv_ItemManifest& Manifest = ItemToTransfer->GetItemManifest();
	if (!HasRoomInActualGrid(Manifest))
	{
		UE_LOG(LogTemp, Warning, TEXT("[InventoryGrid] Fix20 - Cross-panel transfer blocked: no room in target Grid (RepID=%d)"), RepID);
		LobbyTargetGrid->PutHoverItemBack();
		LobbyTargetGrid->ShowCursor();
		return true; // 처리됨 (실패이지만 HoverItem 정리)
	}

	// ★ 핵심: 먼저 아이템을 원래 Grid로 복원한 뒤 전송 (Shift+RMB와 동일한 안정적 흐름)
	// ClearHoverItem은 아이템을 어디에도 배치하지 않고 파괴하므로, PutHoverItemBack으로 먼저 Grid에 복원
	LobbyTargetGrid->PutHoverItemBack();
	LobbyTargetGrid->ShowCursor();

	UE_LOG(LogTemp, Log, TEXT("[InventoryGrid] Fix20 - Cross-panel drag-and-drop transfer (RepID=%d)"), RepID);
	LobbyTargetGrid->OnLobbyTransferRequested.Broadcast(RepID);
	return true;
}

void UInv_InventoryGrid::DropItem()
{
	//위젯 쪽에서 먼저 처리하게 하기
	if (!IsValid(HoverItem))
	{
		// [Fix20] 이 Grid에 HoverItem 없으면, 상대 Grid에서 전송 시도 (패널 배경 클릭)
		TryTransferFromTargetGrid();
		return;
	}
	if (!IsValid(HoverItem->GetInventoryItem())) return;

	// [Fix20] 로비 모드에서는 Drop 차단 — Server_DropItem RPC 호출 방지 (Validate 실패 → 강제 킥 방지)
	if (bLobbyTransferMode)
	{
		UE_LOG(LogTemp, Warning, TEXT("[InventoryGrid] Fix20 - Drop blocked in lobby mode. Returning item to original position."));
		PutHoverItemBack();
		ShowCursor();
		return;
	}

	// TODO : Tell the server to actually drop the item
	// TODO : 서버에서 실제로 아이템을 떨어뜨리도록 지시하는 일
	if (!InventoryComponent.IsValid()) return; // C1: TWeakObjectPtr 무효 시 크래시 방지
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
	UE_LOG(LogTemp, Error, TEXT("[Grid-AddItem진단] %s Grid에 추가됨 — NetMode=%d, 포인터=%p, Category=%d, 호출자=%s"),
		Item ? *Item->GetItemManifest().GetItemType().ToString() : TEXT("nullptr"),
		GetWorld() ? (int32)GetWorld()->GetNetMode() : -1,
		Item,
		(int32)ItemCategory,
		TEXT(__FUNCTION__));

#if INV_DEBUG_WIDGET
	// 🔍 [진단] AddItem 시 Grid 주소 및 SlottedItems 상태 확인
	UE_LOG(LogTemp, Error, TEXT("🔍 [AddItem 진단] Grid주소=%p, Category=%d, SlottedItems=%d, Item=%s, EntryIndex=%d"),
		this, (int32)ItemCategory, SlottedItems.Num(),
		Item ? *Item->GetItemManifest().GetItemType().ToString() : TEXT("nullptr"), EntryIndex);

	// 🔍 [진단] 중복 아이템 존재 여부 확인
	for (const auto& [DiagIdx, DiagSlotted] : SlottedItems)
	{
		if (!IsValid(DiagSlotted)) continue;
		UInv_InventoryItem* DiagItem = DiagSlotted->GetInventoryItem();
		if (DiagItem == Item)
		{
			UE_LOG(LogTemp, Error, TEXT("🔍 [AddItem 진단] ⚠️ 중복 감지: Item=%s(ptr=%p)가 이미 GridIndex=%d에 있음! (기존 EntryIndex=%d, 새 EntryIndex=%d)"),
				*Item->GetItemManifest().GetItemType().ToString(), Item, DiagIdx,
				DiagSlotted->GetEntryIndex(), EntryIndex);
		}
	}
#endif

	//아이템 그리드 체크 부분?
	// [Phase 9] 컨테이너 Grid에서는 카테고리 체크 바이패스 (모든 아이템 혼합)
	if (OwnerType != EGridOwnerType::Container && !MatchesCategory(Item))
	{
#if INV_DEBUG_WIDGET
		UE_LOG(LogTemp, Log, TEXT("[AddItem] 카테고리 불일치: %s, Grid=%d"),
			*Item->GetItemManifest().GetItemType().ToString(), (int32)ItemCategory);
#endif
		return;
	}

	// R키 회전: Entry에서 bRotated 읽기 (PostReplicatedAdd → AddItem 경로)
	bool bEntryRotated = false;
	if (OwnerType == EGridOwnerType::Container && ContainerComp.IsValid())
	{
		// 컨테이너 Grid: ContainerComp의 FastArray에서 읽기
		const TArray<FInv_InventoryEntry>& ContEntries = ContainerComp->ContainerInventoryList.Entries;
		if (ContEntries.IsValidIndex(EntryIndex))
		{
			bEntryRotated = ContEntries[EntryIndex].bRotated;
		}
	}
	else if (InventoryComponent.IsValid())
	{
		// 플레이어 Grid: InventoryComponent의 FastArray에서 읽기
		const TArray<FInv_InventoryEntry>& Entries = InventoryComponent->GetInventoryList().Entries;
		if (Entries.IsValidIndex(EntryIndex))
		{
			bEntryRotated = Entries[EntryIndex].bRotated;
		}
	}

#if INV_DEBUG_WIDGET
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
#endif

	for (const auto& [Index, SlottedItem] : SlottedItems)
	{
		if (!IsValid(SlottedItem)) continue;

		// 🔍 디버깅: 각 슬롯의 EntryIndex 상태 확인
		int32 SlotEntryIndex = SlottedItem->GetEntryIndex();
#if INV_DEBUG_WIDGET
		UE_LOG(LogTemp, Warning, TEXT("[AddItem] 슬롯[%d] 검사: SlottedItem.EntryIndex=%d, 찾는값=%d, 매칭=%s"),
			Index, SlotEntryIndex, EntryIndex, (SlotEntryIndex == EntryIndex) ? TEXT("YES") : TEXT("NO"));
#endif

		// ⚠️ EntryIndex가 -1이면 아직 설정되지 않은 상태
		if (SlotEntryIndex == -1 || SlotEntryIndex == 0)
		{
#if INV_DEBUG_WIDGET
			UE_LOG(LogTemp, Warning, TEXT("[AddItem] ⚠️ 슬롯[%d]의 EntryIndex가 초기값(%d)! SetEntryIndex() 호출 누락 의심"), Index, SlotEntryIndex);
#endif
		}

		// ⭐ EntryIndex로 매칭!
		if (SlotEntryIndex == EntryIndex)
		{
			// ✅ EntryIndex 매칭 발견! 스택 카운트만 업데이트
			int32 NewStackCount = Item->GetTotalStackCount();
			int32 OldStackCount = GridSlots[Index]->GetStackCount();

#if INV_DEBUG_WIDGET
			UE_LOG(LogTemp, Warning, TEXT("[AddItem] ⭐ EntryIndex 매칭! GridIndex=%d, EntryIndex=%d"), Index, EntryIndex);
			UE_LOG(LogTemp, Warning, TEXT("[AddItem]   스택 업데이트: %d → %d"), OldStackCount, NewStackCount);
#endif

			// Item 포인터도 업데이트 (리플리케이션 후 포인터가 바뀔 수 있음)
			GridSlots[Index]->SetInventoryItem(Item);
			GridSlots[Index]->SetStackCount(NewStackCount);
			if (IsValid(SlottedItem))
			{
				SlottedItem->UpdateStackCount(NewStackCount);
			}

#if INV_DEBUG_WIDGET
			UE_LOG(LogTemp, Warning, TEXT("========== [AddItem] 스택 업데이트 완료! (EntryIndex 매칭) =========="));
#endif
			return; // ⭐ 새 슬롯 추가 필요 없음!
		}
	}

#if INV_DEBUG_WIDGET
	// ⚠️ 1단계 검색 실패!
	UE_LOG(LogTemp, Warning, TEXT("[AddItem] ❌ 1단계 실패: EntryIndex=%d 를 가진 SlottedItem을 찾지 못함! 2단계(포인터 검색)으로 진행..."), EntryIndex);

	// ⭐⭐⭐ 2단계: Item 포인터로 검색 (Fallback)
	UE_LOG(LogTemp, Warning, TEXT("[AddItem] 🔍 2단계: Item 포인터=%p 로 검색 시작..."), Item);
#endif
	for (const auto& [Index, SlottedItem] : SlottedItems)
	{
		if (!GridSlots.IsValidIndex(Index)) continue;

		UInv_InventoryItem* GridSlotItem = GridSlots[Index]->GetInventoryItem().Get();
		if (GridSlotItem == Item) // ⭐ 포인터 비교로 같은 아이템인지 확인
		{
			// ✅ 기존 아이템 발견! 스택 카운트만 업데이트
			int32 NewStackCount = Item->GetTotalStackCount();
			int32 OldStackCount = GridSlots[Index]->GetStackCount();

#if INV_DEBUG_WIDGET
			UE_LOG(LogTemp, Warning, TEXT("[AddItem] ⭐ Item 포인터 매칭! GridIndex=%d"), Index);
			UE_LOG(LogTemp, Warning, TEXT("[AddItem]   스택 업데이트: %d → %d"), OldStackCount, NewStackCount);
#endif

			GridSlots[Index]->SetStackCount(NewStackCount);
			if (IsValid(SlottedItem))
			{
				SlottedItem->UpdateStackCount(NewStackCount);
			}

#if INV_DEBUG_WIDGET
			UE_LOG(LogTemp, Warning, TEXT("========== [AddItem] 스택 업데이트 완료! (Item 포인터 매칭) =========="));
#endif
			return; // ⭐ 새 슬롯 추가 필요 없음!
		}
	}

#if INV_DEBUG_WIDGET
	UE_LOG(LogTemp, Warning, TEXT("[AddItem] ❌ 기존 슬롯 못 찾음 (EntryIndex/포인터 모두 실패), 새 슬롯 생성..."));
#endif

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
#if INV_DEBUG_WIDGET
				UE_LOG(LogTemp, Warning, TEXT("[AddItem] [Phase 5 Fix] Entry[%d] GridIndex=%d (saved pos), placing..."),
					EntryIndex, SavedGridIndex);
#endif

				const int32 ActualStackCount = Item->GetTotalStackCount();
				const bool bStackable = Item->IsStackable();
				AddItemAtIndex(Item, SavedGridIndex, bStackable, ActualStackCount, EntryIndex);

				// ⭐ [Phase 5 Fix] 로드 시에는 Server_UpdateItemGridPosition RPC 스킵!
				bSuppressServerSync = true;
				UpdateGridSlots(Item, SavedGridIndex, bStackable, ActualStackCount);
				bSuppressServerSync = false;

#if INV_DEBUG_WIDGET
				UE_LOG(LogTemp, Warning, TEXT("[AddItem] [Phase 5 Fix] Placed at saved GridIndex=%d (no RPC)"), SavedGridIndex);
				UE_LOG(LogTemp, Warning, TEXT("========== [AddItem] 아이템 추가 완료 =========="));
#endif
				return;
			}
		}
	}

	// ⭐⭐⭐ 2.5단계: TargetGridIndex 체크 (Split 아이템의 마우스 위치 배치!)
	// Entry에 TargetGridIndex가 설정되어 있으면 해당 위치에 직접 배치
#if INV_DEBUG_WIDGET
	UE_LOG(LogTemp, Warning, TEXT("[AddItem] 🔍 2.5단계 조건 체크 시작"));
	UE_LOG(LogTemp, Warning, TEXT("[AddItem]   InventoryComponent.IsValid()=%s"), InventoryComponent.IsValid() ? TEXT("true") : TEXT("false"));
#endif

	if (InventoryComponent.IsValid())
	{
		FInv_InventoryFastArray& InventoryList = InventoryComponent->GetInventoryList();
#if INV_DEBUG_WIDGET
		UE_LOG(LogTemp, Warning, TEXT("[AddItem]   Entries.Num()=%d, EntryIndex=%d, IsValidIndex=%s"),
			InventoryList.Entries.Num(), EntryIndex,
			InventoryList.Entries.IsValidIndex(EntryIndex) ? TEXT("true") : TEXT("false"));
#endif

		if (InventoryList.Entries.IsValidIndex(EntryIndex))
		{
			int32 TargetGridIndex = InventoryList.Entries[EntryIndex].TargetGridIndex;
#if INV_DEBUG_WIDGET
			UE_LOG(LogTemp, Warning, TEXT("[AddItem] 🔍 2.5단계: Entry[%d]의 TargetGridIndex=%d 체크"), EntryIndex, TargetGridIndex);
#endif

			// TargetGridIndex가 유효하고, 해당 슬롯이 비어있으면 직접 배치
			if (TargetGridIndex != INDEX_NONE && GridSlots.IsValidIndex(TargetGridIndex) && !SlottedItems.Contains(TargetGridIndex))
			{
#if INV_DEBUG_WIDGET
				UE_LOG(LogTemp, Warning, TEXT("[AddItem] ✅ TargetGridIndex=%d가 유효하고 비어있음! 직접 배치 진행"), TargetGridIndex);
#endif

				// 직접 배치
				const int32 ActualStackCount = Item->GetTotalStackCount();
				const bool bStackable = Item->IsStackable();
				AddItemAtIndex(Item, TargetGridIndex, bStackable, ActualStackCount, EntryIndex);
				UpdateGridSlots(Item, TargetGridIndex, bStackable, ActualStackCount);

				// ⭐ 배치 후 TargetGridIndex 초기화 (재사용 방지)
				InventoryList.Entries[EntryIndex].TargetGridIndex = INDEX_NONE;

#if INV_DEBUG_WIDGET
				UE_LOG(LogTemp, Warning, TEXT("[AddItem] ✅ TargetGridIndex=%d에 배치 완료! (Split 아이템 마우스 위치)"), TargetGridIndex);
				UE_LOG(LogTemp, Warning, TEXT("========== [AddItem] 아이템 추가 완료 =========="));
#endif
				return;
			}
			else if (TargetGridIndex != INDEX_NONE)
			{
#if INV_DEBUG_WIDGET
				UE_LOG(LogTemp, Warning, TEXT("[AddItem] ⚠️ TargetGridIndex=%d가 유효하지 않거나 이미 차지됨, 기본 로직으로 진행"), TargetGridIndex);
#endif
			}
		}
	}

	// ⭐⭐⭐ 3단계: 빈 슬롯 찾기 (PostReplicatedAdd/Change 순서 문제 해결!)
	// PostReplicatedAdd가 먼저 실행되어 Grid[0]을 차지한 경우,
	// PostReplicatedChange가 Grid[0]을 찾지 못해 중복 배치되는 문제 방지
#if INV_DEBUG_WIDGET
	UE_LOG(LogTemp, Warning, TEXT("[AddItem] 🔍 3단계: 빈 슬롯 검색 시작 (HasRoomForItem 재사용)"));
#endif

	//공간이 있다고 부르는 부분.
	FInv_SlotAvailabilityResult Result = HasRoomForItem(Item);
	Result.EntryIndex = EntryIndex; // ⭐ Entry Index 저장

#if INV_DEBUG_WIDGET
	UE_LOG(LogTemp, Warning, TEXT("[AddItem] Result.EntryIndex=%d로 설정됨"), Result.EntryIndex);
	UE_LOG(LogTemp, Warning, TEXT("[AddItem] HasRoomForItem 반환: %d개 슬롯"), Result.SlotAvailabilities.Num());
#endif

	// ⭐⭐⭐ 필터링: 이미 차지된 슬롯 제외! (중복 배치 방지)
	TArray<FInv_SlotAvailability> ActuallyEmptySlots;
	for (const FInv_SlotAvailability& SlotAvail : Result.SlotAvailabilities)
	{
		if (!SlottedItems.Contains(SlotAvail.Index))
		{
			ActuallyEmptySlots.Add(SlotAvail);
#if INV_DEBUG_WIDGET
			UE_LOG(LogTemp, Warning, TEXT("[AddItem]   슬롯[%d]: 실제로 비어있음 ✅"), SlotAvail.Index);
#endif
		}
		else
		{
#if INV_DEBUG_WIDGET
			UE_LOG(LogTemp, Warning, TEXT("[AddItem]   슬롯[%d]: 이미 차지됨, 건너뜀 ⚠️"), SlotAvail.Index);
#endif
		}
	}

	if (ActuallyEmptySlots.Num() > 0)
	{
#if INV_DEBUG_WIDGET
		UE_LOG(LogTemp, Warning, TEXT("[AddItem] ✅ 실제 빈 슬롯 발견! 개수: %d"), ActuallyEmptySlots.Num());
#endif

		// 실제로 비어있는 슬롯만 사용
		Result.SlotAvailabilities = ActuallyEmptySlots;

		// Create a widget to show the item icon and add it to the correct spot on the grid.
		// 아이콘을 보여주고 그리드의 올바른 위치에 추가하는 위젯을 만듭니다.
		AddItemToIndices(Result, Item, bEntryRotated);

#if INV_DEBUG_WIDGET
		UE_LOG(LogTemp, Warning, TEXT("[AddItem] ✅ 빈 슬롯에 배치 완료! EntryIndex=%d"), EntryIndex);
		UE_LOG(LogTemp, Warning, TEXT("========== [AddItem] 아이템 추가 완료 =========="));
#endif
	}
	else
	{
#if INV_DEBUG_WIDGET
		UE_LOG(LogTemp, Warning, TEXT("[AddItem] ⚠️ 필터링 후 빈 슬롯 없음 (기존 스택 제외)"));
		UE_LOG(LogTemp, Warning, TEXT("[AddItem] 🔍 4단계: 완전히 새로운 빈 슬롯 재검색 (스택 무시)"));
#endif
		
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
#if INV_DEBUG_WIDGET
					UE_LOG(LogTemp, Warning, TEXT("[AddItem]   슬롯[%d]: 완전히 비어있음 ✅"), Index);
#endif
					break; // 하나만 찾으면 충분 (1x1 아이템)
				}
			}
		}

		if (CompletelyEmptySlots.Num() > 0)
		{
#if INV_DEBUG_WIDGET
			UE_LOG(LogTemp, Warning, TEXT("[AddItem] ✅ 완전히 빈 슬롯 발견! 개수: %d"), CompletelyEmptySlots.Num());
#endif

			Result.SlotAvailabilities = CompletelyEmptySlots;
			AddItemToIndices(Result, Item, bEntryRotated);

#if INV_DEBUG_WIDGET
			UE_LOG(LogTemp, Warning, TEXT("[AddItem] ✅ 새 빈 슬롯에 배치 완료! EntryIndex=%d"), EntryIndex);
			UE_LOG(LogTemp, Warning, TEXT("========== [AddItem] 아이템 추가 완료 =========="));
#endif
		}
		else
		{
#if INV_DEBUG_WIDGET
			UE_LOG(LogTemp, Error, TEXT("[AddItem] ❌ 완전히 빈 슬롯도 찾지 못했습니다! 인벤토리가 가득 참!"));
			UE_LOG(LogTemp, Warning, TEXT("========== [AddItem] 아이템 추가 실패 =========="));
#endif
		}
	}

#if INV_DEBUG_WIDGET
	UE_LOG(LogTemp, Warning, TEXT("========== [AddItem] 아이템 추가 완료 =========="));
#endif
}

// 인벤토리에서 아이템 제거 시 UI에서도 삭제
// ⭐ 핵심 변경: EntryIndex는 로그용으로만 사용, 실제 매칭은 포인터 + ItemManifest로!
void UInv_InventoryGrid::RemoveItem(UInv_InventoryItem* Item, int32 EntryIndex)
{
#if INV_DEBUG_WIDGET
	// [Swap버그추적] RemoveItem 호출됨 (서버 삭제 감지)
	UE_LOG(LogTemp, Error, TEXT("===== [RemoveItem] 서버 삭제 감지 ====="));
	UE_LOG(LogTemp, Error, TEXT("  삭제 대상: %s (포인터=%p), EntryIndex=%d"),
		IsValid(Item) ? *Item->GetItemManifest().GetItemType().ToString() : TEXT("NULL"),
		Item, EntryIndex);
	UE_LOG(LogTemp, Error, TEXT("  현재 SlottedItems 수: %d"), SlottedItems.Num());
	FDebug::DumpStackTraceToLog(ELogVerbosity::Error);
#endif

	if (!IsValid(Item))
	{
#if INV_DEBUG_WIDGET
		UE_LOG(LogTemp, Warning, TEXT("[RemoveItem] Item is invalid!"));
#endif
		return;
	}

#if INV_DEBUG_WIDGET
	// 🔍 [진단] RemoveItem 호출 컨텍스트 확인 (항상 출력)
	UE_LOG(LogTemp, Error, TEXT("🔍 [RemoveItem 진단] Grid=%p, Category=%d, SlottedItems=%d, ItemType=%s, EntryIndex=%d"),
		this, (int32)ItemCategory, SlottedItems.Num(),
		*Item->GetItemManifest().GetItemType().ToString(), EntryIndex);
#endif

	// 콜스택 출력 (어디서 호출되는지 확인)
	// FDebug::DumpStackTraceToLog(ELogVerbosity::Error); // 비활성화: 그리드당 ~15ms 렉 유발

#if INV_DEBUG_WIDGET
	UE_LOG(LogTemp, Warning, TEXT("[RemoveItem] ========== 제거 요청 시작 =========="));
	UE_LOG(LogTemp, Warning, TEXT("[RemoveItem] ItemType=%s, EntryIndex=%d, Grid=%d"),
		*Item->GetItemManifest().GetItemType().ToString(), EntryIndex, (int32)ItemCategory);
#endif

	// ⭐ 2-pass 매칭: 1차=포인터+EntryIndex(Split 안전), 2차=포인터+Manifest(인덱스 시프트 대응)
	int32 FoundIndex = INDEX_NONE;
	int32 FallbackIndex = INDEX_NONE; // EntryIndex 불일치 시 폴백

	for (const auto& [GridIndex, SlottedItem] : SlottedItems)
	{
		if (!IsValid(SlottedItem)) continue;

		UInv_InventoryItem* GridSlotItem = SlottedItem->GetInventoryItem();
		if (!IsValid(GridSlotItem)) continue;

		// 1차 검증: 포인터 비교
		if (GridSlotItem == Item)
		{
			// Manifest 타입 검증 (안전장치)
			if (!GridSlotItem->GetItemManifest().GetItemType().MatchesTagExact(
				Item->GetItemManifest().GetItemType()))
			{
				continue;
			}

			// EntryIndex까지 일치하면 정확한 매칭 (Split 후 포인터 공유 대응)
			if (SlottedItem->GetEntryIndex() == EntryIndex)
			{
				FoundIndex = GridIndex;
#if INV_DEBUG_WIDGET
				UE_LOG(LogTemp, Warning, TEXT("[RemoveItem] ✅ 정확한 매칭! GridIndex=%d (포인터+EntryIndex+Manifest 모두 일치)"), GridIndex);
#endif
				break;
			}

			// EntryIndex 불일치지만 포인터+Manifest 일치 → 폴백 후보 저장 (인덱스 시프트 대응)
			if (FallbackIndex == INDEX_NONE)
			{
				FallbackIndex = GridIndex;
#if INV_DEBUG_WIDGET
				UE_LOG(LogTemp, Warning, TEXT("[RemoveItem] ⚠️ 폴백 후보: GridIndex=%d (포인터+Manifest 일치, EntryIndex 불일치: Slot=%d vs 요청=%d)"),
					GridIndex, SlottedItem->GetEntryIndex(), EntryIndex);
#endif
			}
		}
	}

	// 정확한 매칭 실패 시 폴백 사용 (FastArray 인덱스 시프트로 인한 EntryIndex 불일치 대응)
	if (FoundIndex == INDEX_NONE && FallbackIndex != INDEX_NONE)
	{
		FoundIndex = FallbackIndex;
#if INV_DEBUG_WIDGET
		UE_LOG(LogTemp, Warning, TEXT("[RemoveItem] 🔄 폴백 매칭 사용: GridIndex=%d (EntryIndex 시프트 감지)"), FoundIndex);
#endif
	}

	if (FoundIndex == INDEX_NONE)
	{
#if INV_DEBUG_WIDGET
		UE_LOG(LogTemp, Warning, TEXT("[RemoveItem] ❌ Item을 찾지 못함 (다른 그리드에 있을 수 있음)"));
		UE_LOG(LogTemp, Warning, TEXT("[RemoveItem] ========== 제거 요청 종료 (실패) =========="));
#endif
		return;
	}

	RemoveItemFromGrid(Item, FoundIndex);
#if INV_DEBUG_WIDGET
	UE_LOG(LogTemp, Warning, TEXT("[RemoveItem] ✅ 제거 완료! GridIndex=%d"), FoundIndex);
	UE_LOG(LogTemp, Warning, TEXT("[RemoveItem] ========== 제거 요청 종료 (성공) =========="));
#endif
}

// 🆕 [Phase 6] 포인터만으로 아이템 제거 (장착 복원 시 Grid에서 제거용)
bool UInv_InventoryGrid::RemoveSlottedItemByPointer(UInv_InventoryItem* Item)
{
	if (!IsValid(Item))
	{
#if INV_DEBUG_WIDGET
		UE_LOG(LogTemp, Warning, TEXT("[RemoveSlottedItemByPointer] Item is invalid!"));
#endif
		return false;
	}

#if INV_DEBUG_WIDGET
	UE_LOG(LogTemp, Warning, TEXT("[RemoveSlottedItemByPointer] 🔍 포인터로 아이템 검색: %s"),
		*Item->GetItemManifest().GetItemType().ToString());
#endif

	// SlottedItems를 순회해서 같은 포인터를 가진 슬롯 찾기
	int32 FoundGridIndex = INDEX_NONE;
	for (const auto& [GridIndex, SlottedItem] : SlottedItems)
	{
		if (!IsValid(SlottedItem)) continue;

		UInv_InventoryItem* GridSlotItem = SlottedItem->GetInventoryItem();
		if (GridSlotItem == Item)
		{
			FoundGridIndex = GridIndex;
#if INV_DEBUG_WIDGET
			UE_LOG(LogTemp, Warning, TEXT("[RemoveSlottedItemByPointer] ✅ 찾음! GridIndex=%d"), GridIndex);
#endif
			break;
		}
	}

	if (FoundGridIndex == INDEX_NONE)
	{
#if INV_DEBUG_WIDGET
		UE_LOG(LogTemp, Warning, TEXT("[RemoveSlottedItemByPointer] ❌ 해당 아이템을 Grid에서 찾을 수 없음"));
#endif
		return false;
	}

	// Grid에서 제거
	RemoveItemFromGrid(Item, FoundGridIndex);
#if INV_DEBUG_WIDGET
	UE_LOG(LogTemp, Warning, TEXT("[RemoveSlottedItemByPointer] ✅ Grid에서 제거 완료! GridIndex=%d"), FoundGridIndex);
#endif
	return true;
}

// GameplayTag로 모든 스택을 찾아서 업데이트 (Building 시스템용 - Split된 스택 처리)
void UInv_InventoryGrid::UpdateMaterialStacksByTag(const FGameplayTag& MaterialTag)
{
	if (!MaterialTag.IsValid())
	{
#if INV_DEBUG_WIDGET
		UE_LOG(LogTemp, Error, TEXT("UpdateMaterialStacksByTag: Invalid MaterialTag!"));
#endif
		return;
	}

	if (!InventoryComponent.IsValid())
	{
#if INV_DEBUG_WIDGET
		UE_LOG(LogTemp, Error, TEXT("UpdateMaterialStacksByTag: InventoryComponent is invalid!"));
#endif
		return;
	}

#if INV_DEBUG_WIDGET
	UE_LOG(LogTemp, Warning, TEXT("=== UpdateMaterialStacksByTag 호출 ==="));
	UE_LOG(LogTemp, Warning, TEXT("MaterialTag: %s"), *MaterialTag.ToString());
#endif

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

#if INV_DEBUG_WIDGET
	UE_LOG(LogTemp, Warning, TEXT("InventoryList 총량: %d"), TotalCountInInventory);
#endif

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
#if INV_DEBUG_WIDGET
			UE_LOG(LogTemp, Warning, TEXT("슬롯 %d 발견: 현재 개수 %d"), Index, CurrentCount);
#endif
		}
	}

#if INV_DEBUG_WIDGET
	UE_LOG(LogTemp, Warning, TEXT("발견된 슬롯 개수: %d"), SlotsWithCounts.Num());
#endif
	
	// 3단계: 총량을 슬롯에 분배 (순차적으로 채우기)
	int32 RemainingTotal = TotalCountInInventory;
	TArray<int32> IndicesToRemove;
	
	for (const auto& [Index, OldCount] : SlotsWithCounts)
	{
		if (RemainingTotal <= 0)
		{
			// 남은 총량이 0 → 이 슬롯은 삭제
			IndicesToRemove.Add(Index);
#if INV_DEBUG_WIDGET
			UE_LOG(LogTemp, Warning, TEXT("슬롯 %d: 총량 소진, 제거 예약"), Index);
#endif
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
#if INV_DEBUG_WIDGET
				UE_LOG(LogTemp, Warning, TEXT("슬롯 %d: 0개 됨, 제거 예약 (%d → 0)"), Index, OldCount);
#endif
			}
			else if (NewCount != OldCount)
			{
				// 개수 변경 → 업데이트
				GridSlots[Index]->SetStackCount(NewCount);
				SlottedItems[Index]->UpdateStackCount(NewCount);
#if INV_DEBUG_WIDGET
				UE_LOG(LogTemp, Warning, TEXT("슬롯 %d: UI 업데이트 (%d → %d)"), Index, OldCount, NewCount);
#endif
			}
			else
			{
#if INV_DEBUG_WIDGET
				// 변경 없음
				UE_LOG(LogTemp, Log, TEXT("슬롯 %d: 변경 없음 (유지: %d)"), Index, NewCount);
#endif
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
#if INV_DEBUG_WIDGET
			UE_LOG(LogTemp, Warning, TEXT("슬롯 %d: UI에서 제거 완료"), IndexToRemove);
#endif
		}
	}

#if INV_DEBUG_WIDGET
	UE_LOG(LogTemp, Warning, TEXT("=== UpdateMaterialStacksByTag 완료 ==="));
#endif
}

// GridSlot을 직접 순회하며 재료 차감 (Split된 스택 처리)
void UInv_InventoryGrid::ConsumeItemsByTag(const FGameplayTag& MaterialTag, int32 AmountToConsume)
{
	if (!MaterialTag.IsValid() || AmountToConsume <= 0)
	{
#if INV_DEBUG_WIDGET
		UE_LOG(LogTemp, Error, TEXT("ConsumeItemsByTag: Invalid MaterialTag or Amount!"));
#endif
		return;
	}

#if INV_DEBUG_WIDGET
	UE_LOG(LogTemp, Warning, TEXT("=== ConsumeItemsByTag 시작 ==="));
	UE_LOG(LogTemp, Warning, TEXT("MaterialTag: %s, AmountToConsume: %d"), *MaterialTag.ToString(), AmountToConsume);
#endif

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
#if INV_DEBUG_WIDGET
			UE_LOG(LogTemp, Warning, TEXT("✅ 차감 완료! RemainingToConsume이 0 이하입니다. 순회 종료."));
#endif
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

#if INV_DEBUG_WIDGET
		UE_LOG(LogTemp, Warning, TEXT("슬롯 %d: CurrentCount=%d, ToConsume=%d, RemainingBefore=%d"), Index, CurrentCount, ToConsume, RemainingToConsume);
#endif

		RemainingToConsume -= ToConsume; // ⭐ 차감!
		int32 NewCount = CurrentCount - ToConsume;

#if INV_DEBUG_WIDGET
		UE_LOG(LogTemp, Warning, TEXT("슬롯 %d: 차감 후 RemainingToConsume=%d, NewCount=%d"), Index, RemainingToConsume, NewCount);
#endif

		if (NewCount <= 0)
		{
			// 이 슬롯을 전부 소비 → 제거 예약
			IndicesToRemove.Add(Index);
#if INV_DEBUG_WIDGET
			UE_LOG(LogTemp, Warning, TEXT("슬롯 %d: 전부 소비! 제거 예약 (%d → 0)"), Index, CurrentCount);
#endif
		}
		else
		{
			// 슬롯 개수만 감소
			GridSlots[Index]->SetStackCount(NewCount);

			if (SlottedItems.Contains(Index) && IsValid(SlottedItems[Index]))
			{
				SlottedItems[Index]->UpdateStackCount(NewCount);
			}

#if INV_DEBUG_WIDGET
			UE_LOG(LogTemp, Warning, TEXT("슬롯 %d: UI 업데이트 (%d → %d)"), Index, CurrentCount, NewCount);
#endif
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
#if INV_DEBUG_WIDGET
			UE_LOG(LogTemp, Warning, TEXT("슬롯 %d: UI에서 제거 완료"), IndexToRemove);
#endif
		}
	}

#if INV_DEBUG_WIDGET
	if (RemainingToConsume > 0)
	{
		UE_LOG(LogTemp, Error, TEXT("❌ 재료가 부족합니다! 남은 차감량: %d"), RemainingToConsume);
	}
	else
	{
		UE_LOG(LogTemp, Warning, TEXT("✅ 재료 차감 완료! MaterialTag: %s, Amount: %d"), *MaterialTag.ToString(), AmountToConsume);
	}

	UE_LOG(LogTemp, Warning, TEXT("=== ConsumeItemsByTag 완료 ==="));
#endif
}



void UInv_InventoryGrid::AddItemToIndices(const FInv_SlotAvailabilityResult& Result, UInv_InventoryItem* NewItem, bool bRotated)
{
	for (const auto& Availability : Result.SlotAvailabilities)
	{
		const int32 ActualStackCount = NewItem->GetTotalStackCount();

#if INV_DEBUG_WIDGET
		UE_LOG(LogTemp, Warning, TEXT("[AddItemToIndices] 빈 슬롯 배치: Index=%d, AmountToFill=%d (무시), ActualStackCount=%d (사용), Rotated=%s"),
			Availability.Index, Availability.AmountToFill, ActualStackCount, bRotated ? TEXT("Y") : TEXT("N"));
#endif

		AddItemAtIndex(NewItem, Availability.Index, Result.bStackable, ActualStackCount, Result.EntryIndex, bRotated);
		UpdateGridSlots(NewItem, Availability.Index, Result.bStackable, ActualStackCount, bRotated);
	}
}

FVector2D UInv_InventoryGrid::GetDrawSize(const FInv_GridFragment* GridFragment) const
{
	const float IconTileWidth = TileSize - GridFragment->GetGridPadding() * 2; // 아이콘 타일 너비 계산

	return GridFragment->GetGridSize() * IconTileWidth; // 아이콘 크기 반환
}

void UInv_InventoryGrid::SetSlottedItemImage(const UInv_SlottedItem* SlottedItem, const FInv_GridFragment* GridFragment, const FInv_ImageFragment* ImageFragment, bool bRotated) const
{
	FSlateBrush Brush;
	Brush.SetResourceObject(ImageFragment->GetIcon()); // 아이콘 설정
	Brush.DrawAs = ESlateBrushDrawType::Image; // 이미지로 그리기
	// R키 회전: 회전 시 DrawSize는 원본 크기 유지 (RenderTransform으로 시각적 회전)
	Brush.ImageSize = GetDrawSize(GridFragment);
	SlottedItem->SetImageBrush(Brush); // 슬로티드 아이템에 브러시 설정
}

void UInv_InventoryGrid::AddItemAtIndex(UInv_InventoryItem* Item, const int32 Index, const bool bStackable, const int32 StackAmount, const int32 EntryIndex, bool bRotated)
{
#if INV_DEBUG_WIDGET
	UE_LOG(LogTemp, Log, TEXT("[AddItemAtIndex] GridIndex=%d, Item=%s, Rotated=%s"),
		Index, *Item->GetItemManifest().GetItemType().ToString(), bRotated ? TEXT("Y") : TEXT("N"));
#endif

	const FInv_GridFragment* GridFragment = GetFragment<FInv_GridFragment>(Item, FragmentTags::GridFragment);
	const FInv_ImageFragment* ImageFragment = GetFragment<FInv_ImageFragment>(Item, FragmentTags::IconFragment);
	if (!GridFragment || !ImageFragment) return;

	UInv_SlottedItem* SlottedItem = CreateSlottedItem(Item, bStackable, StackAmount, GridFragment, ImageFragment, Index, EntryIndex, bRotated);

	AddSlottedItemToCanvas(Index, GridFragment, SlottedItem, bRotated);

	SlottedItems.Add(Index, SlottedItem);

	UE_LOG(LogTemp, Log, TEXT("[AddItemAtIndex] ✓ SlottedItems[%d] 등록 완료: Item=%s, EntryIndex=%d, Rotated=%s"),
		Index,
		Item ? *Item->GetItemManifest().GetItemType().ToString() : TEXT("nullptr"),
		SlottedItem->GetEntryIndex(),
		bRotated ? TEXT("Y") : TEXT("N"));
}

UInv_SlottedItem* UInv_InventoryGrid::CreateSlottedItem(UInv_InventoryItem* Item, const bool bStackable, const int32 StackAmount, const FInv_GridFragment* GridFragment, const FInv_ImageFragment* ImageFragment, const int32 Index, const int32 EntryIndex, bool bRotated)
{
	// ⭐ [최적화 #6] 풀에서 위젯 획득 (없으면 새로 생성)
	UInv_SlottedItem* SlottedItem = AcquireSlottedItem();
	SlottedItem->SetInventoryItem(Item);
	SetSlottedItemImage(SlottedItem, GridFragment, ImageFragment, bRotated);
	SlottedItem->SetGridIndex(Index);
	SlottedItem->SetEntryIndex(EntryIndex); // ⭐ EntryIndex 설정!
	SlottedItem->SetIsStackable(bStackable);
	SlottedItem->SetRotated(bRotated); // R키 회전 상태 설정
	SlottedItem->SetGridDimensions(GetEffectiveDimensions(GridFragment, bRotated)); // 회전된 크기 설정
	const int32 StackUpdateAmount = bStackable ? StackAmount : 0;
	SlottedItem->UpdateStackCount(StackUpdateAmount);
	// ⭐ [최적화 #6] 풀에서 재사용된 위젯은 이미 바인딩되어 있을 수 있으므로 중복 방지
	if (!SlottedItem->OnSlottedItemClicked.IsAlreadyBound(this, &ThisClass::OnSlottedItemClicked))
	{
		SlottedItem->OnSlottedItemClicked.AddDynamic(this, &ThisClass::OnSlottedItemClicked);
	}

	return SlottedItem;
}

void UInv_InventoryGrid::AddSlottedItemToCanvas(const int32 Index, const FInv_GridFragment* GridFragment, UInv_SlottedItem* SlottedItem, bool bRotated) const
{
	CanvasPanel->AddChild(SlottedItem);
	UCanvasPanelSlot* CanvasSlot = UWidgetLayoutLibrary::SlotAsCanvasSlot(SlottedItem);

	// R키 회전: 캔버스 슬롯 크기는 회전된 dimensions 사용 (레이아웃 반영)
	const FIntPoint EffDim = GetEffectiveDimensions(GridFragment, bRotated);
	const float IconTileWidth = TileSize - GridFragment->GetGridPadding() * 2;
	const FVector2D EffDrawSize = FVector2D(EffDim) * IconTileWidth;
	CanvasSlot->SetSize(EffDrawSize);

	const FVector2D DrawPos = UInv_WidgetUtils::GetPositionFromIndex(Index, Columns) * TileSize;
	const FVector2D DrawPosWithPadding = DrawPos + FVector2D(GridFragment->GetGridPadding());
	CanvasSlot->SetPosition(DrawPosWithPadding);

	// R키 회전: 이미지를 시각적으로 90도 회전 (RenderTransform)
	if (bRotated)
	{
		UImage* ImageIcon = SlottedItem->GetImageIcon();
		if (IsValid(ImageIcon))
		{
			ImageIcon->SetRenderTransformPivot(FVector2D(0.5f, 0.5f));
			ImageIcon->SetRenderTransformAngle(90.f);
		}
	}
	else
	{
		UImage* ImageIcon = SlottedItem->GetImageIcon();
		if (IsValid(ImageIcon))
		{
			ImageIcon->SetRenderTransformAngle(0.f);
		}
	}
}

void UInv_InventoryGrid::UpdateGridSlots(UInv_InventoryItem* NewItem, const int32 Index, bool bStackableItem, const int32 StackAmount, bool bRotated)
{
	// U4: check() → 안전한 early return (데디서버에서 check 실패 시 전체 크래시)
	if (!GridSlots.IsValidIndex(Index))
	{
		UE_LOG(LogTemp, Error, TEXT("[UpdateGridSlots] 유효하지 않은 Index: %d"), Index);
		return;
	}

	if (bStackableItem)
	{
		GridSlots[Index]->SetStackCount(StackAmount);
	}

	const FInv_GridFragment* GridFragment = GetFragment<FInv_GridFragment>(NewItem, FragmentTags::GridFragment);
	// R키 회전: 회전된 dimensions로 슬롯 점유
	const FIntPoint Dimensions = GridFragment ? GetEffectiveDimensions(GridFragment, bRotated) : FIntPoint(1, 1);

	FIntPoint GridPos = UInv_WidgetUtils::GetPositionFromIndex(Index, Columns);
	NewItem->SetGridPosition(GridPos);

	// 서버에 Grid 위치 + 회전 상태 동기화
	if (InventoryComponent.IsValid() && !bSuppressServerSync)
	{
		uint8 GridCategoryValue = static_cast<uint8>(ItemCategory);
		InventoryComponent->Server_UpdateItemGridPosition(NewItem, Index, GridCategoryValue, bRotated);
	}

#if INV_DEBUG_WIDGET
	UE_LOG(LogTemp, Log, TEXT("[UpdateGridSlots] 아이템 %s를 Grid[%d,%d]에 배치 (Index=%d, Category=%d, Rotated=%s)"),
		*NewItem->GetItemManifest().GetItemType().ToString(), GridPos.X, GridPos.Y, Index, static_cast<int32>(ItemCategory),
		bRotated ? TEXT("Y") : TEXT("N"));
#endif

	UInv_InventoryStatics::ForEach2D(GridSlots, Index, Dimensions, Columns, [&](UInv_GridSlot* GridSlot)
	{
		GridSlot->SetInventoryItem(NewItem);
		GridSlot->SetUpperLeftIndex(Index);
		GridSlot->SetOccupiedTexture();
		GridSlot->SetAvailable(false);
	});
	SetOccupiedBits(Index, Dimensions, true);
}

bool UInv_InventoryGrid::IsIndexClaimed(const TSet<int32>& CheckedIndices, const int32 Index) const
{
	return CheckedIndices.Contains(Index);
}


//2차원 격자 생성 아이템칸 만든다는 뜻.
void UInv_InventoryGrid::ConstructGrid()
{
	// U32: 중복 호출 방지
	if (GridSlots.Num() > 0) return;

	GridSlots.Reserve(Rows * Columns); // Tarray 지정 하는 건 알겠는데 GridSlot이거 어디서?
	OccupiedMask.Init(false, Rows * Columns); // ⭐ [최적화 #5] 비트마스크 초기화 (모두 비점유)

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
	if (!IsValid(HoverItem))
	{
		// [Phase 9] 크로스 컨테이너 Grid 드래그 앤 드롭
		if (TryTransferFromLinkedContainerGrid(GridIndex))
		{
			return;
		}

		// [Fix20] 패널 간 드래그 앤 드롭: 상대 Grid에 HoverItem이 있으면 전송
		TryTransferFromTargetGrid();
		return;
	} // 호버 아이템이 유효하다면 리턴
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

#if INV_DEBUG_WIDGET
	// [Swap버그추적] PutDown 진입
	UE_LOG(LogTemp, Error, TEXT("===== [PutDownOnIndex] 진입 ====="));
	UE_LOG(LogTemp, Error, TEXT("  ItemToPutDown: %s (포인터=%p), Index=%d, EntryIndex=%d, StackCount=%d"),
		IsValid(ItemToPutDown) ? *ItemToPutDown->GetItemManifest().GetItemType().ToString() : TEXT("NULL"),
		ItemToPutDown, Index, EntryIndex, StackCount);
#endif

    // Phase 8.1: Split 아이템이면 UI 배치 건너뛰기, 서버 RPC로 처리
    if (HoverItem->IsSplitItem())
    {
        UInv_InventoryItem* OriginalItem = HoverItem->GetOriginalSplitItem();
        if (IsValid(OriginalItem) && InventoryComponent.IsValid())
        {
            int32 OriginalNewStackCount = OriginalItem->GetTotalStackCount() - StackCount;
#if INV_DEBUG_WIDGET
            UE_LOG(LogTemp, Warning, TEXT("[Phase 8.1] Split PutDown - UI 배치 스킵, 서버 RPC만 호출"));
            UE_LOG(LogTemp, Warning, TEXT("  원본 TotalStackCount: %d, 새 개수: %d, Split 개수: %d, 목표 Index: %d"),
                OriginalItem->GetTotalStackCount(), OriginalNewStackCount, StackCount, Index);
#endif
            // ⭐ Index(마우스 위치)를 서버에 전달하여 해당 위치에 배치되도록 함
            InventoryComponent.Get()->Server_SplitItemEntry(OriginalItem, OriginalNewStackCount, StackCount, Index);
        }
        ClearHoverItem();
        return;
    }

    const bool bIsRotated = HoverItem->IsRotated();
    AddItemAtIndex(ItemToPutDown, Index, bIsStackable, StackCount, EntryIndex, bIsRotated);
    UpdateGridSlots(ItemToPutDown, Index, bIsStackable, StackCount, bIsRotated);
#if INV_DEBUG_WIDGET
    UE_LOG(LogTemp, Verbose, TEXT("PutDown: Index=%d, StackCount=%d, Rotated=%s"), Index, StackCount, bIsRotated ? TEXT("Y") : TEXT("N"));
#endif
    ClearHoverItem();
}

void UInv_InventoryGrid::ClearHoverItem() // 호버(잡는모션) 아이템 초기화
{
	bShouldTickForHover = false; // [최적화] Tick 비활성화
	if (!IsValid(HoverItem)) return;

	HoverItem->SetInventoryItem(nullptr); // 호버 아이템의 인벤토리 아이템 초기화
	HoverItem->SetIsStackable(false); // 호버 아이템의 스택 가능 여부 초기화
	HoverItem->SetPreviousGridIndex(INDEX_NONE); // 이전 그리드 인덱스 초기화
	HoverItem->UpdateStackCount(0); // 스택 수 초기화
	
	// ⭐ Phase 8: Split 플래그 초기화
	HoverItem->SetIsSplitItem(false);
	HoverItem->SetOriginalSplitItem(nullptr);
	HoverItem->SetRotated(false); // R키 회전 상태 초기화
	// 이미지 RenderTransform 리셋
	UImage* HoverImage = HoverItem->GetImageIcon();
	if (IsValid(HoverImage))
	{
		HoverImage->SetRenderTransformAngle(0.f);
	}
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

#if INV_DEBUG_WIDGET
	// [Swap버그추적] Swap 진입 상태 덤프
	UE_LOG(LogTemp, Error, TEXT("===== [SwapWithHoverItem] 진입 ====="));
	UE_LOG(LogTemp, Error, TEXT("  HoverItem->GetInventoryItem(): %s (포인터=%p)"),
		IsValid(HoverItem->GetInventoryItem()) ? *HoverItem->GetInventoryItem()->GetItemManifest().GetItemType().ToString() : TEXT("NULL"),
		HoverItem->GetInventoryItem());
	UE_LOG(LogTemp, Error, TEXT("  HoverItem->GetStackCount(): %d, EntryIndex: %d"),
		HoverItem->GetStackCount(), HoverItem->GetEntryIndex());
	UE_LOG(LogTemp, Error, TEXT("  ClickedInventoryItem: %s (포인터=%p)"),
		IsValid(ClickedInventoryItem) ? *ClickedInventoryItem->GetItemManifest().GetItemType().ToString() : TEXT("NULL"),
		ClickedInventoryItem);
	UE_LOG(LogTemp, Error, TEXT("  GridIndex: %d, ItemDropIndex: %d, PreviousGridIndex: %d"),
		GridIndex, ItemDropIndex, HoverItem->GetPreviousGridIndex());
#endif

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

#if INV_DEBUG_WIDGET
	// [Swap버그추적] Swap 완료 상태 덤프
	UE_LOG(LogTemp, Error, TEXT("===== [SwapWithHoverItem] 완료 ====="));
	UE_LOG(LogTemp, Error, TEXT("  새 HoverItem: %s (포인터=%p)"),
		IsValid(HoverItem) && IsValid(HoverItem->GetInventoryItem())
			? *HoverItem->GetInventoryItem()->GetItemManifest().GetItemType().ToString() : TEXT("NULL"),
		IsValid(HoverItem) ? HoverItem->GetInventoryItem() : nullptr);
	UE_LOG(LogTemp, Error, TEXT("  Grid에 배치된 아이템: %s, Index=%d"),
		IsValid(TempInventoryItem) ? *TempInventoryItem->GetItemManifest().GetItemType().ToString() : TEXT("NULL"),
		ItemDropIndex);
	UE_LOG(LogTemp, Error, TEXT("  SlottedItems 총 개수: %d"), SlottedItems.Num());
#endif
}

bool UInv_InventoryGrid::ShouldSwapStackCounts(const int32 RoomInClickedSlot, const int32 HoveredStackCount, const int32 MaxStackSize) const
{
	return RoomInClickedSlot == 0 && HoveredStackCount < MaxStackSize; // 스택 개수가 최대 스택 크기보다 작으면?
}

void UInv_InventoryGrid::SwapStackCounts(const int32 ClickedStackCount, const int32 HoveredStackCount, const int32 Index)
{
	UInv_GridSlot* GridSlot = GridSlots[Index]; // 그리드 슬롯 가져오기
	GridSlot->SetStackCount(HoveredStackCount);
	
	UInv_SlottedItem* ClickedSlottedItem = SlottedItems.FindRef(Index); // 클릭된 슬로티드 아이템 가져오기
	if (ClickedSlottedItem) ClickedSlottedItem->UpdateStackCount(HoveredStackCount); // U26: null 안전

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
	if (UInv_SlottedItem* SI = SlottedItems.FindRef(Index)) SI->UpdateStackCount(NewClickedStackCount); // U26: null 안전
	
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
#if INV_DEBUG_WIDGET
				UE_LOG(LogTemp, Warning, TEXT("ConsumeHoverStacks (Authority): 스택 %d로 설정"), NewClickedStackCount);
#endif
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
	
	UInv_SlottedItem* ClickedSlottedItem = SlottedItems.FindRef(Index); // 클릭된 슬로티드 아이템 가져오기
	if (ClickedSlottedItem) ClickedSlottedItem->UpdateStackCount(NewStackCount); // U26: null 안전

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
#if INV_DEBUG_WIDGET
				UE_LOG(LogTemp, Warning, TEXT("FillInStack (Authority): 스택 %d로 설정"), NewStackCount);
#endif
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
	// U25: 범위 체크
	if (!GridSlots.IsValidIndex(GridIndex)) return;

	UInv_GridSlot* GridSlot = GridSlots[GridIndex]; // 그리드 슬롯 가져오기
	if (GridSlot->IsAvailable()) // 그리드 슬롯이 사용 가능하다면
	{
		GridSlot->SetOccupiedTexture(); // 점유된 텍스처로 설정
	}
}

void UInv_InventoryGrid::OnGridSlotUnhovered(int32 GridIndex, const FPointerEvent& MouseEvent)
{
	if (IsValid(HoverItem)) return; // 호버 아이템이 유효하다면 리턴
	// U25: 범위 체크
	if (!GridSlots.IsValidIndex(GridIndex)) return;

	UInv_GridSlot* GridSlot = GridSlots[GridIndex]; // 그리드 슬롯 가져오기
	if (GridSlot->IsAvailable()) // 그리드 슬롯이 사용 가능하다면
	{
		GridSlot->SetUnoccupiedTexture(); // 비점유된 텍스처로 설정
	}
}

void UInv_InventoryGrid::OnPopUpMenuSplit(int32 SplitAmount, int32 Index) // 아이템 분할 함수
{
	// U6: 범위 체크
	if (!GridSlots.IsValidIndex(Index)) return;
	// 오른쪽 마우스 우클릭 창 불러오는 곳
	UInv_InventoryItem* RightClickedItem = GridSlots[Index]->GetInventoryItem().Get(); // 오른쪽 클릭한 아이템 가져오기
	if (!IsValid(RightClickedItem)) return; // 유효한 아이템인지 확인
	if (!RightClickedItem -> IsStackable()) return; // 스택 가능한 아이템인지 확인

	const int32 UpperLeftIndex = GridSlots[Index]->GetUpperLeftIndex(); // 그리드 슬롯의 왼쪽 위 인덱스 가져오기
	// U6: UpperLeftIndex 범위 체크 (INDEX_NONE(-1) 방어)
	if (!GridSlots.IsValidIndex(UpperLeftIndex)) return;
	UInv_GridSlot* UpperLeftGridSlot = GridSlots[UpperLeftIndex]; // 왼쪽 위 그리드 슬롯 가져오기
	const int32 OriginalStackCount = UpperLeftGridSlot->GetStackCount(); // 원본 스택 수 가져오기
	const int32 NewStackCount = OriginalStackCount - SplitAmount; // 새로운 스택 수 계산 <- 분할된 양을 빼주는 것
	
#if INV_DEBUG_WIDGET
	UE_LOG(LogTemp, Warning, TEXT("🔀 Split 시작: 원본 %d개 → 원본 슬롯 %d개 + 새 Entry %d개"),
		OriginalStackCount, NewStackCount, SplitAmount);
#endif

	// 1단계: UI 업데이트 (빠른 반응성)
	UpperLeftGridSlot->SetStackCount(NewStackCount); // 그리드 슬롯 스택 수 업데이트
	if (UInv_SlottedItem* SI = SlottedItems.FindRef(UpperLeftIndex)) SI->UpdateStackCount(NewStackCount); // U26: null 안전

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

#if INV_DEBUG_WIDGET
	UE_LOG(LogTemp, Warning, TEXT("✅ Split 완료: 서버 TotalStackCount는 %d로 유지됨 (UI만 %d + %d로 나눠짐)"),
		OriginalStackCount, NewStackCount, SplitAmount);
#endif
}

void UInv_InventoryGrid::OnPopUpMenuDrop(int32 Index) // 아이템 버리기 함수
{
	if (!GridSlots.IsValidIndex(Index)) return; // W1: 범위 검사
	//어느 서버에서도 통신을 할 수 있게 만드는 부분 (리슨서버, 호스트 서버, 데디서버 등)
	UInv_InventoryItem* RightClickedItem = GridSlots[Index]->GetInventoryItem().Get();
	if (!IsValid(RightClickedItem)) return; // 유효한 아이템인지 확인
	
	PickUp(RightClickedItem, Index); // 아이템 집기
	DropItem(); // 아이템 버리기
}

// 아이템 소비 상호작용 부분
void UInv_InventoryGrid::OnPopUpMenuConsume(int32 Index)
{
	if (!GridSlots.IsValidIndex(Index)) return; // W1: 범위 검사
	UInv_InventoryItem* RightClickedItem = GridSlots[Index]->GetInventoryItem().Get(); // 오른쪽 클릭한 아이템 가져오기
	if (!IsValid(RightClickedItem)) return; // 유효한 아이템인지 확인

	const int32 UpperLeftIndex = GridSlots[Index]->GetUpperLeftIndex(); // 그리드 슬롯의 왼쪽 위 인덱스 가져오기
	if (!GridSlots.IsValidIndex(UpperLeftIndex)) return; // C3: INDEX_NONE(-1) 방어
	UInv_GridSlot* UpperLeftGridSlot = GridSlots[UpperLeftIndex]; // 왼쪽 위 그리드 슬롯 가져오기
	const int32 StackCount = UpperLeftGridSlot -> GetStackCount(); // 스택 수 가져오기
	const int32 NewStackCount = StackCount - 1; // 새로운 스택 수 계산 <- 1개 소비하는 것
	
	UpperLeftGridSlot->SetStackCount(NewStackCount); // 그리드 슬롯 스택 수 업데이트
	if (UInv_SlottedItem* SI = SlottedItems.FindRef(UpperLeftIndex)) SI->UpdateStackCount(NewStackCount); // U26: null 안전

	// 서버에서 내가 소모되는 것을 서버에게 알리는 부분.
	if (!InventoryComponent.IsValid()) return; // C2: TWeakObjectPtr 무효 시 크래시 방지
	InventoryComponent->Server_ConsumeItem(RightClickedItem);
	
	if (NewStackCount <= 0)
	{
		RemoveItemFromGrid(RightClickedItem, Index); // 그리드에서 아이템 제거
	}
}

// 아이템을 들고 있을 때 다른 UI를 건드리지 못하게 하는 것.
void UInv_InventoryGrid::OnInventoryMenuToggled(bool bOpen)
{
#if INV_DEBUG_WIDGET
	// [Swap버그추적] 인벤토리 토글
	UE_LOG(LogTemp, Error, TEXT("===== [OnInventoryMenuToggled] bOpen=%s ====="),
		bOpen ? TEXT("TRUE") : TEXT("FALSE"));
	if (!bOpen)
	{
		UE_LOG(LogTemp, Error, TEXT("  닫기 시점: HoverItem 유효=%s, HoverItem 아이템=%s"),
			IsValid(HoverItem) ? TEXT("Y") : TEXT("N"),
			IsValid(HoverItem) && IsValid(HoverItem->GetInventoryItem())
				? *HoverItem->GetInventoryItem()->GetItemManifest().GetItemType().ToString() : TEXT("NULL"));
	}
#endif

	if (!bOpen)
	{
		PutHoverItemBack();
		CloseAttachmentPanel(); // 인벤토리 닫을 때 부착물 패널도 닫기
	}
}

// ════════════════════════════════════════════════════════════════
// 📌 [부착물 시스템 Phase 3] 부착물 관리 팝업 버튼 클릭 핸들러
// ════════════════════════════════════════════════════════════════
// 호출 경로: ItemPopUp의 Button_Attachment 클릭 → OnAttachment 델리게이트 → 이 함수
// 처리 흐름:
//   1. GridSlots[Index]에서 우클릭된 아이템 가져오기
//   2. SlottedItem에서 EntryIndex 가져오기 (없으면 FindEntryIndexForItem 검색)
//   3. OpenAttachmentPanel 호출
// ════════════════════════════════════════════════════════════════
void UInv_InventoryGrid::OnPopUpMenuAttachment(int32 Index)
{
	if (!GridSlots.IsValidIndex(Index)) return; // W1: 범위 검사
	UInv_InventoryItem* RightClickedItem = GridSlots[Index]->GetInventoryItem().Get();
	if (!IsValid(RightClickedItem)) return;
	if (!RightClickedItem->HasAttachmentSlots()) return;

	// SlottedItem에서 EntryIndex 가져오기
	const int32 UpperLeftIndex = GridSlots[Index]->GetUpperLeftIndex();
	int32 EntryIndex = INDEX_NONE;

	if (SlottedItems.Contains(UpperLeftIndex))
	{
		EntryIndex = SlottedItems[UpperLeftIndex]->GetEntryIndex();
	}

	// EntryIndex가 유효하지 않으면 InventoryComponent에서 검색
	if (EntryIndex == INDEX_NONE && InventoryComponent.IsValid())
	{
		EntryIndex = InventoryComponent->FindEntryIndexForItem(RightClickedItem);
	}

	if (EntryIndex == INDEX_NONE)
	{
		UE_LOG(LogTemp, Warning, TEXT("[Attachment UI] EntryIndex를 찾을 수 없음!"));
		return;
	}

	OpenAttachmentPanel(RightClickedItem, EntryIndex);
}

// ════════════════════════════════════════════════════════════════
// PopupMenu Transfer 버튼 콜백 — 로비 전송 (RMB 메뉴에서 Transfer 클릭)
// ════════════════════════════════════════════════════════════════
void UInv_InventoryGrid::OnPopUpMenuTransfer(int32 Index)
{
	// UpperLeftIndex로 대형 아이템 처리
	int32 LookupIndex = Index;
	if (GridSlots.IsValidIndex(Index) && GridSlots[Index])
	{
		const int32 UpperLeft = GridSlots[Index]->GetUpperLeftIndex();
		if (UpperLeft >= 0)
		{
			LookupIndex = UpperLeft;
		}
	}

	UInv_SlottedItem* Slotted = SlottedItems.FindRef(LookupIndex);
	if (!Slotted) return;

	int32 RepID = INDEX_NONE;
	if (InventoryComponent.IsValid())
	{
		UInv_InventoryItem* SlottedItem = Slotted->GetInventoryItem();
		if (IsValid(SlottedItem))
		{
			const TArray<FInv_InventoryEntry>& Entries = InventoryComponent->GetInventoryList().Entries;
			for (const FInv_InventoryEntry& Entry : Entries)
			{
				if (Entry.Item == SlottedItem)
				{
					RepID = Entry.ReplicationID;
					break;
				}
			}
		}
	}

	UE_LOG(LogTemp, Log, TEXT("[InventoryGrid] PopupMenu Transfer → RepID=%d, GridIndex=%d (UpperLeft=%d)"), RepID, Index, LookupIndex);
	if (RepID != INDEX_NONE)
	{
		// [Fix19] 대상 Grid 용량 사전 체크 - 공간 없으면 전송 차단
		if (LobbyTargetGrid.IsValid())
		{
			UInv_InventoryItem* ItemToTransfer = Slotted->GetInventoryItem();
			if (IsValid(ItemToTransfer))
			{
				const FInv_ItemManifest& Manifest = ItemToTransfer->GetItemManifest();
				if (!LobbyTargetGrid->HasRoomInActualGrid(Manifest))
				{
					UE_LOG(LogTemp, Warning, TEXT("[InventoryGrid] Fix19 - Transfer blocked (PopupMenu): no room in target Grid (RepID=%d)"), RepID);
					return;
				}
			}
		}
		else
		{
			// [W2] LobbyTargetGrid 미설정 경고
			UE_LOG(LogTemp, Warning, TEXT("[InventoryGrid] W2 - LobbyTargetGrid 미설정 (PopupMenu Transfer). 용량 체크 없이 전송 진행"));
		}

		OnLobbyTransferRequested.Broadcast(RepID);
	}
	else
	{
		UE_LOG(LogTemp, Error, TEXT("[InventoryGrid] PopupMenu Transfer 실패 → ReplicationID 미발견"));
	}
}

// ════════════════════════════════════════════════════════════════
// 📌 OpenAttachmentPanel — 부착물 관리 패널 열기
// ════════════════════════════════════════════════════════════════
// 호출 경로: OnPopUpMenuAttachment → 이 함수
// 처리 흐름:
//   1. AttachmentPanelClass 유효성 체크
//   2. 기존 패널 열려있으면 닫기
//   3. 패널 위젯 없으면 생성 + OwningCanvasPanel에 추가 + 위치 설정
//   4. SetInventoryComponent / SetOwningGrid 참조 설정
//   5. OpenForWeapon 호출
// ════════════════════════════════════════════════════════════════
void UInv_InventoryGrid::OpenAttachmentPanel(UInv_InventoryItem* WeaponItem, int32 WeaponEntryIndex)
{
	if (!IsValid(WeaponItem) || !InventoryComponent.IsValid()) return;
	if (!AttachmentPanelClass)
	{
		UE_LOG(LogTemp, Warning, TEXT("[Attachment UI] AttachmentPanelClass가 설정되지 않음!"));
		return;
	}

	// 기존 패널이 열려있으면 닫기
	if (IsValid(AttachmentPanel) && AttachmentPanel->IsOpen())
	{
		AttachmentPanel->ClosePanel();
	}

	// 패널 위젯 생성 (처음 한 번만)
	if (!IsValid(AttachmentPanel))
	{
		AttachmentPanel = CreateWidget<UInv_AttachmentPanel>(this, AttachmentPanelClass);
		if (!IsValid(AttachmentPanel))
		{
			UE_LOG(LogTemp, Error, TEXT("[Attachment UI] AttachmentPanel 생성 실패!"));
			return;
		}

		// [Fix25] 항상 Viewport에 직접 추가 → 화면 중앙 배치
		// CanvasPanel 자식 방식은 부모 위젯의 클리핑 경계를 벗어나면
		// 히트 테스트가 차단되어 슬롯 클릭이 먹히지 않는 문제 발생
		AttachmentPanel->AddToViewport(100);

		// WBP 루트 CanvasPanel의 첫 자식(Overlay)을 화면 중앙 배치 (고정 크기)
		UCanvasPanel* RootCanvas = Cast<UCanvasPanel>(AttachmentPanel->GetRootWidget());
		if (RootCanvas && RootCanvas->GetChildrenCount() > 0)
		{
			UWidget* OverlayChild = RootCanvas->GetChildAt(0);
			UCanvasPanelSlot* OverlaySlot = UWidgetLayoutLibrary::SlotAsCanvasSlot(OverlayChild);
			if (OverlaySlot)
			{
				// 중앙 앵커 + 고정 크기 (AutoSize=false)
				// AutoSize=true 시 Border_Background 텍스처 원본 크기(1443x1958)로 확장되는 버그 수정
				FAnchors CenterAnchor(0.5f, 0.5f, 0.5f, 0.5f);
				OverlaySlot->SetAnchors(CenterAnchor);
				OverlaySlot->SetAlignment(FVector2D(0.5f, 0.5f));
				OverlaySlot->SetAutoSize(false);
				// 중앙 앵커 + AutoSize=false: Offsets = (PosX, PosY, Width, Height)
				OverlaySlot->SetOffsets(FMargin(0.f, 0.f, 500.f, 600.f));
			}
		}

		UE_LOG(LogTemp, Log, TEXT("[Attachment UI] 뷰포트 중앙 배치 (Fix25: 인게임+로비 공통)"));

		// 패널 닫힘 콜백 바인딩
		AttachmentPanel->OnPanelClosed.AddDynamic(this, &ThisClass::OnAttachmentPanelClosed);
	}

	// 참조 설정 (패널이 직접 Server RPC 호출)
	AttachmentPanel->SetInventoryComponent(InventoryComponent.Get());
	AttachmentPanel->SetOwningGrid(this);

	// 패널 열기
	AttachmentPanel->OpenForWeapon(WeaponItem, WeaponEntryIndex);

#if INV_DEBUG_WIDGET
	UE_LOG(LogTemp, Log, TEXT("[Attachment UI] 패널 열림: WeaponEntry=%d (Fix25: 항상 Viewport 배치)"),
		WeaponEntryIndex);
#endif
}

// ════════════════════════════════════════════════════════════════
// 📌 CloseAttachmentPanel — 부착물 패널 닫기
// ════════════════════════════════════════════════════════════════
void UInv_InventoryGrid::CloseAttachmentPanel()
{
	if (IsValid(AttachmentPanel) && AttachmentPanel->IsOpen())
	{
		AttachmentPanel->ClosePanel();
	}
}

// ════════════════════════════════════════════════════════════════
// 📌 IsAttachmentPanelOpen — 부착물 패널이 열려있는지 확인
// ════════════════════════════════════════════════════════════════
bool UInv_InventoryGrid::IsAttachmentPanelOpen() const
{
	return IsValid(AttachmentPanel) && AttachmentPanel->IsOpen();
}

// ════════════════════════════════════════════════════════════════
// 📌 OnAttachmentPanelClosed — 패널 닫힘 콜백
// ════════════════════════════════════════════════════════════════
void UInv_InventoryGrid::OnAttachmentPanelClosed()
{
#if INV_DEBUG_WIDGET
	UE_LOG(LogTemp, Log, TEXT("[Attachment UI] 패널 닫힘 콜백 (InventoryGrid)"));
#endif
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

#if INV_DEBUG_WIDGET
			UE_LOG(LogTemp, Verbose, TEXT("GridSlot[%d]: %s x %d (누적: %d)"),
				UpperLeftIndex, *MaterialTag.ToString(), StackCount, TotalCount);
#endif
		}
	}

#if INV_DEBUG_WIDGET
	UE_LOG(LogTemp, Log, TEXT("GetTotalMaterialCountFromSlots(%s) = %d"), *MaterialTag.ToString(), TotalCount);
#endif
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

#if INV_DEBUG_WIDGET
	UE_LOG(LogTemp, Warning, TEXT("[ACTUAL GRID CHECK] 아이템 크기: %dx%d"), ItemSize.X, ItemSize.Y);
	UE_LOG(LogTemp, Warning, TEXT("[ACTUAL GRID CHECK] Grid 크기: %dx%d"), Columns, Rows);
#endif

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
#if INV_DEBUG_WIDGET
			UE_LOG(LogTemp, Warning, TEXT("[ACTUAL GRID CHECK] ✅ 공간 발견! [%d, %d]부터 %dx%d"),
				StartX, StartY, ItemSize.X, ItemSize.Y);
#endif
			return true; // 공간 발견!
		}
	}

#if INV_DEBUG_WIDGET
	UE_LOG(LogTemp, Warning, TEXT("[ACTUAL GRID CHECK] ❌ 공간 없음!"));
#endif
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
TArray<FInv_SavedItemData> UInv_InventoryGrid::CollectGridState(const TSet<UInv_InventoryItem*>* ItemsToSkip) const
{
	TArray<FInv_SavedItemData> Result;

	// [진단1] Skip 동작 추적용 카운터
	int32 DiagCollectedCount = 0;
	int32 DiagSkippedCount = 0;

	// 카테고리 이름 변환
	const TCHAR* GridCategoryNames[] = { TEXT("장비"), TEXT("소모품"), TEXT("재료") };
	const int32 CategoryIndex = static_cast<int32>(ItemCategory);
	const TCHAR* GridCategoryStr = (CategoryIndex >= 0 && CategoryIndex < 3) ? GridCategoryNames[CategoryIndex] : TEXT("???");

	UE_LOG(LogTemp, Error, TEXT("[CollectGridState진단] 시작 — Grid %d (%s), ItemsToSkip=%s, Skip수=%d"),
		CategoryIndex, GridCategoryStr,
		ItemsToSkip ? TEXT("있음") : TEXT("nullptr"),
		ItemsToSkip ? ItemsToSkip->Num() : 0);

#if INV_DEBUG_WIDGET
	UE_LOG(LogTemp, Warning, TEXT(""));
	UE_LOG(LogTemp, Warning, TEXT("    ┌─── [CollectGridState] Grid %d (%s) ───┐"), CategoryIndex, GridCategoryStr);
	UE_LOG(LogTemp, Warning, TEXT("    │ Grid 크기: %d x %d (총 %d 슬롯)"), Columns, Rows, Columns * Rows);
	UE_LOG(LogTemp, Warning, TEXT("    │ SlottedItems 개수: %d"), SlottedItems.Num());
	if (ItemsToSkip)
	{
		UE_LOG(LogTemp, Warning, TEXT("    │ ItemsToSkip 개수: %d"), ItemsToSkip->Num());
	}
#endif

	if (SlottedItems.Num() == 0)
	{
#if INV_DEBUG_WIDGET
		UE_LOG(LogTemp, Warning, TEXT("    │ → 수집할 아이템 없음 (빈 Grid)"));
		UE_LOG(LogTemp, Warning, TEXT("    └────────────────────────────────────────┘"));
#endif
		return Result;
	}

#if INV_DEBUG_WIDGET
	UE_LOG(LogTemp, Warning, TEXT("    │"));
	UE_LOG(LogTemp, Warning, TEXT("    │ ▶ SlottedItems 순회 시작:"));
#endif

	// SlottedItems 순회 (각 GridIndex에 있는 아이템)
	int32 ItemIndex = 0;
	for (const auto& Pair : SlottedItems)
	{
		const int32 GridIndex = Pair.Key;
		const UInv_SlottedItem* SlottedItem = Pair.Value;

#if INV_DEBUG_WIDGET
		UE_LOG(LogTemp, Warning, TEXT("    │"));
		UE_LOG(LogTemp, Warning, TEXT("    │   [%d] GridIndex=%d"), ItemIndex, GridIndex);
#endif

		// SlottedItem 유효성 검사
		if (!IsValid(SlottedItem))
		{
#if INV_DEBUG_WIDGET
			UE_LOG(LogTemp, Warning, TEXT("    │       ⚠️ SlottedItem이 nullptr! 건너뜀"));
#endif
			continue;
		}

		// InventoryItem 가져오기
		UInv_InventoryItem* Item = SlottedItem->GetInventoryItem();
		if (!IsValid(Item))
		{
#if INV_DEBUG_WIDGET
			UE_LOG(LogTemp, Warning, TEXT("    │       ⚠️ InventoryItem이 nullptr! 건너뜀"));
#endif
			continue;
		}

		// [BugFix] 장착 아이템 필터링 — Grid에 남아있는 장착 아이템을 제외하여 이중 수집 방지
		if (ItemsToSkip && ItemsToSkip->Contains(Item))
		{
			UE_LOG(LogTemp, Error, TEXT("[CollectGridState진단] SKIP: %s (포인터=%p)"),
				*Item->GetItemManifest().GetItemType().ToString(), Item);
			DiagSkippedCount++;
#if INV_DEBUG_WIDGET
			UE_LOG(LogTemp, Warning, TEXT("    │       🚫 장착 아이템이므로 건너뜀 (ItemsToSkip): %s"), *Item->GetItemManifest().GetItemType().ToString());
#endif
			continue;
		}

		// GridSlot 유효성 검사
		if (!GridSlots.IsValidIndex(GridIndex))
		{
#if INV_DEBUG_WIDGET
			UE_LOG(LogTemp, Warning, TEXT("    │       ⚠️ GridIndex(%d)가 범위 밖! (GridSlots.Num=%d) 건너뜀"),
				GridIndex, GridSlots.Num());
#endif
			continue;
		}

		// ============================================
		// ⭐ 핵심: GridSlot에서 StackCount 읽기
		// ============================================
		// 서버의 TotalStackCount가 아니라 UI 슬롯의 개별 스택 수량!
		// Split 시: 서버(20개) → UI슬롯1(9개) + UI슬롯2(11개)
		int32 StackCount = GridSlots[GridIndex]->GetStackCount();
		const int32 ServerStackCount = Item->GetTotalStackCount();

		// ⭐ [Fix11] 비스택(장비) 아이템은 UI StackCount가 0일 수 있음
		// Stackable Fragment가 없는 아이템은 "존재 = 1개"이므로 최소 1로 보정
		if (StackCount <= 0 && !Item->IsStackable())
		{
			StackCount = 1;
			UE_LOG(LogTemp, Log, TEXT("[Fix11] Grid[%d] %s: 비스택 아이템 StackCount 0→1 보정"), GridIndex, *Item->GetItemManifest().GetItemType().ToString());
		}

#if INV_DEBUG_WIDGET
		UE_LOG(LogTemp, Warning, TEXT("    │       ItemType: %s"), *Item->GetItemManifest().GetItemType().ToString());
		UE_LOG(LogTemp, Warning, TEXT("    │       UI StackCount: %d (⭐ 저장할 값)"), StackCount);
		UE_LOG(LogTemp, Warning, TEXT("    │       서버 TotalStackCount: %d (참고용)"), ServerStackCount);

		// Split 감지 로그
		if (StackCount != ServerStackCount && ServerStackCount > 0)
		{
			UE_LOG(LogTemp, Warning, TEXT("    │       🔀 Split 감지! UI(%d) ≠ 서버(%d)"), StackCount, ServerStackCount);
		}
#endif

		// ============================================
		// GridIndex → GridPosition 변환
		// ============================================
		const FIntPoint GridPosition = UInv_WidgetUtils::GetPositionFromIndex(GridIndex, Columns);
#if INV_DEBUG_WIDGET
		UE_LOG(LogTemp, Warning, TEXT("    │       GridIndex(%d) → Position(%d, %d)"),
			GridIndex, GridPosition.X, GridPosition.Y);
#endif

		// ============================================
		// 저장 데이터 생성
		// ============================================
		FInv_SavedItemData SavedData;
		SavedData.ItemType = Item->GetItemManifest().GetItemType();
		SavedData.StackCount = StackCount > 0 ? StackCount : 1;  // Non-stackable은 1
		SavedData.GridPosition = GridPosition;
		SavedData.GridCategory = static_cast<uint8>(ItemCategory);
		SavedData.bRotated = SlottedItem->IsRotated(); // R키 회전 상태 저장

		// ── [Phase 6 Attachment] 부착물 데이터 수집 ──
		// 무기 아이템인 경우 AttachmentHostFragment의 AttachedItems 수집
		if (Item->HasAttachmentSlots())
		{
			const FInv_ItemManifest& ItemManifest = Item->GetItemManifest();
			const FInv_AttachmentHostFragment* HostFrag = ItemManifest.GetFragmentOfType<FInv_AttachmentHostFragment>();
			if (HostFrag)
			{
				for (const FInv_AttachedItemData& Attached : HostFrag->GetAttachedItems())
				{
					FInv_SavedAttachmentData AttSave;
					AttSave.AttachmentItemType = Attached.AttachmentItemType;
					AttSave.SlotIndex = Attached.SlotIndex;

					// AttachableFragment에서 AttachmentType 추출
					const FInv_AttachableFragment* AttachableFrag =
						Attached.ItemManifestCopy.GetFragmentOfType<FInv_AttachableFragment>();
					if (AttachableFrag)
					{
						AttSave.AttachmentType = AttachableFrag->GetAttachmentType();
					}

					SavedData.Attachments.Add(AttSave);
				}
			}
		}

		// ════════════════════════════════════════════════════════════════
		// 📌 [Phase 1 최적화] Fragment 직렬화 — 랜덤 스탯 보존
		// ════════════════════════════════════════════════════════════════
		{
			const FInv_ItemManifest& ItemManifest = Item->GetItemManifest();
			SavedData.SerializedManifest = ItemManifest.SerializeFragments();

#if INV_DEBUG_SAVE
			UE_LOG(LogTemp, Warning,
				TEXT("    │       📦 [Phase 1 최적화] Fragment 직렬화 (클라이언트): %s → %d바이트"),
				*SavedData.ItemType.ToString(), SavedData.SerializedManifest.Num());
#endif

			// 부착물의 Fragment도 각각 직렬화
			const FInv_AttachmentHostFragment* SerializeHostFrag = ItemManifest.GetFragmentOfType<FInv_AttachmentHostFragment>();
			if (SerializeHostFrag)
			{
				for (int32 AttIdx = 0; AttIdx < SavedData.Attachments.Num(); ++AttIdx)
				{
					FInv_SavedAttachmentData& AttSave = SavedData.Attachments[AttIdx];
					const FInv_AttachedItemData* AttachedData = SerializeHostFrag->GetAttachedItemData(AttSave.SlotIndex);
					if (AttachedData)
					{
						AttSave.SerializedManifest = AttachedData->ItemManifestCopy.SerializeFragments();

#if INV_DEBUG_SAVE
						UE_LOG(LogTemp, Warning,
							TEXT("    │         📦 부착물[%d] Fragment 직렬화: %s → %d바이트"),
							AttIdx, *AttSave.AttachmentItemType.ToString(),
							AttSave.SerializedManifest.Num());
#endif
					}
				}
			}
		}

		UE_LOG(LogTemp, Error, TEXT("[CollectGridState진단] 수집: %s (포인터=%p, Pos=(%d,%d))"),
			*Item->GetItemManifest().GetItemType().ToString(), Item,
			GridPosition.X, GridPosition.Y);
		DiagCollectedCount++;

		Result.Add(SavedData);

#if INV_DEBUG_WIDGET
		UE_LOG(LogTemp, Warning, TEXT("    │       ✅ 수집 완료: %s"), *SavedData.ToString());
#endif

		ItemIndex++;
	}

	UE_LOG(LogTemp, Error, TEXT("[CollectGridState진단] 완료 — Grid %d: 총 수집=%d개, Skip=%d개"),
		CategoryIndex, DiagCollectedCount, DiagSkippedCount);

#if INV_DEBUG_WIDGET
	UE_LOG(LogTemp, Warning, TEXT("    │"));
	UE_LOG(LogTemp, Warning, TEXT("    │ 📦 Grid %d 수집 결과: %d개 아이템"), CategoryIndex, Result.Num());
	UE_LOG(LogTemp, Warning, TEXT("    └────────────────────────────────────────┘"));
#endif

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

#if INV_DEBUG_WIDGET
	UE_LOG(LogTemp, Warning, TEXT(""));
	UE_LOG(LogTemp, Warning, TEXT("    ┌─── [RestoreItemPositions] Grid %d (%s) ───┐"), CategoryIndex, GridCategoryStr);
	UE_LOG(LogTemp, Warning, TEXT("    │ 복원할 아이템: %d개"), SavedItems.Num());
#endif

	// ============================================
	// Fix 12: Two-pass clear-and-place 방식
	// ============================================
	// 문제: 기존 순서 기반 1:1 매칭은 SavedItems 순서(Entry 생성 순서)와
	//       SortedKeys 순서(GridIndex 오름차순)가 불일치하면
	//       서로 다른 타입의 아이템이 뒤바뀌어 배치됨.
	//       또한 PostReplicatedAdd의 장착 아이템이 슬롯을 선점하여
	//       MoveItemByCurrentIndex가 충돌로 실패하는 연쇄 반응 발생.
	// 해결: Phase 1에서 Grid를 완전히 비우고,
	//       Phase 2에서 ItemType 매칭으로 올바른 위치에 직접 배치.
	//       충돌이 원천 차단되므로 Fix 9 fallback 최소화.
	// ============================================

	// 1. 이 Grid 카테고리에 해당하는 저장 데이터만 필터링
	TArray<FInv_SavedItemData> FilteredSavedItems;
	for (const FInv_SavedItemData& SavedItem : SavedItems)
	{
		if (SavedItem.GridCategory == static_cast<uint8>(ItemCategory) && !SavedItem.bEquipped)
		{
			FilteredSavedItems.Add(SavedItem);
		}
	}

#if INV_DEBUG_WIDGET
	UE_LOG(LogTemp, Warning, TEXT("    │ 이 Grid 카테고리 아이템: %d개"), FilteredSavedItems.Num());
#endif

	// 2. 현재 Grid의 모든 아이템 정보 수집
	struct FCollectedItemInfo
	{
		UInv_SlottedItem* SlottedItem;
		UInv_InventoryItem* InventoryItem;
		int32 OriginalGridIndex;
		FIntPoint Dimensions;
		float ItemPadding;
		bool bUsed;
	};

	TArray<FCollectedItemInfo> CollectedItems;
	TArray<int32> AllKeys;
	SlottedItems.GetKeys(AllKeys);

	for (int32 Key : AllKeys)
	{
		UInv_SlottedItem* Slotted = SlottedItems.FindRef(Key);
		if (IsValid(Slotted) && Slotted->GetInventoryItem())
		{
			FCollectedItemInfo Info;
			Info.SlottedItem = Slotted;
			Info.InventoryItem = Slotted->GetInventoryItem();
			Info.OriginalGridIndex = Key;
			const FInv_GridFragment* GridFragment = GetFragment<FInv_GridFragment>(Info.InventoryItem, FragmentTags::GridFragment);
			Info.Dimensions = GridFragment ? GridFragment->GetGridSize() : FIntPoint(1, 1);
			Info.ItemPadding = GridFragment ? GridFragment->GetGridPadding() : 0.0f;
			Info.bUsed = false;
			CollectedItems.Add(Info);
		}
	}

#if INV_DEBUG_WIDGET
	UE_LOG(LogTemp, Warning, TEXT("    │ 현재 SlottedItems 개수: %d"), CollectedItems.Num());
#endif

	// ============================================
	// Phase 1: Grid의 모든 슬롯 비우기
	// ============================================
	for (const FCollectedItemInfo& Info : CollectedItems)
	{
		UInv_InventoryStatics::ForEach2D(GridSlots, Info.OriginalGridIndex, Info.Dimensions, Columns, [](UInv_GridSlot* GridSlot)
		{
			if (GridSlot)
			{
				GridSlot->SetInventoryItem(nullptr);
				GridSlot->SetUpperLeftIndex(INDEX_NONE);
				GridSlot->SetStackCount(0);
				GridSlot->SetAvailable(true);
				GridSlot->SetUnoccupiedTexture();
			}
		});
		SetOccupiedBits(Info.OriginalGridIndex, Info.Dimensions, false);
	}
	SlottedItems.Empty();

#if INV_DEBUG_WIDGET
	UE_LOG(LogTemp, Warning, TEXT("    │ Phase 1: Grid 비우기 완료 (%d개 아이템 수집)"), CollectedItems.Num());
#endif

	// ============================================
	// Phase 2: ItemType 매칭으로 저장 위치에 직접 배치
	// ============================================
	// 배치 람다 — 지정된 GridIndex에 아이템 배치
	auto PlaceItemAtGridIndex = [this](FCollectedItemInfo& Info, int32 TargetIndex, const FIntPoint& TargetPosition, int32 StackCount)
	{
		SlottedItems.Add(TargetIndex, Info.SlottedItem);

		bool bIsFirstSlot = true;
		UInv_InventoryStatics::ForEach2D(GridSlots, TargetIndex, Info.Dimensions, Columns, [&](UInv_GridSlot* GridSlot)
		{
			if (GridSlot)
			{
				GridSlot->SetInventoryItem(Info.InventoryItem);
				GridSlot->SetUpperLeftIndex(TargetIndex);
				GridSlot->SetOccupiedTexture();
				GridSlot->SetAvailable(false);
				if (bIsFirstSlot)
				{
					GridSlot->SetStackCount(StackCount);
					bIsFirstSlot = false;
				}
			}
		});
		SetOccupiedBits(TargetIndex, Info.Dimensions, true);

		Info.SlottedItem->SetGridIndex(TargetIndex);
		Info.SlottedItem->UpdateStackCount(StackCount);

		UCanvasPanelSlot* CanvasSlot = Cast<UCanvasPanelSlot>(Info.SlottedItem->Slot);
		if (CanvasSlot)
		{
			const FVector2D DrawPos = FVector2D(TargetPosition.X * TileSize, TargetPosition.Y * TileSize);
			CanvasSlot->SetPosition(DrawPos + FVector2D(Info.ItemPadding));
		}

		Info.bUsed = true;
	};

	int32 RestoredCount = 0;
	for (int32 i = 0; i < FilteredSavedItems.Num(); i++)
	{
		const FInv_SavedItemData& SavedItem = FilteredSavedItems[i];
		const int32 TargetIndex = UInv_WidgetUtils::GetIndexFromPosition(SavedItem.GridPosition, Columns);

#if INV_DEBUG_WIDGET
		UE_LOG(LogTemp, Warning, TEXT("    │"));
		UE_LOG(LogTemp, Warning, TEXT("    │ [%d] %s x%d → Pos(%d,%d) (TargetIndex=%d)"),
			i, *SavedItem.ItemType.ToString(), SavedItem.StackCount,
			SavedItem.GridPosition.X, SavedItem.GridPosition.Y, TargetIndex);
#endif

		// ItemType으로 매칭되는 미사용 아이템 검색
		FCollectedItemInfo* MatchedItem = nullptr;
		for (FCollectedItemInfo& Info : CollectedItems)
		{
			if (!Info.bUsed && Info.InventoryItem->GetItemManifest().GetItemType() == SavedItem.ItemType)
			{
				MatchedItem = &Info;
				break;
			}
		}

		if (!MatchedItem)
		{
#if INV_DEBUG_WIDGET
			UE_LOG(LogTemp, Warning, TEXT("    │     ⚠️ 매칭 실패: %s 타입 아이템 없음"), *SavedItem.ItemType.ToString());
#endif
			continue;
		}

		// 목표 위치에 직접 배치 시도 (Phase 1에서 비웠으므로 충돌 없어야 함)
		if (GridSlots.IsValidIndex(TargetIndex) && !GridSlots[TargetIndex]->GetInventoryItem().IsValid())
		{
			PlaceItemAtGridIndex(*MatchedItem, TargetIndex, SavedItem.GridPosition, SavedItem.StackCount);
			RestoredCount++;
#if INV_DEBUG_WIDGET
			UE_LOG(LogTemp, Warning, TEXT("    │     ✅ 복원 성공!"));
#endif
		}
		else
		{
			// Fix 9: 세이브 데이터 좌표 중복 시 빈 슬롯으로 fallback 배치
			bool bFallbackSuccess = false;
			for (int32 SlotIdx = 0; SlotIdx < GridSlots.Num(); SlotIdx++)
			{
				if (GridSlots.IsValidIndex(SlotIdx) && !GridSlots[SlotIdx]->GetInventoryItem().IsValid())
				{
					const FIntPoint FallbackPos = UInv_WidgetUtils::GetPositionFromIndex(SlotIdx, Columns);
					PlaceItemAtGridIndex(*MatchedItem, SlotIdx, FallbackPos, SavedItem.StackCount);
					bFallbackSuccess = true;
					RestoredCount++;
					UE_LOG(LogTemp, Warning, TEXT("[Fix9] 좌표 충돌 fallback 성공: %s, 원래 Pos=(%d,%d) → fallback Pos=(%d,%d)"),
						*SavedItem.ItemType.ToString(),
						SavedItem.GridPosition.X, SavedItem.GridPosition.Y,
						FallbackPos.X, FallbackPos.Y);
					break;
				}
			}
			if (!bFallbackSuccess)
			{
				UE_LOG(LogTemp, Warning, TEXT("[Fix9] 좌표 충돌 fallback 실패 — 빈 슬롯 없음: %s, Pos=(%d,%d)"),
					*SavedItem.ItemType.ToString(),
					SavedItem.GridPosition.X, SavedItem.GridPosition.Y);
			}
#if INV_DEBUG_WIDGET
			UE_LOG(LogTemp, Warning, TEXT("    │     ⚠️ 복원 실패 (fallback %s)"),
				bFallbackSuccess ? TEXT("성공") : TEXT("실패"));
#endif
		}
	}

	// Phase 3: 세이브 데이터에 없는 잔여 아이템 처리 (빈 슬롯에 배치)
	for (FCollectedItemInfo& Info : CollectedItems)
	{
		if (!Info.bUsed)
		{
			for (int32 SlotIdx = 0; SlotIdx < GridSlots.Num(); SlotIdx++)
			{
				if (GridSlots.IsValidIndex(SlotIdx) && !GridSlots[SlotIdx]->GetInventoryItem().IsValid())
				{
					const FIntPoint FallbackPos = UInv_WidgetUtils::GetPositionFromIndex(SlotIdx, Columns);
					PlaceItemAtGridIndex(Info, SlotIdx, FallbackPos, 1);
					UE_LOG(LogTemp, Warning, TEXT("[Fix12] 잔여 아이템 배치: %s → fallback Pos=(%d,%d)"),
						*Info.InventoryItem->GetItemManifest().GetItemType().ToString(),
						FallbackPos.X, FallbackPos.Y);
					break;
				}
			}
		}
	}

#if INV_DEBUG_WIDGET
	UE_LOG(LogTemp, Warning, TEXT("    │"));
	UE_LOG(LogTemp, Warning, TEXT("    │ 📦 복원 결과: %d개 성공"), RestoredCount);
	UE_LOG(LogTemp, Warning, TEXT("    └────────────────────────────────────────┘"));
#endif

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
		
		// 서버에 올바른 위치 전송 (회전 상태 포함)
		InventoryComponent->Server_UpdateItemGridPosition(Item, GridIndex, GridCategoryValue, SlottedItem->IsRotated());
		SentCount++;
		
#if INV_DEBUG_WIDGET
		UE_LOG(LogTemp, Log, TEXT("[SendAllItemPositionsToServer] Grid%d: %s → Index=%d"),
			CategoryIndex, *Item->GetItemManifest().GetItemType().ToString(), GridIndex);
#endif
	}

#if INV_DEBUG_WIDGET
	UE_LOG(LogTemp, Warning, TEXT("[SendAllItemPositionsToServer] Grid%d: %d개 아이템 위치 전송 완료"),
		CategoryIndex, SentCount);
#endif
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
#if INV_DEBUG_WIDGET
		UE_LOG(LogTemp, Warning, TEXT("    │     ⚠️ [MoveItemToPosition] 매칭되는 아이템 없음"));
		UE_LOG(LogTemp, Warning, TEXT("    │         ItemType: %s, StackCount: %d"), *ItemType.ToString(), StackCount);
#endif
		return false;
	}

	// ============================================
	// Step 2: 현재 위치가 목표 위치면 스킵
	// ============================================
	if (CurrentIndex == TargetIndex)
	{
#if INV_DEBUG_WIDGET
		UE_LOG(LogTemp, Warning, TEXT("    │     ✅ [MoveItemToPosition] 이미 목표 위치에 있음 (Index=%d)"), CurrentIndex);
#endif
		return true;
	}

	// ============================================
	// Step 3: 목표 위치가 비어있는지 확인
	// ============================================
	if (!GridSlots.IsValidIndex(TargetIndex))
	{
#if INV_DEBUG_WIDGET
		UE_LOG(LogTemp, Warning, TEXT("    │     ⚠️ [MoveItemToPosition] 유효하지 않은 목표 Index: %d"), TargetIndex);
#endif
		return false;
	}

	// 목표 슬롯이 이미 점유되어 있는지 확인
	if (GridSlots[TargetIndex]->GetInventoryItem().IsValid())
	{
#if INV_DEBUG_WIDGET
		UE_LOG(LogTemp, Warning, TEXT("    │     ⚠️ [MoveItemToPosition] 목표 위치가 이미 점유됨 (Index=%d)"), TargetIndex);
#endif
		return false;
	}

#if INV_DEBUG_WIDGET
	UE_LOG(LogTemp, Warning, TEXT("    │     🔄 [MoveItemToPosition] 이동 시작: Index %d → %d"), CurrentIndex, TargetIndex);
#endif

	// ============================================
	// Step 4: 원래 위치의 GridSlots 해제
	// ============================================
	UInv_InventoryItem* InventoryItem = FoundSlottedItem->GetInventoryItem();
	const FInv_GridFragment* GridFragment = GetFragment<FInv_GridFragment>(InventoryItem, FragmentTags::GridFragment);
	// R키 회전: SlottedItem의 회전 상태로 실효 dimensions 계산
	const bool bItemRotated = FoundSlottedItem->IsRotated();
	FIntPoint Dimensions = FIntPoint(1, 1);
	if (GridFragment)
	{
		Dimensions = GetEffectiveDimensions(GridFragment, bItemRotated);
	}

	// U12+U13: 원래 위치의 모든 GridSlot 완전 초기화 (MoveItemByCurrentIndex 패턴과 일치)
	UInv_InventoryStatics::ForEach2D(GridSlots, CurrentIndex, Dimensions, Columns, [&](UInv_GridSlot* GridSlot)
	{
		if (GridSlot)
		{
			GridSlot->SetInventoryItem(nullptr);
			GridSlot->SetUpperLeftIndex(INDEX_NONE); // U13: 고스트 슬롯 방지
			GridSlot->SetStackCount(0);
			GridSlot->SetAvailable(true);
			GridSlot->SetUnoccupiedTexture();
		}
	});
	// U12: OccupiedMask 비트 해제 (이전 위치)
	SetOccupiedBits(CurrentIndex, Dimensions, false);

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
	// U12: OccupiedMask 비트 설정 (새 위치)
	SetOccupiedBits(TargetIndex, Dimensions, true);

	// ============================================
	// Step 7: 위젯 위치 업데이트
	// ============================================
	UCanvasPanelSlot* CanvasSlot = Cast<UCanvasPanelSlot>(FoundSlottedItem->Slot);
	if (CanvasSlot)
	{
		const FVector2D DrawPos = FVector2D(TargetPosition.X * TileSize, TargetPosition.Y * TileSize);
		float ItemPadding = 0.0f;
		if (GridFragment)
		{
			ItemPadding = GridFragment->GetGridPadding();
			// R키 회전: 캔버스 슬롯 크기도 회전된 dimensions로 설정
			const float IconTileWidth = TileSize - ItemPadding * 2;
			CanvasSlot->SetSize(FVector2D(Dimensions) * IconTileWidth);
		}
		const FVector2D DrawPosWithPadding = DrawPos + FVector2D(ItemPadding);
		CanvasSlot->SetPosition(DrawPosWithPadding);
	}

#if INV_DEBUG_WIDGET
	UE_LOG(LogTemp, Warning, TEXT("    │     ✅ [MoveItemToPosition] 이동 완료!"));
	UE_LOG(LogTemp, Warning, TEXT("    │         %s x%d: Index %d → %d, Pos(%d,%d)"),
		*ItemType.ToString(), StackCount, CurrentIndex, TargetIndex, TargetPosition.X, TargetPosition.Y);
#endif

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
#if INV_DEBUG_WIDGET
		UE_LOG(LogTemp, Warning, TEXT("    │     ✅ [MoveItemByCurrentIndex] 이미 목표 위치에 있음 (Index=%d)"), CurrentIndex);
#endif
		return true;
	}

	// ============================================
	// Step 2: CurrentIndex에 SlottedItem이 있는지 확인
	// ============================================
	UInv_SlottedItem* FoundSlottedItem = SlottedItems.FindRef(CurrentIndex);
	if (!IsValid(FoundSlottedItem))
	{
#if INV_DEBUG_WIDGET
		UE_LOG(LogTemp, Warning, TEXT("    │     ⚠️ [MoveItemByCurrentIndex] CurrentIndex=%d에 SlottedItem 없음"), CurrentIndex);
#endif
		return false;
	}

	UInv_InventoryItem* InventoryItem = FoundSlottedItem->GetInventoryItem();
	if (!InventoryItem)
	{
#if INV_DEBUG_WIDGET
		UE_LOG(LogTemp, Warning, TEXT("    │     ⚠️ [MoveItemByCurrentIndex] InventoryItem이 nullptr"));
#endif
		return false;
	}

	// ============================================
	// Step 3: 목표 위치가 비어있는지 확인
	// ============================================
	if (!GridSlots.IsValidIndex(TargetIndex))
	{
#if INV_DEBUG_WIDGET
		UE_LOG(LogTemp, Warning, TEXT("    │     ⚠️ [MoveItemByCurrentIndex] 유효하지 않은 목표 Index: %d"), TargetIndex);
#endif
		return false;
	}

	if (GridSlots[TargetIndex]->GetInventoryItem().IsValid())
	{
#if INV_DEBUG_WIDGET
		UE_LOG(LogTemp, Warning, TEXT("    │     ⚠️ [MoveItemByCurrentIndex] 목표 위치가 이미 점유됨 (Index=%d)"), TargetIndex);
#endif
		return false;
	}

#if INV_DEBUG_WIDGET
	UE_LOG(LogTemp, Warning, TEXT("    │     🔄 [MoveItemByCurrentIndex] 이동 시작: Index %d → %d"), CurrentIndex, TargetIndex);
#endif

	// ============================================
	// Step 4: 아이템 크기 정보 가져오기 (회전 상태 반영)
	// ============================================
	const FInv_GridFragment* GridFragment = GetFragment<FInv_GridFragment>(InventoryItem, FragmentTags::GridFragment);
	const bool bItemRotated = FoundSlottedItem->IsRotated();
	FIntPoint Dimensions = GetEffectiveDimensions(GridFragment, bItemRotated);

	// ============================================
	// ⭐ Step 4.5: 기존 위치의 StackCount 저장 (핵심 수정!)
	// ============================================
	// ⭐ Phase 5: SavedStackCount가 전달되면 그 값을 사용, 아니면 현재 슬롯의 StackCount 사용
	const int32 OriginalStackCount = (SavedStackCount > 0) ? SavedStackCount : GridSlots[CurrentIndex]->GetStackCount();
#if INV_DEBUG_WIDGET
	UE_LOG(LogTemp, Warning, TEXT("    │     📦 기존 StackCount: %d (SavedStackCount=%d)"), OriginalStackCount, SavedStackCount);
#endif

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
	SetOccupiedBits(CurrentIndex, Dimensions, false); // ⭐ [최적화 #5] 이전 위치 비트마스크 해제

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
#if INV_DEBUG_WIDGET
				UE_LOG(LogTemp, Warning, TEXT("    │     📦 새 위치에 StackCount=%d 설정"), OriginalStackCount);
#endif
			}
		}
	});
	SetOccupiedBits(TargetIndex, Dimensions, true); // ⭐ [최적화 #5] 새 위치 비트마스크 설정

	// ============================================
	// ⭐ Step 7.5: SlottedItem 위젯의 GridIndex 업데이트 (핵심 수정!)
	// ============================================
	// 문제: SlottedItem 클릭 시 저장된 GridIndex를 Broadcast함
	// 해결: 새 위치의 GridIndex로 업데이트해야 클릭이 정상 동작
	FoundSlottedItem->SetGridIndex(TargetIndex);
	// ⭐ Phase 5: SlottedItem UI 텍스트도 업데이트 (로드 후 "1"로 표시되는 버그 수정)
	FoundSlottedItem->UpdateStackCount(OriginalStackCount);
#if INV_DEBUG_WIDGET
	UE_LOG(LogTemp, Warning, TEXT("    │     🔧 SlottedItem.GridIndex=%d로 업데이트, UI StackCount=%d"), TargetIndex, OriginalStackCount);
#endif

	// ============================================
	// Step 8: 위젯 위치 + 크기 업데이트 (회전 상태 반영)
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

		// 회전 상태에 맞는 캔버스 크기 설정
		const FVector2D RotatedDrawSize = GetDrawSizeRotated(GridFragment, bItemRotated);
		CanvasSlot->SetSize(RotatedDrawSize);
	}

#if INV_DEBUG_WIDGET
	UE_LOG(LogTemp, Warning, TEXT("    │     ✅ [MoveItemByCurrentIndex] 이동 완료!"));
	UE_LOG(LogTemp, Warning, TEXT("    │         %s: Index %d → %d, Pos(%d,%d)"),
		*InventoryItem->GetItemManifest().GetItemType().ToString(),
		CurrentIndex, TargetIndex, TargetPosition.X, TargetPosition.Y);
#endif

	return true;
}

// ════════════════════════════════════════════════════════════════════════════════
// [Phase 9] 컨테이너 Grid 크로스 드래그 & 드롭 함수들
// ════════════════════════════════════════════════════════════════════════════════

void UInv_InventoryGrid::SetContainerComponent(UInv_LootContainerComponent* InContainerComp)
{
	ContainerComp = InContainerComp;
}

UInv_LootContainerComponent* UInv_InventoryGrid::GetContainerComponent() const
{
	return ContainerComp.Get();
}

// [B1+B2 Fix] RPC 호출용으로만 InvComp를 설정 (델리게이트 바인딩 + SyncExistingItems 스킵)
// 컨테이너 Grid에서 사용 — 플레이어 InvComp의 델리게이트를 바인딩하면 플레이어 아이템이 표시되는 버그 방지
void UInv_InventoryGrid::SetInventoryComponentForRPC(UInv_InventoryComponent* InComp)
{
	if (!IsValid(InComp)) return;

	// bSkipAutoInit 경로: 지연된 ConstructGrid 실행
	if (GridSlots.Num() == 0 && CanvasPanel)
	{
		UE_LOG(LogTemp, Log, TEXT("[InventoryGrid] SetInventoryComponentForRPC → 지연된 ConstructGrid 실행"));
		ConstructGrid();
	}

	// RPC 호출용 참조만 저장 (델리게이트 바인딩 없음, SyncExistingItems 호출 없음)
	InventoryComponent = InComp;

	UE_LOG(LogTemp, Log, TEXT("[InventoryGrid] SetInventoryComponentForRPC 완료 → InvComp=%s (RPC전용, 델리게이트 없음)"),
		*InComp->GetName());
}

// [B1+B2 Fix] 컨테이너 FastArray의 기존 아이템을 Grid에 동기화
// SetInventoryComponent의 SyncExistingItems 대신, ContainerComp의 Entries를 순회
void UInv_InventoryGrid::SyncContainerItems(UInv_LootContainerComponent* InContainerComp)
{
	if (!IsValid(InContainerComp)) return;

	const TArray<FInv_InventoryEntry>& Entries = InContainerComp->ContainerInventoryList.Entries;
	int32 SyncCount = 0;

	for (int32 i = 0; i < Entries.Num(); ++i)
	{
		const FInv_InventoryEntry& Entry = Entries[i];
		if (!IsValid(Entry.Item)) continue;

		// AddItem(Item, EntryIndex) — Grid에 아이템 표시
		AddItem(Entry.Item, i);
		++SyncCount;
	}

	UE_LOG(LogTemp, Log, TEXT("[InventoryGrid] SyncContainerItems: %d개 컨테이너 아이템 동기화 완료"), SyncCount);
}

void UInv_InventoryGrid::SetLinkedContainerGrid(UInv_InventoryGrid* OtherGrid)
{
	LinkedContainerGrid = OtherGrid;
}

bool UInv_InventoryGrid::HasLinkedHoverItem() const
{
	if (!LinkedContainerGrid.IsValid()) return false;
	return LinkedContainerGrid->HasHoverItem();
}

UInv_HoverItem* UInv_InventoryGrid::GetLinkedHoverItem() const
{
	if (!LinkedContainerGrid.IsValid()) return nullptr;
	return LinkedContainerGrid->GetHoverItem();
}

bool UInv_InventoryGrid::TryTransferFromLinkedContainerGrid(int32 GridIndex)
{
	if (!LinkedContainerGrid.IsValid() || !HasLinkedHoverItem()) return false;
	if (!InventoryComponent.IsValid()) return false;

	UInv_HoverItem* LinkedHover = GetLinkedHoverItem();
	if (!IsValid(LinkedHover)) return false;

	const int32 EntryIndex = LinkedHover->GetEntryIndex();
	UInv_LootContainerComponent* LinkedCC = LinkedContainerGrid->GetContainerComponent();

	// 방향 판별: 연결된 Grid가 Container이고 내가 Player → TakeItem
	if (LinkedContainerGrid->GetOwnerType() == EGridOwnerType::Container
		&& OwnerType == EGridOwnerType::Player)
	{
		if (IsValid(LinkedCC))
		{
			InventoryComponent->Server_TakeItemFromContainer(LinkedCC, EntryIndex, GridIndex);
		}
	}
	// 연결된 Grid가 Player이고 내가 Container → PutItem
	else if (LinkedContainerGrid->GetOwnerType() == EGridOwnerType::Player
		&& OwnerType == EGridOwnerType::Container)
	{
		UInv_LootContainerComponent* MyCC = GetContainerComponent();
		if (IsValid(MyCC))
		{
			InventoryComponent->Server_PutItemInContainer(MyCC, EntryIndex, GridIndex);
		}
	}

	// 원본 Grid의 HoverItem 정리
	LinkedContainerGrid->ClearHoverItem();
	return true;
}

// ════════════════════════════════════════════════════════════════════════════════
// [Phase 11] HandleQuickTransfer — Ctrl+LMB: 빠른 전송
// Shift+RMB 로비 전송 + Phase 9 컨테이너 전송 로직 재사용
// ════════════════════════════════════════════════════════════════════════════════
void UInv_InventoryGrid::HandleQuickTransfer(int32 GridIndex)
{
	// UpperLeftIndex 리졸브 (대형 아이템 호환)
	int32 LookupIndex = GridIndex;
	if (GridSlots.IsValidIndex(GridIndex) && GridSlots[GridIndex])
	{
		const int32 UpperLeft = GridSlots[GridIndex]->GetUpperLeftIndex();
		if (UpperLeft >= 0) LookupIndex = UpperLeft;
	}

	UInv_SlottedItem* Slotted = SlottedItems.FindRef(LookupIndex);
	if (!IsValid(Slotted)) return;

	UInv_InventoryItem* SlottedItem = Slotted->GetInventoryItem();
	if (!IsValid(SlottedItem)) return;
	if (!InventoryComponent.IsValid()) return;

	// ──────────────────────────────────────────────────────────────
	// 1) 컨테이너 모드 (Phase 9) — LinkedContainerGrid가 설정된 경우
	// ──────────────────────────────────────────────────────────────
	if (LinkedContainerGrid.IsValid())
	{
		// Entry 인덱스 찾기
		int32 EntryIdx = INDEX_NONE;
		const TArray<FInv_InventoryEntry>& Entries =
			(OwnerType == EGridOwnerType::Container && ContainerComp.IsValid())
			? ContainerComp->ContainerInventoryList.Entries
			: InventoryComponent->GetInventoryList().Entries;

		for (int32 i = 0; i < Entries.Num(); ++i)
		{
			if (Entries[i].Item == SlottedItem)
			{
				EntryIdx = i;
				break;
			}
		}

		if (EntryIdx != INDEX_NONE)
		{
			UInv_LootContainerComponent* LinkedCC = LinkedContainerGrid->GetContainerComponent();
			UInv_LootContainerComponent* MyCC = GetContainerComponent();

			if (OwnerType == EGridOwnerType::Container && IsValid(MyCC))
			{
				// 컨테이너 → 플레이어
				InventoryComponent->Server_TakeItemFromContainer(MyCC, EntryIdx, -1);
			}
			else if (OwnerType == EGridOwnerType::Player && IsValid(LinkedCC))
			{
				// 플레이어 → 컨테이너
				InventoryComponent->Server_PutItemInContainer(LinkedCC, EntryIdx, -1);
			}
		}
		return;
	}

	// ──────────────────────────────────────────────────────────────
	// 2) 로비 전송 모드 — LobbyTargetGrid가 설정된 경우
	// ──────────────────────────────────────────────────────────────
	if (bLobbyTransferMode)
	{
		// ReplicationID 기반 전송
		int32 RepID = INDEX_NONE;
		const TArray<FInv_InventoryEntry>& Entries = InventoryComponent->GetInventoryList().Entries;
		for (const FInv_InventoryEntry& Entry : Entries)
		{
			if (Entry.Item == SlottedItem)
			{
				RepID = Entry.ReplicationID;
				break;
			}
		}

		if (RepID == INDEX_NONE)
		{
			UE_LOG(LogTemp, Error, TEXT("[HandleQuickTransfer] Ctrl+LMB 전송 실패 → ReplicationID 미발견"));
			return;
		}

		// [Fix19] 대상 Grid 용량 사전 체크
		if (LobbyTargetGrid.IsValid())
		{
			const FInv_ItemManifest& Manifest = SlottedItem->GetItemManifest();
			if (!LobbyTargetGrid->HasRoomInActualGrid(Manifest))
			{
				UE_LOG(LogTemp, Warning, TEXT("[HandleQuickTransfer] Ctrl+LMB 전송 차단: 대상 Grid 공간 부족 (RepID=%d)"), RepID);
				return;
			}
		}
		else
		{
			UE_LOG(LogTemp, Warning, TEXT("[HandleQuickTransfer] LobbyTargetGrid 미설정 (Ctrl+LMB). 용량 체크 없이 전송 진행"));
		}

		UE_LOG(LogTemp, Log, TEXT("[HandleQuickTransfer] Ctrl+LMB 빠른 전송 → RepID=%d"), RepID);
		OnLobbyTransferRequested.Broadcast(RepID);
		return;
	}

	// 3) 일반 게임 모드 — 전송 대상 없음, 무시
}

// ════════════════════════════════════════════════════════════════════════════════
// [Phase 11] HandleQuickEquip — Alt+LMB: 빠른 장착/해제
// ════════════════════════════════════════════════════════════════════════════════
void UInv_InventoryGrid::HandleQuickEquip(int32 GridIndex)
{
	// 로비 모드 / 컨테이너 Grid → 장착 불가 컨텍스트
	if (bLobbyTransferMode) return;
	if (OwnerType == EGridOwnerType::Container) return;

	// UpperLeftIndex 리졸브
	int32 LookupIndex = GridIndex;
	if (GridSlots.IsValidIndex(GridIndex) && GridSlots[GridIndex])
	{
		const int32 UpperLeft = GridSlots[GridIndex]->GetUpperLeftIndex();
		if (UpperLeft >= 0) LookupIndex = UpperLeft;
	}

	UInv_SlottedItem* Slotted = SlottedItems.FindRef(LookupIndex);
	if (!IsValid(Slotted)) return;

	UInv_InventoryItem* Item = Slotted->GetInventoryItem();
	if (!IsValid(Item)) return;

	// 장비 아이템인지 확인 (EquipmentFragment 존재 여부)
	const FInv_EquipmentFragment* EquipFrag = Item->GetItemManifest().GetFragmentOfType<FInv_EquipmentFragment>();
	if (!EquipFrag) return; // 장비 아이템 아님

	if (!InventoryComponent.IsValid()) return;

	// Entry에서 장착 상태 확인
	const TArray<FInv_InventoryEntry>& Entries = InventoryComponent->GetInventoryList().Entries;
	int32 EntryIdx = INDEX_NONE;
	for (int32 i = 0; i < Entries.Num(); ++i)
	{
		if (Entries[i].Item == Item)
		{
			EntryIdx = i;
			break;
		}
	}
	if (EntryIdx == INDEX_NONE) return;

	const FInv_InventoryEntry& Entry = Entries[EntryIdx];

	if (Entry.bIsEquipped)
	{
		// 이미 장착됨 → 해제 RPC (Server_EquipSlotClicked에 ItemToEquip=nullptr, ItemToUnequip=Item)
		InventoryComponent->Server_EquipSlotClicked(nullptr, Item, Entry.WeaponSlotIndex);

		// 클라이언트 브로드캐스트 (데디서버)
		if (GetOwningPlayer() && GetOwningPlayer()->GetNetMode() == NM_Client)
		{
			InventoryComponent->OnItemUnequipped.Broadcast(Item, Entry.WeaponSlotIndex);
		}

		UE_LOG(LogTemp, Log, TEXT("[HandleQuickEquip] Alt+LMB 빠른 해제 → WeaponSlot=%d"), Entry.WeaponSlotIndex);
	}
	else
	{
		// 미장착 → 장착 요청 (SpatialInventory가 슬롯 배정 처리)
		OnQuickEquipRequested.Broadcast(Item, EntryIdx);
		UE_LOG(LogTemp, Log, TEXT("[HandleQuickEquip] Alt+LMB 빠른 장착 요청 → EntryIdx=%d"), EntryIdx);
	}
}

// ════════════════════════════════════════════════════════════════════════════════
// [Phase 11] HandleQuickSplit — Shift+LMB: 스택 반분할 (다이얼로그 없음)
// ════════════════════════════════════════════════════════════════════════════════
void UInv_InventoryGrid::HandleQuickSplit(int32 GridIndex)
{
	// HoverItem이 이미 있으면 무시 (드래그 중이 아닌 경우에만 동작)
	if (IsValid(HoverItem)) return;

	if (!GridSlots.IsValidIndex(GridIndex)) return;

	// UpperLeftIndex 리졸브
	const int32 UpperLeft = GridSlots[GridIndex]->GetUpperLeftIndex();
	const int32 LookupIndex = (UpperLeft >= 0 && GridSlots.IsValidIndex(UpperLeft)) ? UpperLeft : GridIndex;

	UInv_InventoryItem* Item = GridSlots[LookupIndex]->GetInventoryItem().Get();
	if (!IsValid(Item)) return;
	if (!Item->IsStackable()) return; // 스택 불가능 아이템은 무시

	// UpperLeftGridSlot에서 현재 스택 수 가져오기
	UInv_GridSlot* UpperLeftGridSlot = GridSlots[LookupIndex];
	const int32 CurrentStackCount = UpperLeftGridSlot->GetStackCount();
	if (CurrentStackCount < 2) return; // 1개 이하면 분할 불가

	// 반분할 양 계산 (올림)
	const int32 SplitAmount = (CurrentStackCount + 1) / 2;

	UE_LOG(LogTemp, Log, TEXT("[HandleQuickSplit] Shift+LMB 반분할 → 원본 %d개, 분할 %d개"), CurrentStackCount, SplitAmount);

	// 기존 OnPopUpMenuSplit 로직 재사용
	OnPopUpMenuSplit(SplitAmount, LookupIndex);
}
