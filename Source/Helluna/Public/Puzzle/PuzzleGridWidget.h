// Source/Helluna/Public/Puzzle/PuzzleGridWidget.h

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "PuzzleGridWidget.generated.h"

class UUniformGridPanel;
class UCanvasPanel;
class UTextBlock;
class UImage;
class APuzzleCubeActor;
class AHellunaHeroController;
class UPuzzleCellWidget;

/**
 * 퍼즐 그리드 위젯 — 4×4 에너지 회로 퍼즐 UI
 * 방향키로 셀 선택, E키로 회전, ESC로 퇴출
 *
 * [보스전 로드맵]
 * 보스 버전에서도 이 위젯을 그대로 사용.
 * PuzzleShieldComponent가 PuzzleCubeActor 역할을 대체하므로
 * OwningCube → OwningShieldComponent로 참조만 변경하면 됨.
 * SUCCESS 후 전원 HUD에 "보호막 해제!" 알림 + 딜타임(5분) 카운트다운 표시 필요.
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
	// SUCCESS 애니메이션 위젯
	// =========================================================================================

	/** 성공 오버레이 (전체 화면, 기본 Collapsed) */
	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UCanvasPanel> SuccessOverlay;

	/** 방사형 시안 글로우 */
	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UImage> SuccessGlowImage;

	/** 상단 스캔라인 */
	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UImage> ScanlineTopImage;

	/** 하단 스캔라인 */
	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UImage> ScanlineBottomImage;

	/** "SUCCESS" 메인 텍스트 */
	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> SuccessMainText;

	/** "PUZZLE UNLOCKED" 서브 텍스트 */
	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> SuccessSubText;

	// =========================================================================================
	// FAIL 애니메이션 위젯
	// =========================================================================================

	/** 실패 오버레이 (전체 화면, 기본 Collapsed) */
	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UCanvasPanel> FailOverlay;

	/** 빨간 전체 플래시 */
	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UImage> FailFlashImage;

	/** 빨간 비네트 (가장자리) */
	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UImage> FailVignetteImage;

	/** 빨간 스캔라인 */
	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UImage> FailScanlineImage;

	/** "FAIL" 메인 텍스트 */
	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> FailMainText;

	/** "TIME EXPIRED" 서브 텍스트 */
	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> FailSubText;

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

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Puzzle|Textures",
		meta = (DisplayName = "Select Burst FX Texture (이동 이펙트)"))
	TObjectPtr<UTexture2D> SelectBurstTexture;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Puzzle|Textures",
		meta = (DisplayName = "Rotate Flash FX Texture (회전 이펙트)"))
	TObjectPtr<UTexture2D> RotateFlashTexture;

	// =========================================================================================
	// 등장/퇴장 애니메이션 (Controller에서 호출)
	// =========================================================================================

	/** 퇴장 애니메이션 재생 (ExitPuzzle에서 호출) */
	void PlayCloseAnimation();

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

	/** 방향키 입력으로 선택 셀 이동 (Controller에서 호출) */
	void MoveSelection(FIntPoint Direction);

	/** 선택된 셀 회전 요청 (Controller에서 호출) */
	void RotateSelectedCell();

	/** 선택 행/열 getter */
	int32 GetSelectedRow() const { return SelectedRow; }
	int32 GetSelectedCol() const { return SelectedCol; }

protected:
	virtual void NativeConstruct() override;
	virtual void NativeDestruct() override;

private:
	// =========================================================================================
	// 셀 선택
	// =========================================================================================

	/** 현재 선택된 셀 행/열 */
	int32 SelectedRow = 0;
	int32 SelectedCol = 0;

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

	/** 클라이언트 측 데미지 타이머 카운트다운 */
	float ClientTimerRemaining = 0.f;
	FTimerHandle ClientTimerHandle;

	/** 클라이언트 측 데미지 타이머 갱신 */
	void UpdateClientTimer();

	// =========================================================================================
	// 퍼즐 제한시간 카운트다운
	// =========================================================================================

	/** 퍼즐 카운트다운 타이머 */
	FTimerHandle PuzzleCountdownTimerHandle;
	float PuzzleTimeRemaining = 30.f;

	/** FAIL 표시 중 입력 차단 */
	bool bShowingFail = false;

	/** 카운트다운 시작 */
	void StartCountdown(float TimeLimit);

	/** 매초 카운트다운 업데이트 */
	void TickCountdown();

	/** 시간 초과 시 FAIL 표시 */
	void ShowFailMessage();

	/** 타임아웃 델리게이트 핸들러 */
	UFUNCTION()
	void OnPuzzleTimedOut();

	// =========================================================================================
	// SUCCESS 애니메이션
	// =========================================================================================

	/** SUCCESS 애니메이션 타이머 */
	FTimerHandle SuccessAnimTimerHandle;
	float SuccessAnimProgress = 0.f;
	bool bPlayingSuccessAnim = false;

	/** SUCCESS 애니메이션 재생 */
	void PlaySuccessAnimation();

	/** SUCCESS 애니메이션 프레임 업데이트 */
	void TickSuccessAnimation();

	/** SUCCESS 애니메이션 종료 */
	void FinishSuccessAnimation();

	// =========================================================================================
	// FAIL 애니메이션
	// =========================================================================================

	/** FAIL 애니메이션 타이머 */
	FTimerHandle FailAnimTimerHandle;
	float FailAnimProgress = 0.f;
	bool bPlayingFailAnim = false;

	/** FAIL 애니메이션 재생 */
	void PlayFailAnimation();

	/** FAIL 애니메이션 프레임 업데이트 */
	void TickFailAnimation();

	/** FAIL 애니메이션 종료 */
	void FinishFailAnimation();

	// =========================================================================================
	// 그리드 흔들림
	// =========================================================================================

	/** 흔들림 타이머 */
	FTimerHandle ShakeTimerHandle;
	float ShakeProgress = 0.f;

	/** GridPanel 원래 위치 저장 */
	FVector2D GridOriginalPosition;

	/** 그리드 흔들림 시작 */
	void StartGridShake();

	/** 그리드 흔들림 프레임 업데이트 */
	void TickGridShake();

	/** 그리드 흔들림 정지 + 위치 복원 */
	void StopGridShake();

	// =========================================================================================
	// 등장/퇴장 애니메이션
	// =========================================================================================

	/** 등장 애니메이션 타이머 */
	FTimerHandle OpenAnimTimerHandle;
	float OpenAnimProgress = 0.f;
	bool bPlayingOpenAnim = false;

	/** 퇴장 애니메이션 타이머 */
	FTimerHandle CloseAnimTimerHandle;
	float CloseAnimProgress = 0.f;
	bool bPlayingCloseAnim = false;

	/** 등장 애니메이션 재생 */
	void PlayOpenAnimation();

	/** 등장 애니메이션 프레임 업데이트 */
	void TickOpenAnimation();

	/** 등장 애니메이션 종료 */
	void FinishOpenAnimation();

	/** 퇴장 애니메이션 프레임 업데이트 */
	void TickCloseAnimation();

	/** 퇴장 애니메이션 종료 */
	void FinishCloseAnimation();
};
