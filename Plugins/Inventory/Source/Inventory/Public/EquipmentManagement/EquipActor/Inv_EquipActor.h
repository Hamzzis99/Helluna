// Gihyeon's Inventory Project

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "GameplayTagContainer.h"
#include "Inv_EquipActor.generated.h"

class UGameplayAbility; // TODO: [독립화] 졸작 후 삭제. GAS 의존 제거.
class USoundBase;
struct FInv_AttachableFragment;

UCLASS()
class INVENTORY_API AInv_EquipActor : public AActor
{
	GENERATED_BODY()

public:
	AInv_EquipActor();
	
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
	
	FGameplayTag GetEquipmentType() const { return EquipmentType; }
	void SetEquipmentType(FGameplayTag Type) { EquipmentType = Type; }

	// ============================================
	// ⭐ [WeaponBridge] 무기 스폰 GA 클래스 Getter
	// ⭐ 팀원의 GA_SpawnWeapon을 직접 호출하기 위함
	// ============================================
	// TODO: [독립화] 졸작 후 삭제. GA 매핑은 게임 모듈(WeaponBridgeComponent)로 이전.
	// WeaponGAMap: TMap<FGameplayTag, TSubclassOf<UGameplayAbility>>으로 게임에서 관리.
	TSubclassOf<UGameplayAbility> GetSpawnWeaponAbility() const { return SpawnWeaponAbility; }

	// ============================================
	// ⭐ [WeaponBridge] 무기 슬롯 인덱스 (0=주무기, 1=보조무기)
	// ============================================
	int32 GetWeaponSlotIndex() const { return WeaponSlotIndex; }
	void SetWeaponSlotIndex(int32 Index) { WeaponSlotIndex = Index; }

	// ============================================
	// ⭐ [WeaponBridge] 등 소켓 이름 Getter
	// ⭐ WeaponSlotIndex에 따라 적절한 소켓 반환
	// ============================================
	FName GetBackSocketName() const
	{
		return (WeaponSlotIndex == 1) ? SecondaryBackSocket : PrimaryBackSocket;
	}

	// ============================================
	// ⭐ [WeaponBridge] 무기 숨김/표시 (서버 RPC + 리플리케이트)
	// ⭐ 클라이언트에서 호출 → 서버로 RPC → 리플리케이트
	// ============================================
	void SetWeaponHidden(bool bNewHidden);
	bool IsWeaponHidden() const { return bIsWeaponHidden; }

protected:
	// ⭐ [WeaponBridge] Hidden 상태 변경 시 호출 (리플리케이션)
	UFUNCTION()
	void OnRep_IsWeaponHidden();
	
	// ⭐ [WeaponBridge] 서버 RPC - 클라이언트→서버
	UFUNCTION(Server, Reliable)
	void Server_SetWeaponHidden(bool bNewHidden);

	// [Phase 7] 리플리케이션 콜백
	UFUNCTION()
	void OnRep_bSuppressed();

	UFUNCTION()
	void OnRep_bLaserActive();

private:

	UPROPERTY(EditAnywhere, Category = "Inventory", meta = (DisplayName = "장비 타입 태그"))
	FGameplayTag EquipmentType;

	// ============================================
	// ⭐ [WeaponBridge] 무기 스폰 GA
	// ⭐ 팀원이 만든 GA_Hero_SpawnWeapon 블루프린트 지정
	// ⭐ 1키 입력 시 이 GA를 활성화하여 무기 스폰
	// ⭐ 예: GA_Hero_SpawnWeapon (도끼), GA_Hero_SpawnWeapon2 (총) 등
	// ============================================
	// TODO: [독립화] 졸작 후 삭제. 이 값을 WeaponBridgeComponent의 WeaponGAMap으로 이전.
	// 삭제 전 반드시 BP에 설정된 GA 클래스 값을 기록해둘 것.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Inventory|Weapon", meta = (AllowPrivateAccess = "true", DisplayName = "무기 스폰 GA"))
	TSubclassOf<UGameplayAbility> SpawnWeaponAbility;

	// ============================================
	// ⭐ [WeaponBridge] 무기 슬롯 인덱스
	// ⭐ 0 = 주무기 슬롯, 1 = 보조무기 슬롯
	// ⭐ 장착 시 EquipmentComponent에서 설정
	// ============================================
	UPROPERTY(Replicated, VisibleAnywhere, Category = "Inventory|Weapon", meta = (DisplayName = "무기 슬롯 인덱스"))
	int32 WeaponSlotIndex = -1;

	// ============================================
	// ⭐ [WeaponBridge] 무기 숨김 상태 (리플리케이트)
	// ⭐ 손에 무기를 들면 true, 집어넣으면 false
	// ============================================
	UPROPERTY(ReplicatedUsing = OnRep_IsWeaponHidden, VisibleAnywhere, Category = "Inventory|Weapon")
	bool bIsWeaponHidden = false;

	// ============================================
	// ⭐ [WeaponBridge] 등 장착 소켓 (블루프린트에서 설정)
	// ⭐ 주무기(SlotIndex=0)일 때 사용할 소켓
	// ============================================
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Inventory|Weapon|Socket", meta = (AllowPrivateAccess = "true", DisplayName = "주무기 등 소켓"))
	FName PrimaryBackSocket = TEXT("WeaponSocket_Primary");

	// ============================================
	// ⭐ [WeaponBridge] 등 장착 소켓 (블루프린트에서 설정)
	// ⭐ 보조무기(SlotIndex=1)일 때 사용할 소켓
	// ============================================
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Inventory|Weapon|Socket", meta = (AllowPrivateAccess = "true", DisplayName = "보조무기 등 소켓"))
	FName SecondaryBackSocket = TEXT("WeaponSocket_Secondary");

	// ════════════════════════════════════════════════════════════════
	// TODO: [독립화] 졸작 후 여기에 HandSocket 프로퍼티 추가
	//
	// UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Inventory|Weapon|Socket",
	//     meta = (AllowPrivateAccess = "true", DisplayName = "손 소켓"))
	// FName HandSocket = TEXT("weapon_r");
	//
	// + public에 Getter/함수 추가:
	//   FName GetHandSocket() const { return HandSocket; }
	//   void AttachToHand(USkeletalMeshComponent* AttachMesh);
	//   void AttachToBack(USkeletalMeshComponent* AttachMesh);
	//
	// AttachToHand: DetachFromActor → AttachToComponent(HandSocket, Snap) → SetWeaponHidden(false)
	// AttachToBack: DetachFromActor → AttachToComponent(GetBackSocketName(), Snap) → SetWeaponHidden(false)
	// ════════════════════════════════════════════════════════════════

	// ════════════════════════════════════════════════════════════════
	// [Phase 7] 부착물 효과 오버라이드 시스템
	// ════════════════════════════════════════════════════════════════
	// 부착물이 EquipActor의 상태를 변경하고,
	// 발사 GA/카메라 시스템은 EquipActor의 getter로 현재 값을 읽는다.
	// GA 수정 없이 부착물 효과를 추가할 수 있다.
	//
	// 사용법 (팀원 GA 측):
	//   발사 시: USoundBase* Sound = EquipActor->GetFireSound();
	//   조준 시: float FOV = EquipActor->GetZoomFOV();
	//   레이저: 장착 시 자동으로 Visibility 변경.
	// ════════════════════════════════════════════════════════════════

	// -- 소음기 --
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Inventory|Attachment|Effects",
		meta = (AllowPrivateAccess = "true", DisplayName = "기본 발사 사운드"))
	TObjectPtr<USoundBase> DefaultFireSound = nullptr;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Inventory|Attachment|Effects",
		meta = (AllowPrivateAccess = "true", DisplayName = "소음기 발사 사운드"))
	TObjectPtr<USoundBase> SuppressedFireSound = nullptr;

	UPROPERTY(ReplicatedUsing = OnRep_bSuppressed, VisibleAnywhere, Category = "Inventory|Attachment|Effects")
	bool bSuppressed = false;

	// -- 스코프 --
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Inventory|Attachment|Effects",
		meta = (AllowPrivateAccess = "true", DisplayName = "기본 줌 FOV",
				ClampMin = 10.0, ClampMax = 120.0))
	float DefaultZoomFOV = 90.f;

	UPROPERTY(Replicated, VisibleAnywhere, Category = "Inventory|Attachment|Effects")
	float OverrideZoomFOV = 0.f;

	// -- 레이저 --
	UPROPERTY(ReplicatedUsing = OnRep_bLaserActive, VisibleAnywhere, Category = "Inventory|Attachment|Effects")
	bool bLaserActive = false;

	// 레이저 비주얼 컴포넌트. 무기 BP에서 직접 추가하고 이 변수에 바인딩한다.
	// nullptr이어도 안전하다 (IsValid 체크).
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Inventory|Attachment|Effects",
		meta = (AllowPrivateAccess = "true", DisplayName = "레이저 컴포넌트 (BP에서 설정)"))
	TObjectPtr<UStaticMeshComponent> LaserBeamComponent = nullptr;

	// ════════════════════════════════════════════════════════════════
	// 📌 [Phase 5] 부착물 메시 관리
	// ════════════════════════════════════════════════════════════════
	// 슬롯 인덱스 → 스폰된 StaticMeshComponent 매핑
	UPROPERTY()
	TMap<int32, TObjectPtr<UStaticMeshComponent>> AttachmentMeshComponents;

public:
	// ════════════════════════════════════════════════════════════════
	// [Phase 7] 효과 Getter — 발사 GA / 카메라 시스템에서 호출
	// ════════════════════════════════════════════════════════════════

	// 현재 사용할 발사 사운드 반환 (소음기 장착 여부에 따라 분기)
	UFUNCTION(BlueprintCallable, Category = "Inventory|Attachment")
	USoundBase* GetFireSound() const;

	// 현재 사용할 줌 FOV 반환 (스코프 장착 여부에 따라 분기)
	UFUNCTION(BlueprintCallable, Category = "Inventory|Attachment")
	float GetZoomFOV() const;

	UFUNCTION(BlueprintCallable, Category = "Inventory|Attachment")
	bool IsSuppressed() const { return bSuppressed; }

	UFUNCTION(BlueprintCallable, Category = "Inventory|Attachment")
	bool IsLaserActive() const { return bLaserActive; }

	// ════════════════════════════════════════════════════════════════
	// [Phase 7] 효과 Setter — 부착물 장착/분리 시 호출
	// ════════════════════════════════════════════════════════════════

	void SetSuppressed(bool bNewSuppressed);
	void SetZoomFOVOverride(float NewFOV);
	void ClearZoomFOVOverride();
	void SetLaserActive(bool bNewActive);

	// ════════════════════════════════════════════════════════════════
	// [Phase 7] 부착물 효과 일괄 적용/해제
	// ════════════════════════════════════════════════════════════════
	// AttachableFragment의 플래그를 읽어서 EquipActor 상태를 변경한다.
	void ApplyAttachmentEffects(const FInv_AttachableFragment* AttachableFrag);
	void RemoveAttachmentEffects(const FInv_AttachableFragment* AttachableFrag);

	// ════════════════════════════════════════════════════════════════
	// 📌 [Phase 5] 부착물 메시 컴포넌트 스폰 및 소켓에 부착
	// ════════════════════════════════════════════════════════════════
	// @param SlotIndex  - 슬롯 인덱스 (AttachmentHostFragment의 슬롯 번호)
	// @param Mesh       - 부착할 스태틱 메시
	// @param SocketName - 부착할 소켓 이름 (SlotDef.AttachSocket)
	// @param Offset     - 소켓 기준 오프셋 (AttachableFragment.AttachOffset)
	void AttachMeshToSocket(int32 SlotIndex, UStaticMesh* Mesh, FName SocketName, const FTransform& Offset);

	// 슬롯의 부착물 메시 제거
	void DetachMeshFromSocket(int32 SlotIndex);

	// 모든 부착물 메시 제거 (무기 해제 시)
	void DetachAllMeshes();
};