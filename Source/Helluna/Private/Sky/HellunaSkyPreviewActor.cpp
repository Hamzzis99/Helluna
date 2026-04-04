// Capstone Project Helluna — 에디터 전용 밤/낮 미리보기 액터

#include "Sky/HellunaSkyPreviewActor.h"
#include "EngineUtils.h"
#include "Components/VolumetricCloudComponent.h"

AHellunaSkyPreviewActor::AHellunaSkyPreviewActor()
{
	PrimaryActorTick.bCanEverTick = false;

	// 에디터 전용 — 런타임(패키징)에 포함되지 않음
	bIsEditorOnlyActor = true;

#if WITH_EDITORONLY_DATA
	// 에디터에서 보이는 아이콘 (빈 액터라 찾기 어려우므로)
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
		if (It->GetName().Contains(TEXT("Ultra_Dynamic_Sky")))
			return *It;
	}
	return nullptr;
}

void AHellunaSkyPreviewActor::ApplyPreview()
{
	AActor* UDS = FindUDSActor();
	if (!UDS) return;

	UClass* UDSClass = UDS->GetClass();
	const bool bIsNight = (PreviewMode == ESkyPreviewMode::Night);
	const float TargetTime = bIsNight ? NightPreviewTime : DayPreviewTime;

	// Time of Day 설정
	if (FProperty* TimeProp = FindFProperty<FProperty>(UDSClass, TEXT("Time of Day")))
	{
		if (FFloatProperty* FP = CastField<FFloatProperty>(TimeProp))
			FP->SetPropertyValue_InContainer(UDS, TargetTime);
		else if (FDoubleProperty* DP = CastField<FDoubleProperty>(TimeProp))
			DP->SetPropertyValue_InContainer(UDS, static_cast<double>(TargetTime));
	}

	// Animate OFF (미리보기 중 시간 고정)
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

#if WITH_EDITOR
void AHellunaSkyPreviewActor::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);

	if (!PropertyChangedEvent.Property) return;

	const FName PropName = PropertyChangedEvent.GetPropertyName();

	if (PropName == GET_MEMBER_NAME_CHECKED(AHellunaSkyPreviewActor, PreviewMode)
		|| PropName == GET_MEMBER_NAME_CHECKED(AHellunaSkyPreviewActor, NightPreviewTime)
		|| PropName == GET_MEMBER_NAME_CHECKED(AHellunaSkyPreviewActor, DayPreviewTime))
	{
		ApplyPreview();
	}
}
#endif
