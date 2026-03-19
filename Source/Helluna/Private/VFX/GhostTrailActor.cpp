// Capstone Project Helluna — Ghost Trail After Image

#include "VFX/GhostTrailActor.h"
#include "Components/PoseableMeshComponent.h"
#include "AbilitySystem/HeroAbility/HeroGameplayAbility_GunParry.h"

AGhostTrailActor::AGhostTrailActor()
{
	PrimaryActorTick.bCanEverTick = true;

	GhostMesh = CreateDefaultSubobject<UPoseableMeshComponent>(TEXT("GhostMesh"));
	SetRootComponent(GhostMesh);
	GhostMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	GhostMesh->SetGenerateOverlapEvents(false);
	GhostMesh->SetCollisionResponseToAllChannels(ECollisionResponse::ECR_Ignore);
	GhostMesh->CastShadow = false;
	GhostMesh->bReceivesDecals = false;

	bReplicates = false;
}

void AGhostTrailActor::Initialize(USkeletalMeshComponent* SourceMesh, UMaterialInterface* Material,
	float InFadeDuration, float InInitialOpacity, FLinearColor InGhostColor)
{
	if (!SourceMesh || !GhostMesh) return;

	FadeDuration = FMath::Max(InFadeDuration, 0.01f);
	InitialOpacity = InInitialOpacity;
	ElapsedTime = 0.f;

	// SkeletalMesh 복사 + 포즈 복사
	GhostMesh->SetSkinnedAssetAndUpdate(SourceMesh->GetSkeletalMeshAsset());
	GhostMesh->CopyPoseFromSkeletalComponent(SourceMesh);

	// 모든 머티리얼 슬롯에 DynamicMaterialInstance 오버라이드
	if (Material)
	{
		const int32 NumMaterials = GhostMesh->GetNumMaterials();
		for (int32 i = 0; i < NumMaterials; i++)
		{
			UMaterialInstanceDynamic* MID = UMaterialInstanceDynamic::Create(Material, this);
			if (MID)
			{
				MID->SetScalarParameterValue(TEXT("Opacity"), InitialOpacity);
				MID->SetScalarParameterValue(TEXT("EmissiveStrength"), 5.0f);
				MID->SetVectorParameterValue(TEXT("GhostColor"), InGhostColor);
				GhostMesh->SetMaterial(i, MID);
				DynamicMaterials.Add(MID);
			}
		}
	}

	// 안전장치: FadeDuration 이후 자동 소멸
	SetLifeSpan(FadeDuration + 0.1f);

	UE_LOG(LogGunParry, Verbose, TEXT("[GhostTrail] Initialize — Materials=%d, Opacity=%.2f, FadeDuration=%.1f"),
		DynamicMaterials.Num(), InitialOpacity, FadeDuration);
}

void AGhostTrailActor::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

	ElapsedTime += DeltaTime;
	const float Alpha = FMath::Clamp(ElapsedTime / FadeDuration, 0.f, 1.f);
	const float CurrentOpacity = FMath::Lerp(InitialOpacity, 0.f, Alpha);

	for (UMaterialInstanceDynamic* MID : DynamicMaterials)
	{
		if (MID)
		{
			MID->SetScalarParameterValue(TEXT("Opacity"), CurrentOpacity);
		}
	}

	if (Alpha >= 1.f)
	{
		UE_LOG(LogGunParry, Warning, TEXT("[GhostTrail] 잔상 페이드아웃 완료 — Destroy"));
		Destroy();
	}
}
