#include "Items/Manifest/Inv_ItemManifest.h"

#include "Items/Inv_InventoryItem.h"
#include "Items/Components/Inv_ItemComponent.h"
#include "Items/Fragments/Inv_ItemFragment.h"
#include "Items/Fragments/Inv_AttachmentFragments.h"
#include "Widgets/Composite/Inv_CompositeBase.h"
#include "Serialization/MemoryWriter.h"
#include "Serialization/MemoryReader.h"
#include "Serialization/ObjectAndNameAsStringProxyArchive.h"
#include "Inventory.h"

UInv_InventoryItem* FInv_ItemManifest::Manifest(UObject* NewOuter) // 인벤토리의 인터페이스? 복사본이라고?
{
	UInv_InventoryItem* Item = NewObject<UInv_InventoryItem>(NewOuter, UInv_InventoryItem::StaticClass()); // 새로운 객체는 뭐가 될지 Input 파라미터

	// ★ [Phase 8 진단] Manifest 복사 전 — 원본 SlotPosition 확인
	{
		const FInv_AttachmentHostFragment* DiagHost = GetFragmentOfType<FInv_AttachmentHostFragment>();
		if (DiagHost)
		{
			const auto& DiagSlots = DiagHost->GetSlotDefinitions();
			UE_LOG(LogTemp, Error, TEXT("[Phase8진단] Manifest 복사 전: SlotDefs=%d"), DiagSlots.Num());
			for (int32 d = 0; d < DiagSlots.Num(); ++d)
			{
				UE_LOG(LogTemp, Error, TEXT("[Phase8진단]   [%d] %s → Position=%d"), d,
					*DiagSlots[d].SlotType.ToString(), (int32)DiagSlots[d].SlotPosition);
			}
		}
		const FInv_EquipmentFragment* DiagEquip = GetFragmentOfType<FInv_EquipmentFragment>();
		if (DiagEquip)
		{
			UE_LOG(LogTemp, Error, TEXT("[Phase8진단] Manifest 복사 전: PreviewMesh=%s"),
				DiagEquip->HasPreviewMesh() ? TEXT("있음") : TEXT("없음"));
		}
	}

	//재고 항목
	Item->SetItemManifest(*this); // 이 매니페스트로 아이템 매니페스트 설정

	// ★ [Phase 8 진단] Manifest 복사 후 — 복사본 SlotPosition 확인
	{
		const FInv_AttachmentHostFragment* DiagHost = Item->GetItemManifest().GetFragmentOfType<FInv_AttachmentHostFragment>();
		if (DiagHost)
		{
			const auto& DiagSlots = DiagHost->GetSlotDefinitions();
			UE_LOG(LogTemp, Error, TEXT("[Phase8진단] Manifest 복사 후: SlotDefs=%d"), DiagSlots.Num());
			for (int32 d = 0; d < DiagSlots.Num(); ++d)
			{
				UE_LOG(LogTemp, Error, TEXT("[Phase8진단]   [%d] %s → Position=%d"), d,
					*DiagSlots[d].SlotType.ToString(), (int32)DiagSlots[d].SlotPosition);
			}
		}
		const FInv_EquipmentFragment* DiagEquip = Item->GetItemManifest().GetFragmentOfType<FInv_EquipmentFragment>();
		if (DiagEquip)
		{
			UE_LOG(LogTemp, Error, TEXT("[Phase8진단] Manifest 복사 후: PreviewMesh=%s"),
				DiagEquip->HasPreviewMesh() ? TEXT("있음") : TEXT("없음"));
		}
	}

	//비어있더라도 호출 해주는 함수
	for (auto& Fragment : Item->GetItemManifestMutable().GetFragmentsMutable()) // 각 프래그먼트에 대해
	{
		if (!Fragment.IsValid()) continue; // ⚠️ 빈 TInstancedStruct 방어 (GetMutable은 IsValid 실패 시 check 크래시)
		Fragment.GetMutable().Manifest(); // 프래그먼트 매니페스트 호출
	}
	Item->GetItemManifestMutable().BuildFragmentCache(); // ⭐ [최적화 #3] 아이템의 Fragment 캐시 구축
	ClearFragments();

	return Item;
}

void FInv_ItemManifest::AssimilateInventoryFragments(UInv_CompositeBase* Composite) const// 인벤토리 구성요소 동화
{
	const auto& InventoryItemFragments = GetAllFragmentsOfType<FInv_InventoryItemFragment>(); // 모든 인벤토리 아이템 프래그먼트 가져오기
	for (const auto* Fragment : InventoryItemFragments) // 각 프래그먼트에 대해
	{
		Composite->ApplyFunction([Fragment](UInv_CompositeBase* Widget)// 이 apply 함수는 람다 뿐만이 아닌 모든 자식 노드(leaf)에도 적용해줌
		{
			Fragment->Assimilate(Widget); // 프래그먼트 동화
		});
	}
}

// 아이템 픽업 액터 생성
void FInv_ItemManifest::SpawnPickupActor(const UObject* WorldContextObject, const FVector& SpawnLocation, const FRotator& SpawnRotation)
{
	if (!IsValid(PickupActorClass) || !IsValid(WorldContextObject)) return; // 픽업 액터 클래스가 유효하지 않거나 월드 컨텍스트 객체가 유효하지 않으면 반환

	AActor* SpawnedActor = WorldContextObject->GetWorld()->SpawnActor<AActor>(PickupActorClass, SpawnLocation, SpawnRotation); // 픽업 액터 생성
	if (!IsValid(SpawnedActor)) return; 

	// Set the item manifest, item category, item type, etc.
	// 아이템 매니페스트, 아이템 카테고리, 아이템 타입 등을 설정하는 부분
	UInv_ItemComponent* ItemComp = SpawnedActor->FindComponentByClass<UInv_ItemComponent>();
	check(ItemComp); // ItemComp가 유효한지 확인

	ItemComp->InitItemManifest(*this); // 아이템 매니페스트 초기화
}

void FInv_ItemManifest::ClearFragments()
{
	for (auto& Fragment : Fragments)
	{
		Fragment.Reset();
	}
	Fragments.Empty();
	FragmentTypeCache.Reset(); // ⭐ [최적화 #3] 캐시도 초기화
}

// ⭐ [최적화 #3] Fragment 타입별 인덱스 캐시 구축
void FInv_ItemManifest::BuildFragmentCache()
{
	FragmentTypeCache.Reset();
	FragmentTypeCache.Reserve(Fragments.Num());
	for (int32 i = 0; i < Fragments.Num(); ++i)
	{
		if (Fragments[i].IsValid())
		{
			FragmentTypeCache.Add(Fragments[i].GetScriptStruct(), i);
		}
	}
}

// ════════════════════════════════════════════════════════════════════════════════
// 📌 [Phase 1 최적화] Manifest Fragment 직렬화
// ════════════════════════════════════════════════════════════════════════════════
//
// 📌 직렬화 포맷 (바이너리 구조):
//    [4바이트: Fragment 개수 (int32)]
//    [Fragment 0: TInstancedStruct의 네이티브 직렬화]
//    [Fragment 1: TInstancedStruct의 네이티브 직렬화]
//    ...
//
// 📌 TInstancedStruct의 네이티브 직렬화가 포함하는 것:
//    - UScriptStruct 포인터 (어떤 USTRUCT인지)
//    - 해당 USTRUCT의 모든 UPROPERTY 값
//    → FInv_LabeledNumberFragment의 Value, Min, Max, bRandomizeOnManifest 등
//    → FInv_AttachmentHostFragment의 SlotDefinitions, AttachedItems 등
//    → FInv_EquipmentFragment의 EquipModifiers, EquipActorClass 등
//
// 📌 주의사항:
//    - UObject 포인터(예: UStaticMesh*)는 패키지 경로로 직렬화됨
//      → 해당 에셋이 존재해야 역직렬화 성공
//    - SaveVersion 불일치 시 역직렬화 실패 가능
//      → DeserializeAndApplyFragments에서 실패 시 기존 Fragment 유지
//
// ════════════════════════════════════════════════════════════════════════════════

TArray<uint8> FInv_ItemManifest::SerializeFragments() const
{
	TArray<uint8> OutData;

	// ── 빈 Fragment 체크 ──
	if (Fragments.Num() == 0)
	{
#if INV_DEBUG_SAVE
		UE_LOG(LogTemp, Warning,
			TEXT("[ManifestSerialize] SerializeFragments: Fragment 0개 — 빈 배열 반환. ItemType=%s"),
			*ItemType.ToString());
#endif
		return OutData;
	}

	// ── FMemoryWriter로 직렬화 ──
	// bIsPersistent = true: SaveGame용 영속 직렬화
	FMemoryWriter MemWriter(OutData, /*bIsPersistent=*/ true);

	// FObjectAndNameAsStringProxyArchive로 감싸서 UObject 포인터를 패키지 경로 문자열로 변환
	// Fragment 안의 TObjectPtr<UTexture2D>, TSubclassOf<AInv_EquipActor> 등을 처리
	FObjectAndNameAsStringProxyArchive Writer(MemWriter, /*bInLoadingPhase=*/ false);
	Writer.SetIsPersistent(true);

	// Fragment 개수 먼저 기록
	int32 FragmentCount = Fragments.Num();
	Writer << FragmentCount;

	// 각 TInstancedStruct를 네이티브 직렬화
	for (const TInstancedStruct<FInv_ItemFragment>& Fragment : Fragments)
	{
		// TInstancedStruct는 const_cast 없이 직렬화 불가 (FArchive::operator<<는 non-const)
		// UE 내부적으로도 Save 시 동일 패턴 사용
		TInstancedStruct<FInv_ItemFragment>& MutableFragment =
			const_cast<TInstancedStruct<FInv_ItemFragment>&>(Fragment);
		MutableFragment.Serialize(Writer);
	}

#if INV_DEBUG_SAVE
	UE_LOG(LogTemp, Warning,
		TEXT("[ManifestSerialize] SerializeFragments 완료: ItemType=%s, Fragment=%d개, 바이트=%d"),
		*ItemType.ToString(), FragmentCount, OutData.Num());
#endif

	return OutData;
}

// ════════════════════════════════════════════════════════════════════════════════
// 📌 [Phase 1 최적화] Manifest Fragment 역직렬화
// ════════════════════════════════════════════════════════════════════════════════
//
// 📌 처리 흐름:
//    1. 바이트 배열에서 Fragment 개수 읽기
//    2. 각 TInstancedStruct 역직렬화
//    3. 현재 Fragments 배열을 역직렬화된 것으로 교체
//
// 📌 실패 시 동작:
//    - 기존 Fragments 유지 (CDO 기본값)
//    - false 반환 → 호출자가 로그 출력
//
// 📌 성공 시 동작:
//    - 기존 Fragments 배열을 완전히 교체
//    - bRandomizeOnManifest가 false로 저장되어 있으므로
//      이후 Manifest() 호출되어도 재랜덤 안 됨
//    - 부착물 데이터(AttachmentHostFragment의 AttachedItems)도 복원됨
//
// ════════════════════════════════════════════════════════════════════════════════

bool FInv_ItemManifest::DeserializeAndApplyFragments(const TArray<uint8>& InData)
{
	// ── 빈 데이터 체크 ──
	if (InData.Num() == 0)
	{
#if INV_DEBUG_SAVE
		UE_LOG(LogTemp, Warning,
			TEXT("[ManifestSerialize] DeserializeAndApplyFragments: 빈 데이터 — 기존 Fragment 유지. ItemType=%s"),
			*ItemType.ToString());
#endif
		return false;
	}

	// ── FMemoryReader로 역직렬화 ──
	FMemoryReader MemReader(InData, /*bIsPersistent=*/ true);

	// FObjectAndNameAsStringProxyArchive로 감싸서 패키지 경로 문자열 → UObject 포인터 복원
	FObjectAndNameAsStringProxyArchive Reader(MemReader, /*bInLoadingPhase=*/ true);
	Reader.SetIsPersistent(true);

	// Fragment 개수 읽기
	int32 FragmentCount = 0;
	Reader << FragmentCount;

	// 개수 유효성 검증 (음수이거나 비정상적으로 크면 거부)
	if (FragmentCount < 0 || FragmentCount > 100)
	{
		UE_LOG(LogTemp, Error,
			TEXT("[ManifestSerialize] ❌ DeserializeAndApplyFragments: 비정상 FragmentCount=%d — 역직렬화 중단. ItemType=%s"),
			FragmentCount, *ItemType.ToString());
		return false;
	}

	// 임시 배열에 역직렬화 (실패 시 원본 보호)
	TArray<TInstancedStruct<FInv_ItemFragment>> TempFragments;
	TempFragments.Reserve(FragmentCount);

	for (int32 i = 0; i < FragmentCount; ++i)
	{
		TInstancedStruct<FInv_ItemFragment> NewFragment;
		NewFragment.Serialize(Reader);

		// 역직렬화된 Fragment의 ScriptStruct가 유효한지 검증
		if (!NewFragment.IsValid())
		{
			UE_LOG(LogTemp, Error,
				TEXT("[ManifestSerialize] ❌ Fragment[%d] 역직렬화 실패 (IsValid=false) — 전체 복원 중단. ItemType=%s"),
				i, *ItemType.ToString());
			return false;
		}

		TempFragments.Add(MoveTemp(NewFragment));
	}

	// Reader에 에러가 있었는지 확인
	if (Reader.IsError())
	{
		UE_LOG(LogTemp, Error,
			TEXT("[ManifestSerialize] ❌ FMemoryReader 에러 발생 — 전체 복원 중단. ItemType=%s"),
			*ItemType.ToString());
		return false;
	}

	// ── 성공: 기존 Fragments를 교체 ──
	Fragments = MoveTemp(TempFragments);
	BuildFragmentCache(); // ⭐ [최적화 #3] 역직렬화 후 캐시 재구축

#if INV_DEBUG_SAVE
	UE_LOG(LogTemp, Warning,
		TEXT("[ManifestSerialize] ✅ DeserializeAndApplyFragments 성공: ItemType=%s, Fragment=%d개 복원"),
		*ItemType.ToString(), Fragments.Num());

	// 복원된 Fragment 타입 목록 출력
	for (int32 i = 0; i < Fragments.Num(); ++i)
	{
		const UScriptStruct* ScriptStruct = Fragments[i].GetScriptStruct();
		UE_LOG(LogTemp, Warning,
			TEXT("[ManifestSerialize]   [%d] %s"),
			i, ScriptStruct ? *ScriptStruct->GetName() : TEXT("nullptr"));
	}
#endif

	return true;
}
