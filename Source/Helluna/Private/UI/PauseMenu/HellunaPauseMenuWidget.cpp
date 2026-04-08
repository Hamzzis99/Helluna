// File: Source/Helluna/Private/UI/PauseMenu/HellunaPauseMenuWidget.cpp
// ════════════════════════════════════════════════════════════════════════════════
// 일시정지 메뉴 위젯 구현
// ════════════════════════════════════════════════════════════════════════════════

#include "UI/PauseMenu/HellunaPauseMenuWidget.h"
#include "UI/PauseMenu/HellunaConfirmDialogWidget.h"
#include "Components/Button.h"
#include "Components/TextBlock.h"
#include "Components/Image.h"
#include "Animation/WidgetAnimation.h"
#include "Blueprint/WidgetBlueprintGeneratedClass.h"
#include "Kismet/KismetSystemLibrary.h"
#include "Controller/HellunaHeroController.h"
#include "MDF_Function/MDF_Instance/MDF_GameInstance.h"

// ═══════════════════════════════════════════════════════════════════
// 색상 상수
// ═══════════════════════════════════════════════════════════════════
const FLinearColor UHellunaPauseMenuWidget::CyanColor  = FLinearColor(0.314f, 0.706f, 1.0f, 1.0f);  // #50B4FF
const FLinearColor UHellunaPauseMenuWidget::RedColor   = FLinearColor(1.0f, 0.251f, 0.376f, 1.0f);   // #FF4060
const FLinearColor UHellunaPauseMenuWidget::WhiteColor = FLinearColor::White;

// ═══════════════════════════════════════════════════════════════════
// 초기화
// ═══════════════════════════════════════════════════════════════════

void UHellunaPauseMenuWidget::NativeOnInitialized()
{
	Super::NativeOnInitialized();

	// ── 버튼 이벤트 바인딩 ──
	if (IsValid(Button_Resume))
	{
		Button_Resume->OnHovered.AddUniqueDynamic(this, &ThisClass::OnResumeHovered);
		Button_Resume->OnUnhovered.AddUniqueDynamic(this, &ThisClass::OnResumeUnhovered);
		Button_Resume->OnClicked.AddUniqueDynamic(this, &ThisClass::OnResumeClicked);
	}
	if (IsValid(Button_Settings))
	{
		Button_Settings->OnHovered.AddUniqueDynamic(this, &ThisClass::OnSettingsHovered);
		Button_Settings->OnUnhovered.AddUniqueDynamic(this, &ThisClass::OnSettingsUnhovered);
		Button_Settings->OnClicked.AddUniqueDynamic(this, &ThisClass::OnSettingsClicked);
	}
	if (IsValid(Button_ReturnLobby))
	{
		Button_ReturnLobby->OnHovered.AddUniqueDynamic(this, &ThisClass::OnReturnLobbyHovered);
		Button_ReturnLobby->OnUnhovered.AddUniqueDynamic(this, &ThisClass::OnReturnLobbyUnhovered);
		Button_ReturnLobby->OnClicked.AddUniqueDynamic(this, &ThisClass::OnReturnLobbyClicked);
	}
	if (IsValid(Button_QuitGame))
	{
		Button_QuitGame->OnHovered.AddUniqueDynamic(this, &ThisClass::OnQuitGameHovered);
		Button_QuitGame->OnUnhovered.AddUniqueDynamic(this, &ThisClass::OnQuitGameUnhovered);
		Button_QuitGame->OnClicked.AddUniqueDynamic(this, &ThisClass::OnQuitGameClicked);
	}

	// ── 호버 바/화살표 초기 상태: 투명 (공간은 유지하여 정렬 안정) ──
	auto HideByOpacity = [](UWidget* W)
	{
		if (IsValid(W))
		{
			W->SetVisibility(ESlateVisibility::HitTestInvisible);
			W->SetRenderOpacity(0.0f);
		}
	};
	HideByOpacity(Img_ResumeBar);
	HideByOpacity(Img_SettingsBar);
	HideByOpacity(Img_ReturnLobbyBar);
	HideByOpacity(Img_QuitGameBar);
	HideByOpacity(Text_ResumeArrow);
	HideByOpacity(Text_SettingsArrow);
	HideByOpacity(Text_ReturnLobbyArrow);
	HideByOpacity(Text_QuitGameArrow);

	// ── SlideIn 애니메이션: BindWidgetAnimOptional로 자동 바인딩 ──
	UE_LOG(LogTemp, Log, TEXT("[PauseMenu] NativeOnInitialized — Anim_SlideIn=%s"),
		IsValid(Anim_SlideIn) ? TEXT("Bound") : TEXT("NotBound"));
}

void UHellunaPauseMenuWidget::NativeDestruct()
{
	// 활성 다이얼로그 정리
	if (IsValid(ActiveDialog))
	{
		ActiveDialog->RemoveFromParent();
		ActiveDialog = nullptr;
	}

	Super::NativeDestruct();
}

// ═══════════════════════════════════════════════════════════════════
// 애니메이션
// ═══════════════════════════════════════════════════════════════════

void UHellunaPauseMenuWidget::PlayOpenAnimation()
{
	// 애니메이션 제거됨 — 즉시 표시
}

void UHellunaPauseMenuWidget::CloseWithAnimation()
{
	if (bClosing) return;
	bClosing = true;

	if (APlayerController* PC = GetOwningPlayer())
	{
		if (AHellunaHeroController* HC = Cast<AHellunaHeroController>(PC))
		{
			HC->ClearPauseMenuInstance();
		}
		else
		{
			PC->SetShowMouseCursor(false);
			PC->SetInputMode(FInputModeGameOnly());
		}
	}
	RemoveFromParent();
}

void UHellunaPauseMenuWidget::OnCloseAnimationFinished()
{
	// 미사용 — 애니메이션 제거됨
}


// ═══════════════════════════════════════════════════════════════════
// 호버 헬퍼
// ═══════════════════════════════════════════════════════════════════

void UHellunaPauseMenuWidget::SetButtonHoverState(
	UTextBlock* Text, UImage* Bar, UTextBlock* Arrow,
	bool bHovered, bool bIsRedButton)
{
	// 작업 1: 텍스트 색상 변경
	if (IsValid(Text))
	{
		if (bHovered)
		{
			Text->SetColorAndOpacity(FSlateColor(bIsRedButton ? RedColor : CyanColor));
		}
		else
		{
			// 게임 종료 버튼은 기본 빨간색, 나머지는 흰색
			Text->SetColorAndOpacity(FSlateColor(bIsRedButton ? RedColor : WhiteColor));
		}
	}

	// 작업 2: 왼쪽 세로 바 표시/숨김 (RenderOpacity로 레이아웃 유지)
	if (IsValid(Bar))
	{
		Bar->SetRenderOpacity(bHovered ? 1.0f : 0.0f);
	}

	// 작업 3: 우측 › 화살표 표시/숨김 (RenderOpacity로 레이아웃 유지)
	if (IsValid(Arrow))
	{
		Arrow->SetRenderOpacity(bHovered ? 1.0f : 0.0f);
	}
}

// ═══════════════════════════════════════════════════════════════════
// 호버 이벤트 (작업 1~3)
// ═══════════════════════════════════════════════════════════════════

void UHellunaPauseMenuWidget::OnResumeHovered()
{
	SetButtonHoverState(Text_Resume, Img_ResumeBar, Text_ResumeArrow, true, false);
}
void UHellunaPauseMenuWidget::OnResumeUnhovered()
{
	SetButtonHoverState(Text_Resume, Img_ResumeBar, Text_ResumeArrow, false, false);
}
void UHellunaPauseMenuWidget::OnSettingsHovered()
{
	SetButtonHoverState(Text_Settings, Img_SettingsBar, Text_SettingsArrow, true, false);
}
void UHellunaPauseMenuWidget::OnSettingsUnhovered()
{
	SetButtonHoverState(Text_Settings, Img_SettingsBar, Text_SettingsArrow, false, false);
}
void UHellunaPauseMenuWidget::OnReturnLobbyHovered()
{
	SetButtonHoverState(Text_ReturnLobby, Img_ReturnLobbyBar, Text_ReturnLobbyArrow, true, false);
}
void UHellunaPauseMenuWidget::OnReturnLobbyUnhovered()
{
	SetButtonHoverState(Text_ReturnLobby, Img_ReturnLobbyBar, Text_ReturnLobbyArrow, false, false);
}
void UHellunaPauseMenuWidget::OnQuitGameHovered()
{
	SetButtonHoverState(Text_QuitGame, Img_QuitGameBar, Text_QuitGameArrow, true, true);
}
void UHellunaPauseMenuWidget::OnQuitGameUnhovered()
{
	SetButtonHoverState(Text_QuitGame, Img_QuitGameBar, Text_QuitGameArrow, false, true);
}

// ═══════════════════════════════════════════════════════════════════
// 클릭 이벤트 (작업 4~7)
// ═══════════════════════════════════════════════════════════════════

void UHellunaPauseMenuWidget::OnResumeClicked()
{
	// 작업 4: 게임 재개 — 역재생 애니메이션 → 닫기
	CloseWithAnimation();
}

void UHellunaPauseMenuWidget::OnSettingsClicked()
{
	// 일시정지 메뉴 닫고 → 그래픽 설정 열기 (커서/입력 모드는 건드리지 않음)
	if (APlayerController* PC = GetOwningPlayer())
	{
		if (AHellunaHeroController* HC = Cast<AHellunaHeroController>(PC))
		{
			// 인스턴스 레퍼런스만 해제 (커서/입력 복원 안 함 — 그래픽 설정이 이어받음)
			HC->ClearPauseMenuInstanceOnly();
			RemoveFromParent();

			// 그래픽 설정 열기 (내부에서 커서/입력 모드 설정함)
			HC->ToggleGraphicsSettings();
		}
	}
}

void UHellunaPauseMenuWidget::OnReturnLobbyClicked()
{
	// 작업 6: 확인 다이얼로그 → 로비 이동
	ShowReturnLobbyDialog();
}

void UHellunaPauseMenuWidget::OnQuitGameClicked()
{
	// 작업 7: 확인 다이얼로그 → 게임 종료
	ShowQuitGameDialog();
}

// ═══════════════════════════════════════════════════════════════════
// 확인 다이얼로그 (작업 6~8)
// ═══════════════════════════════════════════════════════════════════

void UHellunaPauseMenuWidget::ShowReturnLobbyDialog()
{
	if (!IsValid(ConfirmDialogWidgetClass))
	{
		UE_LOG(LogTemp, Warning, TEXT("[PauseMenu] ConfirmDialogWidgetClass 미설정"));
		return;
	}

	// 기존 다이얼로그 정리
	if (IsValid(ActiveDialog))
	{
		ActiveDialog->OnConfirmed.RemoveAll(this);
		ActiveDialog->OnCancelled.RemoveAll(this);
		ActiveDialog->RemoveFromParent();
		ActiveDialog = nullptr;
	}

	ActiveDialog = CreateWidget<UHellunaConfirmDialogWidget>(GetOwningPlayer(), ConfirmDialogWidgetClass);
	if (!IsValid(ActiveDialog)) return;

	ActiveDialog->SetDialogContent(
		FText::FromString(TEXT("\u26A0")),  // ⚠
		FText::FromString(TEXT("\uB85C\uBE44\uB85C \uB3CC\uC544\uAC00\uC2DC\uACA0\uC2B5\uB2C8\uAE4C?")),  // 로비로 돌아가시겠습니까?
		FText::FromString(TEXT("\u26A0 \uC778\uBCA4\uD1A0\uB9AC\uC5D0 \uC788\uB294 \uBAA8\uB4E0 \uC544\uC774\uD15C\uC744 \uC783\uAC8C \uB429\uB2C8\uB2E4.\n\uC815\uB9D0 \uB098\uAC00\uC2DC\uACA0\uC2B5\uB2C8\uAE4C?")),  // ⚠ 인벤토리에 있는 모든 아이템을 잃게 됩니다.\n정말 나가시겠습니까?
		true  // bIsWarning — 설명 빨간색
	);
	ActiveDialog->OnConfirmed.AddUniqueDynamic(this, &ThisClass::OnReturnLobbyConfirmed);
	ActiveDialog->OnCancelled.AddUniqueDynamic(this, &ThisClass::OnDialogCancelled);
	ActiveDialog->AddToViewport(300);
	ActiveDialog->PlayShowAnimation();
}

void UHellunaPauseMenuWidget::ShowQuitGameDialog()
{
	if (!IsValid(ConfirmDialogWidgetClass))
	{
		UE_LOG(LogTemp, Warning, TEXT("[PauseMenu] ConfirmDialogWidgetClass 미설정"));
		return;
	}

	if (IsValid(ActiveDialog))
	{
		ActiveDialog->OnConfirmed.RemoveAll(this);
		ActiveDialog->OnCancelled.RemoveAll(this);
		ActiveDialog->RemoveFromParent();
		ActiveDialog = nullptr;
	}

	ActiveDialog = CreateWidget<UHellunaConfirmDialogWidget>(GetOwningPlayer(), ConfirmDialogWidgetClass);
	if (!IsValid(ActiveDialog)) return;

	ActiveDialog->SetDialogContent(
		FText::FromString(TEXT("\u2715")),  // ✕
		FText::FromString(TEXT("\uAC8C\uC784\uC744 \uC885\uB8CC\uD558\uC2DC\uACA0\uC2B5\uB2C8\uAE4C?")),  // 게임을 종료하시겠습니까?
		FText::FromString(TEXT("\uC800\uC7A5\uB418\uC9C0 \uC54A\uC740 \uC9C4\uD589 \uC0C1\uD669\uC740 \uC0AC\uB77C\uC9D1\uB2C8\uB2E4.")),  // 저장되지 않은 진행 상황은 사라집니다.
		false
	);
	ActiveDialog->OnConfirmed.AddUniqueDynamic(this, &ThisClass::OnQuitGameConfirmed);
	ActiveDialog->OnCancelled.AddUniqueDynamic(this, &ThisClass::OnDialogCancelled);
	ActiveDialog->AddToViewport(300);
	ActiveDialog->PlayShowAnimation();
}

void UHellunaPauseMenuWidget::OnReturnLobbyConfirmed()
{
	if (IsValid(ActiveDialog))
	{
		ActiveDialog->RemoveFromParent();
		ActiveDialog = nullptr;
	}
	ExecuteReturnToLobby();
}

void UHellunaPauseMenuWidget::OnQuitGameConfirmed()
{
	if (IsValid(ActiveDialog))
	{
		ActiveDialog->RemoveFromParent();
		ActiveDialog = nullptr;
	}

	UWorld* World = GetWorld();
	if (!IsValid(World)) return;

	UKismetSystemLibrary::QuitGame(World, GetOwningPlayer(), EQuitPreference::Quit, false);
}

void UHellunaPauseMenuWidget::OnDialogCancelled()
{
	if (IsValid(ActiveDialog))
	{
		ActiveDialog->RemoveFromParent();
		ActiveDialog = nullptr;
	}
}

// ═══════════════════════════════════════════════════════════════════
// 로비 복귀 (GameResultWidget 패턴 재사용)
// ═══════════════════════════════════════════════════════════════════

void UHellunaPauseMenuWidget::ExecuteReturnToLobby()
{
	APlayerController* PC = GetOwningPlayer();
	if (!IsValid(PC)) return;

	// GameInstance에서 로비 URL 구성
	FString LobbyURL;
	if (UMDF_GameInstance* GI = Cast<UMDF_GameInstance>(PC->GetGameInstance()))
	{
		if (!GI->ConnectedServerIP.IsEmpty())
		{
			LobbyURL = FString::Printf(TEXT("%s:%d"), *GI->ConnectedServerIP, GI->LobbyServerPort);
		}
	}

	if (LobbyURL.IsEmpty())
	{
		UE_LOG(LogTemp, Error, TEXT("[PauseMenu] ReturnToLobby: LobbyURL 구성 실패 — ConnectedServerIP 없음"));
		return;
	}

	UE_LOG(LogTemp, Log, TEXT("[PauseMenu] ReturnToLobby: ClientTravel → %s"), *LobbyURL);

	RemoveFromParent();
	PC->ConsoleCommand(FString::Printf(TEXT("open %s"), *LobbyURL));
}
