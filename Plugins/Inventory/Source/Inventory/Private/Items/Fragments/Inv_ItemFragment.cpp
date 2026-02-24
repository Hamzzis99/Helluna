#include "Items/Fragments/Inv_ItemFragment.h"
#include "Inventory.h"

#include "EquipmentManagement/EquipActor/Inv_EquipActor.h"
#include "Widgets/Composite/Inv_CompositeBase.h"
#include "Widgets/Composite/Inv_Leaf_Image.h"
#include "Widgets/Composite/Inv_Leaf_LabeledValue.h"
#include "Widgets/Composite/Inv_Leaf_Text.h"
#include "Windows/WindowsApplication.h"


// 아이템 프래그먼트 동화 (확장 시키는 역할)
void FInv_InventoryItemFragment::Assimilate(UInv_CompositeBase* Composite) const
{
	if (!MatchesWidgetTag(Composite)) return;
	Composite->Expand();
}

bool FInv_InventoryItemFragment::MatchesWidgetTag(const UInv_CompositeBase* Composite) const
{
	return Composite->GetFragmentTag().MatchesTagExact(GetFragmentTag());
}

void FInv_ImageFragment::Assimilate(UInv_CompositeBase* Composite) const
{
	FInv_InventoryItemFragment::Assimilate(Composite); // 위젯 태그가 일치한지 확인하는 법.
	if (!MatchesWidgetTag(Composite)) return;
	
	UInv_Leaf_Image* Image = Cast<UInv_Leaf_Image>(Composite);
	if (!IsValid(Image)) return;
	
	Image->SetImage(Icon); // 아이콘 설정
	Image->SetBoxSize(IconDimensions); // 박스 크기 설정
	Image->SetImageSize(IconDimensions); // 이미지 크기 설정
}

void FInv_TextFragment::Assimilate(UInv_CompositeBase* Composite) const
{
	FInv_InventoryItemFragment::Assimilate(Composite);
	if (!MatchesWidgetTag(Composite)) return;
	
	UInv_Leaf_Text* LeafText = Cast<UInv_Leaf_Text>(Composite);
	if (!IsValid(LeafText)) return;
	
	LeafText->SetText(FragmentText); // 텍스트 설정
}


// LabeledNumberFragment 라벨이 지정된 숫자를 전달해서 개수와 효과를 나오게 하는 것.
void FInv_LabeledNumberFragment::Assimilate(UInv_CompositeBase* Composite) const
{
	FInv_InventoryItemFragment::Assimilate(Composite);
	if (!MatchesWidgetTag(Composite)) return;
	
	UInv_Leaf_LabeledValue* LabeledValue = Cast<UInv_Leaf_LabeledValue>(Composite);
	if (!IsValid(LabeledValue)) return;

	LabeledValue->SetText_Label(Text_Label, bCollapseLabel);

	FNumberFormattingOptions Options;
	Options.MinimumFractionalDigits = MinFractionalDigits;
	Options.MaximumFractionalDigits = MaxFractionalDigits;
	
	LabeledValue->SetText_Value(FText::AsNumber(Value, &Options), bCollapseValue);
}

void FInv_LabeledNumberFragment::Manifest()
{
	FInv_InventoryItemFragment::Manifest();
	
	if (bRandomizeOnManifest)
	{
		Value = FMath::FRandRange(Min, Max); // 무작위 값 설정
	}
	bRandomizeOnManifest = false;
}

void FInv_ConsumableFragment::OnConsume(APlayerController* PC)
{
	for (auto& Modifier : ConsumeModifiers)
	{
		if (!Modifier.IsValid()) continue;
		auto& ModRef = Modifier.GetMutable();
		ModRef.OnConsume(PC);
	}
}

// 소모품 사용에 있어서 고정 값 사용이 아닌 동시에 값을 사용할 수 있게 동기화 해주는 부분
void FInv_ConsumableFragment::Assimilate(UInv_CompositeBase* Composite) const
{
	FInv_InventoryItemFragment::Assimilate(Composite);
	for (const auto& Modifier : ConsumeModifiers)
	{
		const auto& ModRef = Modifier.Get();
		ModRef.Assimilate(Composite);
	}
}

void FInv_ConsumableFragment::Manifest() // 모든 자식 Fragment를 불러주는 역할.
{
	FInv_InventoryItemFragment::Manifest();
	for (auto& Modifier : ConsumeModifiers)
	{
		if (!Modifier.IsValid()) continue;
		auto& ModRef = Modifier.GetMutable();
		ModRef.Manifest();
	}
}

void FInv_HealthPotionFragment::OnConsume(APlayerController* PC)
{
	// 1. Get a stats component from the PC or the PC->GetPawn()
	// PC 또는 PC->GetPawn()에서 Stats Component를 가져오거나
	// or get the Ability System Component and apply a Gameplay
	// Ability System Component를 통해 Gameplay Effect 적용
	// or call an interface function for Healing()
	// 힐링을 위한 인터페이스 함수 호출
	
	//디버그 메시지
	GEngine->AddOnScreenDebugMessage(-1, 5.f, FColor::Green, 
		FString::Printf(TEXT("Health Potion consumed! Healing by: %f"), GetValue()));
}

void FInv_ManaPotionFragment::OnConsume(APlayerController* PC)
{
	//Replenish mana however you wish
	
	//디버그 메시지
	GEngine->AddOnScreenDebugMessage(-1,5.f,FColor::Blue,
		FString::Printf(TEXT("Consumed Mana Potion! Healed for %f HP"), GetValue()));
}

//장비 장착 관련
void FInv_StrengthModifier::OnEquip(APlayerController* PC)
{
	//디버그 메시지
	GEngine->AddOnScreenDebugMessage(
		-1,
		5.f,
		FColor::Green,
		FString::Printf(TEXT("Strength increased by: %f"),
			GetValue()));
}

void FInv_StrengthModifier::OnUnequip(APlayerController* PC)
{
	//디버그 메시지
	GEngine->AddOnScreenDebugMessage(
		-1,
		5.f,
		FColor::Red,
		FString::Printf(TEXT("Item unequipped. Strength decreased by: %f"),
			GetValue()));
}


// 각 장비마다 방어구 장비 관련 장착 아이템들
void FInv_ArmorModifier::OnEquip(APlayerController* PC)
{
	GEngine->AddOnScreenDebugMessage(
		-1,
		5.f,
		FColor::Green,
		FString::Printf(TEXT("Item equipped. Armor increased by: %f"),
			GetValue()));
}

void FInv_ArmorModifier::OnUnequip(APlayerController* PC)
{
	GEngine->AddOnScreenDebugMessage(
		-1,
		5.f,
		FColor::Red,
		FString::Printf(TEXT("Item unequipped. Armor decreased by: %f"),
			GetValue()));
}

//무기 장비 관련 장착 아이템들
void FInv_DamageModifier::OnEquip(APlayerController* PC)
{
	GEngine->AddOnScreenDebugMessage(
		-1,
		5.f,
		FColor::Green,
		FString::Printf(TEXT("Item equipped. Damage increased by: %f"),
			GetValue()));
}

void FInv_DamageModifier::OnUnequip(APlayerController* PC)
{
	GEngine->AddOnScreenDebugMessage(
		-1,
		5.f,
		FColor::Red,
		FString::Printf(TEXT("Item equipped. Damage increased by: %f"),
			GetValue()));
}
// 여기까지가 무기 관련 장착 장비들


// 전체적인 장비 장착 메인 부분
void FInv_EquipmentFragment::OnEquip(APlayerController* PC)
{
	if (bEquipped) return;
	bEquipped = true;
	for (auto& Modifier : EquipModifiers)
	{
		if (!Modifier.IsValid()) continue;
		auto& ModRef = Modifier.GetMutable(); // 수정 가능한 참조 얻기
		ModRef.OnEquip(PC); // 장착 함수 호출
	}
}
// 장비 장착 해제 부분들
void FInv_EquipmentFragment::OnUnequip(APlayerController* PC)
{
	if (!bEquipped) return;
	bEquipped = false;
	for (auto& Modifier : EquipModifiers)
	{
		if (!Modifier.IsValid()) continue;
		auto& ModRef = Modifier.GetMutable(); // 수정 가능한 참조 얻기
		ModRef.OnUnequip(PC); // 해제 함수 호출
	}
}

// 장비를 뭘 꼇는지 확인하고 이것으로 부터 장착을 시켜주는 것.
void FInv_EquipmentFragment::Assimilate(UInv_CompositeBase* Composite) const
{
	FInv_InventoryItemFragment::Assimilate(Composite);
	for (const auto& Modifier : EquipModifiers)
	{
		const auto& ModRef = Modifier.Get(); // 수정 가능한 참조 얻기
		ModRef.Assimilate(Composite);
	}
}

void FInv_EquipmentFragment::Manifest()
{
	FInv_InventoryItemFragment::Manifest();
	for (auto& Modifier : EquipModifiers)
	{
		if (!Modifier.IsValid()) continue;
		auto& ModRef = Modifier.GetMutable();
		ModRef.Manifest();
	}
}

// 장비 아이템을 장착 시 캐릭터에 장착 시켜주는 것.
AInv_EquipActor* FInv_EquipmentFragment::SpawnAttachedActor(USkeletalMeshComponent* AttachMesh, int32 WeaponSlotIndex) const
{
#if INV_DEBUG_EQUIP
	// ============================================
	// 🔍 [Phase 6 디버깅] 장착 스폰 추적
	// ============================================
	UE_LOG(LogTemp, Warning, TEXT(""));
	UE_LOG(LogTemp, Warning, TEXT("╔══════════════════════════════════════════════════════════════╗"));
	UE_LOG(LogTemp, Warning, TEXT("║  🔍 [EquipmentFragment] SpawnAttachedActor 호출             ║"));
	UE_LOG(LogTemp, Warning, TEXT("╠══════════════════════════════════════════════════════════════╣"));
	UE_LOG(LogTemp, Warning, TEXT("║ WeaponSlotIndex: %d"), WeaponSlotIndex);
	UE_LOG(LogTemp, Warning, TEXT("║ EquipActorClass: %s"), EquipActorClass ? *EquipActorClass->GetName() : TEXT("nullptr ❌"));
	UE_LOG(LogTemp, Warning, TEXT("║ AttachMesh: %s"), AttachMesh ? *AttachMesh->GetName() : TEXT("nullptr ❌"));
	UE_LOG(LogTemp, Warning, TEXT("╚══════════════════════════════════════════════════════════════╝"));
#endif

	if (!IsValid(EquipActorClass) || !IsValid(AttachMesh)) return nullptr;

	AInv_EquipActor* SpawnedActor = AttachMesh->GetWorld()->SpawnActor<AInv_EquipActor>(EquipActorClass);
	if (!IsValid(SpawnedActor)) return nullptr; // 장착 아이템이 없을 시 크래쉬 예외 처리 제거
	
#if INV_DEBUG_EQUIP
	UE_LOG(LogTemp, Warning, TEXT("   ✅ SpawnedActor: %s"), *SpawnedActor->GetName());
#endif
	
	// ⭐ [WeaponBridge] WeaponSlotIndex 설정
	SpawnedActor->SetWeaponSlotIndex(WeaponSlotIndex);
	
	// ⭐ [WeaponBridge] 소켓 결정: WeaponSlotIndex에 따라 등 소켓 선택
	FName ActualSocket = SpawnedActor->GetBackSocketName();
	
	// 기본값(-1)이거나 무기가 아닌 경우 기존 SocketAttachPoint 사용
	if (WeaponSlotIndex < 0 || ActualSocket.IsNone())
	{
		ActualSocket = SocketAttachPoint;
	}
	
#if INV_DEBUG_EQUIP
	UE_LOG(LogTemp, Warning, TEXT("   📍 ActualSocket: %s"), *ActualSocket.ToString());
#endif
	
	SpawnedActor->AttachToComponent(AttachMesh, FAttachmentTransformRules::SnapToTargetNotIncludingScale, ActualSocket);

#if INV_DEBUG_EQUIP
	UE_LOG(LogTemp, Warning, TEXT("   🎉 장착 스폰 완료!"));
	UE_LOG(LogTemp, Warning, TEXT(""));
#endif

	return SpawnedActor;
}

void FInv_EquipmentFragment::DestroyAttachedActor() const
{
	if (EquippedActor.IsValid())
	{
		EquippedActor->Destroy();
	}
}

void FInv_EquipmentFragment::SetEquippedActor(AInv_EquipActor* EquipActor)
{
	EquippedActor = EquipActor;
}

// ════════════════════════════════════════════════════════════════
// 📌 [Phase 8] RestoreDesignTimePreview — 세이브/로드 후 프리뷰 설정 복원
// ════════════════════════════════════════════════════════════════
// 호출 경로: Inv_SaveGameMode::LoadAndSendInventoryToClient → 이 함수
// 처리 흐름:
//   CDO의 EquipmentFragment에서 디자인타임 프리뷰 값을 복사
//   (TSoftObjectPtr 경로, 회전 오프셋, 카메라 거리)
//
// ⚠️ 이유: 프리뷰 필드는 에디터에서 설정하는 디자인타임 값이지만
//    직렬화 시 TSoftObjectPtr 경로가 유실될 수 있으므로
//    로드 후 CDO에서 복원한다 (SlotPosition 복원과 동일 패턴)
// ════════════════════════════════════════════════════════════════
void FInv_EquipmentFragment::RestoreDesignTimePreview(const FInv_EquipmentFragment& CDOEquip)
{
	PreviewStaticMesh = CDOEquip.PreviewStaticMesh;
	PreviewRotationOffset = CDOEquip.PreviewRotationOffset;
	PreviewCameraDistance = CDOEquip.PreviewCameraDistance;
}