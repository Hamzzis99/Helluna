// Gihyeon's Inventory Project
// @author 김기현
//
// 건설 가능 액터 베이스 클래스
// 모든 건설물 데이터(이름, 아이콘, 재료, 프리뷰 등)를 통합.
// 기획자가 이 클래스를 상속한 BP 하나만 만들면 빌드 메뉴에 자동 반영.

#pragma once

#include "CoreMinimal.h"
#include "Building/Actor/Inv_BuildingActor.h"
#include "GameplayTagContainer.h"
#include "Inv_BuildableActor.generated.h"

class UStaticMesh;

/**
 * 건물 분류 카테고리
 * 빌드 메뉴의 지원 / 보조 / 건설 탭에 대응
 */
UENUM(BlueprintType)
enum class EBuildCategory : uint8
{
	Support       UMETA(DisplayName = "지원"),
	Auxiliary     UMETA(DisplayName = "보조"),
	Construction  UMETA(DisplayName = "건설"),
};

/**
 * 건설 가능 액터 베이스 클래스
 * Inv_BuildingActor를 확장하여 빌드 메뉴에 필요한 모든 정보를 포함.
 * BP에서 Class Defaults만 설정하면 빌드 메뉴에 자동 반영.
 */
UCLASS(Blueprintable, Abstract)
class INVENTORY_API AInv_BuildableActor : public AInv_BuildingActor
{
	GENERATED_BODY()

public:
	AInv_BuildableActor();

	// === 빌드 메뉴에서 읽을 정보 (CDO 접근) ===

	// 건설물 이름 (빌드 메뉴 카드 + 디테일 패널에 표시)
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "건설|정보",
		meta = (DisplayName = "건설물 이름"))
	FText BuildingDisplayName;

	// 건설물 설명 (디테일 패널에 표시)
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "건설|정보",
		meta = (DisplayName = "건설물 설명", MultiLine = "true"))
	FText BuildingDescription;

	// 빌드 메뉴 카드 아이콘
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "건설|정보",
		meta = (DisplayName = "카드 아이콘"))
	TObjectPtr<UTexture2D> BuildingIcon;

	// 분류 카테고리 (지원/보조/건설 탭 결정)
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "건설|정보",
		meta = (DisplayName = "건물 분류"))
	EBuildCategory BuildCategory = EBuildCategory::Construction;

	// 건물 고유 ID
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "건설|정보",
		meta = (DisplayName = "건물 ID"))
	int32 BuildingID = 0;

	// === 프리뷰 설정 ===

	// 3D 프리뷰에 사용할 메시 (None이면 BuildingMesh에서 자동 가져옴)
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "건설|프리뷰",
		meta = (DisplayName = "프리뷰 메시",
		Tooltip = "디테일 패널 3D 프리뷰에 표시할 메시. None이면 BuildingMesh 컴포넌트의 메시를 자동 사용."))
	TObjectPtr<UStaticMesh> PreviewMesh;

	// 프리뷰 회전 오프셋
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "건설|프리뷰",
		meta = (DisplayName = "프리뷰 회전 오프셋"))
	FRotator PreviewRotationOffset = FRotator::ZeroRotator;

	// 프리뷰 카메라 거리 (0이면 자동 계산)
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "건설|프리뷰",
		meta = (DisplayName = "프리뷰 카메라 거리", ClampMin = "0"))
	float PreviewCameraDistance = 0.f;

	// === 고스트 설정 ===

	// 고스트 액터 클래스 (None이면 이 액터 클래스 자체를 고스트로 사용)
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "건설|고스트",
		meta = (DisplayName = "고스트 액터 클래스",
		Tooltip = "건설 미리보기 고스트. None이면 실제 건물 클래스를 반투명으로 표시."))
	TSubclassOf<AActor> GhostActorClass;

	// 실제 건설 완료 시 스폰할 액터 클래스 (None이면 이 BuildableActor 자체를 스폰)
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "건설|건물",
		meta = (DisplayName = "실제 건물 액터 클래스",
		Tooltip = "건설 완료 시 월드에 스폰할 액터. None이면 이 BuildableActor 자체를 스폰."))
	TSubclassOf<AActor> ActualBuildingClass;

	// === 재료 정보 (최대 3개) ===

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "건설|재료",
		meta = (DisplayName = "필요 재료 1 태그", Categories = "GameItems.Craftables"))
	FGameplayTag RequiredMaterialTag1;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "건설|재료",
		meta = (DisplayName = "필요 재료 1 개수"))
	int32 RequiredAmount1 = 0;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "건설|재료",
		meta = (DisplayName = "재료 1 아이콘"))
	TObjectPtr<UTexture2D> MaterialIcon1;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "건설|재료",
		meta = (DisplayName = "필요 재료 2 태그", Categories = "GameItems.Craftables"))
	FGameplayTag RequiredMaterialTag2;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "건설|재료",
		meta = (DisplayName = "필요 재료 2 개수"))
	int32 RequiredAmount2 = 0;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "건설|재료",
		meta = (DisplayName = "재료 2 아이콘"))
	TObjectPtr<UTexture2D> MaterialIcon2;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "건설|재료",
		meta = (DisplayName = "필요 재료 3 태그", Categories = "GameItems.Craftables"))
	FGameplayTag RequiredMaterialTag3;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "건설|재료",
		meta = (DisplayName = "필요 재료 3 개수"))
	int32 RequiredAmount3 = 0;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "건설|재료",
		meta = (DisplayName = "재료 3 아이콘"))
	TObjectPtr<UTexture2D> MaterialIcon3;

	// === 유틸리티 ===

	// 실제 프리뷰 메시 반환 (PreviewMesh 우선, 없으면 BuildingMesh에서)
	UFUNCTION(BlueprintCallable, Category = "건설|프리뷰")
	UStaticMesh* GetEffectivePreviewMesh() const;
};
