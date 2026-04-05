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

void AHellunaSkyPreviewActor::SetUDSFloat(AActor* UDS, const TCHAR* PropName, float Value)
{
	if (FProperty* Prop = FindFProperty<FProperty>(UDS->GetClass(), PropName))
	{
		if (FFloatProperty* FP = CastField<FFloatProperty>(Prop))
			FP->SetPropertyValue_InContainer(UDS, Value);
		else if (FDoubleProperty* DP = CastField<FDoubleProperty>(Prop))
			DP->SetPropertyValue_InContainer(UDS, static_cast<double>(Value));
	}
}

void AHellunaSkyPreviewActor::SetUDSBool(AActor* UDS, const TCHAR* PropName, bool Value)
{
	if (FProperty* Prop = FindFProperty<FProperty>(UDS->GetClass(), PropName))
	{
		if (FBoolProperty* BP = CastField<FBoolProperty>(Prop))
			BP->SetPropertyValue_InContainer(UDS, Value);
	}
}

void AHellunaSkyPreviewActor::ApplyTimePreview()
{
	AActor* UDS = FindUDSActor();
	if (!UDS) return;

	const bool bIsNight = (PreviewMode == ESkyPreviewMode::Night);
	const float TargetTime = bIsNight ? NightPreviewTime : DayPreviewTime;

	// ── 시간 ──
	SetUDSFloat(UDS, TEXT("Time of Day"), TargetTime);
	SetUDSBool(UDS, TEXT("Animate Time of Day"), false);

	// ── UDS Construction Script 재실행 ──
	UDS->RerunConstructionScripts();
}

void AHellunaSkyPreviewActor::ApplyWeatherPreview()
{
	AActor* UDW = FindUDWActor();
	if (!UDW) return;

	// 현재 모드에 맞는 프리셋 선택
	const ESkyWeatherPreset ActivePreset =
		(PreviewMode == ESkyPreviewMode::Night) ? NightWeatherPreset : DayWeatherPreset;

	UClass* UDWClass = UDW->GetClass();
	FProperty* WeatherProp = FindFProperty<FProperty>(UDWClass, TEXT("Weather"));
	if (!WeatherProp) return;

	FObjectProperty* ObjProp = CastField<FObjectProperty>(WeatherProp);
	if (!ObjProp) return;

	if (ActivePreset == ESkyWeatherPreset::None)
	{
		ObjProp->SetObjectPropertyValue_InContainer(UDW, nullptr);
	}
	else
	{
		const FString AssetPath = GetWeatherPresetPath(ActivePreset);
		if (AssetPath.IsEmpty()) return;

		UObject* WeatherAsset = StaticLoadObject(UObject::StaticClass(), nullptr, *AssetPath);
		if (!WeatherAsset) return;

		ObjProp->SetObjectPropertyValue_InContainer(UDW, WeatherAsset);
	}

	// UDW Construction Script 재실행 → 날씨 즉시 반영
	UDW->RerunConstructionScripts();

	// UDS도 재실행 — 날씨가 UDS 구름/안개 값을 결정하므로
	AActor* UDS = FindUDSActor();
	if (UDS)
	{
		UDS->RerunConstructionScripts();
	}
}

void AHellunaSkyPreviewActor::ApplyFullPreview()
{
	ApplyTimePreview();
	ApplyWeatherPreview();
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

	// 밤/낮 모드 전환 → 시간 + 날씨 동시 적용
	if (PropName == GET_MEMBER_NAME_CHECKED(AHellunaSkyPreviewActor, PreviewMode))
	{
		ApplyFullPreview();
		return;
	}

	// 시간 값 변경
	if (PropName == GET_MEMBER_NAME_CHECKED(AHellunaSkyPreviewActor, NightPreviewTime)
		|| PropName == GET_MEMBER_NAME_CHECKED(AHellunaSkyPreviewActor, DayPreviewTime))
	{
		ApplyTimePreview();
	}

	// 날씨 변경 (각 모드별)
	if (PropName == GET_MEMBER_NAME_CHECKED(AHellunaSkyPreviewActor, DayWeatherPreset)
		|| PropName == GET_MEMBER_NAME_CHECKED(AHellunaSkyPreviewActor, NightWeatherPreset))
	{
		ApplyWeatherPreview();
	}
}
#endif
