// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "HellunaWidget_SpaceShip.generated.h"

/**
 * 
 */

class AResourceUsingObject_SpaceShip;
class UActorComponent;
class UProgressBar;
class UTextBlock;
class UWidgetAnimation;

UCLASS()
class HELLUNA_API UHellunaWidget_SpaceShip : public UUserWidget
{
    GENERATED_BODY()

public:
    UFUNCTION(BlueprintCallable, Category = "Repair")
    void SetTargetShip(AResourceUsingObject_SpaceShip* InShip);

protected:
    UPROPERTY()
    TObjectPtr<AResourceUsingObject_SpaceShip> TargetShip;

    UFUNCTION()
    void OnRepairChanged(int32 Current, int32 Need);

    UFUNCTION(BlueprintImplementableEvent, Category = "Repair")
    void BP_UpdateRepair(int32 Current, int32 Need, float Percent);

    // [ShipHP] Ship hull HP widgets. Names must match the designer widgets (BindWidget).
    UPROPERTY(meta = (BindWidgetOptional))
    TObjectPtr<UProgressBar> ShipHPBar;

    UPROPERTY(meta = (BindWidgetOptional))
    TObjectPtr<UTextBlock> ShipHPText;

    UPROPERTY(meta = (BindWidgetOptional))
    TObjectPtr<UTextBlock> ShipHPPercentText;

    UPROPERTY(meta = (BindWidgetOptional))
    TObjectPtr<UTextBlock> ShipStatusText;

    UPROPERTY(meta = (BindWidgetOptional))
    TObjectPtr<UTextBlock> ShipDestroyedText;

    UPROPERTY(meta = (BindWidgetOptional))
    TObjectPtr<UTextBlock> TextBlock_0;

    UPROPERTY(meta = (BindWidgetAnimOptional), Transient)
    TObjectPtr<UWidgetAnimation> CriticalPulse;

    UFUNCTION()
    void OnShipHealthChanged(UActorComponent* HealthComponent, float OldHealth, float NewHealth, AActor* InstigatorActor);

    // [ShipHP] Proportional fill from Health/MaxHealth, plus color band, texts and status.
    void RefreshShipHP(float Health, float MaxHealth, float Percent);

    // [ShipHP] Critical blink driven in tick (UMG anim bindings cannot be authored via Python).
    virtual void NativeTick(const FGeometry& MyGeometry, float InDeltaTime) override;
    virtual void NativeDestruct() override;

    bool bShipCritical = false;
    float ShipPulseTime = 0.f;
};
