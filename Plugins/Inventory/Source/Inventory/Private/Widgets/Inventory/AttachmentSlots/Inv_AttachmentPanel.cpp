// Gihyeon's Inventory Project
//
// ════════════════════════════════════════════════════════════════════════════════
// 📌 부착물 패널 위젯 (Attachment Panel) — Phase 8 리뉴얼
// ════════════════════════════════════════════════════════════════════════════════
//
// 📌 핵심 흐름:
//    OpenForWeapon → SetupWeaponPreview + BuildSlotWidgets → 십자형 배치
//    좌클릭 → TryAttachHoverItem → Server_AttachItemToWeapon
//    우클릭 → TryDetachItem → Server_DetachItemFromWeapon
//    NativeTick → UpdateSlotHighlights + 드래그 회전
//    ClosePanel → CleanupWeaponPreview + ClearSlotWidgets → 패널 숨기기
//
// ════════════════════════════════════════════════════════════════════════════════

#include "Widgets/Inventory/AttachmentSlots/Inv_AttachmentPanel.h"
#include "Inventory.h"  // INV_DEBUG_ATTACHMENT 매크로

#include "Widgets/Inventory/AttachmentSlots/Inv_AttachmentSlotWidget.h"
#include "Widgets/Inventory/Spatial/Inv_InventoryGrid.h"
#include "Widgets/Inventory/HoverItem/Inv_HoverItem.h"
#include "Items/Inv_InventoryItem.h"
#include "Items/Fragments/Inv_AttachmentFragments.h"
#include "Items/Fragments/Inv_ItemFragment.h"
#include "InventoryManagement/Components/Inv_InventoryComponent.h"
#include "Components/Button.h"
#include "Components/TextBlock.h"
#include "Components/Image.h"
#include "Components/VerticalBox.h"
#include "Blueprint/WidgetLayoutLibrary.h"
#include "Blueprint/WidgetTree.h"
#include "Interaction/Preview/Inv_WeaponPreviewActor.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Engine/StaticMesh.h"
#include "Materials/MaterialInstanceDynamic.h"

// ════════════════════════════════════════════════════════════════
// 📌 NativeOnInitialized — 위젯 초기화
// ════════════════════════════════════════════════════════════════
void UInv_AttachmentPanel::NativeOnInitialized()
{
	Super::NativeOnInitialized();

	// 닫기 버튼 클릭 바인딩
	if (IsValid(Button_Close))
	{
		Button_Close->OnClicked.AddDynamic(this, &ThisClass::OnCloseButtonClicked);
	}

	// WBP에 배치된 슬롯 위젯 자동 수집
	CollectSlotWidgetsFromTree();

	// WBP에서 디자이너가 지정한 Image_WeaponPreview의 Brush.ImageSize를 캐싱
	// NativeOnInitialized는 Visibility와 무관하게 위젯 생성 시 항상 호출됨
	// (NativeConstruct는 Collapsed→Visible 전환 시점이라 SetupWeaponPreview보다 늦음)
	if (IsValid(Image_WeaponPreview))
	{
		CachedPreviewImageSize = Image_WeaponPreview->GetBrush().ImageSize;
		UE_LOG(LogTemp, Log, TEXT("[Attachment UI] CachedPreviewImageSize = (%.1f, %.1f)"),
			CachedPreviewImageSize.X, CachedPreviewImageSize.Y);
	}
}

// ════════════════════════════════════════════════════════════════
// 📌 NativeConstruct
// ════════════════════════════════════════════════════════════════
// ⚠️ CachedPreviewImageSize 캐싱은 여기서 하지 않음!
//    이유: 위젯이 초기 Collapsed 상태일 때 NativeConstruct는
//    SetVisibility(Visible) 시점에야 호출되므로,
//    그보다 먼저 실행되는 SetupWeaponPreview()에서 캐싱값이 (0,0)이 됨.
//    → 캐싱은 NativeOnInitialized에서 수행.
void UInv_AttachmentPanel::NativeConstruct()
{
	Super::NativeConstruct();
}

// ════════════════════════════════════════════════════════════════
// 📌 NativeTick — 매 프레임 호출 (하이라이트 + 드래그 회전)
// ════════════════════════════════════════════════════════════════
void UInv_AttachmentPanel::NativeTick(const FGeometry& MyGeometry, float InDeltaTime)
{
	Super::NativeTick(MyGeometry, InDeltaTime);

	if (!bIsOpen) return;

	UpdateSlotHighlights();

	// Phase 8: 드래그 회전 처리
	if (bIsDragging && WeaponPreviewActor.IsValid())
	{
		DragLastPosition = DragCurrentPosition;
		DragCurrentPosition = UWidgetLayoutLibrary::GetMousePositionOnViewport(GetOwningPlayer());

		const float HorizontalDelta = DragLastPosition.X - DragCurrentPosition.X;
		const float VerticalDelta = DragLastPosition.Y - DragCurrentPosition.Y;
		if (!FMath::IsNearlyZero(HorizontalDelta) || !FMath::IsNearlyZero(VerticalDelta))
		{
			WeaponPreviewActor->RotatePreview(HorizontalDelta, VerticalDelta);
		}
	}
}

// ════════════════════════════════════════════════════════════════
// 📌 SetInventoryComponent — 인벤토리 컴포넌트 참조 설정
// ════════════════════════════════════════════════════════════════
void UInv_AttachmentPanel::SetInventoryComponent(UInv_InventoryComponent* InvComp)
{
	InventoryComponent = InvComp;
}

// ════════════════════════════════════════════════════════════════
// 📌 SetOwningGrid — 소유 Grid 참조 설정
// ════════════════════════════════════════════════════════════════
void UInv_AttachmentPanel::SetOwningGrid(UInv_InventoryGrid* Grid)
{
	OwningGrid = Grid;
}

// ════════════════════════════════════════════════════════════════
// 📌 OpenForWeapon — 무기 아이템의 부착물 슬롯을 패널에 표시
// ════════════════════════════════════════════════════════════════
// 호출 경로: InventoryGrid::OpenAttachmentPanel → 이 함수
// 처리 흐름:
//   1. CurrentWeaponItem, CurrentWeaponEntryIndex 캐시
//   2. 무기 아이콘 → Image_WeaponIcon 설정
//   3. 무기 이름 → Text_WeaponName 설정
//   4. BuildSlotWidgets() 호출
//   5. SetVisibility(Visible), bIsOpen = true
// 실패 조건: WeaponItem이 nullptr이거나 부착물 슬롯이 없을 때
// ════════════════════════════════════════════════════════════════
void UInv_AttachmentPanel::OpenForWeapon(UInv_InventoryItem* WeaponItem, int32 WeaponEntryIndex)
{
	if (!IsValid(WeaponItem))
	{
		UE_LOG(LogTemp, Warning, TEXT("[Attachment UI] OpenForWeapon 실패: WeaponItem이 nullptr"));
		return;
	}

	if (!WeaponItem->HasAttachmentSlots())
	{
		UE_LOG(LogTemp, Warning, TEXT("[Attachment UI] OpenForWeapon 실패: 부착물 슬롯이 없는 아이템"));
		return;
	}

	CurrentWeaponItem = WeaponItem;
	CurrentWeaponEntryIndex = WeaponEntryIndex;

	// 무기 아이콘 설정
	const FInv_ImageFragment* ImageFrag = WeaponItem->GetItemManifest().GetFragmentOfType<FInv_ImageFragment>();
	if (ImageFrag && IsValid(Image_WeaponIcon))
	{
		UTexture2D* Icon = ImageFrag->GetIcon();
		if (IsValid(Icon))
		{
			FSlateBrush WeaponBrush;
			WeaponBrush.SetResourceObject(Icon);
			WeaponBrush.ImageSize = FVector2D(48.f, 48.f);
			Image_WeaponIcon->SetBrush(WeaponBrush);
		}
	}

	// 무기 이름 설정
	if (IsValid(Text_WeaponName))
	{
		const FInv_TextFragment* TextFrag = WeaponItem->GetItemManifest().GetFragmentOfType<FInv_TextFragment>();
		if (TextFrag)
		{
			Text_WeaponName->SetText(TextFrag->GetText());
		}
		else
		{
			Text_WeaponName->SetText(FText::FromString(TEXT("무기")));
		}
	}

	// 슬롯 위젯 생성
	BuildSlotWidgets();

	// Phase 8: 3D 무기 프리뷰 설정
	SetupWeaponPreview();

	// 패널 보이기
	SetVisibility(ESlateVisibility::Visible);
	bIsOpen = true;

#if INV_DEBUG_ATTACHMENT
	// ★ [부착진단-패널] OpenForWeapon: 패널 열기 시 부착물 데이터 상태 ★
	{
		const FInv_AttachmentHostFragment* DiagHost =
			WeaponItem->GetItemManifest().GetFragmentOfType<FInv_AttachmentHostFragment>();
		UE_LOG(LogTemp, Error, TEXT("[부착진단-패널] OpenForWeapon: WeaponItem=%s, HostFrag=%s, AttachedItems=%d, EntryIndex=%d"),
			*WeaponItem->GetItemManifest().GetItemType().ToString(),
			DiagHost ? TEXT("유효") : TEXT("nullptr"),
			DiagHost ? DiagHost->GetAttachedItems().Num() : -1,
			WeaponEntryIndex);
	}

	UE_LOG(LogTemp, Log, TEXT("[Attachment UI] 패널 열림: 무기=%s, 슬롯=%d개, EntryIndex=%d"),
		*WeaponItem->GetItemManifest().GetItemType().ToString(),
		WeaponItem->GetAttachmentSlotCount(),
		WeaponEntryIndex);
#endif
}

// ════════════════════════════════════════════════════════════════
// 📌 ClosePanel — 패널 닫기 및 정리
// ════════════════════════════════════════════════════════════════
// 호출 경로: OnCloseButtonClicked / InventoryGrid::CloseAttachmentPanel → 이 함수
// 처리 흐름:
//   1. ClearSlotWidgets() — 슬롯 위젯 정리
//   2. 상태 초기화
//   3. SetVisibility(Collapsed)
//   4. OnPanelClosed 브로드캐스트
// ════════════════════════════════════════════════════════════════
void UInv_AttachmentPanel::ClosePanel()
{
	CleanupWeaponPreview();
	ClearSlotWidgets();
	bIsDragging = false;
	bIsOpen = false;
	CurrentWeaponItem.Reset();
	CurrentWeaponEntryIndex = INDEX_NONE;

	SetVisibility(ESlateVisibility::Collapsed);

	OnPanelClosed.Broadcast();

#if INV_DEBUG_ATTACHMENT
	UE_LOG(LogTemp, Log, TEXT("[Attachment UI] 패널 닫힘"));
#endif
}

// ════════════════════════════════════════════════════════════════
// 📌 BuildSlotWidgets — WBP BindWidget 슬롯을 무기 데이터로 초기화
// ════════════════════════════════════════════════════════════════
// 호출 경로: OpenForWeapon → 이 함수
// 처리 흐름:
//   1. ClearSlotWidgets() — 이전 델리게이트 해제
//   2. ResetAllSlots() — 4개 슬롯 전부 Hidden + SetEmpty
//   3. SlotDef의 SlotType → DerivePositionFromSlotType → GetSlotWidgetForPosition
//   4. 해당 BindWidget 슬롯이 있으면 InitSlot + Visible + 델리게이트 바인딩
// ════════════════════════════════════════════════════════════════
void UInv_AttachmentPanel::BuildSlotWidgets()
{
	ClearSlotWidgets();
	ResetAllSlots();

	// ★ [디버그] 수집된 슬롯 위젯 상태 확인
	UE_LOG(LogTemp, Log, TEXT("[Attachment UI] 수집된 슬롯 위젯: %d개"), CollectedSlotWidgets.Num());
	for (const auto& Collected : CollectedSlotWidgets)
	{
		if (IsValid(Collected))
		{
			UE_LOG(LogTemp, Log, TEXT("[Attachment UI]   - %s (Tag: %s)"),
				*Collected->GetName(), *Collected->GetSlotType().ToString());
		}
	}

	if (!CurrentWeaponItem.IsValid()) return;

	// 무기의 AttachmentHostFragment 가져오기
	const FInv_AttachmentHostFragment* HostFrag = CurrentWeaponItem->GetItemManifest().GetFragmentOfType<FInv_AttachmentHostFragment>();
	if (!HostFrag)
	{
		UE_LOG(LogTemp, Error, TEXT("[Attachment UI] AttachmentHostFragment를 찾을 수 없음!"));
		return;
	}

#if INV_DEBUG_ATTACHMENT
	{
		UE_LOG(LogTemp, Error, TEXT("[부착진단-패널] BuildSlotWidgets: WeaponItem=%s, HostFrag=%s, SlotDefs=%d, AttachedItems=%d"),
			*CurrentWeaponItem->GetItemManifest().GetItemType().ToString(),
			HostFrag ? TEXT("유효") : TEXT("nullptr"),
			HostFrag->GetSlotDefinitions().Num(),
			HostFrag->GetAttachedItems().Num());
		for (int32 d = 0; d < HostFrag->GetAttachedItems().Num(); d++)
		{
			const FInv_AttachedItemData& DiagData = HostFrag->GetAttachedItems()[d];
			UE_LOG(LogTemp, Error, TEXT("[부착진단-패널]   [%d] Type=%s (Slot=%d), ManifestCopy.ItemType=%s"),
				d, *DiagData.AttachmentItemType.ToString(), DiagData.SlotIndex,
				*DiagData.ItemManifestCopy.GetItemType().ToString());
		}
	}
#endif

	const TArray<FInv_AttachmentSlotDef>& SlotDefs = HostFrag->GetSlotDefinitions();

	for (int32 i = 0; i < SlotDefs.Num(); ++i)
	{
		// SlotDef의 SlotType ↔ WBP 슬롯 위젯의 SlotType 태그 매칭
		UInv_AttachmentSlotWidget* SlotWidget = FindSlotWidgetByTag(SlotDefs[i].SlotType);
		if (!IsValid(SlotWidget))
		{
			UE_LOG(LogTemp, Warning, TEXT("[Attachment UI] 슬롯[%d] %s: WBP에 해당 태그의 슬롯 위젯 없음 (건너뜀)"),
				i, *SlotDefs[i].SlotType.ToString());
			SlotWidgets.Add(nullptr);
			continue;
		}

		// 장착된 부착물 데이터 확인
		const FInv_AttachedItemData* AttachedData = HostFrag->GetAttachedItemData(i);

		// 슬롯 초기화
		SlotWidget->InitSlot(i, SlotDefs[i], AttachedData);

		// 슬롯 클릭 델리게이트 바인딩
		SlotWidget->OnSlotClicked.AddDynamic(this, &ThisClass::OnSlotClicked);

		// 슬롯 보이기
		SlotWidget->SetVisibility(ESlateVisibility::Visible);

		UE_LOG(LogTemp, Log, TEXT("[Attachment UI] 슬롯[%d] %s → Widget=%s"),
			i, *SlotDefs[i].SlotType.ToString(),
			*SlotWidget->GetName());

		SlotWidgets.Add(SlotWidget);
	}

#if INV_DEBUG_ATTACHMENT
	UE_LOG(LogTemp, Log, TEXT("[Attachment UI] 슬롯 위젯 %d개 초기화 완료 (WidgetTree 태그 매칭)"), SlotWidgets.Num());
#endif
}

// ════════════════════════════════════════════════════════════════
// 📌 ClearSlotWidgets — 델리게이트 해제 및 배열 정리
// ════════════════════════════════════════════════════════════════
// BindWidget 슬롯은 WBP 소유이므로 RemoveFromParent 하지 않음
void UInv_AttachmentPanel::ClearSlotWidgets()
{
	for (TObjectPtr<UInv_AttachmentSlotWidget>& Widget : SlotWidgets)
	{
		if (IsValid(Widget))
		{
			Widget->OnSlotClicked.RemoveAll(this);
		}
	}
	SlotWidgets.Empty();
}

// ════════════════════════════════════════════════════════════════
// 📌 RefreshSlotStates — 슬롯 상태 새로고침
// ════════════════════════════════════════════════════════════════
// 호출 경로: TryAttachHoverItem / TryDetachItem → 이 함수
// 처리 흐름:
//   1. 무기의 AttachmentHostFragment 재조회
//   2. 각 SlotWidget에 대해 AttachedItemData 유무로 SetOccupied/SetEmpty 호출
// ════════════════════════════════════════════════════════════════
void UInv_AttachmentPanel::RefreshSlotStates()
{
	if (!CurrentWeaponItem.IsValid()) return;

	const FInv_AttachmentHostFragment* HostFrag = CurrentWeaponItem->GetItemManifest().GetFragmentOfType<FInv_AttachmentHostFragment>();
	if (!HostFrag) return;

	for (int32 i = 0; i < SlotWidgets.Num(); ++i)
	{
		if (!IsValid(SlotWidgets[i])) continue;

		const FInv_AttachedItemData* AttachedData = HostFrag->GetAttachedItemData(i);
		if (AttachedData)
		{
			SlotWidgets[i]->SetOccupied(*AttachedData);
		}
		else
		{
			SlotWidgets[i]->SetEmpty();
		}
	}
}

// ════════════════════════════════════════════════════════════════
// 📌 UpdateSlotHighlights — HoverItem 호환 슬롯 실시간 하이라이트
// ════════════════════════════════════════════════════════════════
// 호출 경로: NativeTick → 이 함수
// 처리 흐름:
//   1. OwningGrid에 HoverItem이 있는지 확인
//   2. HoverItem의 아이템이 부착물인지 확인 (IsAttachableItem)
//   3. 부착물이면 각 SlotWidget의 SlotType과 AttachmentType 비교
//   4. 호환이면 SetHighlighted(true), 아니면 SetHighlighted(false)
//   5. HoverItem 없으면 모든 SlotWidget SetHighlighted(false)
// Phase 연결: Phase 3 UI — 드래그 중 호환 슬롯 표시
// ════════════════════════════════════════════════════════════════
void UInv_AttachmentPanel::UpdateSlotHighlights()
{
	// Grid에서 HoverItem 확인
	if (!OwningGrid.IsValid())
	{
		return;
	}

	UInv_HoverItem* HoverItem = OwningGrid->GetHoverItem();

	if (IsValid(HoverItem))
	{
		UInv_InventoryItem* HoverInvItem = HoverItem->GetInventoryItem();
		if (IsValid(HoverInvItem) && HoverInvItem->IsAttachableItem())
		{
			// 부착물의 AttachableFragment에서 AttachmentType 가져오기
			const FInv_AttachableFragment* AttachableFrag = HoverInvItem->GetItemManifest().GetFragmentOfType<FInv_AttachableFragment>();
			if (AttachableFrag)
			{
				const FGameplayTag AttachType = AttachableFrag->GetAttachmentType();

				// 각 슬롯과 호환성 비교
				for (TObjectPtr<UInv_AttachmentSlotWidget>& SlotWidget : SlotWidgets)
				{
					if (!IsValid(SlotWidget)) continue;

					// 슬롯이 비어있고, 호환 타입이면 하이라이트
					const bool bCompatible = !SlotWidget->IsOccupied() && SlotWidget->GetSlotType().MatchesTag(AttachType);
					SlotWidget->SetHighlighted(bCompatible);
				}
				return;
			}
		}
	}

	// HoverItem 없거나 부착물이 아닌 경우: 모든 하이라이트 끄기
	for (TObjectPtr<UInv_AttachmentSlotWidget>& SlotWidget : SlotWidgets)
	{
		if (IsValid(SlotWidget))
		{
			SlotWidget->SetHighlighted(false);
		}
	}
}

// ════════════════════════════════════════════════════════════════
// 📌 OnSlotClicked — 슬롯 클릭 콜백 (좌클릭=장착, 우클릭=분리 분기)
// ════════════════════════════════════════════════════════════════
// 호출 경로: SlotWidget::NativeOnMouseButtonDown → OnSlotClicked 델리게이트 → 이 함수
// 처리 흐름:
//   1. 우클릭 + 슬롯 Occupied → TryDetachItem
//   2. 좌클릭 + OwningGrid에 HoverItem 있음 → TryAttachHoverItem
//   3. 그 외 → 로그만 출력
// ════════════════════════════════════════════════════════════════
void UInv_AttachmentPanel::OnSlotClicked(int32 SlotIndex, const FPointerEvent& MouseEvent)
{
	const bool bIsRightClick = MouseEvent.GetEffectingButton() == EKeys::RightMouseButton;
	const bool bIsLeftClick = MouseEvent.GetEffectingButton() == EKeys::LeftMouseButton;

	// 슬롯 인덱스 유효성 검사
	if (SlotIndex < 0 || SlotIndex >= SlotWidgets.Num())
	{
		UE_LOG(LogTemp, Warning, TEXT("[Attachment UI] 유효하지 않은 슬롯 인덱스: %d"), SlotIndex);
		return;
	}

	const bool bSlotOccupied = IsValid(SlotWidgets[SlotIndex]) && SlotWidgets[SlotIndex]->IsOccupied();

	if (bIsRightClick && bSlotOccupied)
	{
		// 우클릭 + 점유된 슬롯 → 분리
		TryDetachItem(SlotIndex);
	}
	else if (bIsLeftClick && OwningGrid.IsValid() && OwningGrid->HasHoverItem())
	{
		// 좌클릭 + HoverItem 있음 → 장착 시도
		TryAttachHoverItem(SlotIndex);
	}
	else
	{
#if INV_DEBUG_ATTACHMENT
		UE_LOG(LogTemp, Log, TEXT("[Attachment UI] 슬롯 %d 클릭됨 — 조건 미충족 (우클릭=%s, 좌클릭=%s, 점유=%s, HoverItem=%s)"),
			SlotIndex,
			bIsRightClick ? TEXT("O") : TEXT("X"),
			bIsLeftClick ? TEXT("O") : TEXT("X"),
			bSlotOccupied ? TEXT("O") : TEXT("X"),
			(OwningGrid.IsValid() && OwningGrid->HasHoverItem()) ? TEXT("O") : TEXT("X"));
#endif
	}
}

// ════════════════════════════════════════════════════════════════
// 📌 TryAttachHoverItem — HoverItem을 슬롯에 장착 시도
// ════════════════════════════════════════════════════════════════
// 호출 경로: OnSlotClicked (좌클릭) → 이 함수
// 처리 흐름:
//   1. HoverItem에서 InventoryItem 가져오기
//   2. 부착물의 EntryIndex → FindEntryIndexForItem으로 재검색
//   3. WeaponEntryIndex → FindCurrentWeaponEntryIndex로 재검색
//   4. CanAttachToWeapon 체크
//   5. 통과 → Server_AttachItemToWeapon RPC + ClearHoverItem + RefreshSlotStates
//   6. 실패 → 경고 로그
// ⚠️ EntryIndex 밀림 대비: 포인터로 현재 인덱스 재검색
// ════════════════════════════════════════════════════════════════
void UInv_AttachmentPanel::TryAttachHoverItem(int32 SlotIndex)
{
	if (!OwningGrid.IsValid() || !InventoryComponent.IsValid()) return;

	UInv_HoverItem* HoverItem = OwningGrid->GetHoverItem();
	if (!IsValid(HoverItem)) return;

	UInv_InventoryItem* AttachmentItem = HoverItem->GetInventoryItem();
	if (!IsValid(AttachmentItem)) return;

	// 부착물 아이템인지 확인
	if (!AttachmentItem->IsAttachableItem())
	{
		UE_LOG(LogTemp, Warning, TEXT("[Attachment UI] 장착 실패: 부착물이 아닌 아이템"));
		return;
	}

	// ⚠️ EntryIndex 밀림 대비: 포인터로 현재 인덱스 재검색
	const int32 AttachmentEntryIndex = InventoryComponent->FindEntryIndexForItem(AttachmentItem);
	const int32 WeaponEntryIndex = FindCurrentWeaponEntryIndex();

	if (AttachmentEntryIndex == INDEX_NONE)
	{
		UE_LOG(LogTemp, Error, TEXT("[Attachment UI] 장착 실패: 부착물 EntryIndex를 찾을 수 없음"));
		return;
	}
	if (WeaponEntryIndex == INDEX_NONE)
	{
		UE_LOG(LogTemp, Error, TEXT("[Attachment UI] 장착 실패: 무기 EntryIndex를 찾을 수 없음"));
		return;
	}

	// 호환성 체크
#if INV_DEBUG_ATTACHMENT
	UE_LOG(LogTemp, Log, TEXT("[Attachment UI] CanAttach 체크: WeaponEntry=%d, AttachEntry=%d, Slot=%d"),
		WeaponEntryIndex, AttachmentEntryIndex, SlotIndex);
#endif
	if (!InventoryComponent->CanAttachToWeapon(WeaponEntryIndex, AttachmentEntryIndex, SlotIndex))
	{
		UE_LOG(LogTemp, Warning, TEXT("[Attachment UI] 장착 실패: 호환 안 됨 (슬롯=%d)"), SlotIndex);
		return;
	}

	// ⭐ [낙관적 UI 갱신] RPC 완료를 기다리지 않고 즉시 슬롯 상태 업데이트
	// Standalone/ListenServer에서 RPC가 동기 실행되어도 UI는 즉시 반영됨
	if (SlotWidgets.IsValidIndex(SlotIndex) && IsValid(SlotWidgets[SlotIndex]))
	{
		FInv_AttachedItemData PreviewData;
		PreviewData.SlotIndex = SlotIndex;
		PreviewData.AttachmentItemType = AttachmentItem->GetItemManifest().GetItemType();
		PreviewData.ItemManifestCopy = AttachmentItem->GetItemManifest();
		SlotWidgets[SlotIndex]->SetOccupied(PreviewData);
	}

	// 서버 RPC 호출
	InventoryComponent->Server_AttachItemToWeapon(WeaponEntryIndex, AttachmentEntryIndex, SlotIndex);

	// HoverItem 정리 및 커서 복원
	OwningGrid->ClearHoverItem();
	OwningGrid->ShowCursor();

#if INV_DEBUG_ATTACHMENT
	UE_LOG(LogTemp, Log, TEXT("[Attachment UI] 장착 성공: 슬롯 %d에 %s (WeaponEntry=%d, AttachEntry=%d)"),
		SlotIndex,
		*AttachmentItem->GetItemManifest().GetItemType().ToString(),
		WeaponEntryIndex,
		AttachmentEntryIndex);
#endif
}

// ════════════════════════════════════════════════════════════════
// 📌 TryDetachItem — 점유된 슬롯에서 부착물 분리
// ════════════════════════════════════════════════════════════════
// 호출 경로: OnSlotClicked (우클릭) → 이 함수
// 처리 흐름:
//   1. WeaponEntryIndex 재검색 (밀림 대비)
//   2. Server_DetachItemFromWeapon RPC 호출
//   3. RefreshSlotStates
// ════════════════════════════════════════════════════════════════
void UInv_AttachmentPanel::TryDetachItem(int32 SlotIndex)
{
	if (!InventoryComponent.IsValid()) return;

	// ⚠️ EntryIndex 밀림 대비: 포인터로 재검색
	const int32 WeaponEntryIndex = FindCurrentWeaponEntryIndex();
	if (WeaponEntryIndex == INDEX_NONE)
	{
		UE_LOG(LogTemp, Error, TEXT("[Attachment UI] 분리 실패: 무기 EntryIndex를 찾을 수 없음"));
		return;
	}

	// ⭐ [낙관적 UI 갱신] 즉시 슬롯을 비운 상태로 표시
	if (SlotWidgets.IsValidIndex(SlotIndex) && IsValid(SlotWidgets[SlotIndex]))
	{
		SlotWidgets[SlotIndex]->SetEmpty();
	}

	// 서버 RPC 호출
	InventoryComponent->Server_DetachItemFromWeapon(WeaponEntryIndex, SlotIndex);

#if INV_DEBUG_ATTACHMENT
	UE_LOG(LogTemp, Log, TEXT("[Attachment UI] 분리 완료: 슬롯 %d (WeaponEntry=%d)"),
		SlotIndex, WeaponEntryIndex);
#endif
}

// ════════════════════════════════════════════════════════════════
// 📌 OnCloseButtonClicked — 닫기 버튼 클릭 핸들러
// ════════════════════════════════════════════════════════════════
void UInv_AttachmentPanel::OnCloseButtonClicked()
{
	ClosePanel();
}

// ════════════════════════════════════════════════════════════════
// 📌 FindCurrentWeaponEntryIndex — 무기 포인터로 현재 EntryIndex 재검색
// ════════════════════════════════════════════════════════════════
// 호출 경로: TryAttachHoverItem / TryDetachItem → 이 함수
// 처리 흐름:
//   1. InventoryComponent의 FindEntryIndexForItem 호출
//   2. 못 찾으면 INDEX_NONE + 에러 로그
// ⚠️ 부착물 분리 시 InventoryList에 아이템이 추가되어 EntryIndex 밀림 가능
// ════════════════════════════════════════════════════════════════
int32 UInv_AttachmentPanel::FindCurrentWeaponEntryIndex() const
{
	if (!InventoryComponent.IsValid() || !CurrentWeaponItem.IsValid())
	{
		return INDEX_NONE;
	}

	const int32 FoundIndex = InventoryComponent->FindEntryIndexForItem(CurrentWeaponItem.Get());
	if (FoundIndex == INDEX_NONE)
	{
		UE_LOG(LogTemp, Error, TEXT("[Attachment UI] 무기 EntryIndex 검색 실패! CurrentWeaponItem 포인터가 InventoryList에 없음"));
	}
	else
	{
#if INV_DEBUG_ATTACHMENT
		// 캐시된 값과 달라졌으면 로그
		if (FoundIndex != CurrentWeaponEntryIndex)
		{
			UE_LOG(LogTemp, Warning, TEXT("[Attachment UI] EntryIndex 밀림 감지: 캐시=%d → 실제=%d"),
				CurrentWeaponEntryIndex, FoundIndex);
		}
#endif
	}

	return FoundIndex;
}

// ════════════════════════════════════════════════════════════════
// 📌 ResetAllSlots — 수집된 슬롯 전부 Hidden + SetEmpty (초기화)
// ════════════════════════════════════════════════════════════════
void UInv_AttachmentPanel::ResetAllSlots()
{
	for (const TObjectPtr<UInv_AttachmentSlotWidget>& SlotWidget : CollectedSlotWidgets)
	{
		if (IsValid(SlotWidget))
		{
			SlotWidget->SetEmpty();
			SlotWidget->SetHighlighted(false);
			SlotWidget->SetVisibility(ESlateVisibility::Hidden);
			SlotWidget->OnSlotClicked.RemoveAll(this);
		}
	}
}

// ════════════════════════════════════════════════════════════════
// 📌 CollectSlotWidgetsFromTree — WidgetTree에서 슬롯 위젯 자동 수집
// ════════════════════════════════════════════════════════════════
// 호출 경로: NativeOnInitialized → 이 함수 (1회)
// 처리 흐름:
//   1. WidgetTree->ForEachWidget으로 전체 순회
//   2. UInv_AttachmentSlotWidget으로 Cast 성공한 위젯만 수집
//   3. SlotType이 유효한지 검증 (미설정 경고)
// 성능: 위젯 수 20개 미만, 초기화 시 1회만 실행 — 부하 무시 가능
// ════════════════════════════════════════════════════════════════
void UInv_AttachmentPanel::CollectSlotWidgetsFromTree()
{
	CollectedSlotWidgets.Empty();

	if (!WidgetTree)
	{
		UE_LOG(LogTemp, Error, TEXT("[Attachment UI] WidgetTree가 nullptr — 슬롯 수집 불가"));
		return;
	}

	WidgetTree->ForEachWidget([this](UWidget* Widget)
	{
		UInv_AttachmentSlotWidget* SlotWidget = Cast<UInv_AttachmentSlotWidget>(Widget);
		if (IsValid(SlotWidget))
		{
			if (!SlotWidget->GetSlotType().IsValid())
			{
				UE_LOG(LogTemp, Warning, TEXT("[Attachment UI] 슬롯 위젯 '%s'에 SlotType이 설정되지 않음! WBP에서 태그 지정 필요"),
					*SlotWidget->GetName());
			}
			CollectedSlotWidgets.Add(SlotWidget);
		}
	});

	UE_LOG(LogTemp, Log, TEXT("[Attachment UI] WidgetTree에서 슬롯 위젯 %d개 수집 완료"), CollectedSlotWidgets.Num());
}

// ════════════════════════════════════════════════════════════════
// 📌 FindSlotWidgetByTag — SlotType GameplayTag로 슬롯 위젯 검색
// ════════════════════════════════════════════════════════════════
UInv_AttachmentSlotWidget* UInv_AttachmentPanel::FindSlotWidgetByTag(const FGameplayTag& SlotType) const
{
	for (const TObjectPtr<UInv_AttachmentSlotWidget>& SlotWidget : CollectedSlotWidgets)
	{
		if (IsValid(SlotWidget) && SlotWidget->GetSlotType().MatchesTagExact(SlotType))
		{
			return SlotWidget;
		}
	}
	return nullptr;
}

// ════════════════════════════════════════════════════════════════
// 📌 SetupWeaponPreview — 3D 무기 프리뷰 액터 스폰 및 설정
// ════════════════════════════════════════════════════════════════
// 호출 경로: OpenForWeapon → 이 함수
// 처리 흐름:
//   1. EquipmentFragment에서 PreviewStaticMesh 확인
//   2. 없으면 2D 아이콘 폴백 (Image_WeaponIcon 유지)
//   3. 있으면 WeaponPreviewActor 스폰 → SetPreviewMesh
//   4. RenderTarget으로 Image_WeaponPreview에 Material 연결
// ════════════════════════════════════════════════════════════════
void UInv_AttachmentPanel::SetupWeaponPreview()
{
	if (!CurrentWeaponItem.IsValid()) return;

	// EquipmentFragment에서 프리뷰 메시 정보 가져오기
	const FInv_EquipmentFragment* EquipFrag = CurrentWeaponItem->GetItemManifest().GetFragmentOfType<FInv_EquipmentFragment>();
	if (!EquipFrag || !EquipFrag->HasPreviewMesh())
	{
		// 프리뷰 메시 없음 → Image_WeaponPreview 숨기고 2D 아이콘 사용
		if (IsValid(Image_WeaponPreview))
		{
			Image_WeaponPreview->SetVisibility(ESlateVisibility::Collapsed);
		}
#if INV_DEBUG_ATTACHMENT
		UE_LOG(LogTemp, Log, TEXT("[Attachment UI] 프리뷰 메시 없음 → 2D 아이콘 폴백"));
#endif
		return;
	}

	// 프리뷰 메시 동기 로드
	UStaticMesh* PreviewMesh = EquipFrag->GetPreviewStaticMesh().LoadSynchronous();
	if (!IsValid(PreviewMesh))
	{
		UE_LOG(LogTemp, Warning, TEXT("[Attachment UI] 프리뷰 메시 로드 실패!"));
		if (IsValid(Image_WeaponPreview))
		{
			Image_WeaponPreview->SetVisibility(ESlateVisibility::Collapsed);
		}
		return;
	}

	// 프리뷰 액터 스폰
	UWorld* World = GetWorld();
	if (!IsValid(World)) return;

	FActorSpawnParameters SpawnParams;
	SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

	// BP 클래스가 설정되어 있으면 사용, 아니면 C++ 기본 클래스 폴백
	UClass* ClassToSpawn = WeaponPreviewActorClass
		? WeaponPreviewActorClass.Get()
		: AInv_WeaponPreviewActor::StaticClass();

	AInv_WeaponPreviewActor* NewPreview = World->SpawnActor<AInv_WeaponPreviewActor>(
		ClassToSpawn,
		FVector(0.f, 0.f, PreviewSpawnZ),
		FRotator::ZeroRotator,
		SpawnParams
	);

	if (!IsValid(NewPreview))
	{
		UE_LOG(LogTemp, Error, TEXT("[Attachment UI] WeaponPreviewActor 스폰 실패!"));
		return;
	}

	WeaponPreviewActor = NewPreview;

	// 프리뷰 메시 설정 (회전 오프셋 + 카메라 거리)
	NewPreview->SetPreviewMesh(
		PreviewMesh,
		EquipFrag->GetPreviewRotationOffset(),
		EquipFrag->GetPreviewCameraDistance()
	);

	// RenderTarget → Material → Image_WeaponPreview 연결
	// SCS_FinalColorLDR은 알파=0을 출력하므로, Material에서 RGB만 사용하고
	// Opacity=1로 강제하여 불투명 렌더링 보장
	UTextureRenderTarget2D* RT = NewPreview->GetRenderTarget();
	if (IsValid(RT) && IsValid(Image_WeaponPreview))
	{
		UMaterialInterface* PreviewMat = LoadObject<UMaterialInterface>(
			nullptr, TEXT("/Game/UI/Materials/M_WeaponPreview"));

		if (IsValid(PreviewMat))
		{
			UMaterialInstanceDynamic* MID = UMaterialInstanceDynamic::Create(PreviewMat, this);
			MID->SetTextureParameterValue(TEXT("PreviewTexture"), RT);
			Image_WeaponPreview->SetBrushFromMaterial(MID);

			// SetBrushFromMaterial이 RenderTarget 해상도로 덮어쓴 ImageSize를
			// NativeOnInitialized에서 캐싱한 WBP 원본 값으로 복원
			FSlateBrush FixedBrush = Image_WeaponPreview->GetBrush();
			if (!CachedPreviewImageSize.IsNearlyZero())
			{
				FixedBrush.ImageSize = CachedPreviewImageSize;
			}
			else
			{
				// 캐싱 실패 안전장치: WBP 기본값 (256, 256) 사용
				FixedBrush.ImageSize = FVector2D(256.f, 256.f);
				UE_LOG(LogTemp, Warning, TEXT("[Attachment UI] CachedPreviewImageSize가 0! 폴백값 (256, 256) 사용"));
			}
			Image_WeaponPreview->SetBrush(FixedBrush);
		}
		else
		{
			// 폴백: Material 로드 실패 시 기존 방식 (알파 문제 있을 수 있음)
			UE_LOG(LogTemp, Warning, TEXT("[Attachment UI] M_WeaponPreview 로드 실패! FSlateBrush 폴백"));
			FSlateBrush PreviewBrush;
			PreviewBrush.SetResourceObject(RT);
			PreviewBrush.ImageSize = CachedPreviewImageSize.IsNearlyZero()
				? FVector2D(256.f, 256.f)
				: CachedPreviewImageSize;
			PreviewBrush.DrawAs = ESlateBrushDrawType::Image;
			PreviewBrush.Tiling = ESlateBrushTileType::NoTile;
			Image_WeaponPreview->SetBrush(PreviewBrush);
		}

		// 크기는 WBP 디자이너에서 설정한 레이아웃을 그대로 사용
		Image_WeaponPreview->SetVisibility(ESlateVisibility::Visible);
	}

#if INV_DEBUG_ATTACHMENT
	UE_LOG(LogTemp, Log, TEXT("[Attachment UI] 3D 프리뷰 설정 완료: Mesh=%s"), *PreviewMesh->GetName());
	if (IsValid(Image_WeaponPreview))
	{
		const FVector2D DesiredSize = Image_WeaponPreview->GetDesiredSize();
		UE_LOG(LogTemp, Error, TEXT("[Weapon Preview] Image_WeaponPreview DesiredSize=(%.1f, %.1f), Visibility=%d"),
			DesiredSize.X, DesiredSize.Y,
			(int32)Image_WeaponPreview->GetVisibility());
	}
	if (IsValid(RT))
	{
		UE_LOG(LogTemp, Error, TEXT("[Weapon Preview] RenderTarget: %dx%d, Format=%d, ClearColor=(%.2f,%.2f,%.2f)"),
			RT->SizeX, RT->SizeY, (int32)RT->GetFormat(),
			RT->ClearColor.R, RT->ClearColor.G, RT->ClearColor.B);
	}
#endif
}

// ════════════════════════════════════════════════════════════════
// 📌 CleanupWeaponPreview — 프리뷰 액터 파괴 및 정리
// ════════════════════════════════════════════════════════════════
void UInv_AttachmentPanel::CleanupWeaponPreview()
{
	if (WeaponPreviewActor.IsValid())
	{
		WeaponPreviewActor->Destroy();
		WeaponPreviewActor.Reset();
	}

	// Image_WeaponPreview 정리
	if (IsValid(Image_WeaponPreview))
	{
		Image_WeaponPreview->SetBrush(FSlateBrush());
		Image_WeaponPreview->SetVisibility(ESlateVisibility::Collapsed);
	}
}

// ════════════════════════════════════════════════════════════════
// 📌 NativeOnMouseButtonDown — 드래그 시작 (Phase 8)
// ════════════════════════════════════════════════════════════════
// 호출 경로: UMG 마우스 이벤트 → 이 함수
// 처리 흐름:
//   1. 좌클릭 감지
//   2. HoverItem 들고 있는지 확인
//      → 들고 있으면: 드래그 시작하지 않음 (Super 호출로 슬롯까지 이벤트 전파)
//      → 비어 있으면: 드래그 시작 (FReply::Handled로 이벤트 소비)
//   3. 좌클릭 외 → Super 호출
//
// ⚠️ HoverItem 우선 이유:
//    FReply::Handled()가 이벤트를 소비하면 자식 슬롯 위젯까지
//    이벤트가 전파되지 않음. HoverItem 들고 있을 때는 장착이 우선이므로
//    Super()로 넘겨서 슬롯 위젯의 OnSlotClicked까지 도달하게 해야 함.
//
// Phase 연결: Phase 8 — CharacterDisplay 동일 드래그 패턴 + HoverItem 가드
// ════════════════════════════════════════════════════════════════
FReply UInv_AttachmentPanel::NativeOnMouseButtonDown(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent)
{
	if (InMouseEvent.GetEffectingButton() == EKeys::LeftMouseButton)
	{
		// ⭐ HoverItem 들고 있으면 드래그 대신 슬롯 장착이 우선
		// Super() 호출로 이벤트를 자식 위젯(슬롯)까지 전파시킴
		if (OwningGrid.IsValid() && OwningGrid->HasHoverItem())
		{
			return Super::NativeOnMouseButtonDown(InGeometry, InMouseEvent);
		}

		// HoverItem 없으면 드래그 회전 시작
		DragCurrentPosition = UWidgetLayoutLibrary::GetMousePositionOnViewport(GetOwningPlayer());
		DragLastPosition = DragCurrentPosition;
		bIsDragging = true;
		return FReply::Handled();
	}

	return Super::NativeOnMouseButtonDown(InGeometry, InMouseEvent);
}

// ════════════════════════════════════════════════════════════════
// 📌 NativeOnMouseButtonUp — 드래그 종료
// ════════════════════════════════════════════════════════════════
FReply UInv_AttachmentPanel::NativeOnMouseButtonUp(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent)
{
	bIsDragging = false;
	return Super::NativeOnMouseButtonUp(InGeometry, InMouseEvent);
}

// ════════════════════════════════════════════════════════════════
// 📌 NativeOnMouseLeave — 마우스가 패널을 벗어나면 드래그 중지
// ════════════════════════════════════════════════════════════════
void UInv_AttachmentPanel::NativeOnMouseLeave(const FPointerEvent& InMouseEvent)
{
	Super::NativeOnMouseLeave(InMouseEvent);
	bIsDragging = false;
}
