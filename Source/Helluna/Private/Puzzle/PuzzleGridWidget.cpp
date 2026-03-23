// Source/Helluna/Private/Puzzle/PuzzleGridWidget.cpp

#include "Puzzle/PuzzleGridWidget.h"
#include "Puzzle/PuzzleCubeActor.h"
#include "Puzzle/PuzzleTypes.h"
#include "Puzzle/PuzzleCellWidget.h"
#include "Controller/HellunaHeroController.h"
#include "Components/UniformGridPanel.h"
#include "Components/TextBlock.h"
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

	// GridPanel / PuzzleCellWidgetClass 체크
	if (!GridPanel || !PuzzleCellWidgetClass)
	{
		UE_LOG(LogTemp, Warning,
			TEXT("[PuzzleWidget] InitGrid: GridPanel 또는 PuzzleCellWidgetClass가 null"));
		return;
	}

	// 기존 셀 정리
	GridPanel->ClearChildren();
	CellWidgets.Empty();

	// 4x4 셀 생성 — WBP에서 정의된 셀 위젯 인스턴스화
	for (int32 Row = 0; Row < GridSize; ++Row)
	{
		for (int32 Col = 0; Col < GridSize; ++Col)
		{
			UPuzzleCellWidget* Cell = CreateWidget<UPuzzleCellWidget>(this, PuzzleCellWidgetClass);
			if (!Cell)
			{
				continue;
			}

			// 배경 텍스처 설정
			Cell->SetBgTexture(EmptyCellTexture);

			CellWidgets.Add(Cell);
			GridPanel->AddChildToUniformGrid(Cell, Row, Col);
		}
	}

	UE_LOG(LogTemp, Warning,
		TEXT("[PuzzleWidget] InitGrid: Created %d cell widgets using %s"),
		CellWidgets.Num(), *GetNameSafe(PuzzleCellWidgetClass));

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

	for (int32 i = 0; i < TotalCells && i < CellWidgets.Num(); ++i)
	{
		if (!Grid.Cells.IsValidIndex(i) || !IsValid(CellWidgets[i]))
		{
			continue;
		}

		const FPuzzleCell& Cell = Grid.Cells[i];
		UPuzzleCellWidget* CellWidget = CellWidgets[i];

		// 파이프 타입에 따른 텍스처 선택
		UTexture2D* Texture = nullptr;
		switch (Cell.PipeType)
		{
		case EPuzzlePipeType::Straight: Texture = StraightPipeTexture; break;
		case EPuzzlePipeType::Curve:    Texture = CurvePipeTexture;    break;
		case EPuzzlePipeType::Start:    Texture = StartNodeTexture;    break;
		case EPuzzlePipeType::End:      Texture = EndNodeTexture;      break;
		default:                        Texture = nullptr;             break;
		}

		// 셀 위젯에 로직만 전달 — 렌더링은 WBP가 담당
		CellWidget->SetPipeTexture(Texture, static_cast<float>(Cell.Rotation));
		CellWidget->SetConnectedTint(ConnectedCells.Contains(i));
	}

	// 로그
	UE_LOG(LogTemp, Warning,
		TEXT("[PuzzleWidget] RefreshGrid: Updated %d cells, Connected=%d"),
		FMath::Min(TotalCells, CellWidgets.Num()), ConnectedCells.Num());

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

	for (int32 i = 0; i < CellWidgets.Num(); ++i)
	{
		if (!IsValid(CellWidgets[i]))
		{
			continue;
		}

		const int32 Row = i / GridSize;
		const int32 Col = i % GridSize;
		CellWidgets[i]->SetSelected(Row == SelectedRow && Col == SelectedCol);
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
