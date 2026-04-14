// Capstone Project Helluna
// 콘솔 커맨드 "Helluna.GenerateTimeDistortionPP" — 시간 왜곡 존 PP 머티리얼을 코드로 생성한다.
// 사용법: 에디터 실행 후 ~ 콘솔에서 Helluna.GenerateTimeDistortionPP 입력.
// 결과: /Game/BossEvent/Materials/M_PP_TimeDistortion

#if WITH_EDITOR

#include "CoreMinimal.h"
#include "HAL/IConsoleManager.h"
#include "Materials/Material.h"
#include "Materials/MaterialExpressionVectorParameter.h"
#include "Materials/MaterialExpressionScalarParameter.h"
#include "Materials/MaterialExpressionSceneTexture.h"
#include "Materials/MaterialExpressionConstant.h"
#include "Materials/MaterialExpressionConstant3Vector.h"
#include "Materials/MaterialExpressionDotProduct.h"
#include "Materials/MaterialExpressionDistance.h"
#include "Materials/MaterialExpressionWorldPosition.h"
#include "Materials/MaterialExpressionIf.h"
#include "Materials/MaterialExpressionLinearInterpolate.h"
#include "Materials/MaterialExpressionMultiply.h"
#include "Materials/MaterialExpressionComponentMask.h"
#include "MaterialEditingLibrary.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "UObject/Package.h"
#include "UObject/SavePackage.h"
#include "Misc/PackageName.h"

namespace HellunaTDZPPGen
{
	template <typename T>
	static T* NewExpr(UMaterial* Material, int32 X, int32 Y)
	{
		return Cast<T>(UMaterialEditingLibrary::CreateMaterialExpression(
			Material, T::StaticClass(), X, Y));
	}

	static void Generate()
	{
		const FString PackagePath = TEXT("/Game/BossEvent/Materials");
		const FString AssetName   = TEXT("M_PP_TimeDistortion");
		const FString FullPath    = PackagePath / AssetName;

		// 기존 에셋이 있으면 중단 — 덮어쓰기는 의도치 않은 덮어쓰기 방지를 위해 금지
		if (FPackageName::DoesPackageExist(FullPath))
		{
			UE_LOG(LogTemp, Warning,
				TEXT("[TDZ] %s 가 이미 존재합니다. 재생성하려면 먼저 삭제하세요."), *FullPath);
			return;
		}

		UPackage* Package = CreatePackage(*FullPath);
		if (!Package) return;
		Package->FullyLoad();

		UMaterial* Material = NewObject<UMaterial>(
			Package, *AssetName, RF_Public | RF_Standalone | RF_Transactional);
		if (!Material)
		{
			UE_LOG(LogTemp, Error, TEXT("[TDZ] Material 생성 실패"));
			return;
		}

		Material->MaterialDomain  = MD_PostProcess;
		Material->BlendableLocation = BL_SceneColorAfterTonemapping;
		Material->BlendablePriority = 0;

		// ---- 파라미터 ----
		auto* PZoneCenter = NewExpr<UMaterialExpressionVectorParameter>(Material, -1800,  200);
		PZoneCenter->ParameterName = TEXT("ZoneCenter");
		PZoneCenter->DefaultValue  = FLinearColor(0.f, 0.f, 0.f, 0.f);

		auto* PZoneRadius = NewExpr<UMaterialExpressionScalarParameter>(Material, -1400,  350);
		PZoneRadius->ParameterName = TEXT("ZoneRadius");
		PZoneRadius->DefaultValue  = 1000.f;

		auto* PStrength = NewExpr<UMaterialExpressionScalarParameter>(Material,  -500,  600);
		PStrength->ParameterName = TEXT("Strength");
		PStrength->DefaultValue  = 1.f;

		// ---- 씬 컬러 ----
		auto* STScene = NewExpr<UMaterialExpressionSceneTexture>(Material, -1800, -400);
		STScene->SceneTextureId = PPI_PostProcessInput0;

		// ---- 회색 계산: Dot(SceneColor, Luma) ----
		auto* CLuma = NewExpr<UMaterialExpressionConstant3Vector>(Material, -1500, -250);
		CLuma->Constant = FLinearColor(0.299f, 0.587f, 0.114f);

		auto* Dot = NewExpr<UMaterialExpressionDotProduct>(Material, -1200, -350);
		Dot->A.Expression = STScene;
		Dot->A.OutputIndex = 0; // Color
		Dot->B.Expression = CLuma;

		// ---- 존 마스크 (월드 거리) ----
		auto* WPos = NewExpr<UMaterialExpressionWorldPosition>(Material, -1800, 100);
		// 기본값 WPT_Default — 픽셀의 재구성 월드 위치

		auto* Dist = NewExpr<UMaterialExpressionDistance>(Material, -1400, 150);
		Dist->A.Expression = WPos;
		Dist->B.Expression = PZoneCenter;

		auto* CZero = NewExpr<UMaterialExpressionConstant>(Material, -1200, 450);
		CZero->R = 0.f;
		auto* COne = NewExpr<UMaterialExpressionConstant>(Material, -1200, 520);
		COne->R = 1.f;

		auto* IfInside = NewExpr<UMaterialExpressionIf>(Material, -1000, 200);
		IfInside->A.Expression = Dist;
		IfInside->B.Expression = PZoneRadius;
		IfInside->AGreaterThanB.Expression = CZero;
		IfInside->AEqualsB.Expression      = CZero;
		IfInside->ALessThanB.Expression    = COne;

		// ---- 스텐실 제외 마스크 (플레이어/보스/Orb 보호) ----
		auto* STStencil = NewExpr<UMaterialExpressionSceneTexture>(Material, -1800, 700);
		STStencil->SceneTextureId = PPI_CustomStencil;

		auto* StencilR = NewExpr<UMaterialExpressionComponentMask>(Material, -1500, 750);
		StencilR->R = true; StencilR->G = false; StencilR->B = false; StencilR->A = false;
		StencilR->Input.Expression = STStencil;
		StencilR->Input.OutputIndex = 0;

		auto* CHalf = NewExpr<UMaterialExpressionConstant>(Material, -1300, 900);
		CHalf->R = 0.5f;

		auto* IfStencil = NewExpr<UMaterialExpressionIf>(Material, -1000, 750);
		IfStencil->A.Expression = StencilR;
		IfStencil->B.Expression = CHalf;
		IfStencil->AGreaterThanB.Expression = CZero; // 스텐실 >= 1 → 제외
		IfStencil->AEqualsB.Expression      = COne;
		IfStencil->ALessThanB.Expression    = COne;  // 스텐실 == 0 → 적용

		// ---- 최종 마스크 = 존 * 스텐실 * Strength ----
		auto* Mul1 = NewExpr<UMaterialExpressionMultiply>(Material, -700, 400);
		Mul1->A.Expression = IfInside;
		Mul1->B.Expression = IfStencil;

		auto* Mul2 = NewExpr<UMaterialExpressionMultiply>(Material, -300, 500);
		Mul2->A.Expression = Mul1;
		Mul2->B.Expression = PStrength;

		// ---- Lerp(Original, Gray, Mask) ----
		auto* Lerp = NewExpr<UMaterialExpressionLinearInterpolate>(Material, 100, 0);
		Lerp->A.Expression = STScene;
		Lerp->A.OutputIndex = 0;
		Lerp->B.Expression = Dot; // 스칼라 → vec3 자동 브로드캐스트
		Lerp->Alpha.Expression = Mul2;

		// ---- Emissive 연결 ----
		UMaterialEditingLibrary::ConnectMaterialProperty(Lerp, FString(), MP_EmissiveColor);

		// ---- 컴파일 & 저장 ----
		UMaterialEditingLibrary::RecompileMaterial(Material);
		Material->PostEditChange();
		Material->MarkPackageDirty();
		FAssetRegistryModule::AssetCreated(Material);

		const FString FileName = FPackageName::LongPackageNameToFilename(
			FullPath, FPackageName::GetAssetPackageExtension());

		FSavePackageArgs SaveArgs;
		SaveArgs.TopLevelFlags = RF_Public | RF_Standalone;
		SaveArgs.SaveFlags     = SAVE_NoError;
		const bool bSaved = UPackage::SavePackage(Package, Material, *FileName, SaveArgs);

		UE_LOG(LogTemp, Warning,
			TEXT("[TDZ] GenerateTimeDistortionPP %s -> %s"),
			bSaved ? TEXT("SUCCESS") : TEXT("FAILED"), *FullPath);
	}
}

static FAutoConsoleCommand GHellunaGenerateTimeDistortionPPCmd(
	TEXT("Helluna.GenerateTimeDistortionPP"),
	TEXT("시간 왜곡 존 PP 머티리얼(M_PP_TimeDistortion)을 /Game/BossEvent/Materials/ 에 생성한다."),
	FConsoleCommandDelegate::CreateStatic(&HellunaTDZPPGen::Generate)
);

#endif // WITH_EDITOR
