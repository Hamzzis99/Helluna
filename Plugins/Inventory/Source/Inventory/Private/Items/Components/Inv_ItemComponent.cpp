// Gihyeon's Inventory Project


#include "Items/Components/Inv_ItemComponent.h"
#include "Net/UnrealNetwork.h"

// Sets default values for this component's properties
UInv_ItemComponent::UInv_ItemComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
	PickupMessage = FString("E - Pick Up Item"); // 기본 픽업 메시지 설정 이거 한글로 되는지 한 번 나중에 검사 TEXT를 쓰라는데..
	SetIsReplicatedByDefault(true); // 기본적으로 복제 설정 (개수 버그 수정 시키는 것.)
}

void UInv_ItemComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	
	DOREPLIFETIME(ThisClass, ItemManifest); // 아이템 매니페스트 복제 설정
}

void UInv_ItemComponent::PickedUp()
{
	OnPickedUp(); // 픽업 이벤트 호출 중단점 설정 시 물건이 사라지지가 않네? 분기점을 나중에 시간나면 걸어보자 (디버깅)
	GetOwner()->Destroy(); // 아이템을 줍게 되면 남아있는 떨어져있는 흔적을 지우게 만드는 것.
}

void UInv_ItemComponent::InitItemManifest(FInv_ItemManifest CopyOfManifest)
{
	ItemManifest = CopyOfManifest; // 아이템 매니페스트 복사본 설정
}
