// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Framework/Commands/Commands.h"
#include "meshyStyle.h"

class FmeshyCommands : public TCommands<FmeshyCommands>
{
public:

	FmeshyCommands()
		: TCommands<FmeshyCommands>(TEXT("meshy"), NSLOCTEXT("Contexts", "meshy", "meshy Plugin"), NAME_None, FmeshyStyle::GetStyleSetName())
	{
	}

	// TCommands<> interface
	virtual void RegisterCommands() override;

public:
	TSharedPtr< FUICommandInfo > OpenPluginWindow;
};