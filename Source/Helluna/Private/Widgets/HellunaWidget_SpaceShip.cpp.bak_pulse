// Fill out your copyright notice in the Description page of Project Settings.


#include "Widgets/HellunaWidget_SpaceShip.h"
#include "Object/ResourceUsingObject/ResourceUsingObject_SpaceShip.h"
#include "Character/EnemyComponent/HellunaHealthComponent.h"
#include "Components/ProgressBar.h"
#include "Components/TextBlock.h"

void UHellunaWidget_SpaceShip::SetTargetShip(AResourceUsingObject_SpaceShip* InShip)
{
    if (TargetShip)
    {
        TargetShip->OnRepairProgressChanged.RemoveAll(this);
        if (UHellunaHealthComponent* OldHP = TargetShip->GetShipHealthComponent())
        {
            OldHP->OnHealthChanged.RemoveAll(this);
        }
    }

    TargetShip = InShip;

    if (!TargetShip)
        return;

    TargetShip->OnRepairProgressChanged.AddDynamic(this, &UHellunaWidget_SpaceShip::OnRepairChanged);

    OnRepairChanged(TargetShip->GetCurrentResource(), TargetShip->GetNeedResource());

    if (UHellunaHealthComponent* HP = TargetShip->GetShipHealthComponent())
    {
        HP->OnHealthChanged.AddDynamic(this, &UHellunaWidget_SpaceShip::OnShipHealthChanged);
        RefreshShipHP(HP->GetHealth(), HP->GetMaxHealth(), TargetShip->GetShipHealthPercent());
    }
}

void UHellunaWidget_SpaceShip::OnRepairChanged(int32 Current, int32 Need)
{
    if (!TargetShip)
        return;

    BP_UpdateRepair(Current, Need, TargetShip->GetRepairPercent());
}

void UHellunaWidget_SpaceShip::OnShipHealthChanged(UActorComponent* HealthComponent, float OldHealth, float NewHealth, AActor* InstigatorActor)
{
    if (!TargetShip)
        return;

    RefreshShipHP(NewHealth, TargetShip->GetShipMaxHealth(), TargetShip->GetShipHealthPercent());
}

void UHellunaWidget_SpaceShip::RefreshShipHP(float Health, float MaxHealth, float Percent)
{
    const float P = FMath::Clamp(Percent, 0.f, 1.f);

    FLinearColor BarColor;
    if (P > 0.55f)
    {
        BarColor = FLinearColor(0.247f, 0.878f, 0.631f);
    }
    else if (P > 0.25f)
    {
        BarColor = FLinearColor(1.000f, 0.761f, 0.302f);
    }
    else
    {
        BarColor = FLinearColor(1.000f, 0.286f, 0.373f);
    }

    if (ShipHPBar)
    {
        ShipHPBar->SetPercent(P);
        ShipHPBar->SetFillColorAndOpacity(BarColor);
    }

    if (ShipHPText)
    {
        ShipHPText->SetText(FText::FromString(FString::Printf(TEXT("%d / %d"),
            FMath::RoundToInt(Health), FMath::RoundToInt(MaxHealth))));
    }

    if (ShipHPPercentText)
    {
        ShipHPPercentText->SetText(FText::FromString(FString::Printf(TEXT("%d%%"),
            FMath::RoundToInt(P * 100.f))));
    }

    const bool bDestroyed = (Health <= 0.f);
    const bool bCritical = (!bDestroyed && P <= 0.25f);

    if (ShipStatusText)
    {
        const FString S = bDestroyed ? TEXT("Ship destroyed") : (bCritical ? TEXT("Critical") : TEXT("Nominal"));
        const FLinearColor C = (bDestroyed || bCritical) ? FLinearColor(1.f, 0.42f, 0.49f) : FLinearColor(0.247f, 0.878f, 0.631f);
        ShipStatusText->SetText(FText::FromString(S));
        ShipStatusText->SetColorAndOpacity(FSlateColor(C));
    }

    if (ShipDestroyedText)
    {
        ShipDestroyedText->SetVisibility(bDestroyed ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Collapsed);
    }

    if (CriticalPulse)
    {
        if (bCritical && !IsAnimationPlaying(CriticalPulse))
        {
            PlayAnimation(CriticalPulse, 0.f, 0);
        }
        else if (!bCritical && IsAnimationPlaying(CriticalPulse))
        {
            StopAnimation(CriticalPulse);
        }
    }
}

void UHellunaWidget_SpaceShip::NativeDestruct()
{
    if (TargetShip)
    {
        TargetShip->OnRepairProgressChanged.RemoveAll(this);
        if (UHellunaHealthComponent* HP = TargetShip->GetShipHealthComponent())
        {
            HP->OnHealthChanged.RemoveAll(this);
        }
        TargetShip = nullptr;
    }

    Super::NativeDestruct();
}
