// Gihyeon's Inventory Project

//플레이어 컨트롤러와 관련이 있네?

#include "EquipmentManagement/Components/Inv_EquipmentComponent.h"

#include "EquipmentManagement/EquipActor/Inv_EquipActor.h"
#include "GameFramework/Character.h"
#include "GameFramework/PlayerController.h"
#include "InventoryManagement/Components/Inv_InventoryComponent.h"
#include "InventoryManagement/Utils/Inv_InventoryStatics.h"
#include "Items/Inv_InventoryItem.h"
#include "Items/Fragments/Inv_ItemFragment.h"

//프록시 매시 부분
void UInv_EquipmentComponent::SetOwningSkeletalMesh(USkeletalMeshComponent* OwningMesh)
{
	OwningSkeletalMesh = OwningMesh;
}

void UInv_EquipmentComponent::InitializeOwner(APlayerController* PlayerController)
{
	if (IsValid(PlayerController))
	{
		OwningPlayerController = PlayerController;
	}
	InitInventoryComponent();
}

//프록시 매시 부분

void UInv_EquipmentComponent::BeginPlay()
{
	Super::BeginPlay();
	
	InitPlayerController();
}

// 플레이어 컨트롤러 초기화
void UInv_EquipmentComponent::InitPlayerController()
{
	
	if (OwningPlayerController = Cast<APlayerController>(GetOwner()); OwningPlayerController.IsValid()) // 소유자가 플레이어 컨트롤러인지 확인
	{
		if (ACharacter* OwnerCharacter = Cast<ACharacter>(OwningPlayerController->GetPawn()); IsValid(OwnerCharacter)) // 플레이어 컨트롤러의 폰이 캐릭터인지 확인
		{
			OnPossessedPawnChange(nullptr, OwnerCharacter); // 이미 폰이 소유된 경우 즉시 호출
		}
		else
		{
			OwningPlayerController->OnPossessedPawnChanged.AddDynamic(this, &ThisClass::OnPossessedPawnChange);  //컨트롤러를 멀티캐스트 델리게이트 식으로. 위임하는 부분. (이미 완료 됐을 경우 이걸 호출 안 하니 깔끔해진다.)
		}
	}
}

// 폰 변경 시 호출되는 함수 (컨트롤러 소유권?) <- 아이템을 장착하면 Pawn이 바뀌니까 그 것을 이제 다시 절차적으로 검증 시키는 역할 (말투 정교화가 필요하다.)
void UInv_EquipmentComponent::OnPossessedPawnChange(APawn* OldPawn, APawn* NewPawn)
{
	if (ACharacter* OwnerCharacter = Cast<ACharacter>(OwningPlayerController->GetPawn()); IsValid(OwnerCharacter)) // 플레이어 컨트롤러의 폰이 캐릭터인지 확인
	{
		OwningSkeletalMesh = OwnerCharacter->GetMesh(); // 멀티플레이를 코딩할 때 이 부분이 중요함. (지금 InitInventoryComponent) 부분을 보면 Nullptr이 반환 되잖아. (멀티플레이에 고려해야 할 것은 Controller 부분이구나.)
	}
	InitInventoryComponent();
}


void UInv_EquipmentComponent::InitInventoryComponent()
{
	// 인벤토리 컴포넌트 가져오기
	InventoryComponent = UInv_InventoryStatics::GetInventoryComponent(OwningPlayerController.Get());
	if (!InventoryComponent.IsValid()) return;
	// 델리게이트 바인딩 
	if (!InventoryComponent->OnItemEquipped.IsAlreadyBound(this, &ThisClass::OnItemEquipped))
	{
		InventoryComponent->OnItemEquipped.AddDynamic(this, &ThisClass::OnItemEquipped);
	}
	// 델리게이트 바인딩
	if (!InventoryComponent->OnItemUnequipped.IsAlreadyBound(this, &ThisClass::OnItemUnequipped))
	{
		InventoryComponent->OnItemUnequipped.AddDynamic(this, &ThisClass::OnItemUnequipped);
	}
}

// 장착된 액터 스폰
AInv_EquipActor* UInv_EquipmentComponent::SpawnEquippedActor(FInv_EquipmentFragment* EquipmentFragment, const FInv_ItemManifest& Manifest, USkeletalMeshComponent* AttachMesh)
{
	AInv_EquipActor* SpawnedEquipActor = EquipmentFragment->SpawnAttachedActor(AttachMesh); // 장착된 액터 스폰
	if (!IsValid(SpawnedEquipActor)) return nullptr; // 장착 아이템이 없을 시 크래쉬 예외 처리 제거
	
	SpawnedEquipActor->SetEquipmentType(EquipmentFragment->GetEquipmentType()); // 장비 타입 설정 (게임플레이 태그)
	SpawnedEquipActor->SetOwner(GetOwner()); // 소유자 설정
	EquipmentFragment->SetEquippedActor(SpawnedEquipActor); // 장착된 액터 설정
	return SpawnedEquipActor;
}

AInv_EquipActor* UInv_EquipmentComponent::FindEquippedActor(const FGameplayTag& EquipmentTypeTag)
{
	auto FoundActor = EquippedActors.FindByPredicate([&EquipmentTypeTag](const AInv_EquipActor* EquippedActor)
	{
		return EquippedActor->GetEquipmentType().MatchesTagExact(EquipmentTypeTag);
	});
	return FoundActor ? *FoundActor : nullptr; // 액터를 찾았으면? 찾아서 제거를 시킨다. (에러 날 수도 있음.)
}

// 이거는 하나에만 있다는 가정하에 있는 것. 만약 무기를 두 개 만들 시 왼손 오른손 두 개로 나워야함.
void UInv_EquipmentComponent::RemoveEquippedActor(const FGameplayTag& EquipmentTypeTag)
{
	if (AInv_EquipActor* EquippedActor = FindEquippedActor(EquipmentTypeTag); IsValid(EquippedActor)) // 이걸 잘 활용해서 무기를 두 개 장착할 수 있게 해보자.
	{
		EquippedActors.Remove(EquippedActor);
		EquippedActor->Destroy();
	}
}

// 아이템 장착 시 호출되는 함수
void UInv_EquipmentComponent::OnItemEquipped(UInv_InventoryItem* EquippedItem)
{
	if (!IsValid(EquippedItem)) return;
	if (!OwningPlayerController->HasAuthority()) return;

	FInv_ItemManifest& ItemManifest = EquippedItem->GetItemManifestMutable();
	FInv_EquipmentFragment* EquipmentFragment = ItemManifest.GetFragmentOfTypeMutable<FInv_EquipmentFragment>();
	if (!EquipmentFragment) return;
	
	if (!bIsProxy) // 프록시 부분
	{
		EquipmentFragment->OnEquip(OwningPlayerController.Get());
	}
	
	if (!OwningSkeletalMesh.IsValid()) return;
	AInv_EquipActor* SpawnedEquipActor = SpawnEquippedActor(EquipmentFragment, ItemManifest, OwningSkeletalMesh.Get()); // 장비 아이템을 장착하면서 필요 조건들 
	
	EquippedActors.Add(SpawnedEquipActor); // 장착된 장비를 착용시켜주는 것. (태그를 찾고)
}

// 아이템 해제 시 호출되는 함수
void UInv_EquipmentComponent::OnItemUnequipped(UInv_InventoryItem* UnequippedItem)
{
	if (!IsValid(UnequippedItem)) return;
	if (!OwningPlayerController->HasAuthority()) return;

	FInv_ItemManifest& ItemManifest = UnequippedItem->GetItemManifestMutable();
	FInv_EquipmentFragment* EquipmentFragment = ItemManifest.GetFragmentOfTypeMutable<FInv_EquipmentFragment>();
	if (!EquipmentFragment) return;

	if (!bIsProxy) // 프록시 부분
	{
		EquipmentFragment->OnUnequip(OwningPlayerController.Get());
	}
	
	//장비 제거하는 부분
	RemoveEquippedActor(EquipmentFragment->GetEquipmentType());
}






