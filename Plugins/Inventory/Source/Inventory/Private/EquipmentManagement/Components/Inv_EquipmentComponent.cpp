// Gihyeon's Inventory Project

//플레이어 컨트롤러와 관련이 있네?

#include "EquipmentManagement/Components/Inv_EquipmentComponent.h"
#include "Inventory.h"

#include "EquipmentManagement/EquipActor/Inv_EquipActor.h"
#include "GameFramework/Character.h"
#include "GameFramework/PlayerController.h"
#include "InventoryManagement/Components/Inv_InventoryComponent.h"
#include "InventoryManagement/Utils/Inv_InventoryStatics.h"
#include "Items/Inv_InventoryItem.h"
#include "Items/Fragments/Inv_ItemFragment.h"
#include "Items/Fragments/Inv_AttachmentFragments.h"
#include "Abilities/GameplayAbility.h" // TODO: [독립화] 졸작 후 삭제


//디버그용
#include "Engine/Engine.h"

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

// ============================================
// 🆕 [Phase 6] 컴포넌트 파괴 시 장착 액터 정리
// ============================================
void UInv_EquipmentComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
#if INV_DEBUG_EQUIP
	UE_LOG(LogTemp, Warning, TEXT(""));
	UE_LOG(LogTemp, Warning, TEXT("╔══════════════════════════════════════════════════════════════╗"));
	UE_LOG(LogTemp, Warning, TEXT("║ [EquipmentComponent] EndPlay - 장착 액터 정리                 ║"));
	UE_LOG(LogTemp, Warning, TEXT("╠══════════════════════════════════════════════════════════════╣"));
	UE_LOG(LogTemp, Warning, TEXT("║ EndPlayReason: %d"), static_cast<int32>(EndPlayReason));
	UE_LOG(LogTemp, Warning, TEXT("║ EquippedActors 개수: %d"), EquippedActors.Num());
	UE_LOG(LogTemp, Warning, TEXT("╚══════════════════════════════════════════════════════════════╝"));
#endif

	// 모든 장착 액터 파괴
	for (TObjectPtr<AInv_EquipActor>& EquipActor : EquippedActors)
	{
		if (EquipActor.Get() && IsValid(EquipActor.Get()))
		{
#if INV_DEBUG_EQUIP
			UE_LOG(LogTemp, Warning, TEXT("   🗑️ 장착 액터 파괴: %s (Slot: %d)"), 
				*EquipActor->GetName(), EquipActor->GetWeaponSlotIndex());
#endif
			EquipActor->Destroy();
		}
	}
	EquippedActors.Empty();
	
#if INV_DEBUG_EQUIP
	UE_LOG(LogTemp, Warning, TEXT("   ✅ 장착 액터 정리 완료!"));
	UE_LOG(LogTemp, Warning, TEXT(""));
#endif

	Super::EndPlay(EndPlayReason);
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
			OwningPlayerController->OnPossessedPawnChanged.AddUniqueDynamic(this, &ThisClass::OnPossessedPawnChange);  //컨트롤러를 멀티캐스트 델리게이트 식으로. 위임하는 부분. (이미 완료 됐을 경우 이걸 호출 안 하니 깔끔해진다.)
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
		InventoryComponent->OnItemEquipped.AddUniqueDynamic(this, &ThisClass::OnItemEquipped);
	}
	// 델리게이트 바인딩
	if (!InventoryComponent->OnItemUnequipped.IsAlreadyBound(this, &ThisClass::OnItemUnequipped))
	{
		InventoryComponent->OnItemUnequipped.AddUniqueDynamic(this, &ThisClass::OnItemUnequipped);
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

// ⭐ [WeaponBridge] WeaponSlotIndex를 고려하여 정확한 무기 제거
void UInv_EquipmentComponent::RemoveEquippedActor(const FGameplayTag& EquipmentTypeTag, int32 WeaponSlotIndex)
{
	AInv_EquipActor* ActorToRemove = nullptr;
	
	if (WeaponSlotIndex >= 0)
	{
		// ⭐ 태그 + SlotIndex로 정확한 무기 찾기 (무기류)
		for (AInv_EquipActor* Actor : EquippedActors)
		{
			if (IsValid(Actor) && 
				Actor->GetEquipmentType().MatchesTagExact(EquipmentTypeTag) &&
				Actor->GetWeaponSlotIndex() == WeaponSlotIndex)
			{
				ActorToRemove = Actor;
				break;
			}
		}
#if INV_DEBUG_EQUIP
		UE_LOG(LogTemp, Warning, TEXT("⭐ [EquipmentComponent] RemoveEquippedActor - Tag: %s, SlotIndex: %d, Found: %s"), 
			*EquipmentTypeTag.ToString(), WeaponSlotIndex, 
			ActorToRemove ? *ActorToRemove->GetName() : TEXT("nullptr"));
#endif
	}
	else
	{
		// ⭐ 기존 동작: 태그만으로 찾기 (비무기류 장비용)
		ActorToRemove = FindEquippedActor(EquipmentTypeTag);
	}
	
	if (IsValid(ActorToRemove))
	{
		EquippedActors.Remove(ActorToRemove);
		ActorToRemove->Destroy();
#if INV_DEBUG_EQUIP
		UE_LOG(LogTemp, Warning, TEXT("⭐ [EquipmentComponent] 액터 제거 완료: %s"), *ActorToRemove->GetName());
#endif
	}
}

// 아이템 장착 시 호출되는 함수
void UInv_EquipmentComponent::OnItemEquipped(UInv_InventoryItem* EquippedItem, int32 WeaponSlotIndex)
{
	// ⭐ 서버/클라이언트 확인
	AActor* OwnerActor = GetOwner();
	bool bIsServer = OwnerActor ? OwnerActor->HasAuthority() : false;
#if INV_DEBUG_EQUIP
	UE_LOG(LogTemp, Warning, TEXT("⭐ [EquipmentComponent] OnItemEquipped 호출됨 - %s (Owner: %s, WeaponSlotIndex: %d)"), 
		bIsServer ? TEXT("서버") : TEXT("클라이언트"),
		OwnerActor ? *OwnerActor->GetName() : TEXT("nullptr"),
		WeaponSlotIndex);
#endif
	
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
			// 무기 자체의 EquipModifiers 적용
			EquipmentFragment->OnEquip(OwningPlayerController.Get());

			// ════════════════════════════════════════════════════════════════
			// 📌 [부착물 시스템 Phase 2] 무기에 달린 부착물들의 스탯도 일괄 적용
			// 순서: 무기 스탯 OnEquip → 부착물 스탯 OnEquip
			// ════════════════════════════════════════════════════════════════
			FInv_AttachmentHostFragment* HostFragment = ItemManifest.GetFragmentOfTypeMutable<FInv_AttachmentHostFragment>();
			if (HostFragment && HostFragment->GetAttachedItems().Num() > 0)
			{
				HostFragment->OnEquipAllAttachments(OwningPlayerController.Get());
#if INV_DEBUG_EQUIP
				UE_LOG(LogTemp, Warning, TEXT("📌 [Attachment] 무기 장착 시 부착물 스탯 %d개 일괄 적용"),
					HostFragment->GetAttachedItems().Num());
#endif
			}
		}
		
		if (!OwningSkeletalMesh.IsValid()) return;
		AInv_EquipActor* SpawnedEquipActor = SpawnEquippedActor(EquipmentFragment, ItemManifest, OwningSkeletalMesh.Get(), WeaponSlotIndex);
		
		if (IsValid(SpawnedEquipActor))
		{
#if INV_DEBUG_EQUIP
			// WeaponSlotIndex는 이미 SpawnAttachedActor에서 설정됨
			UE_LOG(LogTemp, Warning, TEXT("⭐ [EquipmentComponent] SpawnedEquipActor WeaponSlotIndex: %d"), SpawnedEquipActor->GetWeaponSlotIndex());
#endif

			EquippedActors.Add(SpawnedEquipActor);
#if INV_DEBUG_EQUIP
			UE_LOG(LogTemp, Warning, TEXT("⭐ [EquipmentComponent] 서버: EquippedActors에 추가됨: %s (총 %d개) - this: %p"),
				*SpawnedEquipActor->GetName(), EquippedActors.Num(), this);
#endif

			// ════════════════════════════════════════════════════════════════
			// 📌 [Phase 5] 무기 장착 시 부착물 메시도 함께 스폰
			// 처리 흐름:
			//   1. AttachmentHostFragment의 AttachedItems 순회
			//   2. 각 부착물의 AttachableFragment에서 Mesh, Socket, Offset 가져오기
			//   3. EquipActor->AttachMeshToSocket 호출
			// ════════════════════════════════════════════════════════════════
			FInv_AttachmentHostFragment* AttachHostFrag = ItemManifest.GetFragmentOfTypeMutable<FInv_AttachmentHostFragment>();
			if (AttachHostFrag)
			{
				const TArray<FInv_AttachmentSlotDef>& SlotDefs = AttachHostFrag->GetSlotDefinitions();
				for (const FInv_AttachedItemData& AttachedData : AttachHostFrag->GetAttachedItems())
				{
					// 부착물의 AttachableFragment에서 메시 정보 가져오기
					const FInv_AttachableFragment* AttachableFrag = AttachedData.ItemManifestCopy.GetFragmentOfType<FInv_AttachableFragment>();
					if (AttachableFrag && AttachableFrag->GetAttachmentMesh())
					{
						// 소켓 폴백: 무기 SlotDef → 부착물 AttachableFragment → NAME_None
						FName SocketName = NAME_None;
						if (SlotDefs.IsValidIndex(AttachedData.SlotIndex) && !SlotDefs[AttachedData.SlotIndex].AttachSocket.IsNone())
						{
							SocketName = SlotDefs[AttachedData.SlotIndex].AttachSocket;  // 1순위: 무기 SlotDef 오버라이드
						}
						else
						{
							SocketName = AttachableFrag->GetAttachSocket();  // 2순위: 부착물 기본 소켓
						}

						SpawnedEquipActor->AttachMeshToSocket(
							AttachedData.SlotIndex,
							AttachableFrag->GetAttachmentMesh(),
							SocketName,
							AttachableFrag->GetAttachOffset()
						);
					}

					// ════════════════════════════════════════════════════════════════
					// 📌 [Phase 7] 무기 장착 시 부착물 효과도 일괄 적용
					// (소음기/스코프/레이저 등)
					// ════════════════════════════════════════════════════════════════
					if (AttachableFrag)
					{
						SpawnedEquipActor->ApplyAttachmentEffects(AttachableFrag);
#if INV_DEBUG_ATTACHMENT
						UE_LOG(LogTemp, Warning, TEXT("📌 [Phase 7] 무기 장착 시 부착물 효과 적용: 슬롯 %d, bIsSuppressor=%s"),
							AttachedData.SlotIndex,
							AttachableFrag->GetIsSuppressor() ? TEXT("TRUE") : TEXT("FALSE"));
#endif
					}
				}
			}
		}
		else
		{
#if INV_DEBUG_EQUIP
			UE_LOG(LogTemp, Error, TEXT("⭐ [EquipmentComponent] 서버: SpawnedEquipActor가 null!"));
#endif
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
#if INV_DEBUG_EQUIP
			UE_LOG(LogTemp, Warning, TEXT("⭐ [EquipmentComponent] 클라이언트: 리플리케이트된 액터 추가: %s (총 %d개) - this: %p"),
				*ReplicatedActor->GetName(), EquippedActors.Num(), this);
#endif
			// 부착물 메시는 EquipActor::OnRep_AttachmentVisuals에서 자동 처리됨
		}
		else
		{
#if INV_DEBUG_EQUIP
			UE_LOG(LogTemp, Warning, TEXT("⭐ [EquipmentComponent] 클라이언트: EquippedActor 아직 없음 - 나중에 추가될 예정"));
#endif
		}
	}
}

// 아이템 해제 시 호출되는 함수
void UInv_EquipmentComponent::OnItemUnequipped(UInv_InventoryItem* UnequippedItem, int32 WeaponSlotIndex)
{
	AActor* OwnerActor = GetOwner();
	bool bIsServer = OwnerActor ? OwnerActor->HasAuthority() : false;
	
#if INV_DEBUG_EQUIP
	UE_LOG(LogTemp, Warning, TEXT("⭐ [EquipmentComponent] OnItemUnequipped 호출됨 - %s (WeaponSlotIndex: %d)"),
		bIsServer ? TEXT("서버") : TEXT("클라이언트"), WeaponSlotIndex);
#endif
	
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
#if INV_DEBUG_EQUIP
			UE_LOG(LogTemp, Warning, TEXT("⭐ [EquipmentComponent] 손에 든 무기 해제 - 델리게이트 브로드캐스트 (SlotIndex: %d)"), WeaponSlotIndex);
#endif
			
			// TODO: [독립화] 졸작 후 nullptr(SpawnWeaponAbility) 파라미터 삭제 → 4파라미터
			// 손 무기 파괴 델리게이트 브로드캐스트 (클라이언트에서 UI와 연결된 캐릭터에 전달)
			OnWeaponEquipRequested.Broadcast(
				EquipmentFragment->GetEquipmentType(),
				nullptr,  // 이미 파괴될 예정이므로 nullptr
				nullptr,
				false,  // bEquip = false (집어넣기/파괴)
				WeaponSlotIndex  // 해제되는 슬롯 인덱스
			);
			
			// 활성 슬롯 초기화
			ActiveWeaponSlot = EInv_ActiveWeaponSlot::None;
#if INV_DEBUG_EQUIP
			UE_LOG(LogTemp, Warning, TEXT("⭐ [EquipmentComponent] ActiveWeaponSlot = None으로 초기화"));
#endif
		}
	}

	// ⭐ 서버에서만 장비 제거 및 Destroy 실행
	if (!bIsServer) return;

	if (!bIsProxy) // 프록시 부분
	{
		// ════════════════════════════════════════════════════════════════
		// 📌 [부착물 시스템 Phase 2] 부착물 스탯 일괄 해제 → 무기 스탯 해제
		// 순서: 부착물 스탯 OnUnequip → 무기 스탯 OnUnequip
		// ════════════════════════════════════════════════════════════════
		FInv_AttachmentHostFragment* HostFragment = ItemManifest.GetFragmentOfTypeMutable<FInv_AttachmentHostFragment>();
		if (HostFragment && HostFragment->GetAttachedItems().Num() > 0)
		{
			HostFragment->OnUnequipAllAttachments(OwningPlayerController.Get());
#if INV_DEBUG_EQUIP
			UE_LOG(LogTemp, Warning, TEXT("📌 [Attachment] 무기 해제 시 부착물 스탯 %d개 일괄 해제"),
				HostFragment->GetAttachedItems().Num());
#endif
		}

		// 무기 자체의 EquipModifiers 해제
		EquipmentFragment->OnUnequip(OwningPlayerController.Get());
	}
	
	// ════════════════════════════════════════════════════════════════
	// 📌 [Phase 5] 무기 해제 시 부착물 메시도 함께 제거
	// ════════════════════════════════════════════════════════════════
	{
		AInv_EquipActor* WeaponActor = FindEquippedActor(EquipmentFragment->GetEquipmentType());
		if (IsValid(WeaponActor))
		{
			WeaponActor->DetachAllMeshes();
		}
	}

	// ⭐ [WeaponBridge] 장비 제거하는 부분 (등 무기 Destroy)
	// ⭐ WeaponSlotIndex를 전달하여 정확한 무기만 제거
	RemoveEquippedActor(EquipmentFragment->GetEquipmentType(), WeaponSlotIndex);
}

// ============================================
// ⭐ [WeaponBridge] 무기 장착 중 상태 설정
// ============================================
void UInv_EquipmentComponent::SetWeaponEquipping(bool bNewEquipping)
{
	bIsWeaponEquipping = bNewEquipping;
#if INV_DEBUG_EQUIP
	UE_LOG(LogTemp, Warning, TEXT("⭐ [EquipmentComponent] SetWeaponEquipping: %s"), 
		bIsWeaponEquipping ? TEXT("true (장착 중 - 전환 차단)") : TEXT("false (장착 완료 - 전환 허용)"));
#endif
}

// ============================================
// ⭐ [WeaponBridge] 무기 꺼내기/집어넣기 구현
// ============================================

void UInv_EquipmentComponent::HandlePrimaryWeaponInput()
{
#if INV_DEBUG_EQUIP
	UE_LOG(LogTemp, Warning, TEXT("⭐ [WeaponBridge] HandlePrimaryWeaponInput 호출됨 (1키)"));
#endif
	
	// ⭐ 장착 애니메이션 진행 중이면 입력 무시
	if (bIsWeaponEquipping)
	{
#if INV_DEBUG_EQUIP
		UE_LOG(LogTemp, Warning, TEXT("⭐ [WeaponBridge] 장착 애니메이션 진행 중 - 입력 무시"));
#endif
		return;
	}
	
	// 주무기가 없으면 무시
	AInv_EquipActor* WeaponActor = FindPrimaryWeaponActor();
	if (!IsValid(WeaponActor))
	{
#if INV_DEBUG_EQUIP
		UE_LOG(LogTemp, Warning, TEXT("⭐ [WeaponBridge] 장착된 주무기 없음 - 입력 무시"));
#endif
		return;
	}
	
#if INV_DEBUG_EQUIP
	UE_LOG(LogTemp, Warning, TEXT("⭐ [WeaponBridge] 현재 활성 슬롯: %d"), static_cast<int32>(ActiveWeaponSlot));
#endif
	
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
		// 김민우 수정 - GA_SpawnWeapon에서 교체 처리를 하도록 수정하였습니다! 중복 호출을 막기 위해서 여기서는 UnequipWeapon 호출을 주석처리 하였습니다.
		//UnequipWeapon();
		EquipPrimaryWeapon();
	}
}

void UInv_EquipmentComponent::HandleSecondaryWeaponInput()
{
#if INV_DEBUG_EQUIP
	UE_LOG(LogTemp, Warning, TEXT("⭐ [WeaponBridge] HandleSecondaryWeaponInput 호출됨 (2키)"));
#endif
	
	// ⭐ 장착 애니메이션 진행 중이면 입력 무시
	if (bIsWeaponEquipping)
	{
#if INV_DEBUG_EQUIP
		UE_LOG(LogTemp, Warning, TEXT("⭐ [WeaponBridge] 장착 애니메이션 진행 중 - 입력 무시"));
#endif
		return;
	}
	
	// 보조무기가 없으면 무시
	AInv_EquipActor* WeaponActor = FindSecondaryWeaponActor();
	if (!IsValid(WeaponActor))
	{
#if INV_DEBUG_EQUIP
		UE_LOG(LogTemp, Warning, TEXT("⭐ [WeaponBridge] 장착된 보조무기 없음 - 입력 무시"));
#endif
		return;
	}
	
#if INV_DEBUG_EQUIP
	UE_LOG(LogTemp, Warning, TEXT("⭐ [WeaponBridge] 현재 활성 슬롯: %d"), static_cast<int32>(ActiveWeaponSlot));
#endif
	
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
		// 김민우 수정 - GA_SpawnWeapon에서 교체 처리를 하도록 수정하였습니다! 중복 호출을 막기 위해서 여기서는 UnequipWeapon 호출을 주석처리 하였습니다.
		//UnequipWeapon();
		EquipSecondaryWeapon();
	}
}

void UInv_EquipmentComponent::EquipPrimaryWeapon()
{
	AInv_EquipActor* WeaponActor = FindPrimaryWeaponActor();
	if (!IsValid(WeaponActor))
	{
#if INV_DEBUG_EQUIP
		UE_LOG(LogTemp, Error, TEXT("⭐ [WeaponBridge] EquipPrimaryWeapon 실패 - WeaponActor 없음"));
#endif
		return;
	}
	
#if INV_DEBUG_EQUIP
	UE_LOG(LogTemp, Warning, TEXT("⭐ [WeaponBridge] 주무기 꺼내기 시작 - %s"), *WeaponActor->GetName());
#endif
	
	// 등 무기 숨기기 (리플리케이트)
	WeaponActor->SetWeaponHidden(true);
#if INV_DEBUG_EQUIP
	UE_LOG(LogTemp, Warning, TEXT("⭐ [WeaponBridge] 등 무기 Hidden 처리 완료"));
#endif
	
	// 무기 스폰 GA 확인
	// TODO: [독립화] 졸작 후 아래 SpawnWeaponAbility 관련 코드 삭제.
	// bUseBuiltInHandWeapon 분기 추가:
	//   true  → WeaponActor->AttachToHand(OwningSkeletalMesh.Get())
	//   false → WeaponActor->SetWeaponHidden(true) (현재 동작 유지)
	// Broadcast도 4파라미터로 변경.
	TSubclassOf<UGameplayAbility> SpawnWeaponAbility = WeaponActor->GetSpawnWeaponAbility();
#if INV_DEBUG_EQUIP
	if (!SpawnWeaponAbility)
	{
		UE_LOG(LogTemp, Error, TEXT("⭐ [WeaponBridge] SpawnWeaponAbility가 설정되지 않음!"));
	}
	else
	{
		UE_LOG(LogTemp, Warning, TEXT("⭐ [WeaponBridge] SpawnWeaponAbility: %s"), *SpawnWeaponAbility->GetName());
	}
#endif

	// 델리게이트 브로드캐스트 (Helluna에서 수신)
	OnWeaponEquipRequested.Broadcast(
		WeaponActor->GetEquipmentType(),
		WeaponActor,
		SpawnWeaponAbility,
		true,  // bEquip = true (꺼내기)
		0      // WeaponSlotIndex = 0 (주무기)
	);
#if INV_DEBUG_EQUIP
	UE_LOG(LogTemp, Warning, TEXT("⭐ [WeaponBridge] 델리게이트 브로드캐스트 완료 (bEquip = true, SlotIndex = 0)"));
#endif

	// 상태 변경
	ActiveWeaponSlot = EInv_ActiveWeaponSlot::Primary;
#if INV_DEBUG_EQUIP
	UE_LOG(LogTemp, Warning, TEXT("⭐ [WeaponBridge] 주무기 꺼내기 완료 - ActiveWeaponSlot = Primary"));
#endif
}

void UInv_EquipmentComponent::EquipSecondaryWeapon()
{
	AInv_EquipActor* WeaponActor = FindSecondaryWeaponActor();
	if (!IsValid(WeaponActor))
	{
#if INV_DEBUG_EQUIP
		UE_LOG(LogTemp, Error, TEXT("⭐ [WeaponBridge] EquipSecondaryWeapon 실패 - WeaponActor 없음"));
#endif
		return;
	}
	
#if INV_DEBUG_EQUIP
	UE_LOG(LogTemp, Warning, TEXT("⭐ [WeaponBridge] 보조무기 꺼내기 시작 - %s"), *WeaponActor->GetName());
#endif
	
	// 등 무기 숨기기 (리플리케이트)
	WeaponActor->SetWeaponHidden(true);
#if INV_DEBUG_EQUIP
	UE_LOG(LogTemp, Warning, TEXT("⭐ [WeaponBridge] 등 무기 Hidden 처리 완료"));
#endif
	
	// 무기 스폰 GA 확인
	// TODO: [독립화] 졸작 후 EquipPrimaryWeapon과 동일하게 변경
	TSubclassOf<UGameplayAbility> SpawnWeaponAbility = WeaponActor->GetSpawnWeaponAbility();
#if INV_DEBUG_EQUIP
	if (!SpawnWeaponAbility)
	{
		UE_LOG(LogTemp, Error, TEXT("⭐ [WeaponBridge] SpawnWeaponAbility가 설정되지 않음!"));
	}
	else
	{
		UE_LOG(LogTemp, Warning, TEXT("⭐ [WeaponBridge] SpawnWeaponAbility: %s"), *SpawnWeaponAbility->GetName());
	}
#endif

	// 델리게이트 브로드캐스트 (Helluna에서 수신)
	OnWeaponEquipRequested.Broadcast(
		WeaponActor->GetEquipmentType(),
		WeaponActor,
		SpawnWeaponAbility,
		true,  // bEquip = true (꺼내기)
		1      // WeaponSlotIndex = 1 (보조무기)
	);
#if INV_DEBUG_EQUIP
	UE_LOG(LogTemp, Warning, TEXT("⭐ [WeaponBridge] 델리게이트 브로드캐스트 완료 (bEquip = true, SlotIndex = 1)"));
#endif
	
	// 상태 변경
	ActiveWeaponSlot = EInv_ActiveWeaponSlot::Secondary;
#if INV_DEBUG_EQUIP
	UE_LOG(LogTemp, Warning, TEXT("⭐ [WeaponBridge] 보조무기 꺼내기 완료 - ActiveWeaponSlot = Secondary"));
#endif
}

void UInv_EquipmentComponent::UnequipWeapon()
{
	// 현재 활성 슬롯에 따라 해당 무기 찾기
	AInv_EquipActor* WeaponActor = nullptr;
	int32 SlotIndex = -1;
	
	if (ActiveWeaponSlot == EInv_ActiveWeaponSlot::Primary)
	{
		WeaponActor = FindPrimaryWeaponActor();
		SlotIndex = 0;
	}
	else if (ActiveWeaponSlot == EInv_ActiveWeaponSlot::Secondary)
	{
		WeaponActor = FindSecondaryWeaponActor();
		SlotIndex = 1;
	}
	
	if (!IsValid(WeaponActor))
	{
#if INV_DEBUG_EQUIP
		UE_LOG(LogTemp, Error, TEXT("⭐ [WeaponBridge] UnequipWeapon 실패 - WeaponActor 없음"));
#endif
		return;
	}
	
#if INV_DEBUG_EQUIP
	UE_LOG(LogTemp, Warning, TEXT("⭐ [WeaponBridge] 무기 집어넣기 시작 - %s (SlotIndex: %d)"), *WeaponActor->GetName(), SlotIndex);
#endif
	
	// TODO: [독립화] 졸작 후 Broadcast를 4파라미터로 변경 (SpawnWeaponAbility 파라미터 삭제)
	// bUseBuiltInHandWeapon 분기 추가:
	//   true  → WeaponActor->AttachToBack(OwningSkeletalMesh.Get())
	//   false → WeaponActor->SetWeaponHidden(false) (현재 동작 유지)
	// 델리게이트 브로드캐스트 (Helluna에서 손 무기 Destroy)
	OnWeaponEquipRequested.Broadcast(
		WeaponActor->GetEquipmentType(),
		WeaponActor,
		nullptr,  // 집어넣기라 GA 필요 없음
		false,    // bEquip = false (집어넣기)
		SlotIndex // WeaponSlotIndex
	);
#if INV_DEBUG_EQUIP
	UE_LOG(LogTemp, Warning, TEXT("⭐ [WeaponBridge] 델리게이트 브로드캐스트 완료 (bEquip = false, SlotIndex = %d)"), SlotIndex);
#endif
	
	// 등 무기 다시 보이기 (리플리케이트)
	WeaponActor->SetWeaponHidden(false);
#if INV_DEBUG_EQUIP
	UE_LOG(LogTemp, Warning, TEXT("⭐ [WeaponBridge] 등 무기 Visible 처리 완료"));
#endif
	
	// 상태 변경
	ActiveWeaponSlot = EInv_ActiveWeaponSlot::None;
#if INV_DEBUG_EQUIP
	UE_LOG(LogTemp, Warning, TEXT("⭐ [WeaponBridge] 무기 집어넣기 완료 - ActiveWeaponSlot = None"));
#endif


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
	
#if INV_DEBUG_EQUIP
	UE_LOG(LogTemp, Warning, TEXT("⭐ [WeaponBridge] FindPrimaryWeaponActor 시작 - %s"), 
		bIsServer ? TEXT("서버") : TEXT("클라이언트"));
#endif
	
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
#if INV_DEBUG_EQUIP
				UE_LOG(LogTemp, Warning, TEXT("⭐ [WeaponBridge] 주무기 찾음 (SlotIndex=0): %s"), *Actor->GetName());
#endif
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
#if INV_DEBUG_EQUIP
		UE_LOG(LogTemp, Warning, TEXT("⭐ [WeaponBridge] 주무기 찾음 (SlotIndex 미설정, 첫 번째): %s"), *FirstWeaponWithUnsetIndex->GetName());
#endif
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
#if INV_DEBUG_EQUIP
							UE_LOG(LogTemp, Warning, TEXT("⭐ [WeaponBridge] 2단계 - 주무기 찾음 (SlotIndex=%d): %s"), SlotIndex, *EquipActor->GetName());
#endif
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
	
#if INV_DEBUG_EQUIP
	UE_LOG(LogTemp, Warning, TEXT("⭐ [WeaponBridge] 주무기 없음"));
#endif
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
	
#if INV_DEBUG_EQUIP
	UE_LOG(LogTemp, Warning, TEXT("⭐ [WeaponBridge] FindSecondaryWeaponActor 시작 - %s"), 
		bIsServer ? TEXT("서버") : TEXT("클라이언트"));
#endif
	
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
#if INV_DEBUG_EQUIP
				UE_LOG(LogTemp, Warning, TEXT("⭐ [WeaponBridge] 보조무기 찾음 (SlotIndex=1): %s"), *Actor->GetName());
#endif
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
#if INV_DEBUG_EQUIP
		UE_LOG(LogTemp, Warning, TEXT("⭐ [WeaponBridge] 보조무기 찾음 (SlotIndex 미설정, 두 번째): %s"), *SecondWeaponWithUnsetIndex->GetName());
#endif
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
#if INV_DEBUG_EQUIP
							UE_LOG(LogTemp, Warning, TEXT("⭐ [WeaponBridge] 2단계 - 보조무기 찾음 (SlotIndex=1): %s"), *EquipActor->GetName());
#endif
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
#if INV_DEBUG_EQUIP
								UE_LOG(LogTemp, Warning, TEXT("⭐ [WeaponBridge] 2단계 - 보조무기 찾음 (SlotIndex 미설정, 두 번째): %s"), *EquipActor->GetName());
#endif
								return EquipActor;
							}
						}
					}
				}
			}
		}
	}
	
#if INV_DEBUG_EQUIP
	UE_LOG(LogTemp, Warning, TEXT("⭐ [WeaponBridge] 보조무기 없음"));
#endif
	return nullptr;
}

//================== 김민우 수정 =====================
//		UnequipWeapon(); 외부에서 호출하는 함수	추가
//==================================================

void UInv_EquipmentComponent::ActiveUnequipWeapon()
{
	// 이미 맨손이면 아무것도 안 함
	if (ActiveWeaponSlot == EInv_ActiveWeaponSlot::None)
	{
		return;
	}

	// 핵심: 입력 토글이 아니라 "언이큅"만 강제
	UnequipWeapon();
}

// ════════════════════════════════════════════════════════════════
// 🆕 [Phase 7.5] 현재 활성 무기의 EquipActor 반환
// ════════════════════════════════════════════════════════════════
// [2026-02-18] 작업자: 김기현
// ────────────────────────────────────────────────────────────────
// ActiveWeaponSlot 열거형에 따라 현재 손에 들고 있는 무기의
// EquipActor를 반환한다.
//
// - EInv_ActiveWeaponSlot::Primary   → FindPrimaryWeaponActor()
// - EInv_ActiveWeaponSlot::Secondary → FindSecondaryWeaponActor()
// - EInv_ActiveWeaponSlot::None      → nullptr (맨손 상태)
//
// FindPrimaryWeaponActor() / FindSecondaryWeaponActor()는
// 기존 private 함수를 그대로 활용하므로 추가 구현 불필요.
// ════════════════════════════════════════════════════════════════
AInv_EquipActor* UInv_EquipmentComponent::GetActiveWeaponActor()
{
	switch (ActiveWeaponSlot)
	{
	case EInv_ActiveWeaponSlot::Primary:
		return FindPrimaryWeaponActor();

	case EInv_ActiveWeaponSlot::Secondary:
		return FindSecondaryWeaponActor();

	default:
		// None = 맨손 상태 → EquipActor 없음
		return nullptr;
	}
}


