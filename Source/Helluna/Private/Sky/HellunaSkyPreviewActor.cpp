// Capstone Project Helluna — 에디터 전용 밤/낮 + 날씨 미리보기 액터

#include "Sky/HellunaSkyPreviewActor.h"
#include "EngineUtils.h"
#include "Components/VolumetricCloudComponent.h"

AHellunaSkyPreviewActor::AHellunaSkyPreviewActor()
{
	PrimaryActorTick.bCanEverTick = false;
	bIsEditorOnlyActor = true;

#if WITH_EDITORONLY_DATA
	if (!IsRunningCommandlet())
	{
		USceneComponent* Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
		SetRootComponent(Root);
	}
#endif
}

AActor* AHellunaSkyPreviewActor::FindUDSActor() const
{
	UWorld* World = GetWorld();
	if (!World) return nullptr;
	for (TActorIterator<AActor> It(World); It; ++It)
	{
		if (It->GetName().Contains(TEXT("Ultra_Dynamic_Sky")) && !It->GetName().Contains(TEXT("Weather")))
			return *It;
	}
	return nullptr;
}

AActor* AHellunaSkyPreviewActor::FindUDWActor() const
{
	UWorld* World = GetWorld();
	if (!World) return nullptr;
	for (TActorIterator<AActor> It(World); It; ++It)
	{
		if (It->GetName().Contains(TEXT("Ultra_Dynamic_Weather")))
			return *It;
	}
	return nullptr;
}

void AHellunaSkyPreviewActor::ApplyTimePreview()
{
	AActor* UDS = FindUDSActor();
	if (!UDS) return;

	UClass* UDSClass = UDS->GetClass();
	const bool bIsNight = (PreviewMode == ESkyPreviewMode::Night);
	const float TargetTime = bIsNight ? NightPreviewTime : DayPreviewTime;

	// Time of Day
	if (FProperty* TimeProp = FindFProperty<FProperty>(UDSClass, TEXT("Time of Day")))
	{
		if (FFloatProperty* FP = CastField<FFloatProperty>(TimeProp))
			FP->SetPropertyValue_InContainer(UDS, TargetTime);
		else if (FDoubleProperty* DP = CastField<FDoubleProperty>(TimeProp))
			DP->SetPropertyValue_InContainer(UDS, static_cast<double>(TargetTime));
	}

	// Animate OFF
	if (FProperty* AnimProp = FindFProperty<FProperty>(UDSClass, TEXT("Animate Time of Day")))
	{
		if (FBoolProperty* BP = CastField<FBoolProperty>(AnimProp))
			BP->SetPropertyValue_InContainer(UDS, false);
	}

	// VolumetricCloud 토글
	if (UVolumetricCloudComponent* CloudComp = UDS->FindComponentByClass<UVolumetricCloudComponent>())
	{
		CloudComp->SetVisibility(!bIsNight);
	}

	// Cloud Shadows 토글
	if (FProperty* ShadowProp = FindFProperty<FProperty>(UDSClass, TEXT("Use Cloud Shadows")))
	{
		if (FBoolProperty* BP = CastField<FBoolProperty>(ShadowProp))
			BP->SetPropertyValue_InContainer(UDS, !bIsNight);
	}
}

void AHellunaSkyPreviewActor::ApplyWeatherPreview()
{
	AActor* UDW = FindUDWActor();
	if (!UDW) return;

	// Change Weather 함수 찾기
	UFunction* Func = UDW->FindFunction(TEXT("Change Weather"));
	if (!Func)
		Func = UDW->FindFunction(TEXT("ChangeWeather"));
	if (!Func) return;

	if (WeatherPreset == ESkyWeatherPreset::None)
	{
		// None = 날씨 해제
		struct { UObject* NewWeatherType; float TransitionTime; } Params;
		Params.NewWeatherType = nullptr;
		Params.TransitionTime = WeatherTransitionTime;
		UDW->ProcessEvent(Func, &Params);
		return;
	}

	// 프리셋 에셋 로드
	const FString AssetPath = GetWeatherPresetPath(WeatherPreset);
	if (AssetPath.IsEmpty()) return;

	UObject* WeatherAsset = StaticLoadObject(UObject::StaticClass(), nullptr, *AssetPath);
	if (!WeatherAsset) return;

	struct { UObject* NewWeatherType; float TransitionTime; } Params;
	Params.NewWeatherType = WeatherAsset;
	Params.TransitionTime = WeatherTransitionTime;
	UDW->ProcessEvent(Func, &Params);
}

FString AHellunaSkyPreviewActor::GetWeatherPresetPath(ESkyWeatherPreset Preset)
{
	static const TCHAR* BasePath = TEXT("/Game/UltraDynamicSky/Blueprints/Weather_Effects/Weather_Presets/");

	switch (Preset)
	{
	case ESkyWeatherPreset::ClearSkies:   return FString(BasePath) + TEXT("Clear_Skies.Clear_Skies");
	case ESkyWeatherPreset::PartlyCloudy: return FString(BasePath) + TEXT("Partly_Cloudy.Partly_Cloudy");
	case ESkyWeatherPreset::Cloudy:       return FString(BasePath) + TEXT("Cloudy.Cloudy");
	case ESkyWeatherPreset::Overcast:     return FString(BasePath) + TEXT("Overcast.Overcast");
	case ESkyWeatherPreset::Foggy:        return FString(BasePath) + TEXT("Foggy.Foggy");
	case ESkyWeatherPreset::RainLight:    return FString(BasePath) + TEXT("Rain_Light.Rain_Light");
	case ESkyWeatherPreset::Rain:         return FString(BasePath) + TEXT("Rain.Rain");
	case ESkyWeatherPreset::Thunderstorm: return FString(BasePath) + TEXT("Rain_Thunderstorm.Rain_Thunderstorm");
	case ESkyWeatherPreset::SnowLight:    return FString(BasePath) + TEXT("Snow_Light.Snow_Light");
	case ESkyWeatherPreset::Snow:         return FString(BasePath) + TEXT("Snow.Snow");
	case ESkyWeatherPreset::Blizzard:     return FString(BasePath) + TEXT("Snow_Blizzard.Snow_Blizzard");
	default: return FString();
	}
}

#if WITH_EDITOR
void AHellunaSkyPreviewActor::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);

	if (!PropertyChangedEvent.Property) return;

	const FName PropName = PropertyChangedEvent.GetPropertyName();

	// 시간 관련 변경
	if (PropName == GET_MEMBER_NAME_CHECKED(AHellunaSkyPreviewActor, PreviewMode)
		|| PropName == GET_MEMBER_NAME_CHECKED(AHellunaSkyPreviewActor, NightPreviewTime)
		|| PropName == GET_MEMBER_NAME_CHECKED(AHellunaSkyPreviewActor, DayPreviewTime))
	{
		ApplyTimePreview();
	}

	// 날씨 관련 변경
	if (PropName == GET_MEMBER_NAME_CHECKED(AHellunaSkyPreviewActor, WeatherPreset)
		|| PropName == GET_MEMBER_NAME_CHECKED(AHellunaSkyPreviewActor, WeatherTransitionTime))
	{
		ApplyWeatherPreview();
	}
}
#endif
