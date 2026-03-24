// Source/Helluna/Public/Puzzle/PuzzleGridWidget.h

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "PuzzleGridWidget.generated.h"

class UUniformGridPanel;
class UTextBlock;
class UImage;
class APuzzleCubeActor;
class AHellunaHeroController;
class UPuzzleCellWidget;

/**
 * 4x4 퍼즐 그리드 위젯
 * 방향키로 셀 선택, Enter/Space로 회전, ESC로 퇴출
 */
UCLASS()
class HELLUNA_API UPuzzleGridWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	// =========================================================================================
	// BindWidgetOptional
	// =========================================================================================

	/** 4x4 셀을 담을 패널 */
	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UUniformGridPanel> GridPanel;

	/** 상태 표시 텍스트 ("퍼즐 풀기" / "해제 성공!" 등) */
	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> StatusText;

	/** 데미지 타임 카운트다운 */
	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> TimerText;

	// =========================================================================================
	// 텍스처 (에디터에서 할당)
	// =========================================================================================

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Puzzle|Textures",
		meta = (DisplayName = "Straight Pipe Texture (직선 파이프)"))
	TObjectPtr<UTexture2D> StraightPipeTexture;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Puzzle|Textures",
		meta = (DisplayName = "Curve Pipe Texture (곡선 파이프)"))
	TObjectPtr<UTexture2D> CurvePipeTexture;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Puzzle|Textures",
		meta = (DisplayName = "Start Node Texture (시작점)"))
	TObjectPtr<UTexture2D> StartNodeTexture;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Puzzle|Textures",
		meta = (DisplayName = "End Node Texture (도착점)"))
	TObjectPtr<UTexture2D> EndNodeTexture;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Puzzle|Textures",
		meta = (DisplayName = "Empty Cell Texture (빈 셀 배경)"))
	TObjectPtr<UTexture2D> EmptyCellTexture;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Puzzle|Textures",
		meta = (DisplayName = "Selected Cell Texture (선택 하이라이트)"))
	TObjectPtr<UTexture2D> SelectedCellTexture;

	// =========================================================================================
	// 공개 함수
	// =========================================================================================

	/** 큐브 참조 저장 + 4x4 그리드 생성 */
	UFUNCTION(BlueprintCallable, Category = "Puzzle")
	void InitGrid(APuzzleCubeActor* Cube);

	/** PuzzleGrid 데이터로 모든 셀 이미지 업데이트 */
	UFUNCTION()
	void RefreshGrid();

	/** Lock 상태 변경 시 콜백 */
	UFUNCTION()
	void OnLockChanged(bool bLocked);

protected:
	virtual void NativeConstruct() override;
	virtual void NativeDestruct() override;
	virtual FReply NativeOnKeyDown(const FGeometry& InGeometry, const FKeyEvent& InKeyEvent) override;

private:
	// =========================================================================================
	// 셀 선택
	// =========================================================================================

	/** 현재 선택된 셀 행/열 */
	int32 SelectedRow = 0;
	int32 SelectedCol = 0;

	/** 방향키 입력으로 선택 셀 이동 */
	void MoveSelection(FIntPoint Direction);

	/** 선택된 셀 회전 요청 */
	void RotateSelectedCell();

	/** 선택 하이라이트 갱신 */
	void UpdateSelectionHighlight();

	// =========================================================================================
	// 내부 참조
	// =========================================================================================

	/** 연결된 큐브 */
	TWeakObjectPtr<APuzzleCubeActor> OwningCube;

	/** 셀 위젯 클래스 (WBP_PuzzleCell 할당) */
	UPROPERTY(EditDefaultsOnly, Category = "Puzzle|UI",
		meta = (DisplayName = "Puzzle Cell Widget Class (퍼즐 셀 위젯 클래스)"))
	TSubclassOf<UPuzzleCellWidget> PuzzleCellWidgetClass;

	/** 셀 위젯 배열 (GridSize*GridSize) */
	UPROPERTY()
	TArray<UPuzzleCellWidget*> CellWidgets;

	/** 연결 상태 캐시 (RefreshGrid마다 갱신) */
	TSet<int32> ConnectedCells;

	/** 클라이언트 측 타이머 카운트다운 */
	float ClientTimerRemaining = 0.f;
	FTimerHandle ClientTimerHandle;

	/** 클라이언트 측 타이머 갱신 */
	void UpdateClientTimer();
};
