// Gihyeon's Inventory Project

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "GameplayTagContainer.h"
#include "Inv_EquipActor.generated.h"

class UGameplayAbility; // TODO: [독립화] 졸작 후 삭제. GAS 의존 제거.
class USoundBase;
struct FInv_AttachableFragment;
struct FInv_AttachmentVisualInfo;

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

	// ★ [Phase 5 리플리케이션] 부착물 비주얼 배열 OnRep
	UFUNCTION()
	void OnRep_AttachmentVisuals();

private:

	// 지정된 소켓을 보유한 자식 컴포넌트를 탐색한다.
	// RootComponent(DefaultSceneRoot)에는 소켓이 없으므로,
	// 실제 소켓이 정의된 메시 컴포넌트를 찾아 반환한다.
	// 찾지 못하면 GetRootComponent() 폴백.
	USceneComponent* FindComponentWithSocket(FName SocketName) const;

	UPROPERTY(EditAnywhere, Category = "인벤토리",
		meta = (DisplayName = "장비 타입 태그", Tooltip = "이 장비의 GameplayTag 타입입니다. 장착/해제 시 식별에 사용됩니다."))
	FGameplayTag EquipmentType;

	// ============================================
	// ⭐ [WeaponBridge] 무기 스폰 GA
	// ⭐ 팀원이 만든 GA_Hero_SpawnWeapon 블루프린트 지정
	// ⭐ 1키 입력 시 이 GA를 활성화하여 무기 스폰
	// ⭐ 예: GA_Hero_SpawnWeapon (도끼), GA_Hero_SpawnWeapon2 (총) 등
	// ============================================
	// TODO: [독립화] 졸작 후 삭제. 이 값을 WeaponBridgeComponent의 WeaponGAMap으로 이전.
	// 삭제 전 반드시 BP에 설정된 GA 클래스 값을 기록해둘 것.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "인벤토리|무기",
		meta = (AllowPrivateAccess = "true", DisplayName = "무기 스폰 GA",
				Tooltip = "무기를 손에 꺼낼 때 활성화할 GameplayAbility 클래스입니다."))
	TSubclassOf<UGameplayAbility> SpawnWeaponAbility;

	// ============================================
	// ⭐ [WeaponBridge] 무기 슬롯 인덱스
	// ⭐ 0 = 주무기 슬롯, 1 = 보조무기 슬롯
	// ⭐ 장착 시 EquipmentComponent에서 설정
	// ============================================
	UPROPERTY(Replicated, VisibleAnywhere, Category = "인벤토리|무기",
		meta = (DisplayName = "무기 슬롯 인덱스", Tooltip = "0 = 주무기 슬롯, 1 = 보조무기 슬롯. 장착 시 EquipmentComponent에서 설정됩니다."))
	int32 WeaponSlotIndex = -1;

	// ============================================
	// ⭐ [WeaponBridge] 무기 숨김 상태 (리플리케이트)
	// ⭐ 손에 무기를 들면 true, 집어넣으면 false
	// ============================================
	UPROPERTY(ReplicatedUsing = OnRep_IsWeaponHidden, VisibleAnywhere, Category = "인벤토리|무기",
		meta = (DisplayName = "무기 숨김 상태", Tooltip = "무기가 손에서 숨겨졌는지 여부입니다. 집어넣기 시 true가 됩니다."))
	bool bIsWeaponHidden = false;

	// ============================================
	// ⭐ [WeaponBridge] 등 장착 소켓 (블루프린트에서 설정)
	// ⭐ 주무기(SlotIndex=0)일 때 사용할 소켓
	// ============================================
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "인벤토리|무기|소켓",
		meta = (AllowPrivateAccess = "true", DisplayName = "주무기 등 소켓",
				Tooltip = "주무기(SlotIndex=0)일 때 등에 부착할 소켓 이름입니다."))
	FName PrimaryBackSocket = TEXT("WeaponSocket_Primary");

	// ============================================
	// ⭐ [WeaponBridge] 등 장착 소켓 (블루프린트에서 설정)
	// ⭐ 보조무기(SlotIndex=1)일 때 사용할 소켓
	// ============================================
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "인벤토리|무기|소켓",
		meta = (AllowPrivateAccess = "true", DisplayName = "보조무기 등 소켓",
				Tooltip = "보조무기(SlotIndex=1)일 때 등에 부착할 소켓 이름입니다."))
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
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "인벤토리|부착물|효과",
		meta = (AllowPrivateAccess = "true", DisplayName = "기본 발사 사운드",
				Tooltip = "소음기가 장착되지 않았을 때 사용하는 기본 발사 사운드입니다."))
	TObjectPtr<USoundBase> DefaultFireSound = nullptr;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "인벤토리|부착물|효과",
		meta = (AllowPrivateAccess = "true", DisplayName = "소음기 발사 사운드",
				Tooltip = "소음기가 장착되었을 때 사용하는 발사 사운드입니다."))
	TObjectPtr<USoundBase> SuppressedFireSound = nullptr;

	UPROPERTY(ReplicatedUsing = OnRep_bSuppressed, VisibleAnywhere, Category = "인벤토리|부착물|효과",
		meta = (DisplayName = "소음기 장착됨", Tooltip = "소음기가 현재 장착되어 있는지 여부입니다."))
	bool bSuppressed = false;

	// -- 스코프 --
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "인벤토리|부착물|효과",
		meta = (AllowPrivateAccess = "true", DisplayName = "기본 줌 FOV",
				Tooltip = "스코프가 장착되지 않았을 때 사용하는 기본 줌 시야각입니다.",
				ClampMin = 10.0, ClampMax = 120.0))
	float DefaultZoomFOV = 90.f;

	UPROPERTY(Replicated, VisibleAnywhere, Category = "인벤토리|부착물|효과",
		meta = (DisplayName = "오버라이드 줌 FOV", Tooltip = "스코프 부착물에 의해 오버라이드된 줌 FOV 값입니다. 0이면 기본값 사용."))
	float OverrideZoomFOV = 0.f;

	// -- 레이저 --
	UPROPERTY(ReplicatedUsing = OnRep_bLaserActive, VisibleAnywhere, Category = "인벤토리|부착물|효과",
		meta = (DisplayName = "레이저 활성화", Tooltip = "레이저 사이트 부착물이 현재 활성화되어 있는지 여부입니다."))
	bool bLaserActive = false;

	// 레이저 비주얼 컴포넌트. 무기 BP에서 직접 추가하고 이 변수에 바인딩한다.
	// nullptr이어도 안전하다 (IsValid 체크).
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "인벤토리|부착물|효과",
		meta = (AllowPrivateAccess = "true", DisplayName = "레이저 컴포넌트 (BP에서 설정)",
				Tooltip = "레이저 비주얼 메시 컴포넌트입니다. 무기 BP에서 직접 추가하고 이 변수에 바인딩합니다."))
	TObjectPtr<UStaticMeshComponent> LaserBeamComponent = nullptr;

	// ════════════════════════════════════════════════════════════════
	// 📌 [Phase 5] 부착물 메시 관리
	// ════════════════════════════════════════════════════════════════
	// 슬롯 인덱스 → 스폰된 StaticMeshComponent 매핑
	UPROPERTY()
	TMap<int32, TObjectPtr<UStaticMeshComponent>> AttachmentMeshComponents;

	// ════════════════════════════════════════════════════════════════
	// ★ [Phase 5 리플리케이션] 부착물 비주얼 데이터 (서버→클라이언트)
	// 서버에서 AttachMeshToSocket 호출 시 이 배열도 갱신됨.
	// 클라이언트는 OnRep_AttachmentVisuals에서 메시를 로컬 생성.
	// ════════════════════════════════════════════════════════════════
	UPROPERTY(ReplicatedUsing = OnRep_AttachmentVisuals)
	TArray<FInv_AttachmentVisualInfo> ReplicatedAttachmentVisuals;

public:
	// ════════════════════════════════════════════════════════════════
	// [Phase 7] 효과 Getter — 발사 GA / 카메라 시스템에서 호출
	// ════════════════════════════════════════════════════════════════

	// 현재 사용할 발사 사운드 반환 (소음기 장착 여부에 따라 분기)
	UFUNCTION(BlueprintCallable, Category = "인벤토리|부착물", meta = (DisplayName = "발사 사운드 가져오기"))
	USoundBase* GetFireSound() const;

	// 현재 사용할 줌 FOV 반환 (스코프 장착 여부에 따라 분기)
	UFUNCTION(BlueprintCallable, Category = "인벤토리|부착물", meta = (DisplayName = "줌 FOV 가져오기"))
	float GetZoomFOV() const;

	UFUNCTION(BlueprintCallable, Category = "인벤토리|부착물", meta = (DisplayName = "소음기 장착 여부"))
	bool IsSuppressed() const { return bSuppressed; }

	UFUNCTION(BlueprintCallable, Category = "인벤토리|부착물", meta = (DisplayName = "레이저 활성화 여부"))
	bool IsLaserActive() const { return bLaserActive; }

	// ════════════════════════════════════════════════════════════════
	// 📌 부착물 시각 정보 Getter (게임 모듈에서 사용)
	// ════════════════════════════════════════════════════════════════
	// 현재 이 EquipActor에 부착된 모든 부착물의 메시/소켓/오프셋 정보를 반환.
	// 게임 모듈(WeaponBridgeComponent 등)에서 다른 액터에 동일한 부착물 시각을
	// 복제할 때 사용한다. 인벤토리 플러그인은 "어디에 쓰이는지" 알 필요 없음.
	UFUNCTION(BlueprintCallable, Category = "인벤토리|부착물",
		meta = (DisplayName = "부착물 시각 정보 가져오기"))
	TArray<FInv_AttachmentVisualInfo> GetAttachmentVisualInfos() const;

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