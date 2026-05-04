/************************************************************************************
 *																					*
 * Copyright (C) 2020 Truong Bui.													*
 * Website:	https://github.com/truong-bui/AsyncLoadingScreen						*
 * Licensed under the MIT License. See 'LICENSE' file for full license information. *
 *																					*
 ************************************************************************************/

#pragma once

#include "Modules/ModuleManager.h"
#include "Templates/SharedPointer.h"

struct FALoadingScreenSettings;
class SWidget;

class ASYNCLOADINGSCREEN_API FAsyncLoadingScreenModule : public IModuleInterface
{
public:

	/** IModuleInterface implementation */

	/**
	 * Called right after the module DLL has been loaded and the module object has been created
	 */
	virtual void StartupModule() override;

	/**
	 * Called before the module is unloaded, right before the module object is destroyed.
	 */
	virtual void ShutdownModule() override;

	/**
	 * Returns true if this module hosts gameplay code
	 *
	 * @return True for "gameplay modules", or false for engine code modules, plugins, etc.
	 */
	virtual bool IsGameModule() const override;

	/**
	 * [Helluna fork] 외부에서 동적으로 LoadingScreen widget을 주입할 때 사용.
	 * SetupLoadingScreen 시 이 widget이 valid하면 ProjectSettings의 Layout 대신 이걸 사용.
	 */
	static void SetExternalLoadingWidget(TSharedPtr<SWidget> InWidget) { ExternalLoadingWidget = InWidget; }
	static void ClearExternalLoadingWidget() { ExternalLoadingWidget.Reset(); }
	static TSharedPtr<SWidget> GetExternalLoadingWidget() { return ExternalLoadingWidget; }

	/**
	 * Singleton-like access to this module's interface. This is just for convenience!
	 * Beware of calling this during the shutdown phase, though.  Your module might have been unloaded already.
	 *
	 * @return Returns singleton instance, loading the module on demand if needed
	 */
	static inline FAsyncLoadingScreenModule& Get()
	{
		return FModuleManager::LoadModuleChecked<FAsyncLoadingScreenModule>("AsyncLoadingScreen");
	}

	/**
	 * Checks to see if this module is loaded and ready. It is only valid to call Get() if IsAvailable() returns true.
	 *
	 * @return True if the module is loaded and ready to use
	 */
	static inline bool IsAvailable()
	{
		return FModuleManager::Get().IsModuleLoaded("AsyncLoadingScreen");
	}

	/**
	 * Is showing Startup Loading Screen?
	 */
	bool IsStartupLoadingScreen() { return bIsStartupLoadingScreen; }

private:
	/**
	 * Loading screen callback, it won't be called if we've already explicitly setup the loading screen
	 */
	void PreSetupLoadingScreen();

	/**
	 * Setup loading screen settings 
	 */
	void SetupLoadingScreen(const FALoadingScreenSettings& LoadingScreenSettings);

	/**
	 * Shuffle the movies list
	 */
	void ShuffleMovies(TArray<FString>& MoviesList);
private:

	bool bIsStartupLoadingScreen = false;

	/** [Helluna fork] 외부 주입 widget. */
	static TSharedPtr<SWidget> ExternalLoadingWidget;
};
