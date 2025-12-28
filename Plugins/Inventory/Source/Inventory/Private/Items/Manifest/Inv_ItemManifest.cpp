#include "Items/Manifest/Inv_ItemManifest.h"

#include "Items/Inv_InventoryItem.h"
#include "Items/Components/Inv_ItemComponent.h"
#include "Items/Fragments/Inv_ItemFragment.h"
#include "Widgets/Composite/Inv_CompositeBase.h"

UInv_InventoryItem* FInv_ItemManifest::Manifest(UObject* NewOuter) // 인벤토리의 인터페이스? 복사본이라고?
{
	UInv_InventoryItem* Item = NewObject<UInv_InventoryItem>(NewOuter, UInv_InventoryItem::StaticClass()); // 새로운 객체는 뭐가 될지 Input 파라미터

	//재고 항목
	Item->SetItemManifest(*this); // 이 매니페스트로 아이템 매니페스트 설정

	//비어있더라도 호출 해주는 함수
	for (auto& Fragment : Item->GetItemManifestMutable().GetFragmentsMutable()) // 각 프래그먼트에 대해
	{
		Fragment.GetMutable().Manifest(); // 프래그먼트 매니페스트 호출
	}
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
	// TODO : 아이템 픽업 액터 생성 로직 구현
	if (!(PickupActorClass) || !IsValid(WorldContextObject)) return; // 픽업 액터 클래스가 유효하지 않거나 월드 컨텍스트 객체가 유효하지 않으면 반환
	
	AActor* SpawnActor = WorldContextObject->GetWorld()->SpawnActor<AActor>(PickupActorClass, SpawnLocation, SpawnRotation); // 픽업 액터 생성
	if (!IsValid(SpawnActor)) return;
	
	// Set the item manifest, item category, item type, etc.
	// 아이템 매니페스트, 아이템 카테고리, 아이템 타입 등을 설정하는 부분
	UInv_ItemComponent* ItemComp = SpawnActor->FindComponentByClass<UInv_ItemComponent>();
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
}
