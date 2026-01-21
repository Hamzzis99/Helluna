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
#include "Abilities/GameplayAbility.h"

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
	// ⭐ 서버/클라이언트 확인
	AActor* OwnerActor = GetOwner();
	bool bIsServer = OwnerActor ? OwnerActor->HasAuthority() : false;
	UE_LOG(LogTemp, Warning, TEXT("⭐ [EquipmentComponent] OnItemEquipped 호출됨 - %s (Owner: %s)"), 
		bIsServer ? TEXT("서버") : TEXT("클라이언트"),
		OwnerActor ? *OwnerActor->GetName() : TEXT("nullptr"));
	
	if (!IsValid(EquippedItem)) return;
	
	// ============================================
	// ⭐ [WeaponBridge 수정] 서버에서만 액터 스폰
	// ⭐ 하지만 클라이언트도 EquippedActors에 추가 필요!
	// ============================================

	FInv_ItemManifest& ItemManifest = EquippedItem->GetItemManifestMutable();
	FInv_EquipmentFragment* EquipmentFragment = ItemManifest.GetFragmentOfTypeMutable<FInv_EquipmentFragment>();
	if (!EquipmentFragment) return;
	
	// ⭐ 서버에서만 OnEquip 콜백과 액터 스폰 실행
	if (bIsServer)
	{
		if (!bIsProxy)
		{
			EquipmentFragment->OnEquip(OwningPlayerController.Get());
		}
		
		if (!OwningSkeletalMesh.IsValid()) return;
		AInv_EquipActor* SpawnedEquipActor = SpawnEquippedActor(EquipmentFragment, ItemManifest, OwningSkeletalMesh.Get());
		
		if (IsValid(SpawnedEquipActor))
		{
			EquippedActors.Add(SpawnedEquipActor);
			UE_LOG(LogTemp, Warning, TEXT("⭐ [EquipmentComponent] 서버: EquippedActors에 추가됨: %s (총 %d개) - this: %p"), 
				*SpawnedEquipActor->GetName(), EquippedActors.Num(), this);
		}
		else
		{
			UE_LOG(LogTemp, Error, TEXT("⭐ [EquipmentComponent] 서버: SpawnedEquipActor가 null!"));
		}
	}
	else
	{
		// ============================================
		// ⭐ [WeaponBridge 수정] 클라이언트: 이미 스폰된 액터 찾아서 추가
		// ⭐ 서버에서 스폰 후 리플리케이트된 액터를 찾음
		// ============================================
		
		// EquipmentFragment에서 이미 설정된 EquippedActor 가져오기
		AInv_EquipActor* ReplicatedActor = EquipmentFragment->GetEquippedActor();
		if (IsValid(ReplicatedActor))
		{
			EquippedActors.Add(ReplicatedActor);
			UE_LOG(LogTemp, Warning, TEXT("⭐ [EquipmentComponent] 클라이언트: 리플리케이트된 액터 추가: %s (총 %d개) - this: %p"), 
				*ReplicatedActor->GetName(), EquippedActors.Num(), this);
		}
		else
		{
			UE_LOG(LogTemp, Warning, TEXT("⭐ [EquipmentComponent] 클라이언트: EquippedActor 아직 없음 - 나중에 추가될 예정"));
		}
	}
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

// ============================================
// ⭐ [WeaponBridge] 무기 꺼내기/집어넣기 구현
// ============================================

void UInv_EquipmentComponent::HandlePrimaryWeaponInput()
{
	UE_LOG(LogTemp, Warning, TEXT("⭐ [WeaponBridge] HandlePrimaryWeaponInput 호출됨"));
	
	// 무기가 없으면 무시
	AInv_EquipActor* WeaponActor = FindWeaponActor();
	if (!IsValid(WeaponActor))
	{
		UE_LOG(LogTemp, Warning, TEXT("⭐ [WeaponBridge] 장착된 무기 없음 - 입력 무시"));
		return;
	}
	
	UE_LOG(LogTemp, Warning, TEXT("⭐ [WeaponBridge] 현재 활성 슬롯: %d"), static_cast<int32>(ActiveWeaponSlot));
	
	// 현재 상태에 따라 토글
	if (ActiveWeaponSlot == EInv_ActiveWeaponSlot::None)
	{
		EquipWeapon();
	}
	else if (ActiveWeaponSlot == EInv_ActiveWeaponSlot::Primary)
	{
		UnequipWeapon();
	}
}

void UInv_EquipmentComponent::EquipWeapon()
{
	AInv_EquipActor* WeaponActor = FindWeaponActor();
	if (!IsValid(WeaponActor))
	{
		UE_LOG(LogTemp, Error, TEXT("⭐ [WeaponBridge] EquipWeapon 실패 - WeaponActor 없음"));
		return;
	}
	
	UE_LOG(LogTemp, Warning, TEXT("⭐ [WeaponBridge] 무기 꺼내기 시작 - %s"), *WeaponActor->GetName());
	
	// 등 무기 숨기기
	WeaponActor->SetActorHiddenInGame(true);
	UE_LOG(LogTemp, Warning, TEXT("⭐ [WeaponBridge] 등 무기 Hidden 처리 완료"));
	
	// 무기 스폰 GA 확인
	TSubclassOf<UGameplayAbility> SpawnWeaponAbility = WeaponActor->GetSpawnWeaponAbility();
	if (!SpawnWeaponAbility)
	{
		UE_LOG(LogTemp, Error, TEXT("⭐ [WeaponBridge] SpawnWeaponAbility가 설정되지 않음!"));
	}
	else
	{
		UE_LOG(LogTemp, Warning, TEXT("⭐ [WeaponBridge] SpawnWeaponAbility: %s"), *SpawnWeaponAbility->GetName());
	}
	
	// 델리게이트 브로드캐스트 (Helluna에서 수신)
	OnWeaponEquipRequested.Broadcast(
		WeaponActor->GetEquipmentType(),
		WeaponActor,
		SpawnWeaponAbility,
		true  // bEquip = true (꺼내기)
	);
	UE_LOG(LogTemp, Warning, TEXT("⭐ [WeaponBridge] 델리게이트 브로드캐스트 완료 (bEquip = true)"));
	
	// 상태 변경
	ActiveWeaponSlot = EInv_ActiveWeaponSlot::Primary;
	UE_LOG(LogTemp, Warning, TEXT("⭐ [WeaponBridge] 무기 꺼내기 완료 - ActiveWeaponSlot = Primary"));
}

void UInv_EquipmentComponent::UnequipWeapon()
{
	AInv_EquipActor* WeaponActor = FindWeaponActor();
	if (!IsValid(WeaponActor))
	{
		UE_LOG(LogTemp, Error, TEXT("⭐ [WeaponBridge] UnequipWeapon 실패 - WeaponActor 없음"));
		return;
	}
	
	UE_LOG(LogTemp, Warning, TEXT("⭐ [WeaponBridge] 무기 집어넣기 시작 - %s"), *WeaponActor->GetName());
	
	// 델리게이트 브로드캐스트 (Helluna에서 손 무기 Destroy)
	OnWeaponEquipRequested.Broadcast(
		WeaponActor->GetEquipmentType(),
		WeaponActor,
		nullptr,  // 집어넣기라 HandWeaponClass 필요 없음
		false     // bEquip = false (집어넣기)
	);
	UE_LOG(LogTemp, Warning, TEXT("⭐ [WeaponBridge] 델리게이트 브로드캐스트 완료 (bEquip = false)"));
	
	// 등 무기 다시 보이기
	WeaponActor->SetActorHiddenInGame(false);
	UE_LOG(LogTemp, Warning, TEXT("⭐ [WeaponBridge] 등 무기 Visible 처리 완료"));
	
	// 상태 변경
	ActiveWeaponSlot = EInv_ActiveWeaponSlot::None;
	UE_LOG(LogTemp, Warning, TEXT("⭐ [WeaponBridge] 무기 집어넣기 완료 - ActiveWeaponSlot = None"));
}

AInv_EquipActor* UInv_EquipmentComponent::FindWeaponActor()
{
	// ============================================
	// ⭐ [WeaponBridge] EquippedActors에서 무기 찾기
	// ⭐ 서버/클라이언트 동기화 문제로 인해 월드에서 직접 검색 추가
	// ============================================
	
	AActor* OwnerActor = GetOwner();
	bool bIsServer = OwnerActor ? OwnerActor->HasAuthority() : false;
	
	UE_LOG(LogTemp, Warning, TEXT("⭐ [WeaponBridge] FindWeaponActor 시작 - %s - this: %p"), 
		bIsServer ? TEXT("서버") : TEXT("클라이언트"), this);
	
	// 1단계: EquippedActors 배열에서 찾기 (서버에서는 이게 동작)
	UE_LOG(LogTemp, Warning, TEXT("⭐ [WeaponBridge] 1단계: EquippedActors 배열 검색 (개수: %d)"), EquippedActors.Num());
	
	for (int32 i = 0; i < EquippedActors.Num(); i++)
	{
		AInv_EquipActor* Actor = EquippedActors[i];
		if (IsValid(Actor))
		{
			UE_LOG(LogTemp, Warning, TEXT("⭐ [WeaponBridge] 1단계 성공! [%d] 찾음: %s"), i, *Actor->GetName());
			return Actor;
		}
	}
	
	// 2단계: 클라이언트에서 배열이 비어있으면 월드에서 직접 검색
	// 서버에서 스폰된 EquipActor가 리플리케이트되어 있을 것
	if (!bIsServer && OwningSkeletalMesh.IsValid())
	{
		UE_LOG(LogTemp, Warning, TEXT("⭐ [WeaponBridge] 2단계: 클라이언트 - 스켈레탈 메시에 부착된 액터 검색"));
		
		// 스켈레탈 메시에 부착된 모든 자식 액터 검색
		TArray<AActor*> AttachedActors;
		if (AActor* MeshOwner = OwningSkeletalMesh->GetOwner())
		{
			MeshOwner->GetAttachedActors(AttachedActors, true);
			
			UE_LOG(LogTemp, Warning, TEXT("⭐ [WeaponBridge] 부착된 액터 개수: %d"), AttachedActors.Num());
			
			for (AActor* AttachedActor : AttachedActors)
			{
				if (AInv_EquipActor* EquipActor = Cast<AInv_EquipActor>(AttachedActor))
				{
					UE_LOG(LogTemp, Warning, TEXT("⭐ [WeaponBridge] 2단계 성공! EquipActor 찾음: %s"), *EquipActor->GetName());
					
					// 배열에 추가해두기 (다음에 빠르게 찾을 수 있도록)
					EquippedActors.Add(EquipActor);
					return EquipActor;
				}
			}
		}
	}
	
	UE_LOG(LogTemp, Warning, TEXT("⭐ [WeaponBridge] FindWeaponActor - 무기 없음 (1,2단계 모두 실패)"));
	return nullptr;
}






