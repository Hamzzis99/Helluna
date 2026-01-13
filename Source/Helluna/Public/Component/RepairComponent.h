// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "GameplayTagContainer.h"
#include "RepairComponent.generated.h"

class UInv_InventoryComponent;
class APlayerController;
class UParticleSystem;
class USoundBase;

/**
 * Repair 요청 구조체 (멀티플레이 큐 처리용)
 */
USTRUCT(BlueprintType)
struct FRepairRequest
{
	GENERATED_BODY()

	/** 요청한 플레이어 컨트롤러 */
	UPROPERTY()
	TObjectPtr<APlayerController> PlayerController;

	/** 재료 1 GameplayTag */
	UPROPERTY()
	FGameplayTag Material1Tag;

	/** 재료 1 사용 개수 */
	UPROPERTY()
	int32 Material1Amount = 0;

	/** 재료 2 GameplayTag */
	UPROPERTY()
	FGameplayTag Material2Tag;

	/** 재료 2 사용 개수 */
	UPROPERTY()
	int32 Material2Amount = 0;

	FRepairRequest()
		: PlayerController(nullptr)
		, Material1Amount(0)
		, Material2Amount(0)
	{}
};

/**
 * Repair 시스템 컴포넌트
 * - SpaceShip 등 수리 가능한 오브젝트에 붙여 사용
 * - Inventory의 Craftable 재료를 소비하여 자원 추가
 * - 멀티플레이 환경에서 동시 Repair 요청을 큐로 처리
 */
UCLASS(ClassGroup = (Custom), meta = (BlueprintSpawnableComponent), Blueprintable)
class HELLUNA_API URepairComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	URepairComponent();

protected:
	virtual void BeginPlay() override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

public:
	// ========================================
	// [서버 RPC] Widget에서 호출
	// ========================================

	/**
	 * Repair 요청 처리 (서버에서 실행)
	 * @param PlayerController - 요청한 플레이어
	 * @param Material1Tag - 재료 1 GameplayTag (예: GameItems.Craftables.FireFernFruit)
	 * @param Material1Amount - 재료 1 사용 개수
	 * @param Material2Tag - 재료 2 GameplayTag (None 가능)
	 * @param Material2Amount - 재료 2 사용 개수
	 */
	UFUNCTION(Server, Reliable, BlueprintCallable, Category = "Repair")
	void Server_ProcessRepairRequest(
		APlayerController* PlayerController,
		FGameplayTag Material1Tag,
		int32 Material1Amount,
		FGameplayTag Material2Tag,
		int32 Material2Amount
	);

	// ========================================
	// [멀티캐스트] 애니메이션 재생
	// ========================================

	/**
	 * 모든 클라이언트에서 Repair 애니메이션 재생
	 * @param RepairLocation - 이펙트 재생 위치
	 * @param TotalAmount - 투입된 총 재료 개수 (애니메이션 반복 횟수)
	 */
	UFUNCTION(NetMulticast, Reliable, Category = "Repair")
	void Multicast_PlayRepairAnimation(FVector RepairLocation, int32 TotalAmount);

	/**
	 * ⭐ 단일 이펙트/사운드 재생 (재료 개수와 무관하게 1번만 재생)
	 * @param RepairLocation - 이펙트 재생 위치
	 */
	UFUNCTION(NetMulticast, Reliable, Category = "Repair")
	void Multicast_PlaySingleRepairEffect(FVector RepairLocation);

	// ========================================
	// [Blueprint에서 설정 가능]
	// ========================================

	/** Repair 가능한 재료 목록 (GameplayTag 필터) */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Repair|Settings", meta = (Categories = "GameItems.Craftables"))
	TArray<FGameplayTag> AllowedMaterialTags;

	/** ⭐ 재료 1 표시 이름 (Blueprint에서 직접 설정) */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Repair|Material Names")
	FText Material1DisplayName = FText::FromString(TEXT("재료 1"));

	/** ⭐ 재료 2 표시 이름 (Blueprint에서 직접 설정) */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Repair|Material Names")
	FText Material2DisplayName = FText::FromString(TEXT("재료 2"));

	/** Repair 애니메이션 이펙트 (Niagara/Particle) */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Repair|VFX")
	TObjectPtr<UParticleSystem> RepairParticleEffect;

	/** Repair 사운드 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Repair|SFX")
	TObjectPtr<USoundBase> RepairSound;

	/** 재료 투입 간격 (초 단위, 애니메이션 딜레이) */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Repair|Animation", meta = (ClampMin = "0.1", ClampMax = "2.0"))
	float MaterialInsertDelay = 0.2f;

	/** 재료 1개당 Repair 자원 효율 (기본 1:1) */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Repair|Settings", meta = (ClampMin = "1", ClampMax = "100"))
	int32 MaterialToResourceRatio = 1;

	// ========================================
	// [Public Functions]
	// ========================================

	/**
	 * 재료가 Repair에 사용 가능한지 확인
	 * @param MaterialTag - 확인할 재료 GameplayTag
	 * @return true면 사용 가능
	 */
	UFUNCTION(BlueprintPure, Category = "Repair")
	bool IsMaterialAllowed(FGameplayTag MaterialTag) const;

	/**
	 * ⭐ 재료의 표시 이름 가져오기 (Blueprint에서 설정한 이름 반환)
	 * @param MaterialIndex - 1 = Material1, 2 = Material2
	 * @return 표시 이름
	 */
	UFUNCTION(BlueprintPure, Category = "Repair")
	FText GetMaterialDisplayName(int32 MaterialIndex) const;

	/**
	 * 현재 Repair 진행 중인지 확인
	 */
	UFUNCTION(BlueprintPure, Category = "Repair")
	bool IsProcessingRepair() const { return bProcessingRepair; }

	/**
	 * ⭐ 테스트용: 단순 재료 소비 (Server RPC)
	 * @param PlayerController - 플레이어
	 * @param MaterialTag - 소비할 재료 Tag
	 * @param Amount - 소비할 개수
	 */
	UFUNCTION(Server, Reliable, BlueprintCallable, Category = "Repair|Test")
	void Server_TestConsumeMaterial(APlayerController* PlayerController, FGameplayTag MaterialTag, int32 Amount);

	/**
	 * ⭐ 재료 소비 후 자원 추가 (클라이언트에서 서버 RPC 호출)
	 * @param TotalResource - 추가할 자원 개수
	 */
	UFUNCTION(Server, Reliable, BlueprintCallable, Category = "Repair")
	void Server_AddRepairResourceFromMaterials(int32 TotalResource);

private:
	// ========================================
	// [내부 함수]
	// ========================================

	/** 재료 유효성 검증 (서버) */
	bool ValidateMaterial(APlayerController* PlayerController, FGameplayTag MaterialTag, int32 Amount);

	/** 인벤토리에서 재료 소비 (서버) */
	void ConsumeMaterialFromInventory(APlayerController* PlayerController, FGameplayTag MaterialTag, int32 Amount);

	/** SpaceShip에 자원 추가 (서버) */
	void AddResourceToTarget(int32 TotalResource);

	/** 다음 Repair 요청 처리 (큐 시스템) */
	void ProcessNextRepairRequest();

	/** 애니메이션 재생 (타이머 기반) */
	void PlayMaterialInsertAnimationStep();

	/** 애니메이션 완료 후 호출 */
	void OnAnimationComplete();

	// ⭐ 모든 플레이어의 InventoryComponent에 델리게이트 바인딩
	void BindToAllPlayerInventories();

	// ⭐ InventoryComponent의 OnMaterialStacksChanged 델리게이트 콜백
	UFUNCTION()
	void OnMaterialConsumed(const FGameplayTag& MaterialTag);

	// ========================================
	// [멤버 변수]
	// ========================================

	/** Repair 요청 큐 (멀티플레이 동시 요청 대응) */
	UPROPERTY(Replicated)
	TArray<FRepairRequest> RepairQueue;

	/** 현재 Repair 처리 중 여부 */
	UPROPERTY(Replicated)
	bool bProcessingRepair = false;

	/** 애니메이션 타이머 핸들 */
	FTimerHandle AnimationTimerHandle;

	/** 현재 애니메이션 재생 횟수 */
	int32 CurrentAnimationCount = 0;

	/** 목표 애니메이션 재생 횟수 */
	int32 TargetAnimationCount = 0;

	/** 애니메이션 재생 위치 */
	FVector AnimationLocation = FVector::ZeroVector;

	/** 플레이어 체크 타이머 핸들 (새 플레이어 접속 감지용) */
	FTimerHandle PlayerCheckTimerHandle;

	/** 바인딩된 InventoryComponent 목록 (중복 바인딩 방지) */
	UPROPERTY()
	TSet<TObjectPtr<UInv_InventoryComponent>> BoundInventoryComponents;
};
