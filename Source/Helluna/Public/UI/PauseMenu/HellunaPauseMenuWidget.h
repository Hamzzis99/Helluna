// ════════════════════════════════════════════════════════════════════════════════
// File: Source/Helluna/Public/UI/PauseMenu/HellunaPauseMenuWidget.h
// ════════════════════════════════════════════════════════════════════════════════
//
// 일시정지 메뉴 위젯 — RE6 레온 휴대폰 스타일 UI
//
// 기능:
//   - 버튼 호버: 텍스트 색상 변경 + 왼쪽 세로 바 + 우측 › 화살표
//   - 게임 재개: PlayAnimationReverse(SlideIn) → RemoveFromParent
//   - 설정: ToggleGraphicsSettings 호출
//   - 로비 돌아가기: 경고 확인 다이얼로그 → ClientTravel
//   - 게임 종료: 확인 다이얼로그 → QuitGame
//
// 작성자: Gihyeon (Claude Code 보조)
// ════════════════════════════════════════════════════════════════════════════════

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "HellunaPauseMenuWidget.generated.h"

class UButton;
class UTextBlock;
class UImage;
class UWidgetAnimation;
class UHellunaConfirmDialogWidget;

UCLASS()
class HELLUNA_API UHellunaPauseMenuWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	virtual void NativeOnInitialized() override;
	virtual void NativeDestruct() override;

	/** 열기 애니메이션 재생 (AddToViewport 후 호출) */
	void PlayOpenAnimation();

	/** 닫기 애니메이션 → 컨트롤러 TogglePauseMenu 호출 */
	void CloseWithAnimation();

protected:
	// ════════════════════════════════════════════════════════════
	// BindWidgetOptional — 버튼
	// ════════════════════════════════════════════════════════════
	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UButton> Button_Resume;

	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UButton> Button_Settings;

	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UButton> Button_ReturnLobby;

	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UButton> Button_QuitGame;

	// ════════════════════════════════════════════════════════════
	// BindWidgetOptional — 텍스트 라벨
	// ════════════════════════════════════════════════════════════
	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> Text_Resume;

	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> Text_Settings;

	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> Text_ReturnLobby;

	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> Text_QuitGame;

	// ════════════════════════════════════════════════════════════
	// BindWidgetOptional — 호버 세로 바 (왼쪽 2px)
	// ════════════════════════════════════════════════════════════
	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UImage> Img_ResumeBar;

	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UImage> Img_SettingsBar;

	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UImage> Img_ReturnLobbyBar;

	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UImage> Img_QuitGameBar;

	// ════════════════════════════════════════════════════════════
	// BindWidgetOptional — 호버 화살표 (우측 ›)
	// ════════════════════════════════════════════════════════════
	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> Text_ResumeArrow;

	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> Text_SettingsArrow;

	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> Text_ReturnLobbyArrow;

	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> Text_QuitGameArrow;

	// ════════════════════════════════════════════════════════════
	// 확인 다이얼로그 클래스
	// ════════════════════════════════════════════════════════════
	UPROPERTY(EditDefaultsOnly, Category = "PauseMenu (일시정지 메뉴)",
		meta = (DisplayName = "Confirm Dialog Widget Class (확인 다이얼로그 위젯 클래스)"))
	TSubclassOf<UHellunaConfirmDialogWidget> ConfirmDialogWidgetClass;

private:
	// ════════════════════════════════════════════════════════════
	// 호버 이벤트
	// ════════════════════════════════════════════════════════════
	UFUNCTION() void OnResumeHovered();
	UFUNCTION() void OnResumeUnhovered();
	UFUNCTION() void OnSettingsHovered();
	UFUNCTION() void OnSettingsUnhovered();
	UFUNCTION() void OnReturnLobbyHovered();
	UFUNCTION() void OnReturnLobbyUnhovered();
	UFUNCTION() void OnQuitGameHovered();
	UFUNCTION() void OnQuitGameUnhovered();

	// ════════════════════════════════════════════════════════════
	// 클릭 이벤트
	// ════════════════════════════════════════════════════════════
	UFUNCTION() void OnResumeClicked();
	UFUNCTION() void OnSettingsClicked();
	UFUNCTION() void OnReturnLobbyClicked();
	UFUNCTION() void OnQuitGameClicked();

	// ════════════════════════════════════════════════════════════
	// 호버 헬퍼
	// ════════════════════════════════════════════════════════════
	void SetButtonHoverState(UTextBlock* Text, UImage* Bar, UTextBlock* Arrow,
		bool bHovered, bool bIsRedButton);

	// ════════════════════════════════════════════════════════════
	// 애니메이션 (BindWidgetAnimOptional — UE 표준 패턴)
	// ════════════════════════════════════════════════════════════

	/** SlideIn 애니메이션 — WBP의 Anim_SlideIn에 자동 바인딩 */
	UPROPERTY(meta = (BindWidgetAnimOptional), Transient)
	TObjectPtr<UWidgetAnimation> Anim_SlideIn;

	/** 닫기 애니메이션 완료 콜백용 델리게이트 */
	FWidgetAnimationDynamicEvent CloseAnimFinishedDelegate;

	UFUNCTION()
	void OnCloseAnimationFinished();

	/** 닫기 진행 중 플래그 (중복 호출 방지) */
	bool bClosing = false;

	// ════════════════════════════════════════════════════════════
	// 확인 다이얼로그
	// ════════════════════════════════════════════════════════════
	UPROPERTY()
	TObjectPtr<UHellunaConfirmDialogWidget> ActiveDialog;

	void ShowReturnLobbyDialog();
	void ShowQuitGameDialog();

	UFUNCTION() void OnReturnLobbyConfirmed();
	UFUNCTION() void OnQuitGameConfirmed();
	UFUNCTION() void OnDialogCancelled();

	/** 로비 복귀 처리 (GameInstance에서 LobbyURL 구성) */
	void ExecuteReturnToLobby();

	// ════════════════════════════════════════════════════════════
	// 색상 상수
	// ════════════════════════════════════════════════════════════
	static const FLinearColor CyanColor;       // #50B4FF
	static const FLinearColor RedColor;        // #FF4060
	static const FLinearColor WhiteColor;      // #FFFFFF
};
