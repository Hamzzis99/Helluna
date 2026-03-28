// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Object/ResourceUsingObject/HellunaBaseResourceUsingObject.h"
#include "ResourceUsingObject_SpaceShip.generated.h"

class UWidgetComponent;
class USpaceShipAttackSlotManager;

/**
 * 
 */

DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnRepairProgressChanged, int32, Current, int32, Need);

// ⭐ 새로 추가: 수리 완료 델리게이트
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnSpaceShipRepairCompleted);


UCLASS()
class HELLUNA_API AResourceUsingObject_SpaceShip : public AHellunaBaseResourceUsingObject
{
	GENERATED_BODY()
	
protected:
	virtual void CollisionBoxBeginOverlap(UPrimitiveComponent* OverlappedComponent, AActor* OtherActor, UPrimitiveComponent* OtherComp, int32 OtherBodyIndex, bool bFromSweep, const FHitResult& SweepResult) override;

	virtual void CollisionBoxEndOverlap(UPrimitiveComponent* OverlappedComponent, AActor* OtherActor, UPrimitiveComponent* OtherComp, int32 OtherBodyIndex) override;

    virtual void BeginPlay() override;

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
	// ★ 공격 슬롯 매니저
	// =========================================================
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Combat",
		meta = (DisplayName = "Attack Slot Manager (공격 슬롯 매니저)"))
	TObjectPtr<USpaceShipAttackSlotManager> AttackSlotManager;

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
