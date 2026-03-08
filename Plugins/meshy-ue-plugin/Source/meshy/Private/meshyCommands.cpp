// Copyright Epic Games, Inc. All Rights Reserved.

#include "meshyCommands.h"

#define LOCTEXT_NAMESPACE "FmeshyModule"

void FmeshyCommands::RegisterCommands()
{
	UI_COMMAND(OpenPluginWindow, "meshy", "Bring up meshy window", EUserInterfaceActionType::Button, FInputChord());
}

#undef LOCTEXT_NAMESPACE
