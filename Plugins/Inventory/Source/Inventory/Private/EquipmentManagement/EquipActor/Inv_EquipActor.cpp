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
	
	// ⭐ [WeaponBridge] WeaponSlotIndex — 스폰 시 1회 설정, 이후 변경 없음
	DOREPLIFETIME_CONDITION(AInv_EquipActor, WeaponSlotIndex, COND_InitialOnly);

	// ⭐ [WeaponBridge] bIsWeaponHidden — 모든 클라이언트가 시각적으로 필요
	DOREPLIFETIME(AInv_EquipActor, bIsWeaponHidden);

	// [Phase 7] 부착물 효과 리플리케이션
	DOREPLIFETIME(AInv_EquipActor, bSuppressed);
	// OverrideZoomFOV — ADS 줌은 소유 클라이언트만 필요
	DOREPLIFETIME_CONDITION(AInv_EquipActor, OverrideZoomFOV, COND_OwnerOnly);
	DOREPLIFETIME(AInv_EquipActor, bLaserActive);

	// ★ [Phase 5 리플리케이션] 부착물 비주얼 데이터
	DOREPLIFETIME(AInv_EquipActor, ReplicatedAttachmentVisuals);
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
// 📌 GetAttachmentVisualInfos — 부착물 시각 정보 일괄 반환
// ════════════════════════════════════════════════════════════════
// AttachmentMeshComponents 맵을 순회하여 각 부착물의
// SlotIndex, Mesh, SocketName, Offset을 DTO로 반환한다.
// 게임 모듈에서 다른 액터(손 무기 등)에 동일한 부착물을 복제할 때 사용.
// ════════════════════════════════════════════════════════════════
TArray<FInv_AttachmentVisualInfo> AInv_EquipActor::GetAttachmentVisualInfos() const
{
	TArray<FInv_AttachmentVisualInfo> Result;

	for (const auto& Pair : AttachmentMeshComponents)
	{
		if (!IsValid(Pair.Value)) continue;

		FInv_AttachmentVisualInfo Info;
		Info.SlotIndex = Pair.Key;
		Info.Mesh = Pair.Value->GetStaticMesh();
		Info.Offset = Pair.Value->GetRelativeTransform();

		// 소켓 이름은 부모 컴포넌트의 AttachSocketName에서 가져옴
		Info.SocketName = Pair.Value->GetAttachSocketName();

		Result.Add(Info);
	}

	return Result;
}

// ════════════════════════════════════════════════════════════════
// FindComponentWithSocket — 소켓을 보유한 자식 컴포넌트 탐색
// ════════════════════════════════════════════════════════════════
// RootComponent(DefaultSceneRoot = USceneComponent)에는 소켓이 없다.
// socket_scope, socket_muzzle 등은 자식 메시 컴포넌트에 정의되어 있으므로
// GetComponents()로 순회하여 DoesSocketExist()가 true인 컴포넌트를 반환한다.
// ════════════════════════════════════════════════════════════════
USceneComponent* AInv_EquipActor::FindComponentWithSocket(FName SocketName) const
{
	TArray<USceneComponent*> SceneComponents;
	GetComponents<USceneComponent>(SceneComponents);

	for (USceneComponent* Comp : SceneComponents)
	{
		if (IsValid(Comp) && Comp->DoesSocketExist(SocketName))
		{
			return Comp;
		}
	}

	// 소켓을 찾지 못한 경우 — 폴백으로 RootComponent 반환
#if INV_DEBUG_EQUIP
	UE_LOG(LogTemp, Warning,
		TEXT("[Attachment Visual] FindComponentWithSocket: 소켓 '%s'을(를) 보유한 컴포넌트를 찾지 못함. RootComponent로 폴백합니다. (Actor: %s)"),
		*SocketName.ToString(),
		*GetName());
#endif

	return GetRootComponent();
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
#if INV_DEBUG_EQUIP
		UE_LOG(LogTemp, Warning, TEXT("[Attachment Visual] AttachMeshToSocket 실패: Mesh가 nullptr (SlotIndex=%d)"), SlotIndex);
#endif
		return;
	}

	// 기존 컴포넌트가 있으면 먼저 제거 (중복 방지)
	DetachMeshFromSocket(SlotIndex);

	// StaticMeshComponent 생성
	UStaticMeshComponent* MeshComp = NewObject<UStaticMeshComponent>(this);
	if (!IsValid(MeshComp))
	{
#if INV_DEBUG_EQUIP
		UE_LOG(LogTemp, Error, TEXT("[Attachment Visual] StaticMeshComponent 생성 실패 (SlotIndex=%d)"), SlotIndex);
#endif
		return;
	}

	MeshComp->SetStaticMesh(Mesh);

	// 부착물은 시각 전용 — 충돌 비활성화 (BlockAllDynamic 기본값이 캐릭터 움직임 방해)
	MeshComp->SetCollisionEnabled(ECollisionEnabled::NoCollision);

	// 소켓을 보유한 실제 메시 컴포넌트를 찾아 부착
	// (RootComponent=DefaultSceneRoot에는 소켓이 없으므로 직접 탐색)
	USceneComponent* TargetComp = FindComponentWithSocket(SocketName);
	FAttachmentTransformRules AttachRules(EAttachmentRule::SnapToTarget, true);
	MeshComp->AttachToComponent(TargetComp, AttachRules, SocketName);

	// 오프셋 적용
	MeshComp->SetRelativeTransform(Offset);

	// 컴포넌트 활성화
	MeshComp->RegisterComponent();

	// 맵에 등록
	AttachmentMeshComponents.Add(SlotIndex, MeshComp);

	// ★ 서버: 리플리케이트 배열에도 추가 (클라이언트 OnRep_AttachmentVisuals 트리거)
	if (HasAuthority())
	{
		// 같은 SlotIndex가 이미 있으면 교체
		ReplicatedAttachmentVisuals.RemoveAll([SlotIndex](const FInv_AttachmentVisualInfo& V) { return V.SlotIndex == SlotIndex; });

		FInv_AttachmentVisualInfo VisualInfo;
		VisualInfo.SlotIndex = SlotIndex;
		VisualInfo.Mesh = Mesh;
		VisualInfo.SocketName = SocketName;
		VisualInfo.Offset = Offset;
		ReplicatedAttachmentVisuals.Add(VisualInfo);
	}

#if INV_DEBUG_ATTACHMENT
	UE_LOG(LogTemp, Log, TEXT("[Attachment Visual] 슬롯 %d에 메시 부착: %s → 소켓 %s"),
		SlotIndex,
		*Mesh->GetName(),
		*SocketName.ToString());
#endif
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
#if INV_DEBUG_ATTACHMENT
		UE_LOG(LogTemp, Log, TEXT("[Attachment Visual] 슬롯 %d 메시 분리"), SlotIndex);
#endif
	}
	AttachmentMeshComponents.Remove(SlotIndex);

	// ★ 서버: 리플리케이트 배열에서도 제거
	if (HasAuthority())
	{
		ReplicatedAttachmentVisuals.RemoveAll([SlotIndex](const FInv_AttachmentVisualInfo& V) { return V.SlotIndex == SlotIndex; });
	}
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

	// ★ 서버: 리플리케이트 배열도 클리어
	if (HasAuthority())
	{
		ReplicatedAttachmentVisuals.Empty();
	}

#if INV_DEBUG_ATTACHMENT
	if (Count > 0)
	{
		UE_LOG(LogTemp, Log, TEXT("[Attachment Visual] 모든 부착물 메시 분리 (%d개)"), Count);
	}
#endif
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
#if INV_DEBUG_ATTACHMENT
	UE_LOG(LogTemp, Log, TEXT("[Attachment Effect] 소음기 %s"), bSuppressed ? TEXT("ON") : TEXT("OFF"));
#endif
}

void AInv_EquipActor::SetZoomFOVOverride(float NewFOV)
{
	OverrideZoomFOV = NewFOV;
#if INV_DEBUG_ATTACHMENT
	UE_LOG(LogTemp, Log, TEXT("[Attachment Effect] 줌 FOV 오버라이드: %.1f"), OverrideZoomFOV);
#endif
}

void AInv_EquipActor::ClearZoomFOVOverride()
{
	OverrideZoomFOV = 0.f;
#if INV_DEBUG_ATTACHMENT
	UE_LOG(LogTemp, Log, TEXT("[Attachment Effect] 줌 FOV 오버라이드 해제 -> 기본값 %.1f"), DefaultZoomFOV);
#endif
}

void AInv_EquipActor::SetLaserActive(bool bNewActive)
{
	bLaserActive = bNewActive;
	if (IsValid(LaserBeamComponent))
	{
		LaserBeamComponent->SetVisibility(bNewActive);
	}
#if INV_DEBUG_ATTACHMENT
	UE_LOG(LogTemp, Log, TEXT("[Attachment Effect] 레이저 %s"), bLaserActive ? TEXT("ON") : TEXT("OFF"));
#endif
}

// -- 리플리케이션 콜백 --

void AInv_EquipActor::OnRep_bSuppressed()
{
	// 사운드는 발사 시점에 GetFireSound()로 읽으므로 추가 처리 불필요
#if INV_DEBUG_ATTACHMENT
	UE_LOG(LogTemp, Log, TEXT("[Attachment Effect] OnRep: 소음기 %s"), bSuppressed ? TEXT("ON") : TEXT("OFF"));
#endif
}

void AInv_EquipActor::OnRep_bLaserActive()
{
	if (IsValid(LaserBeamComponent))
	{
		LaserBeamComponent->SetVisibility(bLaserActive);
	}
#if INV_DEBUG_ATTACHMENT
	UE_LOG(LogTemp, Log, TEXT("[Attachment Effect] OnRep: 레이저 %s"), bLaserActive ? TEXT("ON") : TEXT("OFF"));
#endif
}

// ════════════════════════════════════════════════════════════════
// ★ [Phase 5 리플리케이션] OnRep — 클라이언트에서 부착물 메시 재생성
// 서버가 ReplicatedAttachmentVisuals 배열을 갱신하면
// 클라이언트에서 이 콜백이 호출되어 메시를 로컬 생성한다.
// ════════════════════════════════════════════════════════════════
void AInv_EquipActor::OnRep_AttachmentVisuals()
{
	// Diff 기반 업데이트 — 변경된 슬롯만 파괴/재생성

	// 1) 새 데이터의 SlotIndex → Info 매핑 구축
	TMap<int32, const FInv_AttachmentVisualInfo*> NewSlotMap;
	for (const FInv_AttachmentVisualInfo& Info : ReplicatedAttachmentVisuals)
	{
		NewSlotMap.Add(Info.SlotIndex, &Info);
	}

	// 2) 기존에 있지만 새 데이터에 없는 슬롯 제거
	TArray<int32> SlotsToRemove;
	for (auto& Pair : AttachmentMeshComponents)
	{
		if (!NewSlotMap.Contains(Pair.Key))
		{
			if (IsValid(Pair.Value))
			{
				Pair.Value->DestroyComponent();
			}
			SlotsToRemove.Add(Pair.Key);
		}
	}
	for (const int32 Slot : SlotsToRemove)
	{
		AttachmentMeshComponents.Remove(Slot);
	}

	// 3) 새 데이터 순회 — 동일 메시면 스킵, 다르면 교체
	for (const auto& SlotPair : NewSlotMap)
	{
		const int32 SlotIndex = SlotPair.Key;
		const FInv_AttachmentVisualInfo& Info = *SlotPair.Value;

		if (!IsValid(Info.Mesh)) continue;

		// 기존 메시가 동일하면 스킵 (불필요한 파괴/재생성 방지)
		if (TObjectPtr<UStaticMeshComponent>* ExistingPtr = AttachmentMeshComponents.Find(SlotIndex))
		{
			if (IsValid(*ExistingPtr) && (*ExistingPtr)->GetStaticMesh() == Info.Mesh)
			{
				continue;
			}
			// 메시가 다르면 기존 것 파괴
			if (IsValid(*ExistingPtr))
			{
				(*ExistingPtr)->DestroyComponent();
			}
			AttachmentMeshComponents.Remove(SlotIndex);
		}

		// 새 메시 생성
		UStaticMeshComponent* MeshComp = NewObject<UStaticMeshComponent>(this);
		if (!IsValid(MeshComp)) continue;

		MeshComp->SetStaticMesh(Info.Mesh);
		MeshComp->SetCollisionEnabled(ECollisionEnabled::NoCollision);

		USceneComponent* TargetComp = FindComponentWithSocket(Info.SocketName);
		FAttachmentTransformRules AttachRules(EAttachmentRule::SnapToTarget, true);
		MeshComp->AttachToComponent(TargetComp, AttachRules, Info.SocketName);
		MeshComp->SetRelativeTransform(Info.Offset);
		MeshComp->RegisterComponent();

		AttachmentMeshComponents.Add(SlotIndex, MeshComp);
	}

#if INV_DEBUG_ATTACHMENT
	UE_LOG(LogTemp, Warning, TEXT("★ [Phase 5 OnRep] 클라이언트: 부착물 Diff 업데이트 완료 (슬롯 %d개, Actor: %s)"),
		ReplicatedAttachmentVisuals.Num(), *GetName());
#endif
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
