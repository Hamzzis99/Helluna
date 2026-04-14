// Capstone Project Helluna
// 콘솔 커맨드 "Helluna.GeneratePhantomDistortionPP" — 환영 검무 공간 왜곡 PP 머티리얼을 코드로 생성한다.
// 사용법: 에디터 실행 후 ~ 콘솔에서 Helluna.GeneratePhantomDistortionPP 입력.
// 결과: /Game/BossEvent/Materials/M_PP_PhantomDistortion
//
// 효과:
//  - 월드 스페이스 거리 기반 마스크 (PhantomCenter 반경 안에서만 활성)
//  - 스크린 UV에 sin/cos 노이즈 오프셋 (굴절/왜곡)
//  - R/G/B 3채널을 서로 다른 오프셋으로 샘플 (chromatic aberration)
//  - 마스크 * Strength 로 원본과 lerp
//
// 파라미터:
//   Vector  PhantomCenter      (월드 좌표 / W 미사용)
//   Scalar  DistortionRadius   (cm, 기본 800)
//   Scalar  DistortionStrength (0~1, 기본 0.75)
//   Scalar  ElapsedTime        (초, 애니메이션용)

#if WITH_EDITOR

#include "CoreMinimal.h"
#include "HAL/IConsoleManager.h"
#include "Materials/Material.h"
#include "Materials/MaterialExpressionVectorParameter.h"
#include "Materials/MaterialExpressionScalarParameter.h"
#include "Materials/MaterialExpressionSceneTexture.h"
#include "Materials/MaterialExpressionScreenPosition.h"
#include "Materials/MaterialExpressionWorldPosition.h"
#include "Materials/MaterialExpressionDistance.h"
#include "Materials/MaterialExpressionDivide.h"
#include "Materials/MaterialExpressionOneMinus.h"
#include "Materials/MaterialExpressionSaturate.h"
#include "Materials/MaterialExpressionMultiply.h"
#include "Materials/MaterialExpressionAdd.h"
#include "Materials/MaterialExpressionSine.h"
#include "Materials/MaterialExpressionCosine.h"
#include "Materials/MaterialExpressionComponentMask.h"
#include "Materials/MaterialExpressionAppendVector.h"
#include "Materials/MaterialExpressionLinearInterpolate.h"
#include "Materials/MaterialExpressionConstant.h"
#include "MaterialEditingLibrary.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "UObject/Package.h"
#include "UObject/SavePackage.h"
#include "Misc/PackageName.h"

namespace HellunaPhantomPPGen
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
		const FString AssetName   = TEXT("M_PP_PhantomDistortion");
		const FString FullPath    = PackagePath / AssetName;

		if (FPackageName::DoesPackageExist(FullPath))
		{
			UE_LOG(LogTemp, Warning,
				TEXT("[PhantomPP] %s 가 이미 존재합니다. 재생성하려면 먼저 삭제하세요."), *FullPath);
			return;
		}

		UPackage* Package = CreatePackage(*FullPath);
		if (!Package) return;
		Package->FullyLoad();

		UMaterial* Material = NewObject<UMaterial>(
			Package, *AssetName, RF_Public | RF_Standalone | RF_Transactional);
		if (!Material)
		{
			UE_LOG(LogTemp, Error, TEXT("[PhantomPP] Material 생성 실패"));
			return;
		}

		Material->MaterialDomain    = MD_PostProcess;
		Material->BlendableLocation = BL_SceneColorAfterTonemapping;
		Material->BlendablePriority = 0;

		// =========================================================
		// 파라미터
		// =========================================================
		auto* PPhantomCenter = NewExpr<UMaterialExpressionVectorParameter>(Material, -2200, 200);
		PPhantomCenter->ParameterName = TEXT("PhantomCenter");
		PPhantomCenter->DefaultValue  = FLinearColor(0.f, 0.f, 0.f, 0.f);

		auto* PRadius = NewExpr<UMaterialExpressionScalarParameter>(Material, -2200, 400);
		PRadius->ParameterName = TEXT("DistortionRadius");
		PRadius->DefaultValue  = 800.f;

		auto* PStrength = NewExpr<UMaterialExpressionScalarParameter>(Material, -2200, 500);
		PStrength->ParameterName = TEXT("DistortionStrength");
		PStrength->DefaultValue  = 0.75f;

		auto* PTime = NewExpr<UMaterialExpressionScalarParameter>(Material, -2200, 600);
		PTime->ParameterName = TEXT("ElapsedTime");
		PTime->DefaultValue  = 0.f;

		// =========================================================
		// 월드 스페이스 마스크: saturate(1 - dist/radius) * strength
		// =========================================================
		auto* WPos = NewExpr<UMaterialExpressionWorldPosition>(Material, -2000, 0);

		auto* Dist = NewExpr<UMaterialExpressionDistance>(Material, -1700, 100);
		Dist->A.Expression = WPos;
		Dist->B.Expression = PPhantomCenter;

		auto* DistNorm = NewExpr<UMaterialExpressionDivide>(Material, -1500, 200);
		DistNorm->A.Expression = Dist;
		DistNorm->B.Expression = PRadius;

		auto* Inv = NewExpr<UMaterialExpressionOneMinus>(Material, -1300, 200);
		Inv->Input.Expression = DistNorm;

		auto* Mask = NewExpr<UMaterialExpressionSaturate>(Material, -1100, 200);
		Mask->Input.Expression = Inv;

		// Mask * Strength = 최종 적용 알파
		auto* MaskStrength = NewExpr<UMaterialExpressionMultiply>(Material, -900, 300);
		MaskStrength->A.Expression = Mask;
		MaskStrength->B.Expression = PStrength;

		// =========================================================
		// 스크린 UV 기반 노이즈 오프셋
		//   ScreenUV (0..1) 에 스케일/시간 추가 → sin/cos → 오프셋
		// =========================================================
		auto* ScreenPos = NewExpr<UMaterialExpressionScreenPosition>(Material, -2000, -400);
		// Output 1 = ViewportUV (0..1)

		// ScreenUV * 40 (고주파 노이즈)
		auto* UVScale = NewExpr<UMaterialExpressionConstant>(Material, -1800, -550);
		UVScale->R = 40.f;

		auto* ScaledUV = NewExpr<UMaterialExpressionMultiply>(Material, -1600, -450);
		ScaledUV->A.Expression = ScreenPos;
		ScaledUV->A.OutputIndex = 1; // ViewportUV
		ScaledUV->B.Expression = UVScale;

		// ElapsedTime * 3 (시간 속도)
		auto* TimeSpeed = NewExpr<UMaterialExpressionConstant>(Material, -1800, -300);
		TimeSpeed->R = 3.f;

		auto* TimeScaled = NewExpr<UMaterialExpressionMultiply>(Material, -1600, -250);
		TimeScaled->A.Expression = PTime;
		TimeScaled->B.Expression = TimeSpeed;

		// NoiseInput = ScaledUV + TimeScaled (TimeScaled는 스칼라 → 브로드캐스트)
		auto* NoiseInput = NewExpr<UMaterialExpressionAdd>(Material, -1400, -400);
		NoiseInput->A.Expression = ScaledUV;
		NoiseInput->B.Expression = TimeScaled;

		// sin(NoiseInput) → X 오프셋 벡터 (XY 둘 다 sin)
		auto* SinExpr = NewExpr<UMaterialExpressionSine>(Material, -1200, -500);
		SinExpr->Input.Expression = NoiseInput;

		// cos(NoiseInput) → Y 오프셋
		auto* CosExpr = NewExpr<UMaterialExpressionCosine>(Material, -1200, -300);
		CosExpr->Input.Expression = NoiseInput;

		// Sin의 X 성분 / Cos의 Y 성분 추출 → 2D 오프셋
		auto* SinX = NewExpr<UMaterialExpressionComponentMask>(Material, -1000, -500);
		SinX->R = true; SinX->G = false; SinX->B = false; SinX->A = false;
		SinX->Input.Expression = SinExpr;

		auto* CosY = NewExpr<UMaterialExpressionComponentMask>(Material, -1000, -300);
		CosY->R = false; CosY->G = true; CosY->B = false; CosY->A = false;
		CosY->Input.Expression = CosExpr;

		auto* Offset2D = NewExpr<UMaterialExpressionAppendVector>(Material, -800, -400);
		Offset2D->A.Expression = SinX;
		Offset2D->B.Expression = CosY;

		// 오프셋 스케일 (기본 0.02 = 화면의 2%) * 마스크
		auto* OffsetScale = NewExpr<UMaterialExpressionConstant>(Material, -800, -200);
		OffsetScale->R = 0.02f;

		auto* OffsetScaled = NewExpr<UMaterialExpressionMultiply>(Material, -600, -300);
		OffsetScaled->A.Expression = Offset2D;
		OffsetScaled->B.Expression = OffsetScale;

		auto* OffsetMasked = NewExpr<UMaterialExpressionMultiply>(Material, -400, -300);
		OffsetMasked->A.Expression = OffsetScaled;
		OffsetMasked->B.Expression = MaskStrength;

		// =========================================================
		// Chromatic aberration: R/G/B 채널마다 다른 오프셋으로 SceneTexture 샘플
		// =========================================================
		// R 채널: 풀 오프셋 (1.5배)
		auto* RMul = NewExpr<UMaterialExpressionConstant>(Material, -400, -100);
		RMul->R = 1.5f;

		auto* OffsetR = NewExpr<UMaterialExpressionMultiply>(Material, -200, -150);
		OffsetR->A.Expression = OffsetMasked;
		OffsetR->B.Expression = RMul;

		auto* UVR = NewExpr<UMaterialExpressionAdd>(Material, 0, -200);
		UVR->A.Expression = ScreenPos;
		UVR->A.OutputIndex = 1;
		UVR->B.Expression = OffsetR;

		auto* STR = NewExpr<UMaterialExpressionSceneTexture>(Material, 200, -200);
		STR->SceneTextureId = PPI_PostProcessInput0;
		STR->Coordinates.Expression = UVR;

		auto* STR_R = NewExpr<UMaterialExpressionComponentMask>(Material, 400, -200);
		STR_R->R = true; STR_R->G = false; STR_R->B = false; STR_R->A = false;
		STR_R->Input.Expression = STR;
		STR_R->Input.OutputIndex = 0;

		// G 채널: 오프셋 1.0배
		auto* UVG = NewExpr<UMaterialExpressionAdd>(Material, 0, 0);
		UVG->A.Expression = ScreenPos;
		UVG->A.OutputIndex = 1;
		UVG->B.Expression = OffsetMasked;

		auto* STG = NewExpr<UMaterialExpressionSceneTexture>(Material, 200, 0);
		STG->SceneTextureId = PPI_PostProcessInput0;
		STG->Coordinates.Expression = UVG;

		auto* STG_G = NewExpr<UMaterialExpressionComponentMask>(Material, 400, 0);
		STG_G->R = false; STG_G->G = true; STG_G->B = false; STG_G->A = false;
		STG_G->Input.Expression = STG;
		STG_G->Input.OutputIndex = 0;

		// B 채널: 오프셋 0.5배 (반대 방향 효과용 - 양수로 약하게)
		auto* BMul = NewExpr<UMaterialExpressionConstant>(Material, -400, 50);
		BMul->R = 0.5f;

		auto* OffsetB = NewExpr<UMaterialExpressionMultiply>(Material, -200, 100);
		OffsetB->A.Expression = OffsetMasked;
		OffsetB->B.Expression = BMul;

		auto* UVB = NewExpr<UMaterialExpressionAdd>(Material, 0, 200);
		UVB->A.Expression = ScreenPos;
		UVB->A.OutputIndex = 1;
		UVB->B.Expression = OffsetB;

		auto* STB = NewExpr<UMaterialExpressionSceneTexture>(Material, 200, 200);
		STB->SceneTextureId = PPI_PostProcessInput0;
		STB->Coordinates.Expression = UVB;

		auto* STB_B = NewExpr<UMaterialExpressionComponentMask>(Material, 400, 200);
		STB_B->R = false; STB_B->G = false; STB_B->B = true; STB_B->A = false;
		STB_B->Input.Expression = STB;
		STB_B->Input.OutputIndex = 0;

		// RGB 합성
		auto* AppendRG = NewExpr<UMaterialExpressionAppendVector>(Material, 600, -100);
		AppendRG->A.Expression = STR_R;
		AppendRG->B.Expression = STG_G;

		auto* Distorted = NewExpr<UMaterialExpressionAppendVector>(Material, 800, 0);
		Distorted->A.Expression = AppendRG;
		Distorted->B.Expression = STB_B;

		// =========================================================
		// 원본 샘플 + Lerp
		// =========================================================
		auto* SceneOrig = NewExpr<UMaterialExpressionSceneTexture>(Material, 200, 500);
		SceneOrig->SceneTextureId = PPI_PostProcessInput0;
		// UV 미지정 → 현재 픽셀 UV 사용

		auto* FinalLerp = NewExpr<UMaterialExpressionLinearInterpolate>(Material, 1100, 200);
		FinalLerp->A.Expression = SceneOrig;
		FinalLerp->A.OutputIndex = 0;
		FinalLerp->B.Expression = Distorted;
		FinalLerp->Alpha.Expression = MaskStrength;

		// =========================================================
		// Emissive 연결 + 컴파일/저장
		// =========================================================
		UMaterialEditingLibrary::ConnectMaterialProperty(FinalLerp, FString(), MP_EmissiveColor);

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
			TEXT("[PhantomPP] GeneratePhantomDistortionPP %s -> %s"),
			bSaved ? TEXT("SUCCESS") : TEXT("FAILED"), *FullPath);
	}
}

static FAutoConsoleCommand GHellunaGeneratePhantomDistortionPPCmd(
	TEXT("Helluna.GeneratePhantomDistortionPP"),
	TEXT("환영 검무 공간 왜곡 PP 머티리얼(M_PP_PhantomDistortion)을 /Game/BossEvent/Materials/ 에 생성한다."),
	FConsoleCommandDelegate::CreateStatic(&HellunaPhantomPPGen::Generate)
);

#endif // WITH_EDITOR
