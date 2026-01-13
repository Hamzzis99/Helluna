// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Object/ResourceUsingObject/HellunaBaseResourceUsingObject.h"
#include "ResourceUsingObject_SpaceShip.generated.h"

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
	
};
