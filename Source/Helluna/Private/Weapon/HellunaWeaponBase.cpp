// Fill out your copyright notice in the Description page of Project Settings.


#include "Weapon/HellunaWeaponBase.h"
#include "Components/BoxComponent.h"
#include "Engine/StaticMesh.h"

// Sets default values
AHellunaWeaponBase::AHellunaWeaponBase()
{

	PrimaryActorTick.bCanEverTick = false;

	WeaponMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("WeaponMesh"));
	SetRootComponent(WeaponMesh);
	WeaponMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);

	WeaponCollisionBox = CreateDefaultSubobject<UBoxComponent>(TEXT("WeaponCollisionBox"));
	WeaponCollisionBox->SetupAttachment(GetRootComponent());
	WeaponCollisionBox->SetBoxExtent(FVector(20.f));
	WeaponCollisionBox->SetCollisionEnabled(ECollisionEnabled::NoCollision);

	bReplicates = true;
	SetReplicateMovement(true);

}

// ════════════════════════════════════════════════════════════════
// 📌 ApplyAttachmentVisual — 부착물 메시를 WeaponMesh 소켓에 부착
// ════════════════════════════════════════════════════════════════
// WeaponBridgeComponent::TransferAttachmentVisuals에서 호출.
// EquipActor의 부착물 정보를 받아 이 무기의 동일 소켓에 메시를 복제.
// WeaponMesh는 StaticMeshComponent(RootComponent)이므로
// DoesSocketExist()가 정상 동작한다 (USceneComponent와 다름).
// ════════════════════════════════════════════════════════════════
void AHellunaWeaponBase::ApplyAttachmentVisual(int32 SlotIndex, UStaticMesh* Mesh, FName SocketName, const FTransform& Offset)
{
	if (!IsValid(Mesh) || !IsValid(WeaponMesh)) return;

	// 기존 컴포넌트 제거 (중복 방지)
	if (TObjectPtr<UStaticMeshComponent>* Found = AttachmentVisualComponents.Find(SlotIndex))
	{
		if (IsValid(*Found))
		{
			(*Found)->DestroyComponent();
		}
		AttachmentVisualComponents.Remove(SlotIndex);
	}

	// 새 StaticMeshComponent 생성 → WeaponMesh 소켓에 부착
	UStaticMeshComponent* MeshComp = NewObject<UStaticMeshComponent>(this);
	if (!IsValid(MeshComp)) return;

	MeshComp->SetStaticMesh(Mesh);

	// 소켓 존재 여부 확인 (디버깅용)
	if (!WeaponMesh->DoesSocketExist(SocketName))
	{
		UE_LOG(LogTemp, Warning,
			TEXT("[WeaponBase] ApplyAttachmentVisual: WeaponMesh에 소켓 '%s'이(를) 찾을 수 없음 (Actor: %s). 원점에 부착됩니다."),
			*SocketName.ToString(), *GetName());
	}

	FAttachmentTransformRules AttachRules(EAttachmentRule::SnapToTarget, true);
	MeshComp->AttachToComponent(WeaponMesh, AttachRules, SocketName);
	MeshComp->SetRelativeTransform(Offset);
	MeshComp->RegisterComponent();

	AttachmentVisualComponents.Add(SlotIndex, MeshComp);

	UE_LOG(LogTemp, Log, TEXT("[WeaponBase] 부착물 시각 적용: 슬롯 %d, 메시 %s -> 소켓 %s"),
		SlotIndex, *Mesh->GetName(), *SocketName.ToString());
}

// ════════════════════════════════════════════════════════════════
// 📌 ClearAttachmentVisuals — 모든 부착물 메시 제거
// ════════════════════════════════════════════════════════════════
void AHellunaWeaponBase::ClearAttachmentVisuals()
{
	for (auto& Pair : AttachmentVisualComponents)
	{
		if (IsValid(Pair.Value))
		{
			Pair.Value->DestroyComponent();
		}
	}
	AttachmentVisualComponents.Empty();
}


