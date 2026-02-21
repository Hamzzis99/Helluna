// Gihyeon's Inventory Project

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "GameplayTagContainer.h"
#include "Inv_BuildingComponent.generated.h"

class UInputMappingContext;
class UInputAction;

UCLASS(ClassGroup=(Custom), meta=(BlueprintSpawnableComponent), Blueprintable)
class INVENTORY_API UInv_BuildingComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	// Sets default values for this component's properties
	UInv_BuildingComponent();

protected:
	// Called when the game starts
	virtual void BeginPlay() override;

public:
	// Called every frame
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

public:
	// 블루프린트에서 호출 가능: 위젯에서 건물 선택 시
	UFUNCTION(BlueprintCallable, Category = "건설", meta = (DisplayName = "위젯에서 건물 선택됨"))
	void OnBuildingSelectedFromWidget(
		TSubclassOf<AActor> GhostClass,
		TSubclassOf<AActor> ActualBuildingClass,
		int32 BuildingID,
		FGameplayTag MaterialTag1,
		int32 MaterialAmount1,
		FGameplayTag MaterialTag2,
		int32 MaterialAmount2
	);

	// 빌드 메뉴 닫기 (외부에서 호출 가능 - 인벤토리 닫을 때 사용)
	void CloseBuildMenu();

private:
	// 빌드 모드 시작/종료 함수
	void StartBuildMode();
	void EndBuildMode();
	void CancelBuildMode(); // 우클릭으로 빌드 모드 취소
	
	// 빌드 메뉴 토글 함수
	void ToggleBuildMenu();
	void OpenBuildMenu();
	
	// Crafting Menu 닫기 (B키 눌렀을 때)
	void CloseCraftingMenuIfOpen();
	
	// 설치 액션 함수
	void TryPlaceBuilding();

	// 재료 체크 및 차감 함수
	bool HasRequiredMaterials(const FGameplayTag& MaterialTag, int32 RequiredAmount) const;
	void ConsumeMaterials(const FGameplayTag& MaterialTag, int32 Amount);

	// 서버 RPC: 건물 배치 요청
	UFUNCTION(Server, Reliable)
	void Server_PlaceBuilding(
		TSubclassOf<AActor> BuildingClass, 
		FVector Location, 
		FRotator Rotation, 
		FGameplayTag MaterialTag1, 
		int32 MaterialAmount1,
		FGameplayTag MaterialTag2,
		int32 MaterialAmount2
	);

	// 멀티캐스트 RPC: 모든 클라이언트에게 건물 배치 알림
	UFUNCTION(NetMulticast, Reliable)
	void Multicast_OnBuildingPlaced(AActor* PlacedBuilding);

	// PlayerController 약한 참조
	TWeakObjectPtr<APlayerController> OwningPC;

	// === 빌드 메뉴 위젯 관련 ===
	
	// 빌드 메뉴 위젯 클래스 (블루프린트에서 WBP_BuildMenu 설정)
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "건설|UI",
		meta = (DisplayName = "건설 메뉴 위젯 클래스", Tooltip = "건설 메뉴로 사용할 위젯 블루프린트 클래스. WBP_BuildMenu를 설정하세요.", AllowPrivateAccess = "true"))
	TSubclassOf<UUserWidget> BuildMenuWidgetClass;

	// 생성된 빌드 메뉴 위젯 인스턴스
	UPROPERTY()
	TObjectPtr<UUserWidget> BuildMenuInstance;

	// === 현재 선택된 건물 정보 ===
	
	// 현재 선택된 고스트 액터 클래스
	UPROPERTY()
	TSubclassOf<AActor> SelectedGhostClass;

	// 현재 선택된 실제 건물 액터 클래스
	UPROPERTY()
	TSubclassOf<AActor> SelectedBuildingClass;

	// 현재 선택된 건물 ID
	UPROPERTY()
	int32 CurrentBuildingID = -1;

	// 현재 선택된 건물의 재료 정보
	UPROPERTY()
	FGameplayTag CurrentMaterialTag;

	UPROPERTY()
	int32 CurrentMaterialAmount = 0;

	// 두 번째 재료 정보
	UPROPERTY()
	FGameplayTag CurrentMaterialTag2;

	UPROPERTY()
	int32 CurrentMaterialAmount2 = 0;

	// === Input Mapping Context ===

	// 빌드 메뉴 토글용 IMC (항상 활성화 - B키)
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "건설|입력",
		meta = (DisplayName = "건설 메뉴 입력 매핑", Tooltip = "건설 메뉴 토글용 입력 매핑 컨텍스트. B키로 항상 활성화됩니다.", AllowPrivateAccess = "true"))
	TObjectPtr<UInputMappingContext> BuildingMenuMappingContext;

	// 빌드 액션용 IMC (BuildMode일 때만 활성화 - 좌클릭/우클릭)
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "건설|입력",
		meta = (DisplayName = "건설 액션 입력 매핑", Tooltip = "건설 모드일 때만 활성화되는 입력 매핑 컨텍스트. 좌클릭/우클릭 액션을 포함합니다.", AllowPrivateAccess = "true"))
	TObjectPtr<UInputMappingContext> BuildingActionMappingContext;

	// 빌드 모드 토글 액션 (빌드 메뉴 열기/닫기 - B키)
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "건설|입력",
		meta = (DisplayName = "건설 메뉴 토글 액션", Tooltip = "건설 메뉴를 열고 닫는 입력 액션. 기본 B키에 바인딩됩니다.", AllowPrivateAccess = "true"))
	TObjectPtr<UInputAction> IA_Building;

	// 설치 액션 (실제 배치)
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "건설|입력",
		meta = (DisplayName = "건물 배치 액션", Tooltip = "건물을 실제로 배치하는 입력 액션. 좌클릭에 바인딩됩니다.", AllowPrivateAccess = "true"))
	TObjectPtr<UInputAction> IA_BuildingAction;

	// 빌드 모드 취소 액션 (우클릭)
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "건설|입력",
		meta = (DisplayName = "건설 취소 액션", Tooltip = "건설 모드를 취소하는 입력 액션. 우클릭에 바인딩됩니다.", AllowPrivateAccess = "true"))
	TObjectPtr<UInputAction> IA_CancelBuilding;

	// === 고스트 액터 ===

	// 실제 스폰된 고스트 액터 인스턴스
	UPROPERTY()
	TObjectPtr<AActor> GhostActorInstance;

	// 빌드 모드 상태
	UPROPERTY(BlueprintReadOnly, Category = "건설|상태", meta = (DisplayName = "건설 모드 여부", AllowPrivateAccess = "true"))
	bool bIsInBuildMode = false;

	// 건물 설치 가능 여부 (바닥에 닿았는지, 각도가 적절한지)
	UPROPERTY(BlueprintReadOnly, Category = "건설|상태", meta = (DisplayName = "건물 배치 가능 여부", AllowPrivateAccess = "true"))
	bool bCanPlaceBuilding = false;

	// 최대 배치 거리 (플레이어로부터)
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "건설|설정",
		meta = (DisplayName = "최대 배치 거리", Tooltip = "플레이어로부터 건물을 배치할 수 있는 최대 거리(cm)입니다.", AllowPrivateAccess = "true"))
	float MaxPlacementDistance = 1000.0f;

	// 최대 허용 바닥 각도 (이보다 가파르면 설치 불가)
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "건설|설정",
		meta = (DisplayName = "최대 바닥 각도", Tooltip = "건물 설치를 허용하는 최대 지면 경사각(도)입니다. 이보다 가파르면 설치할 수 없습니다.", AllowPrivateAccess = "true"))
	float MaxGroundAngle = 45.0f;

	// === 동적 입력 바인딩 관리 ===
	
	// 빌드 모드 입력 활성화/비활성화
	void EnableBuildModeInput();
	void DisableBuildModeInput();

	// 동적 바인딩 핸들 (제거용)
	uint32 BuildActionBindingHandle = 0;
	uint32 CancelBuildingBindingHandle = 0;

	// 빌드 모드 IMC 우선순위 (Combat IMC보다 높게 설정)
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "건설|입력",
		meta = (DisplayName = "입력 매핑 우선순위", Tooltip = "건설 모드 입력 매핑 컨텍스트의 우선순위. 전투 IMC보다 높게 설정하세요.", AllowPrivateAccess = "true"))
	int32 BuildingMappingContextPriority = 10;
};
