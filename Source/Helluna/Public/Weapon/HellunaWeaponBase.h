// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "HellunaWeaponBase.generated.h"

class UBoxComponent;
class UStaticMesh;

UCLASS()
class HELLUNA_API AHellunaWeaponBase : public AActor
{
	GENERATED_BODY()
	
public:	

	AHellunaWeaponBase();

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Weapon|Stats", meta = (DisplayName = "데미지"))
	float Damage = 20.f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Weapon|Stats", meta = (DisplayName = "공격 간격(n초에 1번 공격)"))
	float AttackSpeed = 0.1f;

protected:


	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Weapons")
	UStaticMeshComponent* WeaponMesh;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Weapons")
	UBoxComponent* WeaponCollisionBox;

public:	

	FORCEINLINE UBoxComponent* GetWeaponCollisionBox() const { return WeaponCollisionBox; }

	// ════════════════════════════════════════════════════════════════
	// 📌 부착물 시각 복제 (WeaponBridgeComponent에서 호출)
	// ════════════════════════════════════════════════════════════════
	// EquipActor(등 무기)의 부착물 시각 정보를 읽어서
	// 이 무기의 WeaponMesh 소켓에 동일하게 부착한다.

	// 부착물 메시를 이 무기의 WeaponMesh 소켓에 부착
	void ApplyAttachmentVisual(int32 SlotIndex, UStaticMesh* Mesh, FName SocketName, const FTransform& Offset);

	// 모든 부착물 메시 제거
	void ClearAttachmentVisuals();

private:
	// 슬롯 인덱스 → 스폰된 부착물 메시 컴포넌트
	UPROPERTY()
	TMap<int32, TObjectPtr<UStaticMeshComponent>> AttachmentVisualComponents;
};
