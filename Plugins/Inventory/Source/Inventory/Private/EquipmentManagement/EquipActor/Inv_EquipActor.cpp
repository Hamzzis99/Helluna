// Gihyeon's Inventory Project


#include "EquipmentManagement/EquipActor/Inv_EquipActor.h"
#include "Inventory.h"
#include "Net/UnrealNetwork.h"
#include "Items/Fragments/Inv_AttachmentFragments.h"
#include "Sound/SoundBase.h"


AInv_EquipActor::AInv_EquipActor()
{
	// Set this actor to call Tick() every frame.  You can turn this off to improve performance if you don't need it.
	PrimaryActorTick.bCanEverTick = false;
	bReplicates = true; // 서버하고 교환해야 하니 RPC를 켜야겠지?
}

void AInv_EquipActor::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	
	// ⭐ [WeaponBridge] WeaponSlotIndex 리플리케이트
	DOREPLIFETIME(AInv_EquipActor, WeaponSlotIndex);
	
	// ⭐ [WeaponBridge] bIsWeaponHidden 리플리케이트
	DOREPLIFETIME(AInv_EquipActor, bIsWeaponHidden);

	// [Phase 7] 부착물 효과 리플리케이션
	DOREPLIFETIME(AInv_EquipActor, bSuppressed);
	DOREPLIFETIME(AInv_EquipActor, OverrideZoomFOV);
	DOREPLIFETIME(AInv_EquipActor, bLaserActive);
}

// ⭐ [WeaponBridge] 무기 숨김/표시 설정
// ⭐ 서버에서 호출되면 직접 실행, 클라이언트에서 호출되면 Server RPC 전송
void AInv_EquipActor::SetWeaponHidden(bool bNewHidden)
{
	if (HasAuthority())
	{
		// 서버에서 호출됨 - 직접 실행
		bIsWeaponHidden = bNewHidden;
		SetActorHiddenInGame(bNewHidden);
#if INV_DEBUG_EQUIP
		UE_LOG(LogTemp, Warning, TEXT("⭐ [Inv_EquipActor] SetWeaponHidden (서버): %s"), bNewHidden ? TEXT("Hidden") : TEXT("Visible"));
#endif
	}
	else
	{
		// 클라이언트에서 호출됨 - Server RPC로 서버에 요청
		Server_SetWeaponHidden(bNewHidden);
		// 로컬에서도 즉시 적용 (반응성을 위해)
		SetActorHiddenInGame(bNewHidden);
#if INV_DEBUG_EQUIP
		UE_LOG(LogTemp, Warning, TEXT("⭐ [Inv_EquipActor] SetWeaponHidden (클라이언트→서버 RPC): %s"), bNewHidden ? TEXT("Hidden") : TEXT("Visible"));
#endif
	}
}

// ⭐ [WeaponBridge] Server RPC 구현
void AInv_EquipActor::Server_SetWeaponHidden_Implementation(bool bNewHidden)
{
	// 서버에서 실행됨
	bIsWeaponHidden = bNewHidden;
	SetActorHiddenInGame(bNewHidden);
#if INV_DEBUG_EQUIP
	UE_LOG(LogTemp, Warning, TEXT("⭐ [Inv_EquipActor] Server_SetWeaponHidden (서버 RPC 수신): %s"), bNewHidden ? TEXT("Hidden") : TEXT("Visible"));
#endif
}

// ⭐ [WeaponBridge] 클라이언트에서 리플리케이션 수신 시 호출
void AInv_EquipActor::OnRep_IsWeaponHidden()
{
	SetActorHiddenInGame(bIsWeaponHidden);
#if INV_DEBUG_EQUIP
	UE_LOG(LogTemp, Warning, TEXT("⭐ [Inv_EquipActor] OnRep_IsWeaponHidden: %s"), bIsWeaponHidden ? TEXT("Hidden") : TEXT("Visible"));
#endif
}

// ════════════════════════════════════════════════════════════════
// 📌 [Phase 5] AttachMeshToSocket — 부착물 메시를 소켓에 부착
// ════════════════════════════════════════════════════════════════
// 호출 경로: EquipmentComponent::OnItemEquipped / Server_AttachItemToWeapon → 이 함수
// 처리 흐름:
//   1. 기존 컴포넌트가 있으면 제거 (중복 방지)
//   2. NewObject<UStaticMeshComponent> 생성
//   3. StaticMesh 설정 → RootComponent에 부착 (소켓 지정)
//   4. 오프셋 적용 → RegisterComponent
//   5. AttachmentMeshComponents 맵에 등록
// ════════════════════════════════════════════════════════════════
void AInv_EquipActor::AttachMeshToSocket(int32 SlotIndex, UStaticMesh* Mesh, FName SocketName, const FTransform& Offset)
{
	if (!IsValid(Mesh))
	{
		UE_LOG(LogTemp, Warning, TEXT("[Attachment Visual] AttachMeshToSocket 실패: Mesh가 nullptr (SlotIndex=%d)"), SlotIndex);
		return;
	}

	// 기존 컴포넌트가 있으면 먼저 제거 (중복 방지)
	DetachMeshFromSocket(SlotIndex);

	// StaticMeshComponent 생성
	UStaticMeshComponent* MeshComp = NewObject<UStaticMeshComponent>(this);
	if (!IsValid(MeshComp))
	{
		UE_LOG(LogTemp, Error, TEXT("[Attachment Visual] StaticMeshComponent 생성 실패 (SlotIndex=%d)"), SlotIndex);
		return;
	}

	MeshComp->SetStaticMesh(Mesh);

	// RootComponent에 부착 (소켓이 있으면 소켓에, 없으면 루트에)
	FAttachmentTransformRules AttachRules(EAttachmentRule::SnapToTarget, true);
	MeshComp->AttachToComponent(GetRootComponent(), AttachRules, SocketName);

	// 오프셋 적용
	MeshComp->SetRelativeTransform(Offset);

	// 컴포넌트 활성화
	MeshComp->RegisterComponent();

	// 맵에 등록
	AttachmentMeshComponents.Add(SlotIndex, MeshComp);

	UE_LOG(LogTemp, Log, TEXT("[Attachment Visual] 슬롯 %d에 메시 부착: %s → 소켓 %s"),
		SlotIndex,
		*Mesh->GetName(),
		*SocketName.ToString());
}

// ════════════════════════════════════════════════════════════════
// 📌 [Phase 5] DetachMeshFromSocket — 슬롯의 부착물 메시 제거
// ════════════════════════════════════════════════════════════════
// 호출 경로: Server_DetachItemFromWeapon / AttachMeshToSocket(중복 방지) → 이 함수
// 처리 흐름:
//   1. AttachmentMeshComponents에서 SlotIndex 검색
//   2. 있으면 DestroyComponent → 맵에서 제거
// ════════════════════════════════════════════════════════════════
void AInv_EquipActor::DetachMeshFromSocket(int32 SlotIndex)
{
	TObjectPtr<UStaticMeshComponent>* Found = AttachmentMeshComponents.Find(SlotIndex);
	if (Found && IsValid(*Found))
	{
		(*Found)->DestroyComponent();
		UE_LOG(LogTemp, Log, TEXT("[Attachment Visual] 슬롯 %d 메시 분리"), SlotIndex);
	}
	AttachmentMeshComponents.Remove(SlotIndex);
}

// ════════════════════════════════════════════════════════════════
// 📌 [Phase 5] DetachAllMeshes — 모든 부착물 메시 제거 (무기 해제 시)
// ════════════════════════════════════════════════════════════════
// 호출 경로: EquipmentComponent::OnItemUnequipped → 이 함수
// 처리 흐름:
//   1. 모든 MeshComponent DestroyComponent
//   2. AttachmentMeshComponents 맵 비우기
// ════════════════════════════════════════════════════════════════
void AInv_EquipActor::DetachAllMeshes()
{
	int32 Count = AttachmentMeshComponents.Num();

	for (auto& Pair : AttachmentMeshComponents)
	{
		if (IsValid(Pair.Value))
		{
			Pair.Value->DestroyComponent();
		}
	}
	AttachmentMeshComponents.Empty();

	if (Count > 0)
	{
		UE_LOG(LogTemp, Log, TEXT("[Attachment Visual] 모든 부착물 메시 분리 (%d개)"), Count);
	}
}

// ════════════════════════════════════════════════════════════════
// [Phase 7] 부착물 효과 시스템 구현
// ════════════════════════════════════════════════════════════════

// -- Getter --

USoundBase* AInv_EquipActor::GetFireSound() const
{
	if (bSuppressed && IsValid(SuppressedFireSound))
	{
		return SuppressedFireSound;
	}
	return DefaultFireSound;
}

float AInv_EquipActor::GetZoomFOV() const
{
	return (OverrideZoomFOV > 0.f) ? OverrideZoomFOV : DefaultZoomFOV;
}

// -- Setter --

void AInv_EquipActor::SetSuppressed(bool bNewSuppressed)
{
	bSuppressed = bNewSuppressed;
	UE_LOG(LogTemp, Log, TEXT("[Attachment Effect] 소음기 %s"), bSuppressed ? TEXT("ON") : TEXT("OFF"));
}

void AInv_EquipActor::SetZoomFOVOverride(float NewFOV)
{
	OverrideZoomFOV = NewFOV;
	UE_LOG(LogTemp, Log, TEXT("[Attachment Effect] 줌 FOV 오버라이드: %.1f"), OverrideZoomFOV);
}

void AInv_EquipActor::ClearZoomFOVOverride()
{
	OverrideZoomFOV = 0.f;
	UE_LOG(LogTemp, Log, TEXT("[Attachment Effect] 줌 FOV 오버라이드 해제 -> 기본값 %.1f"), DefaultZoomFOV);
}

void AInv_EquipActor::SetLaserActive(bool bNewActive)
{
	bLaserActive = bNewActive;
	if (IsValid(LaserBeamComponent))
	{
		LaserBeamComponent->SetVisibility(bNewActive);
	}
	UE_LOG(LogTemp, Log, TEXT("[Attachment Effect] 레이저 %s"), bLaserActive ? TEXT("ON") : TEXT("OFF"));
}

// -- 리플리케이션 콜백 --

void AInv_EquipActor::OnRep_bSuppressed()
{
	// 사운드는 발사 시점에 GetFireSound()로 읽으므로 추가 처리 불필요
	UE_LOG(LogTemp, Log, TEXT("[Attachment Effect] OnRep: 소음기 %s"), bSuppressed ? TEXT("ON") : TEXT("OFF"));
}

void AInv_EquipActor::OnRep_bLaserActive()
{
	if (IsValid(LaserBeamComponent))
	{
		LaserBeamComponent->SetVisibility(bLaserActive);
	}
	UE_LOG(LogTemp, Log, TEXT("[Attachment Effect] OnRep: 레이저 %s"), bLaserActive ? TEXT("ON") : TEXT("OFF"));
}

// -- 일괄 적용/해제 --

void AInv_EquipActor::ApplyAttachmentEffects(const FInv_AttachableFragment* AttachableFrag)
{
	if (!AttachableFrag) return;

	if (AttachableFrag->GetIsSuppressor())
	{
		SetSuppressed(true);
	}

	if (AttachableFrag->GetZoomFOVOverride() > 0.f)
	{
		SetZoomFOVOverride(AttachableFrag->GetZoomFOVOverride());
	}

	if (AttachableFrag->GetIsLaser())
	{
		SetLaserActive(true);
	}
}

void AInv_EquipActor::RemoveAttachmentEffects(const FInv_AttachableFragment* AttachableFrag)
{
	if (!AttachableFrag) return;

	if (AttachableFrag->GetIsSuppressor())
	{
		SetSuppressed(false);
	}

	if (AttachableFrag->GetZoomFOVOverride() > 0.f)
	{
		ClearZoomFOVOverride();
	}

	if (AttachableFrag->GetIsLaser())
	{
		SetLaserActive(false);
	}
}
