// Fill out your copyright notice in the Description page of Project Settings.


#include "Widgets/HellunaWidget_SpaceShip.h"
#include "Object/ResourceUsingObject/ResourceUsingObject_SpaceShip.h"

void UHellunaWidget_SpaceShip::SetTargetShip(AResourceUsingObject_SpaceShip* InShip)
{

	if (TargetShip)
	{
		TargetShip->OnRepairProgressChanged.RemoveAll(this);
	}

	TargetShip = InShip;

	if (!TargetShip)
		return;

	TargetShip->OnRepairProgressChanged.AddDynamic(this, &UHellunaWidget_SpaceShip::OnRepairChanged);

	OnRepairChanged(TargetShip->GetCurrentResource(), TargetShip->GetNeedResource());
}

void UHellunaWidget_SpaceShip::OnRepairChanged(int32 Current, int32 Need)
{
	if (!TargetShip)
		return;

	BP_UpdateRepair(Current, Need, TargetShip->GetRepairPercent());
}

void UHellunaWidget_SpaceShip::NativeDestruct()
{
	if (TargetShip)
	{
		TargetShip->OnRepairProgressChanged.RemoveAll(this);
		TargetShip = nullptr;
	}

	Super::NativeDestruct();
}

