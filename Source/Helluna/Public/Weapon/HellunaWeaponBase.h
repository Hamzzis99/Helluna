// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "HellunaWeaponBase.generated.h"

class UBoxComponent;
class UStaticMesh;
class UTexture2D;

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

	/** 에디터에서 무기별로 지정하는 HUD 아이콘 이미지 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Weapon|UI",
		meta = (DisplayName = "무기 아이콘"))
	TObjectPtr<UTexture2D> WeaponIcon = nullptr;

	/** 에디터에서 직접 입력하는 무기 표시 이름 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Weapon|UI",
		meta = (DisplayName = "무기 표시 이름"))
	FText WeaponDisplayName = FText::GetEmpty();

protected:

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Weapons")
	UStaticMeshComponent* WeaponMesh;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Weapons")
	UBoxComponent* WeaponCollisionBox;

public:	

	FORCEINLINE UBoxComponent* GetWeaponCollisionBox() const { return WeaponCollisionBox; }
	FORCEINLINE UTexture2D* GetWeaponIcon() const { return WeaponIcon; }
	FORCEINLINE FText GetWeaponDisplayName() const { return WeaponDisplayName; }

	void ApplyAttachmentVisual(int32 SlotIndex, UStaticMesh* Mesh, FName SocketName, const FTransform& Offset);
	void ClearAttachmentVisuals();

private:
	UPROPERTY()
	TMap<int32, TObjectPtr<UStaticMeshComponent>> AttachmentVisualComponents;
};
