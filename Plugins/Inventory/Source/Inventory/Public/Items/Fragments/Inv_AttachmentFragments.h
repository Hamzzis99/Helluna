// Gihyeon's Inventory Project
//
// ════════════════════════════════════════════════════════════════════════════════
// 📌 부착물 시스템 (Attachment System) — 전체 완료
// ════════════════════════════════════════════════════════════════════════════════
//
// 📌 이 파일의 역할:
//    인벤토리 플러그인의 부착물 시스템 핵심 데이터 구조 정의
//    무기에 스코프/소음기/그립 등을 장착하는 타르코프 방식 시스템
//
// 📌 포함된 구조체 (4개):
//    ① FInv_AttachmentSlotDef     — 슬롯 1개의 정의 (순수 데이터, Fragment 아님)
//    ② FInv_AttachedItemData      — 장착된 부착물의 런타임 데이터
//    ③ FInv_AttachmentHostFragment — 무기가 가지는 Fragment ("나는 부착물 슬롯이 있어")
//    ④ FInv_AttachableFragment    — 부착물이 가지는 Fragment ("나는 이 슬롯에 들어가")
//
// ════════════════════════════════════════════════════════════════════════════════
// 🔧 새 부착물 아이템 추가 방법 (BP 설정 가이드)
// ════════════════════════════════════════════════════════════════════════════════
//
// ── STEP 1: 부착물 BP 생성 ──
//    1) Content Browser에서 BP_Inv_Muzzle_Test를 Duplicate하여 새 BP 생성
//       (예: BP_Inv_Laser_Test)
//    2) BP를 열어서 다음 3가지를 반드시 설정:
//
//    ⭐ [필수 1] BP_Inv_Item_Component → Item Manifest → Item Type
//       → GameplayTag를 설정 (예: GameItems.Equipment.Attachments.Laser)
//       → ❌ 이 값이 비어있으면(None) 저장 시 아이템이 수집되지 않아 로드 후 사라짐!
//       → 태그는 Inventory.cpp의 AddNativeGameplayTag에 등록되어 있어야 함
//
//    ⭐ [필수 2] BP_Inv_Item_Component → Item Manifest → Fragments 배열에서
//       FInv_AttachableFragment 항목:
//       → AttachmentType: "AttachmentSlot.Laser" (장착될 슬롯 타입)
//       → AttachmentMesh: 무기 소켓에 부착될 스태틱 메시
//       → 효과 플래그: bIsSuppressor, ZoomFOVOverride, bIsLaser 등
//       → EquipModifiers: 장착 시 적용할 스탯 효과
//
//    ⭐ [필수 3] DT_ItemTypeMapping DataTable에 행 추가
//       → RowName: "Laser_Test" (자유 이름)
//       → ItemType: GameItems.Equipment.Attachments.Laser
//       → ItemActorClass: BP_Inv_Laser_Test
//       → ❌ 이 매핑이 없으면 로드 시 ResolveItemClass가 실패하여 아이템 복원 불가!
//
// ── STEP 2: GameplayTag 등록 확인 ──
//    Inventory.cpp의 StartupModule()에 태그가 등록되어 있어야 함:
//      TagManager.AddNativeGameplayTag("AttachmentSlot.Laser", TEXT("레이저 슬롯"));
//      TagManager.AddNativeGameplayTag("GameItems.Equipment.Attachments.Laser", TEXT("레이저 부착물"));
//    → 이미 등록된 태그를 재사용하면 이 단계는 불필요
//
// ── STEP 3: 무기 BP에 슬롯 추가 (선택) ──
//    무기 BP (예: BP_Inv_Axe)의 Item Manifest → Fragments에서
//    FInv_AttachmentHostFragment → SlotDefinitions 배열에 항목 추가:
//      → SlotType: "AttachmentSlot.Laser"
//      → SlotDisplayName: "레이저 슬롯"
//      → AttachSocket: "socket_laser" (EquipActor 메시에 소켓 필요)
//      → SlotPosition: Right (십자형 UI에서의 위치)
//
// ── STEP 4: UI 위젯 (선택) ──
//    WBP_AttachmentPanel에 해당 태그의 슬롯 위젯이 없으면
//    "[Attachment UI] 슬롯[N] Tag: WBP에 해당 태그의 슬롯 위젯 없음" 로그 출력
//    → 기능에는 문제없으나 UI에 슬롯이 표시 안 됨
//
// ════════════════════════════════════════════════════════════════════════════════
// 🔧 흔한 실수 & 디버깅 체크리스트
// ════════════════════════════════════════════════════════════════════════════════
//
// ❌ "저장 후 로드하면 특정 아이템이 사라짐"
//    → BP_Inv_XXX의 Item Manifest → ItemType이 None인지 확인!
//    → DT_ItemTypeMapping에 해당 태그 행이 있는지 확인!
//
// ❌ "분리 시 OriginalItem nullptr 에러"
//    → 세이브/로드 후 분리 시도인지 확인
//    → Inv_SaveGameMode.cpp Step 8에서 AddAttachedItemFromManifest +
//      SetOriginalItemForSlot이 정상 실행되는지 로그 확인
//
// ❌ "부착물이 그리드에 중복 표시됨"
//    → PostReplicatedAdd에서 bIsAttachedToWeapon=true일 때 OnItemAdded 스킵하는지 확인
//    → FastArray.cpp의 PostReplicatedAdd 참조
//
// ❌ "부착물 분리했는데 그리드에 안 나타남"
//    → Server_DetachItemFromWeapon에서 bIsAttachedToWeapon=false로 복원 후
//      OnItemAdded.Broadcast 호출하는지 확인
//
// ════════════════════════════════════════════════════════════════════════════════
// 📌 Phase 완료 이력
// ════════════════════════════════════════════════════════════════════════════════
//    ✅ Phase 1: Fragment 정의 (이 파일)
//    ✅ Phase 2: 부착/분리 서버 로직 (Inv_InventoryComponent Server RPC)
//    ✅ Phase 3: UI (Inv_AttachmentPanel, Inv_AttachmentSlotWidget)
//    ✅ Phase 4: 드롭/줍기 확장 (ItemManifest에 부착물 데이터 보존)
//    ✅ Phase 5: 시각적 표현 (Inv_EquipActor에 소켓 메시 Attach)
//    ✅ Phase 6: 저장/로드 확장 (FInv_SavedItemData에 부착물 배열)
//    ✅ Phase 7: 부착물 효과 (소음기/줌/레이저)
//    ✅ Phase 8: 부착물 패널 + 3D 프리뷰 + 인벤토리 최적화
//    ✅ BUG FIX: bIsAttachedToWeapon 플래그 방식 (인덱스 밀림 해결)
//    ✅ BUG FIX: 세이브/로드 중복 저장/OriginalItem 복원
//
// 📌 핵심 아키텍처 — bIsAttachedToWeapon 플래그 방식:
//    부착 시: Entry를 FastArray에서 삭제하지 않고, bIsAttachedToWeapon=true 마킹
//             → 그리드에서만 숨김 (OnItemRemoved), 실제 Entry는 유지
//    분리 시: bIsAttachedToWeapon=false 복원 → 그리드에 다시 표시 (OnItemAdded)
//    장점: 인덱스 밀림 없음, OriginalItem 포인터 유지, 리플리케이션 안정
//
// ════════════════════════════════════════════════════════════════════════════════

#pragma once

#include "CoreMinimal.h"
#include "Items/Fragments/Inv_ItemFragment.h"
#include "Items/Manifest/Inv_ItemManifest.h"

#include "Inv_AttachmentFragments.generated.h"

class UStaticMesh;
class APlayerController;

// ════════════════════════════════════════════════════════════════════════════════
// 📌 EInv_AttachmentSlotPosition — 부착물 슬롯 UI 배치 위치
// ════════════════════════════════════════════════════════════════════════════════
// Phase 8: 십자형 레이아웃에서 슬롯이 표시될 방향
// AttachmentPanel의 GridPanel에서 이 값을 읽어 Top/Left/Right/Bottom에 분배
//
// 사용처:
//   - FInv_AttachmentSlotDef::SlotPosition (BP 에디터에서 설정)
//   - Inv_AttachmentPanel::BuildSlotWidgets() (슬롯 배치 분기)
//
// BP 설정 예시:
//   BP_Inv_Rifle의 AttachmentHostFragment → SlotDefinitions:
//     [0] Scope  → SlotPosition = Top
//     [1] Grip   → SlotPosition = Left
//     [2] Laser  → SlotPosition = Right
//     [3] Magazine → SlotPosition = Bottom
// ════════════════════════════════════════════════════════════════════════════════
UENUM(BlueprintType)
enum class EInv_AttachmentSlotPosition : uint8
{
	Top     UMETA(DisplayName = "상단 (스코프 등)"),
	Bottom  UMETA(DisplayName = "하단 (탄창 등)"),
	Left    UMETA(DisplayName = "좌측 (그립 등)"),
	Right   UMETA(DisplayName = "우측 (조명/레이저 등)"),
};

// ════════════════════════════════════════════════════════════════════════════════
// 📌 FInv_AttachmentSlotDef - 부착물 슬롯 정의
// ════════════════════════════════════════════════════════════════════════════════
// 부착물 슬롯 1개의 정의 (Fragment가 아닌 순수 데이터)
// SlotType 태그로 부착물의 AttachmentType과 매칭
// ════════════════════════════════════════════════════════════════════════════════
USTRUCT(BlueprintType)
struct FInv_AttachmentSlotDef
{
	GENERATED_BODY()

	// 슬롯 타입 태그 (예: "AttachmentSlot.Scope", "AttachmentSlot.Muzzle", "AttachmentSlot.Grip")
	UPROPERTY(EditAnywhere, Category = "부착물", meta = (Categories = "AttachmentSlot", DisplayName = "슬롯 타입", Tooltip = "부착물의 AttachmentType과 매칭되는 슬롯 타입 태그"))
	FGameplayTag SlotType;

	// UI에 표시할 슬롯 이름 ("스코프 슬롯", "총구 슬롯")
	UPROPERTY(EditAnywhere, Category = "부착물", meta = (DisplayName = "슬롯 표시 이름", Tooltip = "UI에 표시될 슬롯 이름"))
	FText SlotDisplayName;

	// EquipActor의 소켓 이름 (Phase 5 시각적 부착용)
	UPROPERTY(EditAnywhere, Category = "부착물", meta = (DisplayName = "부착 소켓", Tooltip = "EquipActor 메시의 소켓 이름 (예: socket_scope)"))
	FName AttachSocket{NAME_None};

	// 이 슬롯에 장착 가능한 부착물 수 (보통 1)
	UPROPERTY(EditAnywhere, Category = "부착물", meta = (DisplayName = "최대 장착 수", Tooltip = "이 슬롯에 동시에 장착 가능한 부착물 수", ClampMin = 1))
	int32 MaxCount{1};

	// ════════════════════════════════════════════════════════════════
	// 📌 [Phase 8] 슬롯 UI 배치 위치
	// ════════════════════════════════════════════════════════════════
	// 십자형 레이아웃에서 이 슬롯이 표시될 방향
	// Top = 무기 위(스코프), Bottom = 아래(탄창), Left/Right = 좌우(그립/조명)
	// AttachmentPanel::BuildSlotWidgets()에서 이 값으로 VerticalBox 분배
	// ════════════════════════════════════════════════════════════════
	UPROPERTY(EditAnywhere, Category = "부착물",
		meta = (DisplayName = "슬롯 UI 위치",
				Tooltip = "십자형 부착물 패널에서 이 슬롯이 표시될 방향. Top=상단(스코프), Bottom=하단(탄창), Left=좌측(그립), Right=우측(조명)"))
	EInv_AttachmentSlotPosition SlotPosition = EInv_AttachmentSlotPosition::Top;
};


// ════════════════════════════════════════════════════════════════════════════════
// 📌 FInv_AttachedItemData - 장착된 부착물 런타임 데이터
// ════════════════════════════════════════════════════════════════════════════════
// 무기에 장착된 부착물 1개의 런타임 데이터
// 드롭/줍기, 저장/로드 시 부착물 아이템을 완전 복원하기 위해
// ItemManifest 전체 사본을 보관함
// ════════════════════════════════════════════════════════════════════════════════
USTRUCT(BlueprintType)
struct FInv_AttachedItemData
{
	GENERATED_BODY()

	// 어느 슬롯에 장착되어 있는지
	UPROPERTY()
	int32 SlotIndex{INDEX_NONE};

	// 부착물 아이템 종류 (ResolveItemClass로 복원 시 사용)
	UPROPERTY()
	FGameplayTag AttachmentItemType;

	// 부착물 아이템의 전체 Manifest 사본 (스탯, 아이콘 등 모든 Fragment 포함)
	UPROPERTY()
	FInv_ItemManifest ItemManifestCopy;

	// ⭐ [부착물 시스템] 원본 InventoryItem 포인터 (분리 시 FastArray Entry 복원용)
	// RemoveEntry 대신 bIsAttachedToWeapon 플래그 방식 사용 시 필요
	UPROPERTY()
	TObjectPtr<UInv_InventoryItem> OriginalItem = nullptr;
};


// ════════════════════════════════════════════════════════════════════════════════
// 📌 FInv_AttachmentHostFragment - 부착물 호스트 프래그먼트
// ════════════════════════════════════════════════════════════════════════════════
// "이 아이템은 부착물 슬롯을 가진 호스트(무기)입니다"
// 무기 BP의 ItemManifest Fragments 배열에 추가하여 사용
// SlotDefinitions: 에디터에서 슬롯 구성 정의
// AttachedItems: 런타임에 서버 RPC로 관리되는 장착 상태
// ════════════════════════════════════════════════════════════════════════════════
USTRUCT(BlueprintType)
struct FInv_AttachmentHostFragment : public FInv_ItemFragment
{
	GENERATED_BODY()

	// ── 슬롯 정의 접근 ──

	int32 GetSlotCount() const { return SlotDefinitions.Num(); }
	const FInv_AttachmentSlotDef* GetSlotDef(int32 SlotIndex) const;
	const TArray<FInv_AttachmentSlotDef>& GetSlotDefinitions() const { return SlotDefinitions; }

	// ── 장착 상태 조회 ──

	bool IsSlotOccupied(int32 SlotIndex) const;
	const FInv_AttachedItemData* GetAttachedItemData(int32 SlotIndex) const;
	const TArray<FInv_AttachedItemData>& GetAttachedItems() const { return AttachedItems; }

	// ── 장착/분리 조작 ──

	void AttachItem(int32 SlotIndex, const FInv_AttachedItemData& Data);
	FInv_AttachedItemData DetachItem(int32 SlotIndex);

	// ── OriginalItem 포인터 연결 (로드 복원용) ──
	void SetOriginalItemForSlot(int32 SlotIndex, UInv_InventoryItem* Item)
	{
		for (FInv_AttachedItemData& Data : AttachedItems)
		{
			if (Data.SlotIndex == SlotIndex)
			{
				Data.OriginalItem = Item;
				return;
			}
		}
	}

	// ── 디자인타임 값 복원 (세이브/로드 후) ──
	void RestoreDesignTimeSlotPositions(const TArray<FInv_AttachmentSlotDef>& CDOSlotDefs);

	// ── 부착물 스탯 일괄 적용/해제 ──

	void OnEquipAllAttachments(APlayerController* PC);
	void OnUnequipAllAttachments(APlayerController* PC);

	// ════════════════════════════════════════════════════════════════
	// 📌 [Phase 4] Manifest 시 AttachedItems 보존
	// ════════════════════════════════════════════════════════════════
	// 기본 Manifest()는 Fragment를 초기화하지만
	// AttachedItems는 런타임 장착 데이터이므로 보존해야 함
	// 드롭/줍기 시 부착물 데이터가 유지됨
	// ════════════════════════════════════════════════════════════════
	virtual void Manifest() override;

private:
	// 에디터에서 정의하는 슬롯 배열 (예: 총은 [Scope, Muzzle, Grip] 3개)
	UPROPERTY(EditAnywhere, Category = "부착물", meta = (DisplayName = "슬롯 정의 배열", Tooltip = "이 무기가 가진 부착물 슬롯 목록"))
	TArray<FInv_AttachmentSlotDef> SlotDefinitions;

	// 런타임: 현재 장착된 부착물 목록 (서버 RPC로 변경됨)
	UPROPERTY()
	TArray<FInv_AttachedItemData> AttachedItems;
};


// ════════════════════════════════════════════════════════════════════════════════
// 📌 FInv_AttachableFragment - 부착 가능 아이템 프래그먼트
// ════════════════════════════════════════════════════════════════════════════════
// "이 아이템은 부착물입니다. 특정 슬롯에 들어갈 수 있습니다"
// 스코프, 소음기, 그립 등 부착물 BP의 ItemManifest에 추가하여 사용
// AttachmentType: 어떤 슬롯에 끼울 수 있는지 (SlotType과 매칭)
// EquipModifiers: 장착 시 적용되는 스탯 효과 (기존 구조 재활용)
// ════════════════════════════════════════════════════════════════════════════════
USTRUCT(BlueprintType)
struct FInv_AttachableFragment : public FInv_InventoryItemFragment
{
	GENERATED_BODY()

	// 이 부착물이 해당 슬롯에 호환되는지 검사
	bool CanAttachToSlot(const FInv_AttachmentSlotDef& SlotDef) const;

	// 부착물 장착/해제 시 스탯 적용
	void OnEquip(APlayerController* PC);
	void OnUnequip(APlayerController* PC);

	// 부착물 타입 태그 접근
	FGameplayTag GetAttachmentType() const { return AttachmentType; }

	// 부착물 메시 접근 (Phase 5 시각적 표현용)
	UStaticMesh* GetAttachmentMesh() const { return AttachmentMesh; }
	const FTransform& GetAttachOffset() const { return AttachOffset; }

	// [Phase 7] 효과 플래그 Getter
	bool GetIsSuppressor() const { return bIsSuppressor; }
	float GetZoomFOVOverride() const { return ZoomFOVOverride; }
	bool GetIsLaser() const { return bIsLaser; }

	// UI 동화 / Manifest 초기화
	virtual void Assimilate(UInv_CompositeBase* Composite) const override;
	virtual void Manifest() override;

private:
	// 이 부착물이 들어갈 수 있는 슬롯 타입
	// 예: "AttachmentSlot.Scope" → Scope 슬롯에만 장착 가능
	UPROPERTY(EditAnywhere, Category = "부착물", meta = (Categories = "AttachmentSlot", DisplayName = "부착물 타입", Tooltip = "이 부착물이 장착될 수 있는 슬롯 타입 태그"))
	FGameplayTag AttachmentType;

	// 소켓에 부착될 메시 (Phase 5에서 사용)
	UPROPERTY(EditAnywhere, Category = "부착물", meta = (DisplayName = "부착물 메시", Tooltip = "무기 소켓에 부착될 스태틱 메시"))
	TObjectPtr<UStaticMesh> AttachmentMesh = nullptr;

	// 소켓 기준 오프셋 (위치/회전 미세 조정)
	UPROPERTY(EditAnywhere, Category = "부착물", meta = (DisplayName = "부착 오프셋", Tooltip = "소켓 기준 위치/회전 오프셋"))
	FTransform AttachOffset{FTransform::Identity};

	// 장착 시 적용되는 스탯 효과 (기존 EquipModifier 구조 재활용)
	// 예: DamageModifier +5, ArmorModifier +3
	UPROPERTY(EditAnywhere, Category = "부착물", meta = (ExcludeBaseStruct, DisplayName = "장착 효과 목록", Tooltip = "부착물 장착 시 적용될 스탯 효과들"))
	TArray<TInstancedStruct<FInv_EquipModifier>> EquipModifiers;

	// ════════════════════════════════════════════════════════════════
	// [Phase 7] 부착물 효과 플래그
	// ════════════════════════════════════════════════════════════════
	// BP 에디터에서 체크/값 입력으로 효과를 설정한다.
	// EquipActor::ApplyAttachmentEffects / RemoveAttachmentEffects에서 getter로 읽는다.
	// 새 효과 추가 시 여기에 UPROPERTY + getter 1쌍만 추가하면 된다.
	// ════════════════════════════════════════════════════════════════

	// 소음기 여부 — true이면 EquipActor의 SuppressedFireSound를 사용한다
	UPROPERTY(EditAnywhere, Category = "부착물|효과",
		meta = (DisplayName = "소음기 여부",
				Tooltip = "체크하면 무기 BP에 설정된 소음기 사운드로 전환"))
	bool bIsSuppressor = false;

	// 줌 FOV 오버라이드 — 0보다 크면 조준 시 이 FOV를 적용한다
	UPROPERTY(EditAnywhere, Category = "부착물|효과",
		meta = (DisplayName = "줌 FOV 오버라이드",
				Tooltip = "0보다 크면 조준 시 이 FOV 사용 (예: 45 = 약 2배율)",
				ClampMin = 0.0, ClampMax = 120.0))
	float ZoomFOVOverride = 0.f;

	// 레이저 여부 — true이면 EquipActor의 LaserBeamComponent를 활성화한다
	UPROPERTY(EditAnywhere, Category = "부착물|효과",
		meta = (DisplayName = "레이저 여부",
				Tooltip = "체크하면 무기의 레이저 컴포넌트를 활성화"))
	bool bIsLaser = false;
};