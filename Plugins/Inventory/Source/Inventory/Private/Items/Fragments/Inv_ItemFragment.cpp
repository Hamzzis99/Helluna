#include "Items/Fragments/Inv_ItemFragment.h"

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
	GEngine->AddOnScreenDebugMessage(-1, 5.f, FColor::Green, 
		FString::Printf(TEXT("장비를 장착한 아이템 힘 증가 테스트ㅣ (Strength increased by : %f"), GetValue()));
}

void FInv_StrengthModifier::OnUnequip(APlayerController* PC)
{
	//디버그 메시지
	GEngine->AddOnScreenDebugMessage(-1, 5.f, FColor::Green, 
		FString::Printf(TEXT("장비를 해제한 아이템 힘 증가 테스트 (Item unequipped. Strength decreased by : %f"), GetValue()));
}

// 장착 해제 부분들
void FInv_EquipmentFragment::OnEquip(APlayerController* PC)
{
	if (bEquipped) return;
	bEquipped = true;
	for (auto& Modifier : EquipModifiers)
	{
		auto& ModRef = Modifier.GetMutable(); // 수정 가능한 참조 얻기
		ModRef.OnEquip(PC); // 장착 함수 호출
	}
}

void FInv_EquipmentFragment::OnUnequip(APlayerController* PC)
{
	if (!bEquipped) return;
	bEquipped = false;
	for (auto& Modifier : EquipModifiers)
	{
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
