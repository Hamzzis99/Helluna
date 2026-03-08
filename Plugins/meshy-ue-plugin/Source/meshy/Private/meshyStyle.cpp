// Copyright Epic Games, Inc. All Rights Reserved.

#include "meshyStyle.h"
#include "Styling/SlateStyleRegistry.h"
#include "Framework/Application/SlateApplication.h"
#include "Slate/SlateGameResources.h"
#include "Interfaces/IPluginManager.h"
#include "Styling/SlateStyleMacros.h"
#include "Misc/EngineVersionComparison.h"

// UE 5.6/5.7 版本兼容性宏
#ifndef UE_5_07_OR_LATER
    #if ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION >= 7
        #define UE_5_07_OR_LATER 1
    #else
        #define UE_5_07_OR_LATER 0
    #endif
#endif

TSharedPtr<FSlateStyleSet> FmeshyStyle::StyleInstance = nullptr;

void FmeshyStyle::Initialize()
{
	if (!StyleInstance.IsValid())
	{
		StyleInstance = Create();
		FSlateStyleRegistry::RegisterSlateStyle(*StyleInstance);
	}
}

void FmeshyStyle::Shutdown()
{
	FSlateStyleRegistry::UnRegisterSlateStyle(*StyleInstance);
	ensure(StyleInstance.IsUnique());
	StyleInstance.Reset();
}

FName FmeshyStyle::GetStyleSetName()
{
	static FName StyleSetName(TEXT("meshyStyle"));
	return StyleSetName;
}

const FVector2D Icon16x16(16.0f, 16.0f);
const FVector2D Icon20x20(20.0f, 20.0f);
const FVector2D Icon24x24(24.0f, 24.0f);
const FVector2D Icon64x64(64.0f, 64.0f);

TSharedRef<FSlateStyleSet> FmeshyStyle::Create()
{
	TSharedRef<FSlateStyleSet> Style = MakeShareable(new FSlateStyleSet("meshyStyle"));
	
	// 设置内容根目录为插件目录
	FString PluginDir = IPluginManager::Get().FindPlugin("meshy")->GetBaseDir();
	Style->SetContentRoot(PluginDir);

	// 注册图标，使用相对于插件目录的路径
	FString IconPath = PluginDir / TEXT("Source/meshy/Meshy_Icon_64.png");
	Style->Set("MeshyBridge.Icon", new FSlateImageBrush(IconPath, Icon16x16));
	
	// 同时添加大一点的图标版本
	Style->Set("MeshyBridge.IconLarge", new FSlateImageBrush(IconPath, Icon24x24));

	// 添加按钮样式（这里应该确保Resources目录存在）
	FString ButtonNormalPath = PluginDir / TEXT("Resources/ButtonNormal");
	FString ButtonPressedPath = PluginDir / TEXT("Resources/ButtonPressed");
	FString ButtonHoveredPath = PluginDir / TEXT("Resources/ButtonHovered");
	
	const FButtonStyle BridgeButtonStyle = FButtonStyle()
	.SetNormal(FSlateBoxBrush(ButtonNormalPath, FVector2D(180, 40), FMargin(8/16.0f)))
	.SetPressed(FSlateBoxBrush(ButtonPressedPath, FVector2D(180, 40), FMargin(8/16.0f)))
	.SetHovered(FSlateBoxBrush(ButtonHoveredPath, FVector2D(180, 40), FMargin(8/16.0f)));
	
	Style->Set("BridgeButton", BridgeButtonStyle);
	
	FString FontPath = PluginDir / TEXT("Resources/Fonts/Roboto-Regular.ttf");
	Style->Set("BridgeButton.TextStyle", FTextBlockStyle()
	.SetFont(FSlateFontInfo(FontPath, 16))
	.SetColorAndOpacity(FSlateColor(FLinearColor::White)));

	return Style;
}

void FmeshyStyle::ReloadTextures()
{
	if (FSlateApplication::IsInitialized())
	{
		FSlateApplication::Get().GetRenderer()->ReloadTextureResources();
	}
}

const ISlateStyle& FmeshyStyle::Get()
{
	return *StyleInstance;
}
