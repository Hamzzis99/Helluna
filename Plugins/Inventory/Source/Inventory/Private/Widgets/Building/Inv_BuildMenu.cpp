// Gihyeon's Inventory Project

#include "Widgets/Building/Inv_BuildMenu.h"
#include "Inventory.h"
#include "Components/HorizontalBox.h"
#include "Components/WidgetSwitcher.h"
#include "Components/WrapBox.h"
#include "Components/Button.h"
#include "Components/CanvasPanel.h"
#include "Components/Image.h"
#include "Components/Overlay.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Widgets/Building/Inv_BuildingButton.h"
#include "Building/Preview/Inv_BuildingPreviewActor.h"
#include "Building/Components/Inv_BuildingComponent.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Blueprint/WidgetLayoutLibrary.h"
#include "Blueprint/WidgetTree.h"
#include "GameFramework/PlayerController.h"

void UInv_BuildMenu::NativeOnInitialized()
{
	Super::NativeOnInitialized();

	// === 탭 버튼 OnClicked 바인딩 ===
	if (IsValid(Button_Support))
	{
		Button_Support->OnClicked.AddUniqueDynamic(this, &ThisClass::ShowSupport);
	}
	else
	{
		UE_LOG(LogTemp, Warning, TEXT("[BuildMenu] Button_Support 바인딩 실패 — WBP에 위젯 추가 필요"));
	}

	if (IsValid(Button_Auxiliary))
	{
		Button_Auxiliary->OnClicked.AddUniqueDynamic(this, &ThisClass::ShowAuxiliary);
	}
	else
	{
		UE_LOG(LogTemp, Warning, TEXT("[BuildMenu] Button_Auxiliary 바인딩 실패 — WBP에 위젯 추가 필요"));
	}

	if (IsValid(Button_Construction))
	{
		Button_Construction->OnClicked.AddUniqueDynamic(this, &ThisClass::ShowConstruction);
	}
	else
	{
		UE_LOG(LogTemp, Warning, TEXT("[BuildMenu] Button_Construction 바인딩 실패 — WBP에 위젯 추가 필요"));
	}

	// === Switcher 검증 ===
	if (!IsValid(Switcher))
	{
		UE_LOG(LogTemp, Warning, TEXT("[BuildMenu] Switcher 바인딩 실패 — WBP에 위젯 추가 필요"));
	}

	// === WrapBox 검증 ===
	if (!IsValid(WrapBox_Support))
	{
		UE_LOG(LogTemp, Warning, TEXT("[BuildMenu] WrapBox_Support 바인딩 실패 — WBP에 위젯 추가 필요"));
	}
	if (!IsValid(WrapBox_Auxiliary))
	{
		UE_LOG(LogTemp, Warning, TEXT("[BuildMenu] WrapBox_Auxiliary 바인딩 실패 — WBP에 위젯 추가 필요"));
	}
	if (!IsValid(WrapBox_Construction))
	{
		UE_LOG(LogTemp, Warning, TEXT("[BuildMenu] WrapBox_Construction 바인딩 실패 — WBP에 위젯 추가 필요"));
	}

	// === 디테일 패널 초기화 ===
	if (IsValid(Overlay_Detail))
	{
		Overlay_Detail->SetVisibility(ESlateVisibility::Collapsed);
	}

	if (IsValid(Button_Build))
	{
		Button_Build->OnClicked.AddUniqueDynamic(this, &ThisClass::OnBuildButtonClicked);
	}

	// Image_BuildingPreview 원본 사이즈 캐싱
	if (IsValid(Image_BuildingPreview))
	{
		CachedPreviewImageSize = Image_BuildingPreview->GetBrush().ImageSize;
	}

	// BuildingButton 델리게이트 바인딩
	BindBuildingButtonDelegates();

	// BuildCategory별로 BuildingButton을 올바른 WrapBox에 동적 배치
	DistributeBuildingButtonsToWrapBoxes();

	// === 초기 탭 = 지원 ===
	ShowSupport();

	// === 초기화 완료 로그 ===
	UE_LOG(LogTemp, Warning, TEXT("[BuildMenu] 초기화 완료: Switcher=%s, Overlay_Detail=%s, 탭 버튼 3개 바인딩"),
		IsValid(Switcher) ? TEXT("Valid") : TEXT("NULL"),
		IsValid(Overlay_Detail) ? TEXT("Valid") : TEXT("NULL"));
}

void UInv_BuildMenu::NativeTick(const FGeometry& MyGeometry, float InDeltaTime)
{
	Super::NativeTick(MyGeometry, InDeltaTime);

	// 드래그 회전 처리
	if (bIsDragging && BuildingPreviewActor.IsValid())
	{
		DragLastPosition = DragCurrentPosition;
		DragCurrentPosition = UWidgetLayoutLibrary::GetMousePositionOnViewport(GetOwningPlayer());

		const float HorizontalDelta = DragLastPosition.X - DragCurrentPosition.X;
		const float VerticalDelta = DragLastPosition.Y - DragCurrentPosition.Y;
		if (!FMath::IsNearlyZero(HorizontalDelta) || !FMath::IsNearlyZero(VerticalDelta))
		{
			BuildingPreviewActor->RotatePreview(HorizontalDelta, VerticalDelta);
		}
	}
}

// ════════════════════════════════════════════════════════════════
// 탭 전환
// ════════════════════════════════════════════════════════════════

void UInv_BuildMenu::ShowSupport()
{
	if (IsValid(WrapBox_Support) && IsValid(Button_Support))
	{
		SetActiveTab(WrapBox_Support, Button_Support);

		const int32 SupportCount = IsValid(WrapBox_Support) ? WrapBox_Support->GetChildrenCount() : 0;
		const int32 AuxiliaryCount = IsValid(WrapBox_Auxiliary) ? WrapBox_Auxiliary->GetChildrenCount() : 0;
		const int32 ConstructionCount = IsValid(WrapBox_Construction) ? WrapBox_Construction->GetChildrenCount() : 0;

		UE_LOG(LogTemp, Warning, TEXT("[BuildMenu] 탭 전환: Support (Support=%d, Auxiliary=%d, Construction=%d 개)"),
			SupportCount, AuxiliaryCount, ConstructionCount);
	}
}

void UInv_BuildMenu::ShowAuxiliary()
{
	if (IsValid(WrapBox_Auxiliary) && IsValid(Button_Auxiliary))
	{
		SetActiveTab(WrapBox_Auxiliary, Button_Auxiliary);

		const int32 SupportCount = IsValid(WrapBox_Support) ? WrapBox_Support->GetChildrenCount() : 0;
		const int32 AuxiliaryCount = IsValid(WrapBox_Auxiliary) ? WrapBox_Auxiliary->GetChildrenCount() : 0;
		const int32 ConstructionCount = IsValid(WrapBox_Construction) ? WrapBox_Construction->GetChildrenCount() : 0;

		UE_LOG(LogTemp, Warning, TEXT("[BuildMenu] 탭 전환: Auxiliary (Support=%d, Auxiliary=%d, Construction=%d 개)"),
			SupportCount, AuxiliaryCount, ConstructionCount);
	}
}

void UInv_BuildMenu::ShowConstruction()
{
	if (IsValid(WrapBox_Construction) && IsValid(Button_Construction))
	{
		SetActiveTab(WrapBox_Construction, Button_Construction);

		const int32 SupportCount = IsValid(WrapBox_Support) ? WrapBox_Support->GetChildrenCount() : 0;
		const int32 AuxiliaryCount = IsValid(WrapBox_Auxiliary) ? WrapBox_Auxiliary->GetChildrenCount() : 0;
		const int32 ConstructionCount = IsValid(WrapBox_Construction) ? WrapBox_Construction->GetChildrenCount() : 0;

		UE_LOG(LogTemp, Warning, TEXT("[BuildMenu] 탭 전환: Construction (Support=%d, Auxiliary=%d, Construction=%d 개)"),
			SupportCount, AuxiliaryCount, ConstructionCount);
	}
}

void UInv_BuildMenu::SetActiveTab(UWidget* Content, UButton* ActiveButton)
{
	if (!IsValid(Switcher) || !IsValid(Content) || !IsValid(ActiveButton))
	{
		return;
	}

	// 모든 탭 버튼 활성화
	if (IsValid(Button_Support)) { Button_Support->SetIsEnabled(true); }
	if (IsValid(Button_Auxiliary)) { Button_Auxiliary->SetIsEnabled(true); }
	if (IsValid(Button_Construction)) { Button_Construction->SetIsEnabled(true); }

	// 선택된 탭 버튼 비활성화 (강조 표시)
	ActiveButton->SetIsEnabled(false);

	// Switcher로 콘텐츠 전환
	Switcher->SetActiveWidget(Content);
}

// ════════════════════════════════════════════════════════════════
// 디테일 패널
// ════════════════════════════════════════════════════════════════

void UInv_BuildMenu::OnCardClicked(UInv_BuildingButton* ClickedButton)
{
	if (!IsValid(ClickedButton)) return;

	UE_LOG(LogTemp, Warning, TEXT("[BuildingButton] 카드 클릭 → 디테일 패널 열기 요청: %s"),
		*ClickedButton->GetName());

	OpenDetailPanel(ClickedButton);
}

void UInv_BuildMenu::OpenDetailPanel(UInv_BuildingButton* BuildingButton)
{
	if (!IsValid(BuildingButton)) return;

	// 같은 카드 다시 클릭 → 토글 (닫기)
	if (SelectedBuildingButton.IsValid() && SelectedBuildingButton.Get() == BuildingButton)
	{
		CloseDetailPanel();
		return;
	}

	// 다른 카드 클릭 시 기존 프리뷰 정리
	if (SelectedBuildingButton.IsValid())
	{
		CleanupBuildingPreview();
	}

	SelectedBuildingButton = BuildingButton;

	// 패널 보이기
	if (IsValid(Overlay_Detail))
	{
		Overlay_Detail->SetVisibility(ESlateVisibility::Visible);
	}

	// 건설물 이름
	if (IsValid(Text_DetailName))
	{
		Text_DetailName->SetText(BuildingButton->GetBuildingName());
	}

	// 건설물 설명
	if (IsValid(Text_DetailDesc))
	{
		Text_DetailDesc->SetText(BuildingButton->GetBuildingDescription());
	}

	// 3D 프리뷰 설정
	SetupBuildingPreview();

	// 재료 목록 UI 구성
	PopulateDetailMaterials();

	UE_LOG(LogTemp, Warning, TEXT("[BuildMenu] 디테일 패널 열기: %s (Mesh=%s)"),
		*BuildingButton->GetBuildingName().ToString(),
		BuildingButton->GetPreviewMesh() ? *BuildingButton->GetPreviewMesh()->GetName() : TEXT("NULL"));
}

void UInv_BuildMenu::CloseDetailPanel()
{
	CleanupBuildingPreview();

	if (IsValid(Overlay_Detail))
	{
		Overlay_Detail->SetVisibility(ESlateVisibility::Collapsed);
	}

	SelectedBuildingButton.Reset();

	UE_LOG(LogTemp, Warning, TEXT("[BuildMenu] 디테일 패널 닫기"));
}

void UInv_BuildMenu::SetupBuildingPreview()
{
	if (!SelectedBuildingButton.IsValid()) return;

	CleanupBuildingPreview();

	// 메시 가져오기: BuildableActor CDO의 GetEffectivePreviewMesh()가
	// PreviewMesh → BuildingMesh 폴백을 자동 처리
	UStaticMesh* PreviewMesh = SelectedBuildingButton->GetPreviewMesh();

	if (!IsValid(PreviewMesh))
	{
		UE_LOG(LogTemp, Warning, TEXT("[BuildMenu] 프리뷰 메시를 찾을 수 없음 — 프리뷰 영역 숨김"));
		if (IsValid(Image_BuildingPreview))
		{
			Image_BuildingPreview->SetVisibility(ESlateVisibility::Collapsed);
		}
		return;
	}

	// 프리뷰 액터 스폰
	UWorld* World = GetWorld();
	if (!IsValid(World)) return;

	FActorSpawnParameters SpawnParams;
	SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

	UClass* ClassToSpawn = BuildingPreviewActorClass
		? BuildingPreviewActorClass.Get()
		: AInv_BuildingPreviewActor::StaticClass();

	AInv_BuildingPreviewActor* NewPreview = World->SpawnActor<AInv_BuildingPreviewActor>(
		ClassToSpawn,
		FVector(0.f, 0.f, PreviewSpawnZ),
		FRotator::ZeroRotator,
		SpawnParams
	);

	if (!IsValid(NewPreview))
	{
		UE_LOG(LogTemp, Warning, TEXT("[BuildMenu] BuildingPreviewActor 스폰 실패!"));
		return;
	}

	BuildingPreviewActor = NewPreview;

	// 프리뷰 메시 설정
	NewPreview->SetPreviewMesh(
		PreviewMesh,
		SelectedBuildingButton->GetPreviewRotationOffset(),
		SelectedBuildingButton->GetPreviewCameraDistance()
	);

	// RenderTarget → Material → Image_BuildingPreview 연결
	UTextureRenderTarget2D* RT = NewPreview->GetRenderTarget();
	if (IsValid(RT) && IsValid(Image_BuildingPreview))
	{
		// 기존 M_WeaponPreview 머티리얼 재사용 (RenderTarget 텍스처 파라미터만 사용하므로 구조 동일)
		UMaterialInterface* PreviewMat = LoadObject<UMaterialInterface>(
			nullptr, TEXT("/Inventory/Widgets/Inventory/Attachment/M_WeaponPreview"));

		if (IsValid(PreviewMat))
		{
			UMaterialInstanceDynamic* MID = UMaterialInstanceDynamic::Create(PreviewMat, this);
			MID->SetTextureParameterValue(TEXT("PreviewTexture"), RT);
			Image_BuildingPreview->SetBrushFromMaterial(MID);

			// SetBrushFromMaterial이 덮어쓴 ImageSize를 원본으로 복원
			FSlateBrush FixedBrush = Image_BuildingPreview->GetBrush();
			if (!CachedPreviewImageSize.IsNearlyZero())
			{
				FixedBrush.ImageSize = CachedPreviewImageSize;
			}
			else
			{
				FixedBrush.ImageSize = FVector2D(512.f, 288.f);
			}
			Image_BuildingPreview->SetBrush(FixedBrush);

			UE_LOG(LogTemp, Warning, TEXT("[BuildMenu] 프리뷰 설정: Actor=%s, RT=%s, MID=%s"),
				*NewPreview->GetName(), *RT->GetName(), *MID->GetName());
		}
		else
		{
			// Material 로드 실패 시 직접 Brush 설정
			UE_LOG(LogTemp, Warning, TEXT("[BuildMenu] M_WeaponPreview 로드 실패! FSlateBrush 폴백"));
			FSlateBrush PreviewBrush;
			PreviewBrush.SetResourceObject(RT);
			PreviewBrush.ImageSize = CachedPreviewImageSize.IsNearlyZero()
				? FVector2D(512.f, 288.f)
				: CachedPreviewImageSize;
			PreviewBrush.DrawAs = ESlateBrushDrawType::Image;
			PreviewBrush.Tiling = ESlateBrushTileType::NoTile;
			Image_BuildingPreview->SetBrush(PreviewBrush);
		}

		Image_BuildingPreview->SetVisibility(ESlateVisibility::Visible);
	}
}

void UInv_BuildMenu::CleanupBuildingPreview()
{
	if (BuildingPreviewActor.IsValid())
	{
		BuildingPreviewActor->Destroy();
		BuildingPreviewActor.Reset();
	}

	if (IsValid(Image_BuildingPreview))
	{
		Image_BuildingPreview->SetBrush(FSlateBrush());
		Image_BuildingPreview->SetVisibility(ESlateVisibility::Collapsed);
	}

	UE_LOG(LogTemp, Warning, TEXT("[BuildMenu] 프리뷰 정리"));
}

void UInv_BuildMenu::PopulateDetailMaterials()
{
	if (!IsValid(VerticalBox_DetailMaterials)) return;
	if (!SelectedBuildingButton.IsValid()) return;

	VerticalBox_DetailMaterials->ClearChildren();

	int32 MaterialCount = 0;

	// 재료 정보를 BuildingButton에서 가져와 동적 생성
	auto AddMaterialRow = [this, &MaterialCount](const FGameplayTag& Tag, int32 Amount, UTexture2D* Icon)
	{
		if (!Tag.IsValid() || Amount <= 0) return;

		UHorizontalBox* Row = NewObject<UHorizontalBox>(this);
		if (!IsValid(Row)) return;

		// 아이콘
		if (IsValid(Icon))
		{
			UImage* IconImage = NewObject<UImage>(this);
			if (IsValid(IconImage))
			{
				IconImage->SetBrushFromTexture(Icon);
				FSlateBrush IconBrush = IconImage->GetBrush();
				IconBrush.ImageSize = FVector2D(32.f, 32.f);
				IconImage->SetBrush(IconBrush);
				Row->AddChild(IconImage);
			}
		}

		// 재료 이름 (태그에서 마지막 부분 추출)
		FString TagStr = Tag.ToString();
		int32 LastDot;
		if (TagStr.FindLastChar('.', LastDot))
		{
			TagStr = TagStr.RightChop(LastDot + 1);
		}

		UTextBlock* NameText = NewObject<UTextBlock>(this);
		if (IsValid(NameText))
		{
			NameText->SetText(FText::FromString(TagStr));
			Row->AddChild(NameText);
		}

		// 수량
		UTextBlock* AmountText = NewObject<UTextBlock>(this);
		if (IsValid(AmountText))
		{
			AmountText->SetText(FText::FromString(FString::Printf(TEXT(" x%d"), Amount)));
			Row->AddChild(AmountText);
		}

		VerticalBox_DetailMaterials->AddChild(Row);
		MaterialCount++;
	};

	AddMaterialRow(
		SelectedBuildingButton->GetRequiredMaterialTag(),
		SelectedBuildingButton->GetRequiredAmount(),
		SelectedBuildingButton->GetMaterialIcon1()
	);
	AddMaterialRow(
		SelectedBuildingButton->GetRequiredMaterialTag2(),
		SelectedBuildingButton->GetRequiredAmount2(),
		SelectedBuildingButton->GetMaterialIcon2()
	);
	AddMaterialRow(
		SelectedBuildingButton->GetRequiredMaterialTag3(),
		SelectedBuildingButton->GetRequiredAmount3(),
		SelectedBuildingButton->GetMaterialIcon3()
	);

	UE_LOG(LogTemp, Warning, TEXT("[BuildMenu] 재료 UI 생성: %d개 재료"), MaterialCount);
}

void UInv_BuildMenu::OnBuildButtonClicked()
{
	if (!SelectedBuildingButton.IsValid()) return;

	UE_LOG(LogTemp, Warning, TEXT("[BuildMenu] 건설 버튼 클릭: %s (ID=%d)"),
		*SelectedBuildingButton->GetBuildingName().ToString(),
		SelectedBuildingButton->GetBuildingID());

	// 기존 BuildingButton의 건설 로직 실행
	SelectedBuildingButton->ExecuteBuild();

	// 디테일 패널 닫기
	CloseDetailPanel();
}

// ════════════════════════════════════════════════════════════════
// 마우스 이벤트 (드래그 회전) — AttachmentPanel 패턴 동일
// ════════════════════════════════════════════════════════════════

FReply UInv_BuildMenu::NativeOnMouseButtonDown(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent)
{
	if (InMouseEvent.GetEffectingButton() == EKeys::LeftMouseButton)
	{
		// Image_BuildingPreview 영역 내에서만 드래그 시작
		if (IsValid(Image_BuildingPreview) && Image_BuildingPreview->GetVisibility() == ESlateVisibility::Visible)
		{
			const FGeometry PreviewGeometry = Image_BuildingPreview->GetCachedGeometry();
			const FVector2D LocalPos = PreviewGeometry.AbsoluteToLocal(InMouseEvent.GetScreenSpacePosition());
			const FVector2D PreviewSize = PreviewGeometry.GetLocalSize();

			if (LocalPos.X >= 0.f && LocalPos.Y >= 0.f && LocalPos.X <= PreviewSize.X && LocalPos.Y <= PreviewSize.Y)
			{
				DragCurrentPosition = UWidgetLayoutLibrary::GetMousePositionOnViewport(GetOwningPlayer());
				DragLastPosition = DragCurrentPosition;
				bIsDragging = true;

				UE_LOG(LogTemp, Warning, TEXT("[BuildMenu] 프리뷰 드래그 시작"));
				return FReply::Handled();
			}
		}

		return Super::NativeOnMouseButtonDown(InGeometry, InMouseEvent);
	}

	return Super::NativeOnMouseButtonDown(InGeometry, InMouseEvent);
}

FReply UInv_BuildMenu::NativeOnMouseButtonUp(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent)
{
	bIsDragging = false;
	return Super::NativeOnMouseButtonUp(InGeometry, InMouseEvent);
}

void UInv_BuildMenu::NativeOnMouseLeave(const FPointerEvent& InMouseEvent)
{
	Super::NativeOnMouseLeave(InMouseEvent);
	bIsDragging = false;
}

// ════════════════════════════════════════════════════════════════
// BuildingButton 델리게이트 바인딩
// ════════════════════════════════════════════════════════════════

void UInv_BuildMenu::BindBuildingButtonDelegates()
{
	if (!WidgetTree) return;

	WidgetTree->ForEachWidget([this](UWidget* Widget)
	{
		UInv_BuildingButton* Btn = Cast<UInv_BuildingButton>(Widget);
		if (IsValid(Btn))
		{
			Btn->OnCardClicked.AddUniqueDynamic(this, &ThisClass::OnCardClicked);
		}
	});
}

// ════════════════════════════════════════════════════════════════
// BuildCategory별 BuildingButton → WrapBox 동적 배치
// ════════════════════════════════════════════════════════════════
// WidgetTree를 순회하여 모든 UInv_BuildingButton을 수집한 후,
// 각 버튼의 BuildCategory(지원/보조/건설)에 따라 올바른 WrapBox로 이동.
// TArray에 먼저 수집 후 처리 — ForEachWidget 순회 중 RemoveFromParent 호출 방지.
void UInv_BuildMenu::DistributeBuildingButtonsToWrapBoxes()
{
	if (!WidgetTree) return;
	if (!IsValid(WrapBox_Support) || !IsValid(WrapBox_Auxiliary) || !IsValid(WrapBox_Construction))
	{
		UE_LOG(LogTemp, Warning, TEXT("[BuildMenu] WrapBox 바인딩 실패 - 동적 배치 건너뜀"));
		return;
	}

	int32 SupportCount = 0, AuxiliaryCount = 0, ConstructionCount = 0;

	TArray<UInv_BuildingButton*> AllButtons;
	WidgetTree->ForEachWidget([&AllButtons](UWidget* Widget)
	{
		UInv_BuildingButton* Btn = Cast<UInv_BuildingButton>(Widget);
		if (IsValid(Btn))
		{
			AllButtons.Add(Btn);
		}
	});

	for (UInv_BuildingButton* Btn : AllButtons)
	{
		Btn->RemoveFromParent();

		switch (Btn->GetBuildCategory())
		{
		case EBuildCategory::Support:
			WrapBox_Support->AddChild(Btn);
			SupportCount++;
			break;
		case EBuildCategory::Auxiliary:
			WrapBox_Auxiliary->AddChild(Btn);
			AuxiliaryCount++;
			break;
		case EBuildCategory::Construction:
			WrapBox_Construction->AddChild(Btn);
			ConstructionCount++;
			break;
		}
	}

	UE_LOG(LogTemp, Warning, TEXT("[BuildMenu] BuildingButton 동적 배치 완료: Support=%d, Auxiliary=%d, Construction=%d"),
		SupportCount, AuxiliaryCount, ConstructionCount);
}
