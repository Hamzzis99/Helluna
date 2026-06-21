// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Object/ResourceUsingObject/HellunaBaseResourceUsingObject.h"
#include "Interfaces/Inv_Interface_Primary.h"  // [ShipHeal] 인벤토리 E(PrimaryInteract) 상호작용
#include "ResourceUsingObject_SpaceShip.generated.h"

class UWidgetComponent;
class USpaceShipAttackSlotManager;
class UNavigationInvokerComponent;
class UNavModifierComponent;
class UNavArea;
class UHellunaHealthComponent;

/**
 * 
 */

DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnRepairProgressChanged, int32, Current, int32, Need);

// ⭐ 새로 추가: 수리 완료 델리게이트
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnSpaceShipRepairCompleted);

// [ShipHP] 우주선 파괴(HP 0) 델리게이트 — BP 폭발/사운드 연출 바인딩용 (KillerActor 전달)
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnSpaceShipDestroyed, AActor*, KillerActor);


UCLASS()
class HELLUNA_API AResourceUsingObject_SpaceShip : public AHellunaBaseResourceUsingObject, public IInv_Interface_Primary
{
	GENERATED_BODY()

public:
	// [ShipHeal] 인벤토리 E(PrimaryInteract) 상호작용 — 우주선을 바라보고 E 누르면 회복 메뉴.
	//   Inv_PlayerController::Server_Interact 경유로 서버에서 호출됨 → 해당 클라에 회복 메뉴 열기 요청.
	virtual bool ExecuteInteract_Implementation(APlayerController* Controller) override;
	
protected:
	virtual void CollisionBoxBeginOverlap(UPrimitiveComponent* OverlappedComponent, AActor* OtherActor, UPrimitiveComponent* OtherComp, int32 OtherBodyIndex, bool bFromSweep, const FHitResult& SweepResult) override;

	virtual void CollisionBoxEndOverlap(UPrimitiveComponent* OverlappedComponent, AActor* OtherActor, UPrimitiveComponent* OtherComp, int32 OtherBodyIndex) override;

    virtual void BeginPlay() override;

    /** [ShipFriendlyFire] 우주선은 아군(히어로) 데미지를 받지 않는다. 적(Enemy) 데미지만 Super로 통과.
     *  플레이어 총격이 OnTakeAnyDamage(HP) + OnTakePointDamage(변형) 양쪽으로 흘러드는 걸 진입부에서 차단. */
    virtual float TakeDamage(float DamageAmount, const FDamageEvent& DamageEvent, AController* EventInstigator, AActor* DamageCauser) override;

    AResourceUsingObject_SpaceShip();

public:
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Repair")
    int32 NeedResource = 5;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, ReplicatedUsing = OnRep_CurrentResource, Category = "Repair")
    int32 CurrentResource = 0;

    UPROPERTY(BlueprintAssignable, Category = "Repair")
    FOnRepairProgressChanged OnRepairProgressChanged;

    // ⭐ 새로 추가: 수리 완료 델리게이트
    UPROPERTY(BlueprintAssignable, Category = "Repair")
    FOnSpaceShipRepairCompleted OnRepairCompleted_Delegate;

    /**
     * 수리 자원 추가
     * @param Amount - 추가할 자원 양
     * @return 실제로 추가된 자원 양 (NeedResource 초과분은 추가되지 않음)
     */
    UFUNCTION(BlueprintCallable, Category = "Repair")
    int32 AddRepairResource(int32 Amount);
        
    UFUNCTION(BlueprintPure, Category = "Repair")  //UI���� �������� �ۼ�Ʈ�� ��ȯ
    float GetRepairPercent() const;


    UFUNCTION(BlueprintPure, Category = "Repair")  
    bool IsRepaired() const;

    UFUNCTION()
    void OnRep_CurrentResource();

public:
    UFUNCTION(BlueprintPure, Category = "Repair")
    int32 GetCurrentResource() const { return CurrentResource; }

    UFUNCTION(BlueprintPure, Category = "Repair")
    int32 GetNeedResource() const { return NeedResource; }

    // ⭐ 새로 추가: 수리 완료 이벤트 (Blueprint에서도 오버라이드 가능)
    UFUNCTION(BlueprintNativeEvent, Category = "Repair")
    void OnRepairCompleted();

	// =========================================================
	// ★ [ShipHP] 우주선 체력 (전투 내구도) — 수리 진행도(CurrentResource)와 별개
	//   적 공격이 UGameplayStatics::ApplyPointDamage → OnTakeAnyDamage 로 흘러들어와
	//   ShipHealthComponent 가 HP 를 깎는다. HP 0 → OnDeath → HandleShipDestroyed →
	//   GameMode 패배(EndGame). 캐릭터/적과 동일한 UHellunaHealthComponent 를 재사용.
	// =========================================================

	/** 우주선 체력 컴포넌트 (캐릭터/적과 동일 컴포넌트 재사용, 자동 복제) */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ShipHP",
		meta = (DisplayName = "Ship Health Component (우주선 체력)"))
	TObjectPtr<UHellunaHealthComponent> ShipHealthComponent;

	/** 우주선 최대 체력 (디자이너 튜닝). BeginPlay 서버에서 컴포넌트에 적용. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ShipHP",
		meta = (DisplayName = "우주선 최대 체력", ClampMin = "1.0"))
	float ShipMaxHealth = 1000.f;

	/** 우주선 파괴(HP 0) 시 브로드캐스트 — BP 폭발/사운드 연출 바인딩용 */
	UPROPERTY(BlueprintAssignable, Category = "ShipHP")
	FOnSpaceShipDestroyed OnSpaceShipDestroyed;

	UFUNCTION(BlueprintPure, Category = "ShipHP")
	UHellunaHealthComponent* GetShipHealthComponent() const { return ShipHealthComponent; }

	/** 현재 우주선 HP */
	UFUNCTION(BlueprintPure, Category = "ShipHP")
	float GetShipHealth() const;

	/** 우주선 최대 HP */
	UFUNCTION(BlueprintPure, Category = "ShipHP")
	float GetShipMaxHealth() const;

	/** 우주선 HP 비율 (0~1) — HP 바 UI 용 */
	UFUNCTION(BlueprintPure, Category = "ShipHP")
	float GetShipHealthPercent() const;

	/** 우주선이 파괴(사망)됐는지 */
	UFUNCTION(BlueprintPure, Category = "ShipHP")
	bool IsShipDestroyed() const;

	// =========================================================
	// ★ [ShipHeal] E 회복 메뉴 — 재료 비례 HP 회복 (수리와 별개)
	// =========================================================

	/** 재료 1개당 회복되는 우주선 HP. 디자이너 튜닝(기본 100). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ShipHP",
		meta = (DisplayName = "재료당 회복 HP", ClampMin = "1.0"))
	float HealPerMaterial = 100.f;

	UFUNCTION(BlueprintPure, Category = "ShipHP")
	float GetHealPerMaterial() const { return HealPerMaterial; }

	/** [E회복] 우주선 본체 표면에서 이 거리(cm) 이내면 회복 메뉴 허용 (박스 오버랩 폴백, 거대 메시 대응). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ShipHP",
		meta = (DisplayName = "E 회복 근접 거리(cm)", ClampMin = "50.0"))
	float ShipHealInteractRange = 800.f;

	/** [E회복] 주어진 액터가 우주선 상호작용 범위(박스 오버랩 OR 본체 표면 근접) 안에 있는지 (E 회복 메뉴 근접 판정용) */
	UFUNCTION(BlueprintPure, Category = "ShipHP")
	bool IsActorInInteractRange(const AActor* Other) const;

	// =========================================================
	// ★ 공격 슬롯 매니저
	// =========================================================
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Combat",
		meta = (DisplayName = "Attack Slot Manager (공격 슬롯 매니저)"))
	TObjectPtr<USpaceShipAttackSlotManager> AttackSlotManager;

	// =========================================================
	// ★ Navigation Invoker (World Partition NavMesh 스트리밍 보장)
	// =========================================================
	/** 우주선 주변 NavMesh 데이터를 강제로 스트리밍 로드 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "AI|Navigation",
		meta = (DisplayName = "Navigation Invoker (NavMesh 스트리밍)"))
	TObjectPtr<UNavigationInvokerComponent> NavigationInvoker;

	// =========================================================
	// ★ NavModifier (우주선 주변 Nav 영역 설정)
	// =========================================================
	/** 에디터에서 NavModifier 사용 여부 토글 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "AI|Navigation",
		meta = (DisplayName = "Nav Modifier 사용"))
	bool bUseNavModifier = false;

	/** 우주선 주변 Nav 영역을 수정하는 컴포넌트 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "AI|Navigation",
		meta = (DisplayName = "Nav Modifier Component", EditCondition = "bUseNavModifier"))
	TObjectPtr<UNavModifierComponent> NavModifierComp;

	/** NavModifier에 적용할 NavArea 클래스 (예: NavArea_Obstacle, NavArea_Null 등) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "AI|Navigation",
		meta = (DisplayName = "Nav Area Class", EditCondition = "bUseNavModifier"))
	TSubclassOf<UNavArea> NavModifierAreaClass;

	/** NavModifier 박스 크기 (Half Extent) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "AI|Navigation",
		meta = (DisplayName = "Nav Modifier Extent", EditCondition = "bUseNavModifier"))
	FVector NavModifierExtent = FVector(300.f, 300.f, 200.f);

private:
	/** [cheatdebug/nav] NavModifier를 강제 적용 (NavArea_Null로 덮어쓰고 dirty area 요청). NavSystem 미준비 시 재시도 */
	void ApplyNavModifierRuntime(int32 RetryCount);
	FTimerHandle NavModifierRetryHandle;

protected:
	/** [ShipHP] ShipHealthComponent->OnDeath 콜백 (서버에서만 발화).
	 *  파괴 델리게이트 브로드캐스트 + GameMode 에 패배 통지. 시그니처는 FOnHellunaDeath 와 일치. */
	UFUNCTION()
	void HandleShipDestroyed(AActor* DeadActor, AActor* KillerActor);

#if WITH_EDITOR
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
#endif

	// =========================================================
	// ★ [Phase18] 3D 상호작용 프롬프트 위젯
	// =========================================================
protected:
	/** 3D 프롬프트 WidgetComponent */
	UPROPERTY()
	TObjectPtr<UWidgetComponent> InteractWidgetComp;

	/** 3D 프롬프트 위젯 클래스 (BP에서 할당 — WBP_Inv_InteractPrompt) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Repair|Widget",
		meta = (DisplayName = "Interact Widget Class (3D 상호작용 위젯 클래스)"))
	TSubclassOf<UUserWidget> InteractWidgetClass;

	/** 위젯 Z 오프셋 (우주선 위에 떠있는 높이) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Repair|Widget",
		meta = (DisplayName = "Widget Z Offset (위젯 높이 오프셋)", ClampMin = "0", ClampMax = "500"))
	float InteractWidgetZOffset = 200.0f;

private:
	bool bInteractWidgetVisible = false;
};
