// Source/Helluna/Private/Puzzle/PuzzleGridWidget.cpp

#include "Puzzle/PuzzleGridWidget.h"
#include "Puzzle/PuzzleCubeActor.h"
#include "Puzzle/PuzzleTypes.h"
#include "Controller/HellunaHeroController.h"
#include "Components/UniformGridPanel.h"
#include "Components/TextBlock.h"
#include "Components/Image.h"
#include "Components/Overlay.h"
#include "Components/OverlaySlot.h"
#include "TimerManager.h"

// ============================================================================
// 라이프사이클
// ============================================================================

void UPuzzleGridWidget::NativeConstruct()
{
	Super::NativeConstruct();
	SetIsFocusable(true);
}

void UPuzzleGridWidget::NativeDestruct()
{
	// 델리게이트 언바인딩
	if (OwningCube.IsValid())
	{
		OwningCube->OnPuzzleGridUpdated.RemoveDynamic(this, &UPuzzleGridWidget::RefreshGrid);
		OwningCube->OnPuzzleLockChanged.RemoveDynamic(this, &UPuzzleGridWidget::OnLockChanged);
	}

	// 타이머 정리
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(ClientTimerHandle);
	}

	Super::NativeDestruct();
}

// ============================================================================
// InitGrid — 4x4 그리드 셀 생성
// ============================================================================

void UPuzzleGridWidget::InitGrid(APuzzleCubeActor* Cube)
{
	if (!IsValid(Cube))
	{
		UE_LOG(LogTemp, Warning, TEXT("[PuzzleWidget] InitGrid: Cube가 null"));
		return;
	}

	OwningCube = Cube;
	const int32 GridSize = Cube->PuzzleGrid.GridSize;

	// 로그 #8
	UE_LOG(LogTemp, Warning,
		TEXT("[PuzzleWidget] InitGrid: Cube=%s, GridSize=%d"),
		*GetNameSafe(Cube), GridSize);

	// 델리게이트 바인딩
	Cube->OnPuzzleGridUpdated.AddUniqueDynamic(this, &UPuzzleGridWidget::RefreshGrid);
	Cube->OnPuzzleLockChanged.AddUniqueDynamic(this, &UPuzzleGridWidget::OnLockChanged);

	// GridPanel이 없으면 종료
	if (!GridPanel)
	{
		UE_LOG(LogTemp, Warning, TEXT("[PuzzleWidget] InitGrid: GridPanel이 null — BP에서 바인딩 필요"));
		return;
	}

	// 기존 셀 정리
	GridPanel->ClearChildren();
	CellPipeImages.Empty();
	CellSelectionImages.Empty();

	// 4x4 셀 생성
	for (int32 Row = 0; Row < GridSize; ++Row)
	{
		for (int32 Col = 0; Col < GridSize; ++Col)
		{
			// Overlay (배경 + 파이프 + 선택)
			UOverlay* CellOverlay = NewObject<UOverlay>(this);

			// 배경 이미지
			UImage* BgImage = NewObject<UImage>(this);
			if (EmptyCellTexture)
			{
				BgImage->SetBrushFromTexture(EmptyCellTexture);
			}
			UOverlaySlot* BgSlot = Cast<UOverlaySlot>(CellOverlay->AddChild(BgImage));
			if (BgSlot)
			{
				BgSlot->SetHorizontalAlignment(HAlign_Fill);
				BgSlot->SetVerticalAlignment(VAlign_Fill);
			}

			// 파이프 이미지 (회전 적용)
			UImage* PipeImage = NewObject<UImage>(this);
			PipeImage->SetRenderTransformPivot(FVector2D(0.5f, 0.5f));
			UOverlaySlot* PipeSlot = Cast<UOverlaySlot>(CellOverlay->AddChild(PipeImage));
			if (PipeSlot)
			{
				PipeSlot->SetHorizontalAlignment(HAlign_Fill);
				PipeSlot->SetVerticalAlignment(VAlign_Fill);
			}
			CellPipeImages.Add(PipeImage);

			// 선택 하이라이트 이미지 (기본 숨김)
			UImage* SelectionImage = NewObject<UImage>(this);
			if (SelectedCellTexture)
			{
				SelectionImage->SetBrushFromTexture(SelectedCellTexture);
			}
			SelectionImage->SetVisibility(ESlateVisibility::Collapsed);
			UOverlaySlot* SelSlot = Cast<UOverlaySlot>(CellOverlay->AddChild(SelectionImage));
			if (SelSlot)
			{
				SelSlot->SetHorizontalAlignment(HAlign_Fill);
				SelSlot->SetVerticalAlignment(VAlign_Fill);
			}
			CellSelectionImages.Add(SelectionImage);

			// UniformGridPanel에 추가
			GridPanel->AddChildToUniformGrid(CellOverlay, Row, Col);
		}
	}

	// 초기 상태
	SelectedRow = 0;
	SelectedCol = 0;

	if (StatusText)
	{
		StatusText->SetText(FText::FromString(TEXT("퍼즐 풀기")));
	}

	if (TimerText)
	{
		TimerText->SetText(FText::GetEmpty());
	}

	// 초기 그리드 표시
	RefreshGrid();
	UpdateSelectionHighlight();
}

// ============================================================================
// RefreshGrid — 셀 이미지 갱신
// ============================================================================

void UPuzzleGridWidget::RefreshGrid()
{
	if (!OwningCube.IsValid())
	{
		return;
	}

	const FPuzzleGridData& Grid = OwningCube->PuzzleGrid;
	const int32 TotalCells = Grid.GridSize * Grid.GridSize;

	// 연결 상태 계산
	ConnectedCells = PuzzleUtils::GetConnectedCells(Grid);

	// 로그
	UE_LOG(LogTemp, Warning, TEXT("[PuzzleWidget] RefreshGrid: Connected cells = %d / %d"),
		ConnectedCells.Num(), TotalCells);

	// 연결 Tint 색상
	static const FLinearColor ConnectedColor = FLinearColor(0.0f, 0.8f, 1.0f, 1.0f); // 시안
	static const FLinearColor DefaultColor = FLinearColor(1.0f, 1.0f, 1.0f, 1.0f);   // 흰색

	for (int32 i = 0; i < TotalCells && i < CellPipeImages.Num(); ++i)
	{
		if (!Grid.Cells.IsValidIndex(i) || !IsValid(CellPipeImages[i]))
		{
			continue;
		}

		const FPuzzleCell& Cell = Grid.Cells[i];
		UImage* PipeImage = CellPipeImages[i];

		// 파이프 타입에 따른 텍스처 선택
		UTexture2D* Texture = nullptr;
		switch (Cell.PipeType)
		{
		case EPuzzlePipeType::Straight: Texture = StraightPipeTexture; break;
		case EPuzzlePipeType::Curve:    Texture = CurvePipeTexture;    break;
		case EPuzzlePipeType::Start:    Texture = StartNodeTexture;    break;
		case EPuzzlePipeType::End:      Texture = EndNodeTexture;      break;
		default:                        Texture = EmptyCellTexture;    break;
		}

		if (Texture)
		{
			PipeImage->SetBrushFromTexture(Texture);
		}

		// 회전 적용 (RenderTransform Angle)
		PipeImage->SetRenderTransformAngle(static_cast<float>(Cell.Rotation));

		// 연결 상태에 따른 Tint 색상
		const bool bConnected = ConnectedCells.Contains(i);
		PipeImage->SetColorAndOpacity(bConnected ? ConnectedColor : DefaultColor);
	}

	UpdateSelectionHighlight();
}

// ============================================================================
// 셀 선택 및 회전
// ============================================================================

void UPuzzleGridWidget::MoveSelection(FIntPoint Direction)
{
	const int32 OldRow = SelectedRow;
	const int32 OldCol = SelectedCol;

	const int32 GridSize = OwningCube.IsValid() ? OwningCube->PuzzleGrid.GridSize : 4;

	SelectedRow = FMath::Clamp(SelectedRow + Direction.X, 0, GridSize - 1);
	SelectedCol = FMath::Clamp(SelectedCol + Direction.Y, 0, GridSize - 1);

	// 로그 #9
	UE_LOG(LogTemp, Warning,
		TEXT("[PuzzleWidget] MoveSelection: (%d,%d) → (%d,%d)"),
		OldRow, OldCol, SelectedRow, SelectedCol);

	UpdateSelectionHighlight();
}

void UPuzzleGridWidget::RotateSelectedCell()
{
	if (!OwningCube.IsValid())
	{
		return;
	}

	const int32 GridSize = OwningCube->PuzzleGrid.GridSize;
	const int32 CellIndex = SelectedRow * GridSize + SelectedCol;

	// Locked 체크
	if (OwningCube->PuzzleGrid.Cells.IsValidIndex(CellIndex) &&
		OwningCube->PuzzleGrid.Cells[CellIndex].bLocked)
	{
		// 로그 #10
		UE_LOG(LogTemp, Warning,
			TEXT("[PuzzleWidget] RotateSelectedCell: Cell(%d,%d), bLocked=true"),
			SelectedRow, SelectedCol);
		return;
	}

	// 로그 #10
	UE_LOG(LogTemp, Warning,
		TEXT("[PuzzleWidget] RotateSelectedCell: Cell(%d,%d), bLocked=false"),
		SelectedRow, SelectedCol);

	// Controller를 통해 Server RPC 호출
	AHellunaHeroController* Controller = Cast<AHellunaHeroController>(GetOwningPlayer());
	if (IsValid(Controller))
	{
		Controller->RequestPuzzleRotateCell(CellIndex);
	}
}

void UPuzzleGridWidget::UpdateSelectionHighlight()
{
	const int32 GridSize = OwningCube.IsValid() ? OwningCube->PuzzleGrid.GridSize : 4;

	UE_LOG(LogTemp, Warning, TEXT("[PuzzleWidget] UpdateSelectionHighlight: (%d,%d)"),
		SelectedRow, SelectedCol);

	for (int32 i = 0; i < CellSelectionImages.Num(); ++i)
	{
		if (!IsValid(CellSelectionImages[i]))
		{
			continue;
		}

		const int32 Row = i / GridSize;
		const int32 Col = i % GridSize;

		const bool bSelected = (Row == SelectedRow && Col == SelectedCol);
		CellSelectionImages[i]->SetVisibility(
			bSelected ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Collapsed
		);
	}
}

// ============================================================================
// Lock 상태 변경 콜백
// ============================================================================

void UPuzzleGridWidget::OnLockChanged(bool bLocked)
{
	if (!bLocked)
	{
		// 해제 성공
		if (StatusText)
		{
			StatusText->SetText(FText::FromString(TEXT("해제 성공!")));
		}

		// 클라이언트 측 타이머 시작
		ClientTimerRemaining = OwningCube.IsValid() ? OwningCube->DamageableTime : 10.f;
		if (UWorld* World = GetWorld())
		{
			World->GetTimerManager().SetTimer(
				ClientTimerHandle, this, &UPuzzleGridWidget::UpdateClientTimer,
				0.1f, true
			);
		}
	}
	else
	{
		// 재잠금
		if (StatusText)
		{
			StatusText->SetText(FText::FromString(TEXT("퍼즐 풀기")));
		}

		if (TimerText)
		{
			TimerText->SetText(FText::GetEmpty());
		}

		if (UWorld* World = GetWorld())
		{
			World->GetTimerManager().ClearTimer(ClientTimerHandle);
		}

		// 새 퍼즐로 그리드 갱신
		RefreshGrid();
	}
}

void UPuzzleGridWidget::UpdateClientTimer()
{
	ClientTimerRemaining -= 0.1f;

	if (TimerText)
	{
		const int32 Seconds = FMath::CeilToInt(FMath::Max(ClientTimerRemaining, 0.f));
		TimerText->SetText(FText::AsNumber(Seconds));
	}

	if (ClientTimerRemaining <= 0.f)
	{
		if (UWorld* World = GetWorld())
		{
			World->GetTimerManager().ClearTimer(ClientTimerHandle);
		}
	}
}

// ============================================================================
// NativeOnKeyDown — 키보드 입력 처리
// ============================================================================

FReply UPuzzleGridWidget::NativeOnKeyDown(const FGeometry& InGeometry, const FKeyEvent& InKeyEvent)
{
	const FKey Key = InKeyEvent.GetKey();

	// 방향키 — 셀 선택 이동
	if (Key == EKeys::Up)
	{
		MoveSelection(FIntPoint(-1, 0));
		return FReply::Handled();
	}
	if (Key == EKeys::Down)
	{
		MoveSelection(FIntPoint(1, 0));
		return FReply::Handled();
	}
	if (Key == EKeys::Left)
	{
		MoveSelection(FIntPoint(0, -1));
		return FReply::Handled();
	}
	if (Key == EKeys::Right)
	{
		MoveSelection(FIntPoint(0, 1));
		return FReply::Handled();
	}

	// Enter / Space — 선택 셀 회전
	if (Key == EKeys::Enter || Key == EKeys::SpaceBar)
	{
		RotateSelectedCell();
		return FReply::Handled();
	}

	// ESC — 퍼즐 나가기
	if (Key == EKeys::Escape)
	{
		// 로그 #13
		UE_LOG(LogTemp, Warning, TEXT("[PuzzleController] ExitPuzzle"));

		AHellunaHeroController* Controller = Cast<AHellunaHeroController>(GetOwningPlayer());
		if (IsValid(Controller))
		{
			Controller->ExitPuzzle();
		}
		return FReply::Handled();
	}

	return Super::NativeOnKeyDown(InGeometry, InKeyEvent);
}
