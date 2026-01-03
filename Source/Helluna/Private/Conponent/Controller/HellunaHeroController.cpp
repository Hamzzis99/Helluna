// Fill out your copyright notice in the Description page of Project Settings.


#include "Controller/HellunaHeroController.h"



AHellunaHeroController::AHellunaHeroController()
{
	HeroTeamID = FGenericTeamId(0);
}

FGenericTeamId AHellunaHeroController::GetGenericTeamId() const
{
	return HeroTeamID;
}
