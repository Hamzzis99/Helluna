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
AInv_EquipActor* UInv_EquipmentComponent::SpawnEquippedActor(FInv_EquipmentFragment* EquipmentFragment, const FInv_ItemManifest& Manifest, USkeletalMeshComponent* AttachMesh, int32 WeaponSlotIndex)
{
	AInv_EquipActor* SpawnedEquipActor = EquipmentFragment->SpawnAttachedActor(AttachMesh, WeaponSlotIndex); // 장착된 액터 스폰 (WeaponSlotIndex 전달)
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
void UInv_EquipmentComponent::OnItemEquipped(UInv_InventoryItem* EquippedItem, int32 WeaponSlotIndex)
{
	// ⭐ 서버/클라이언트 확인
	AActor* OwnerActor = GetOwner();
	bool bIsServer = OwnerActor ? OwnerActor->HasAuthority() : false;
	UE_LOG(LogTemp, Warning, TEXT("⭐ [EquipmentComponent] OnItemEquipped 호출됨 - %s (Owner: %s, WeaponSlotIndex: %d)"), 
		bIsServer ? TEXT("서버") : TEXT("클라이언트"),
		OwnerActor ? *OwnerActor->GetName() : TEXT("nullptr"),
		WeaponSlotIndex);
	
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
		AInv_EquipActor* SpawnedEquipActor = SpawnEquippedActor(EquipmentFragment, ItemManifest, OwningSkeletalMesh.Get(), WeaponSlotIndex);
		
		if (IsValid(SpawnedEquipActor))
		{
			// WeaponSlotIndex는 이미 SpawnAttachedActor에서 설정됨
			UE_LOG(LogTemp, Warning, TEXT("⭐ [EquipmentComponent] SpawnedEquipActor WeaponSlotIndex: %d"), SpawnedEquipActor->GetWeaponSlotIndex());
			
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
void UInv_EquipmentComponent::OnItemUnequipped(UInv_InventoryItem* UnequippedItem, int32 WeaponSlotIndex)
{
	AActor* OwnerActor = GetOwner();
	bool bIsServer = OwnerActor ? OwnerActor->HasAuthority() : false;
	
	UE_LOG(LogTemp, Warning, TEXT("⭐ [EquipmentComponent] OnItemUnequipped 호출됨 - %s (WeaponSlotIndex: %d)"),
		bIsServer ? TEXT("서버") : TEXT("클라이언트"), WeaponSlotIndex);
	
	if (!IsValid(UnequippedItem)) return;

	FInv_ItemManifest& ItemManifest = UnequippedItem->GetItemManifestMutable();
	FInv_EquipmentFragment* EquipmentFragment = ItemManifest.GetFragmentOfTypeMutable<FInv_EquipmentFragment>();
	if (!EquipmentFragment) return;
	
	// ⭐ [WeaponBridge] 무기 해제 시 손에 들고 있는 무기도 처리 (클라이언트에서 실행)
	FGameplayTag WeaponsTag = FGameplayTag::RequestGameplayTag(FName("GameItems.Equipment.Weapons"));
	if (EquipmentFragment->GetEquipmentType().MatchesTag(WeaponsTag))
	{
		// 현재 손에 무기를 들고 있고, 해제하는 무기가 해당 슬롯이면 손 무기도 파괴
		bool bIsActiveWeapon = false;
		if (WeaponSlotIndex == 0 && ActiveWeaponSlot == EInv_ActiveWeaponSlot::Primary)
		{
			bIsActiveWeapon = true;
		}
		else if (WeaponSlotIndex == 1 && ActiveWeaponSlot == EInv_ActiveWeaponSlot::Secondary)
		{
			bIsActiveWeapon = true;
		}
		
		if (bIsActiveWeapon)
		{
			UE_LOG(LogTemp, Warning, TEXT("⭐ [EquipmentComponent] 손에 든 무기 해제 - 델리게이트 브로드캐스트"));
			
			// 손 무기 파괴 델리게이트 브로드캐스트 (클라이언트에서 UI와 연결된 캐릭터에 전달)
			OnWeaponEquipRequested.Broadcast(
				EquipmentFragment->GetEquipmentType(),
				nullptr,  // 이미 파괴될 예정이므로 nullptr
				nullptr,
				false  // bEquip = false (집어넣기/파괴)
			);
			
			// 활성 슬롯 초기화
			ActiveWeaponSlot = EInv_ActiveWeaponSlot::None;
			UE_LOG(LogTemp, Warning, TEXT("⭐ [EquipmentComponent] ActiveWeaponSlot = None으로 초기화"));
		}
	}

	// ⭐ 서버에서만 장비 제거 및 Destroy 실행
	if (!bIsServer) return;
	
	if (!bIsProxy) // 프록시 부분
	{
		EquipmentFragment->OnUnequip(OwningPlayerController.Get());
	}
	
	//장비 제거하는 부분 (등 무기 Destroy)
	RemoveEquippedActor(EquipmentFragment->GetEquipmentType());
}

// ============================================
// ⭐ [WeaponBridge] 무기 꺼내기/집어넣기 구현
// ============================================

void UInv_EquipmentComponent::HandlePrimaryWeaponInput()
{
	UE_LOG(LogTemp, Warning, TEXT("⭐ [WeaponBridge] HandlePrimaryWeaponInput 호출됨 (1키)"));
	
	// 주무기가 없으면 무시
	AInv_EquipActor* WeaponActor = FindPrimaryWeaponActor();
	if (!IsValid(WeaponActor))
	{
		UE_LOG(LogTemp, Warning, TEXT("⭐ [WeaponBridge] 장착된 주무기 없음 - 입력 무시"));
		return;
	}
	
	UE_LOG(LogTemp, Warning, TEXT("⭐ [WeaponBridge] 현재 활성 슬롯: %d"), static_cast<int32>(ActiveWeaponSlot));
	
	// 현재 상태에 따라 분기
	if (ActiveWeaponSlot == EInv_ActiveWeaponSlot::None)
	{
		// 맨손 → 주무기 꺼내기
		EquipPrimaryWeapon();
	}
	else if (ActiveWeaponSlot == EInv_ActiveWeaponSlot::Primary)
	{
		// 주무기 들고 있음 → 집어넣기
		UnequipWeapon();
	}
	else if (ActiveWeaponSlot == EInv_ActiveWeaponSlot::Secondary)
	{
		// 보조무기 들고 있음 → 보조무기 집어넣고 주무기 꺼내기
		UnequipWeapon();
		EquipPrimaryWeapon();
	}
}

void UInv_EquipmentComponent::HandleSecondaryWeaponInput()
{
	UE_LOG(LogTemp, Warning, TEXT("⭐ [WeaponBridge] HandleSecondaryWeaponInput 호출됨 (2키)"));
	
	// 보조무기가 없으면 무시
	AInv_EquipActor* WeaponActor = FindSecondaryWeaponActor();
	if (!IsValid(WeaponActor))
	{
		UE_LOG(LogTemp, Warning, TEXT("⭐ [WeaponBridge] 장착된 보조무기 없음 - 입력 무시"));
		return;
	}
	
	UE_LOG(LogTemp, Warning, TEXT("⭐ [WeaponBridge] 현재 활성 슬롯: %d"), static_cast<int32>(ActiveWeaponSlot));
	
	// 현재 상태에 따라 분기
	if (ActiveWeaponSlot == EInv_ActiveWeaponSlot::None)
	{
		// 맨손 → 보조무기 꺼내기
		EquipSecondaryWeapon();
	}
	else if (ActiveWeaponSlot == EInv_ActiveWeaponSlot::Secondary)
	{
		// 보조무기 들고 있음 → 집어넣기
		UnequipWeapon();
	}
	else if (ActiveWeaponSlot == EInv_ActiveWeaponSlot::Primary)
	{
		// 주무기 들고 있음 → 주무기 집어넣고 보조무기 꺼내기
		UnequipWeapon();
		EquipSecondaryWeapon();
	}
}

void UInv_EquipmentComponent::EquipPrimaryWeapon()
{
	AInv_EquipActor* WeaponActor = FindPrimaryWeaponActor();
	if (!IsValid(WeaponActor))
	{
		UE_LOG(LogTemp, Error, TEXT("⭐ [WeaponBridge] EquipPrimaryWeapon 실패 - WeaponActor 없음"));
		return;
	}
	
	UE_LOG(LogTemp, Warning, TEXT("⭐ [WeaponBridge] 주무기 꺼내기 시작 - %s"), *WeaponActor->GetName());
	
	// 등 무기 숨기기 (리플리케이트)
	WeaponActor->SetWeaponHidden(true);
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
	UE_LOG(LogTemp, Warning, TEXT("⭐ [WeaponBridge] 주무기 꺼내기 완료 - ActiveWeaponSlot = Primary"));
}

void UInv_EquipmentComponent::EquipSecondaryWeapon()
{
	AInv_EquipActor* WeaponActor = FindSecondaryWeaponActor();
	if (!IsValid(WeaponActor))
	{
		UE_LOG(LogTemp, Error, TEXT("⭐ [WeaponBridge] EquipSecondaryWeapon 실패 - WeaponActor 없음"));
		return;
	}
	
	UE_LOG(LogTemp, Warning, TEXT("⭐ [WeaponBridge] 보조무기 꺼내기 시작 - %s"), *WeaponActor->GetName());
	
	// 등 무기 숨기기 (리플리케이트)
	WeaponActor->SetWeaponHidden(true);
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
	ActiveWeaponSlot = EInv_ActiveWeaponSlot::Secondary;
	UE_LOG(LogTemp, Warning, TEXT("⭐ [WeaponBridge] 보조무기 꺼내기 완료 - ActiveWeaponSlot = Secondary"));
}

void UInv_EquipmentComponent::UnequipWeapon()
{
	// 현재 활성 슬롯에 따라 해당 무기 찾기
	AInv_EquipActor* WeaponActor = nullptr;
	if (ActiveWeaponSlot == EInv_ActiveWeaponSlot::Primary)
	{
		WeaponActor = FindPrimaryWeaponActor();
	}
	else if (ActiveWeaponSlot == EInv_ActiveWeaponSlot::Secondary)
	{
		WeaponActor = FindSecondaryWeaponActor();
	}
	
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
		nullptr,  // 집어넣기라 GA 필요 없음
		false     // bEquip = false (집어넣기)
	);
	UE_LOG(LogTemp, Warning, TEXT("⭐ [WeaponBridge] 델리게이트 브로드캐스트 완료 (bEquip = false)"));
	
	// 등 무기 다시 보이기 (리플리케이트)
	WeaponActor->SetWeaponHidden(false);
	UE_LOG(LogTemp, Warning, TEXT("⭐ [WeaponBridge] 등 무기 Visible 처리 완료"));
	
	// 상태 변경
	ActiveWeaponSlot = EInv_ActiveWeaponSlot::None;
	UE_LOG(LogTemp, Warning, TEXT("⭐ [WeaponBridge] 무기 집어넣기 완료 - ActiveWeaponSlot = None"));
}

AInv_EquipActor* UInv_EquipmentComponent::FindPrimaryWeaponActor()
{
	// ============================================
	// ⭐ [WeaponBridge] 주무기 찾기
	// ⭐ 1순위: WeaponSlotIndex == 0
	// ⭐ 2순위: WeaponSlotIndex == -1 (미설정)인 첫 번째 무기
	// ============================================
	
	AActor* OwnerActor = GetOwner();
	bool bIsServer = OwnerActor ? OwnerActor->HasAuthority() : false;
	
	UE_LOG(LogTemp, Warning, TEXT("⭐ [WeaponBridge] FindPrimaryWeaponActor 시작 - %s"), 
		bIsServer ? TEXT("서버") : TEXT("클라이언트"));
	
	FGameplayTag WeaponsTag = FGameplayTag::RequestGameplayTag(FName("GameItems.Equipment.Weapons"));
	
	// 1단계: EquippedActors 배열에서 찾기
	AInv_EquipActor* FirstWeaponWithUnsetIndex = nullptr;
	
	for (AInv_EquipActor* Actor : EquippedActors)
	{
		if (IsValid(Actor) && Actor->GetEquipmentType().MatchesTag(WeaponsTag))
		{
			int32 SlotIndex = Actor->GetWeaponSlotIndex();
			
			// 정확히 SlotIndex == 0인 무기 (1순위)
			if (SlotIndex == 0)
			{
				UE_LOG(LogTemp, Warning, TEXT("⭐ [WeaponBridge] 주무기 찾음 (SlotIndex=0): %s"), *Actor->GetName());
				return Actor;
			}
			
			// SlotIndex == -1 (미설정)인 첫 번째 무기 기억 (2순위)
			if (SlotIndex == -1 && !FirstWeaponWithUnsetIndex)
			{
				FirstWeaponWithUnsetIndex = Actor;
			}
		}
	}
	
	// SlotIndex == 0인 무기가 없으면 미설정(-1)인 첫 번째 무기 반환
	if (FirstWeaponWithUnsetIndex)
	{
		UE_LOG(LogTemp, Warning, TEXT("⭐ [WeaponBridge] 주무기 찾음 (SlotIndex 미설정, 첫 번째): %s"), *FirstWeaponWithUnsetIndex->GetName());
		return FirstWeaponWithUnsetIndex;
	}
	
	// 2단계: 클라이언트에서 월드 검색
	if (!bIsServer && OwningSkeletalMesh.IsValid())
	{
		TArray<AActor*> AttachedActors;
		if (AActor* MeshOwner = OwningSkeletalMesh->GetOwner())
		{
			MeshOwner->GetAttachedActors(AttachedActors, true);
			
			for (AActor* AttachedActor : AttachedActors)
			{
				if (AInv_EquipActor* EquipActor = Cast<AInv_EquipActor>(AttachedActor))
				{
					if (EquipActor->GetEquipmentType().MatchesTag(WeaponsTag))
					{
						int32 SlotIndex = EquipActor->GetWeaponSlotIndex();
						
						// SlotIndex == 0 또는 -1(미설정)인 첫 번째 무기
						if (SlotIndex == 0 || SlotIndex == -1)
						{
							UE_LOG(LogTemp, Warning, TEXT("⭐ [WeaponBridge] 2단계 - 주무기 찾음 (SlotIndex=%d): %s"), SlotIndex, *EquipActor->GetName());
							if (!EquippedActors.Contains(EquipActor))
							{
								EquippedActors.Add(EquipActor);
							}
							return EquipActor;
						}
					}
				}
			}
		}
	}
	
	UE_LOG(LogTemp, Warning, TEXT("⭐ [WeaponBridge] 주무기 없음"));
	return nullptr;
}

AInv_EquipActor* UInv_EquipmentComponent::FindSecondaryWeaponActor()
{
	// ============================================
	// ⭐ [WeaponBridge] 보조무기 찾기
	// ⭐ 1순위: WeaponSlotIndex == 1
	// ⭐ 2순위: WeaponSlotIndex == -1 (미설정)인 두 번째 무기
	// ============================================
	
	AActor* OwnerActor = GetOwner();
	bool bIsServer = OwnerActor ? OwnerActor->HasAuthority() : false;
	
	UE_LOG(LogTemp, Warning, TEXT("⭐ [WeaponBridge] FindSecondaryWeaponActor 시작 - %s"), 
		bIsServer ? TEXT("서버") : TEXT("클라이언트"));
	
	FGameplayTag WeaponsTag = FGameplayTag::RequestGameplayTag(FName("GameItems.Equipment.Weapons"));
	
	// 1단계: EquippedActors 배열에서 찾기
	int32 UnsetWeaponCount = 0;
	AInv_EquipActor* SecondWeaponWithUnsetIndex = nullptr;
	
	for (AInv_EquipActor* Actor : EquippedActors)
	{
		if (IsValid(Actor) && Actor->GetEquipmentType().MatchesTag(WeaponsTag))
		{
			int32 SlotIndex = Actor->GetWeaponSlotIndex();
			
			// 정확히 SlotIndex == 1인 무기 (1순위)
			if (SlotIndex == 1)
			{
				UE_LOG(LogTemp, Warning, TEXT("⭐ [WeaponBridge] 보조무기 찾음 (SlotIndex=1): %s"), *Actor->GetName());
				return Actor;
			}
			
			// SlotIndex == -1 (미설정)인 무기 카운트
			if (SlotIndex == -1)
			{
				UnsetWeaponCount++;
				if (UnsetWeaponCount == 2)
				{
					SecondWeaponWithUnsetIndex = Actor;
				}
			}
		}
	}
	
	// SlotIndex == 1인 무기가 없으면 미설정(-1)인 두 번째 무기 반환
	if (SecondWeaponWithUnsetIndex)
	{
		UE_LOG(LogTemp, Warning, TEXT("⭐ [WeaponBridge] 보조무기 찾음 (SlotIndex 미설정, 두 번째): %s"), *SecondWeaponWithUnsetIndex->GetName());
		return SecondWeaponWithUnsetIndex;
	}
	
	// 2단계: 클라이언트에서 월드 검색
	if (!bIsServer && OwningSkeletalMesh.IsValid())
	{
		TArray<AActor*> AttachedActors;
		if (AActor* MeshOwner = OwningSkeletalMesh->GetOwner())
		{
			MeshOwner->GetAttachedActors(AttachedActors, true);
			
			UnsetWeaponCount = 0;
			for (AActor* AttachedActor : AttachedActors)
			{
				if (AInv_EquipActor* EquipActor = Cast<AInv_EquipActor>(AttachedActor))
				{
					if (EquipActor->GetEquipmentType().MatchesTag(WeaponsTag))
					{
						int32 SlotIndex = EquipActor->GetWeaponSlotIndex();
						
						// SlotIndex == 1인 무기
						if (SlotIndex == 1)
						{
							UE_LOG(LogTemp, Warning, TEXT("⭐ [WeaponBridge] 2단계 - 보조무기 찾음 (SlotIndex=1): %s"), *EquipActor->GetName());
							if (!EquippedActors.Contains(EquipActor))
							{
								EquippedActors.Add(EquipActor);
							}
							return EquipActor;
						}
						
						// SlotIndex == -1 (미설정)인 두 번째 무기
						if (SlotIndex == -1)
						{
							if (!EquippedActors.Contains(EquipActor))
							{
								EquippedActors.Add(EquipActor);
							}
							UnsetWeaponCount++;
							if (UnsetWeaponCount == 2)
							{
								UE_LOG(LogTemp, Warning, TEXT("⭐ [WeaponBridge] 2단계 - 보조무기 찾음 (SlotIndex 미설정, 두 번째): %s"), *EquipActor->GetName());
								return EquipActor;
							}
						}
					}
				}
			}
		}
	}
	
	UE_LOG(LogTemp, Warning, TEXT("⭐ [WeaponBridge] 보조무기 없음"));
	return nullptr;
}






