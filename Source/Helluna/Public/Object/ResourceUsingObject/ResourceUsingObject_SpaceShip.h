// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Object/ResourceUsingObject/HellunaBaseResourceUsingObject.h"
#include "ResourceUsingObject_SpaceShip.generated.h"

/**
 * 
 */

DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnRepairProgressChanged, int32, Current, int32, Need);


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

    UFUNCTION(BlueprintCallable, Category = "Repair")
    bool AddRepairResource(int32 Amount);
        
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
	
};
