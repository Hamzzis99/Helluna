// Capstone Project Helluna — 에디터 전용 밤/낮 + 날씨 미리보기 액터

#include "Sky/HellunaSkyPreviewActor.h"
#include "EngineUtils.h"
#include "Components/VolumetricCloudComponent.h"
#include "Kismet/KismetMaterialLibrary.h"
#include "Materials/MaterialParameterCollection.h"

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
#if WITH_EDITOR
	UDS->RerunConstructionScripts();
#endif
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
#if WITH_EDITOR
	UDW->RerunConstructionScripts();
#endif

	// UDS도 재실행 — 날씨가 UDS 구름/안개 값을 결정하므로
	AActor* UDS = FindUDSActor();
	if (UDS)
	{
#if WITH_EDITOR
		UDS->RerunConstructionScripts();
#endif
	}

	// MPC 월드 인스턴스에 DLWE 파라미터 push (에디터 뷰포트 즉시 반영)
	ApplyMaterialWeatherState(ActivePreset);
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

void AHellunaSkyPreviewActor::ApplyMaterialWeatherState(ESkyWeatherPreset Preset)
{
#if WITH_EDITOR
	UWorld* World = GetWorld();
	if (!World) return;

	static const TCHAR* MPCPath = TEXT("/Game/UltraDynamicSky/Materials/Weather/UltraDynamicWeather_Parameters");
	UMaterialParameterCollection* MPC = LoadObject<UMaterialParameterCollection>(nullptr, MPCPath);
	if (!MPC) return;

	// 프리셋별 MPC 파라미터 값
	float Wet = 0.f;
	float Raining = 0.f;
	float DLWEBaseWetness = 0.f;
	float DLWEPuddleCoverage = 0.f;
	float Snowing = 0.f;
	float DLWESnowDustDepth = 0.f;
	float CloudCoverage = 0.f;
	float Fog = 0.f;

	switch (Preset)
	{
	case ESkyWeatherPreset::None:
	case ESkyWeatherPreset::ClearSkies:
		break;
	case ESkyWeatherPreset::PartlyCloudy:
		CloudCoverage = 0.3f;
		break;
	case ESkyWeatherPreset::Cloudy:
		CloudCoverage = 0.6f;
		break;
	case ESkyWeatherPreset::Overcast:
		CloudCoverage = 0.9f;
		Wet = 0.1f;
		DLWEBaseWetness = 0.1f;
		break;
	case ESkyWeatherPreset::Foggy:
		Fog = 0.6f;
		CloudCoverage = 0.5f;
		Wet = 0.15f;
		DLWEBaseWetness = 0.15f;
		break;
	case ESkyWeatherPreset::RainLight:
		Wet = 0.5f;
		Raining = 0.5f;
		DLWEBaseWetness = 0.4f;
		DLWEPuddleCoverage = 0.2f;
		CloudCoverage = 0.7f;
		Fog = 0.1f;
		break;
	case ESkyWeatherPreset::Rain:
		Wet = 0.8f;
		Raining = 0.8f;
		DLWEBaseWetness = 0.7f;
		DLWEPuddleCoverage = 0.5f;
		CloudCoverage = 0.85f;
		Fog = 0.2f;
		break;
	case ESkyWeatherPreset::Thunderstorm:
		Wet = 1.f;
		Raining = 1.f;
		DLWEBaseWetness = 0.85f;
		DLWEPuddleCoverage = 0.7f;
		CloudCoverage = 1.f;
		Fog = 0.3f;
		break;
	case ESkyWeatherPreset::SnowLight:
		Snowing = 0.5f;
		DLWESnowDustDepth = 0.3f;
		CloudCoverage = 0.6f;
		break;
	case ESkyWeatherPreset::Snow:
		Snowing = 0.8f;
		DLWESnowDustDepth = 0.6f;
		CloudCoverage = 0.8f;
		break;
	case ESkyWeatherPreset::Blizzard:
		Snowing = 1.f;
		DLWESnowDustDepth = 1.f;
		CloudCoverage = 1.f;
		Fog = 0.4f;
		break;
	default:
		break;
	}

	UKismetMaterialLibrary::SetScalarParameterValue(World, MPC, TEXT("Wet"), Wet);
	UKismetMaterialLibrary::SetScalarParameterValue(World, MPC, TEXT("Raining"), Raining);
	UKismetMaterialLibrary::SetScalarParameterValue(World, MPC, TEXT("DLWE_Base Wetness"), DLWEBaseWetness);
	UKismetMaterialLibrary::SetScalarParameterValue(World, MPC, TEXT("DLWE_Puddle Coverage"), DLWEPuddleCoverage);
	UKismetMaterialLibrary::SetScalarParameterValue(World, MPC, TEXT("DLWE Puddle Sharpness"), 40.f);
	UKismetMaterialLibrary::SetScalarParameterValue(World, MPC, TEXT("Snowing"), Snowing);
	UKismetMaterialLibrary::SetScalarParameterValue(World, MPC, TEXT("DLWE_Snow/Dust Depth"), DLWESnowDustDepth);
	UKismetMaterialLibrary::SetScalarParameterValue(World, MPC, TEXT("Cloud Coverage"), CloudCoverage);
	UKismetMaterialLibrary::SetScalarParameterValue(World, MPC, TEXT("Fog"), Fog);
#endif
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
