// Source/Helluna/Private/Puzzle/PuzzleGridWidget.cpp

#include "Puzzle/PuzzleGridWidget.h"
#include "Puzzle/PuzzleCubeActor.h"
#include "Puzzle/PuzzleTypes.h"
#include "Puzzle/PuzzleCellWidget.h"
#include "Controller/HellunaHeroController.h"
#include "Components/UniformGridPanel.h"
#include "Components/CanvasPanel.h"
#include "Components/TextBlock.h"
#include "Components/Image.h"
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
		OwningCube->OnPuzzleTimedOutDelegate.RemoveDynamic(this, &UPuzzleGridWidget::OnPuzzleTimedOut);
	}

	// 타이머 정리
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(ClientTimerHandle);
		World->GetTimerManager().ClearTimer(PuzzleCountdownTimerHandle);
		World->GetTimerManager().ClearTimer(SuccessAnimTimerHandle);
		World->GetTimerManager().ClearTimer(FailAnimTimerHandle);
		World->GetTimerManager().ClearTimer(ShakeTimerHandle);
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
	Cube->OnPuzzleTimedOutDelegate.AddUniqueDynamic(this, &UPuzzleGridWidget::OnPuzzleTimedOut);

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

	// 퍼즐 카운트다운 시작
	if (OwningCube.IsValid())
	{
		StartCountdown(OwningCube->PuzzleTimeLimit);
	}
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

	// FAIL 표시 중이었으면 카운트다운 재시작 (서버가 셔플 완료)
	if (bShowingFail && OwningCube.IsValid())
	{
		StartCountdown(OwningCube->PuzzleTimeLimit);
		UE_LOG(LogTemp, Warning, TEXT("[PuzzleWidget] Countdown restarted after reshuffle"));
	}
}

// ============================================================================
// 셀 선택 및 회전
// ============================================================================

void UPuzzleGridWidget::MoveSelection(FIntPoint Direction)
{
	if (bShowingFail || bPlayingSuccessAnim || bPlayingFailAnim) { return; }

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
	if (bShowingFail || bPlayingSuccessAnim || bPlayingFailAnim) { return; }

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

	// 로컬 이펙트 즉시 재생
	if (CellWidgets.IsValidIndex(CellIndex) && IsValid(CellWidgets[CellIndex]))
	{
		CellWidgets[CellIndex]->PlayRotateFlash(RotateFlashTexture);
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
		const bool bSelected = (Row == SelectedRow && Col == SelectedCol);
		CellWidgets[i]->SetSelected(bSelected);

		if (bSelected)
		{
			CellWidgets[i]->PlaySelectBurst(SelectBurstTexture);
		}
	}
}

// ============================================================================
// Lock 상태 변경 콜백
// ============================================================================

void UPuzzleGridWidget::OnLockChanged(bool bLocked)
{
	if (!bLocked)
	{
		// 해제 성공 — 퍼즐 카운트다운 정지
		if (UWorld* World = GetWorld())
		{
			World->GetTimerManager().ClearTimer(PuzzleCountdownTimerHandle);
		}

		// SUCCESS 오버레이가 대체 — StatusText 숨기기
		if (StatusText)
		{
			StatusText->SetText(FText::GetEmpty());
		}

		// SUCCESS 애니메이션 재생
		PlaySuccessAnimation();

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

		// 데미지 타임 종료 → 퍼즐 UI 자동 닫기
		AHellunaHeroController* Controller = Cast<AHellunaHeroController>(GetOwningPlayer());
		if (IsValid(Controller))
		{
			Controller->ExitPuzzle();
			UE_LOG(LogTemp, Warning, TEXT("[PuzzleWidget] Damage time expired — auto-closing puzzle UI"));
		}
	}
}

// NativeOnKeyDown 제거 — Enhanced Input(IMC_Puzzle)이 방향키/E/ESC 처리

// ============================================================================
// 퍼즐 제한시간 카운트다운
// ============================================================================

void UPuzzleGridWidget::StartCountdown(float TimeLimit)
{
	PuzzleTimeRemaining = TimeLimit;
	bShowingFail = false;

	if (StatusText)
	{
		StatusText->SetText(FText::FromString(TEXT("퍼즐 해제")));
	}

	if (TimerText)
	{
		TimerText->SetText(FText::AsNumber(FMath::CeilToInt(PuzzleTimeRemaining)));
	}

	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(PuzzleCountdownTimerHandle);
		World->GetTimerManager().SetTimer(
			PuzzleCountdownTimerHandle, this,
			&UPuzzleGridWidget::TickCountdown,
			1.0f, true);
	}
}

void UPuzzleGridWidget::TickCountdown()
{
	PuzzleTimeRemaining -= 1.0f;

	if (TimerText)
	{
		const int32 Seconds = FMath::Max(0, FMath::CeilToInt(PuzzleTimeRemaining));
		TimerText->SetText(FText::AsNumber(Seconds));
	}

	if (PuzzleTimeRemaining <= 0.f)
	{
		if (UWorld* World = GetWorld())
		{
			World->GetTimerManager().ClearTimer(PuzzleCountdownTimerHandle);
		}
		ShowFailMessage();
	}
}

void UPuzzleGridWidget::ShowFailMessage()
{
	bShowingFail = true;

	if (StatusText)
	{
		StatusText->SetText(FText::GetEmpty()); // FAIL 오버레이가 대체
	}
	if (TimerText)
	{
		TimerText->SetText(FText::GetEmpty());
	}

	// FAIL 애니메이션 재생
	PlayFailAnimation();

	UE_LOG(LogTemp, Warning, TEXT("[PuzzleWidget] FAIL! Waiting for reshuffle..."));
}

void UPuzzleGridWidget::OnPuzzleTimedOut()
{
	// Multicast에서 호출됨 — ShowFailMessage 트리거
	ShowFailMessage();
}

// ============================================================================
// SUCCESS 애니메이션
// ============================================================================

void UPuzzleGridWidget::PlaySuccessAnimation()
{
	if (!SuccessOverlay)
	{
		// WBP에 오버레이 미구성 시 기존 텍스트 폴백
		if (StatusText)
		{
			StatusText->SetText(FText::FromString(TEXT("해제 성공!")));
		}
		return;
	}

	bPlayingSuccessAnim = true;
	SuccessAnimProgress = 0.f;

	// 오버레이 표시
	SuccessOverlay->SetVisibility(ESlateVisibility::SelfHitTestInvisible);

	// 초기 상태 설정
	if (SuccessGlowImage)
	{
		SuccessGlowImage->SetRenderScale(FVector2D(0.3f, 0.3f));
		SuccessGlowImage->SetRenderOpacity(0.f);
	}
	if (ScanlineTopImage)
	{
		ScanlineTopImage->SetRenderScale(FVector2D(0.f, 1.f));
		ScanlineTopImage->SetRenderOpacity(0.f);
	}
	if (ScanlineBottomImage)
	{
		ScanlineBottomImage->SetRenderScale(FVector2D(0.f, 1.f));
		ScanlineBottomImage->SetRenderOpacity(0.f);
	}
	if (SuccessMainText)
	{
		SuccessMainText->SetRenderScale(FVector2D(0.f, 1.5f));
		SuccessMainText->SetRenderOpacity(0.f);
	}
	if (SuccessSubText)
	{
		SuccessSubText->SetRenderOpacity(0.f);
	}

	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(SuccessAnimTimerHandle);
		World->GetTimerManager().SetTimer(
			SuccessAnimTimerHandle, this,
			&UPuzzleGridWidget::TickSuccessAnimation,
			0.016f, true); // ~60fps
	}

	UE_LOG(LogTemp, Warning, TEXT("[PuzzleWidget] PlaySuccessAnimation started"));
}

void UPuzzleGridWidget::TickSuccessAnimation()
{
	SuccessAnimProgress += 0.016f;
	const float T = SuccessAnimProgress;

	// Phase 1: 0.0 ~ 0.3초 — 글로우 확장 + 스캔라인 + 텍스트 등장
	if (T <= 0.3f)
	{
		const float Phase = T / 0.3f; // 0→1

		// 글로우: Scale 0.3→1.2, Opacity 0→0.8
		if (SuccessGlowImage)
		{
			const float S = FMath::Lerp(0.3f, 1.2f, Phase);
			SuccessGlowImage->SetRenderScale(FVector2D(S, S));
			SuccessGlowImage->SetRenderOpacity(FMath::Lerp(0.f, 0.8f, Phase));
		}

		// 스캔라인: ScaleX 0→1.5, Opacity flash
		if (ScanlineTopImage)
		{
			ScanlineTopImage->SetRenderScale(FVector2D(FMath::Lerp(0.f, 1.5f, Phase), 1.f));
			ScanlineTopImage->SetRenderOpacity(Phase < 0.5f ? Phase * 2.f : (1.f - Phase) * 2.f);
		}
		if (ScanlineBottomImage)
		{
			const float Delayed = FMath::Max(0.f, (T - 0.05f) / 0.25f);
			ScanlineBottomImage->SetRenderScale(FVector2D(FMath::Lerp(0.f, 1.5f, Delayed), 1.f));
			ScanlineBottomImage->SetRenderOpacity(Delayed < 0.5f ? Delayed * 2.f : (1.f - Delayed) * 2.f);
		}

		// 텍스트: ScaleX 0→1.15 (찢어지듯), Opacity 0→1
		if (SuccessMainText && T >= 0.1f)
		{
			const float TextPhase = FMath::Min(1.f, (T - 0.1f) / 0.2f);
			// 이징: 빠르게 나타나고 살짝 오버슈트
			const float EasedScale = TextPhase < 0.6f
				? FMath::Lerp(0.f, 1.15f, TextPhase / 0.6f)
				: FMath::Lerp(1.15f, 1.0f, (TextPhase - 0.6f) / 0.4f);
			const float YScale = TextPhase < 0.6f
				? FMath::Lerp(1.5f, 0.95f, TextPhase / 0.6f)
				: FMath::Lerp(0.95f, 1.0f, (TextPhase - 0.6f) / 0.4f);
			SuccessMainText->SetRenderScale(FVector2D(EasedScale, YScale));
			SuccessMainText->SetRenderOpacity(FMath::Min(1.f, TextPhase * 3.f));
		}
	}
	// Phase 2: 0.3 ~ 0.5초 — 서브텍스트 페이드인, 글로우 안정화
	else if (T <= 0.5f)
	{
		const float Phase = (T - 0.3f) / 0.2f;

		if (SuccessGlowImage)
		{
			const float S = FMath::Lerp(1.2f, 1.0f, Phase);
			SuccessGlowImage->SetRenderScale(FVector2D(S, S));
			SuccessGlowImage->SetRenderOpacity(FMath::Lerp(0.8f, 0.6f, Phase));
		}

		if (SuccessSubText)
		{
			SuccessSubText->SetRenderOpacity(Phase);
		}
	}
	// Phase 3: 0.5 ~ 1.5초 — 유지 (글로우 펄스)
	else if (T <= 1.5f)
	{
		if (SuccessGlowImage)
		{
			const float Pulse = 1.0f + FMath::Sin((T - 0.5f) * 4.f) * 0.05f;
			SuccessGlowImage->SetRenderScale(FVector2D(Pulse, Pulse));
		}
	}
	// Phase 4: 1.5 ~ 2.5초 — 페이드아웃
	else if (T <= 2.5f)
	{
		const float Phase = (T - 1.5f) / 1.0f; // 0→1
		const float FadeOut = 1.f - Phase;

		if (SuccessGlowImage)
		{
			const float S = FMath::Lerp(1.0f, 1.5f, Phase);
			SuccessGlowImage->SetRenderScale(FVector2D(S, S));
			SuccessGlowImage->SetRenderOpacity(0.6f * FadeOut);
		}
		if (SuccessMainText)
		{
			SuccessMainText->SetRenderOpacity(FadeOut);
		}
		if (SuccessSubText)
		{
			SuccessSubText->SetRenderOpacity(FadeOut);
		}
	}
	// 완료
	else
	{
		FinishSuccessAnimation();
	}
}

void UPuzzleGridWidget::FinishSuccessAnimation()
{
	bPlayingSuccessAnim = false;

	if (SuccessOverlay)
	{
		SuccessOverlay->SetVisibility(ESlateVisibility::Collapsed);
	}

	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(SuccessAnimTimerHandle);
	}

	UE_LOG(LogTemp, Warning, TEXT("[PuzzleWidget] SuccessAnimation finished"));
}

// ============================================================================
// FAIL 애니메이션
// ============================================================================

void UPuzzleGridWidget::PlayFailAnimation()
{
	if (!FailOverlay)
	{
		// WBP에 오버레이 미구성 시 기존 텍스트 폴백
		if (StatusText)
		{
			StatusText->SetText(FText::FromString(TEXT("FAIL!")));
		}
		return;
	}

	bPlayingFailAnim = true;
	FailAnimProgress = 0.f;

	FailOverlay->SetVisibility(ESlateVisibility::SelfHitTestInvisible);

	// 초기 상태
	if (FailFlashImage)
	{
		FailFlashImage->SetRenderOpacity(0.f);
	}
	if (FailVignetteImage)
	{
		FailVignetteImage->SetRenderOpacity(0.f);
	}
	if (FailScanlineImage)
	{
		FailScanlineImage->SetRenderScale(FVector2D(0.f, 1.f));
		FailScanlineImage->SetRenderOpacity(0.f);
	}
	if (FailMainText)
	{
		FailMainText->SetRenderScale(FVector2D(1.8f, 1.8f));
		FailMainText->SetRenderOpacity(0.f);
	}
	if (FailSubText)
	{
		FailSubText->SetRenderOpacity(0.f);
	}

	// 그리드 흔들림 시작
	StartGridShake();

	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(FailAnimTimerHandle);
		World->GetTimerManager().SetTimer(
			FailAnimTimerHandle, this,
			&UPuzzleGridWidget::TickFailAnimation,
			0.016f, true);
	}

	UE_LOG(LogTemp, Warning, TEXT("[PuzzleWidget] PlayFailAnimation started"));
}

void UPuzzleGridWidget::TickFailAnimation()
{
	FailAnimProgress += 0.016f;
	const float T = FailAnimProgress;

	// Phase 1: 0.0 ~ 0.3초 — 빨간 플래시 + 텍스트 팍! 등장
	if (T <= 0.3f)
	{
		const float Phase = T / 0.3f;

		// 빨간 전체 플래시: 빠르게 번쩍 후 사라짐
		if (FailFlashImage)
		{
			float FlashOpacity;
			if (Phase < 0.3f)
			{
				FlashOpacity = Phase / 0.3f;
			}
			else
			{
				FlashOpacity = FMath::Max(0.f, 1.f - (Phase - 0.3f) / 0.7f);
			}
			FailFlashImage->SetRenderOpacity(FlashOpacity * 0.6f);
		}

		// 비네트: 올라감
		if (FailVignetteImage)
		{
			FailVignetteImage->SetRenderOpacity(FMath::Min(1.f, Phase * 1.5f) * 0.7f);
		}

		// 스캔라인 스윕
		if (FailScanlineImage)
		{
			FailScanlineImage->SetRenderScale(FVector2D(FMath::Lerp(0.f, 1.5f, Phase), 1.f));
			const float ScanOpacity = Phase < 0.5f ? Phase * 2.f : (1.f - Phase) * 2.f;
			FailScanlineImage->SetRenderOpacity(ScanOpacity);
		}

		// FAIL 텍스트: 크게 → 바운스
		if (FailMainText && T >= 0.05f)
		{
			const float TextPhase = FMath::Min(1.f, (T - 0.05f) / 0.2f);
			float Scale;
			if (TextPhase < 0.5f)
			{
				Scale = FMath::Lerp(1.8f, 0.95f, TextPhase / 0.5f);
			}
			else
			{
				Scale = FMath::Lerp(0.95f, 1.0f, (TextPhase - 0.5f) / 0.5f);
			}
			FailMainText->SetRenderScale(FVector2D(Scale, Scale));
			FailMainText->SetRenderOpacity(FMath::Min(1.f, TextPhase * 3.f));
		}
	}
	// Phase 2: 0.3 ~ 0.5초 — 서브텍스트 + 안정화
	else if (T <= 0.5f)
	{
		const float Phase = (T - 0.3f) / 0.2f;

		if (FailFlashImage)
		{
			FailFlashImage->SetRenderOpacity(FMath::Max(0.f, 0.2f - Phase * 0.2f));
		}
		if (FailVignetteImage)
		{
			FailVignetteImage->SetRenderOpacity(FMath::Lerp(0.7f, 0.5f, Phase));
		}
		if (FailSubText)
		{
			FailSubText->SetRenderOpacity(Phase);
		}
	}
	// Phase 3: 0.5 ~ 1.2초 — 유지
	else if (T <= 1.2f)
	{
		// 유지
	}
	// Phase 4: 1.2 ~ 2.0초 — 페이드아웃
	else if (T <= 2.0f)
	{
		const float Phase = (T - 1.2f) / 0.8f;
		const float FadeOut = 1.f - Phase;

		if (FailVignetteImage)
		{
			FailVignetteImage->SetRenderOpacity(0.5f * FadeOut);
		}
		if (FailMainText)
		{
			FailMainText->SetRenderOpacity(FadeOut);
		}
		if (FailSubText)
		{
			FailSubText->SetRenderOpacity(FadeOut);
		}
	}
	// 완료
	else
	{
		FinishFailAnimation();
	}
}

void UPuzzleGridWidget::FinishFailAnimation()
{
	bPlayingFailAnim = false;

	if (FailOverlay)
	{
		FailOverlay->SetVisibility(ESlateVisibility::Collapsed);
	}

	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(FailAnimTimerHandle);
	}

	UE_LOG(LogTemp, Warning, TEXT("[PuzzleWidget] FailAnimation finished"));
}

// ============================================================================
// 그리드 흔들림
// ============================================================================

void UPuzzleGridWidget::StartGridShake()
{
	if (!GridPanel)
	{
		return;
	}

	ShakeProgress = 0.f;
	GridOriginalPosition = GridPanel->GetRenderTransform().Translation;

	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(ShakeTimerHandle);
		World->GetTimerManager().SetTimer(
			ShakeTimerHandle, this,
			&UPuzzleGridWidget::TickGridShake,
			0.016f, true);
	}
}

void UPuzzleGridWidget::TickGridShake()
{
	ShakeProgress += 0.016f;

	if (ShakeProgress >= 0.5f)
	{
		StopGridShake();
		return;
	}

	if (!GridPanel)
	{
		return;
	}

	// 감쇠 진동 (시간이 갈수록 약해짐)
	const float Decay = 1.f - (ShakeProgress / 0.5f);
	const float Intensity = 8.f * Decay;
	const float Frequency = 25.f;

	const float OffsetX = FMath::Sin(ShakeProgress * Frequency * 2.f * UE_PI) * Intensity;
	const float OffsetY = FMath::Cos(ShakeProgress * Frequency * 1.7f * 2.f * UE_PI) * Intensity * 0.5f;

	FWidgetTransform Transform = GridPanel->GetRenderTransform();
	Transform.Translation = GridOriginalPosition + FVector2D(OffsetX, OffsetY);
	GridPanel->SetRenderTransform(Transform);
}

void UPuzzleGridWidget::StopGridShake()
{
	if (GridPanel)
	{
		FWidgetTransform Transform = GridPanel->GetRenderTransform();
		Transform.Translation = GridOriginalPosition;
		GridPanel->SetRenderTransform(Transform);
	}

	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(ShakeTimerHandle);
	}
}
