// Capstone Project Helluna

#include "BossEvent/TimeDistortionMaterialGenerator.h"

#if WITH_EDITOR
#include "Materials/Material.h"
#include "Materials/MaterialExpressionCustom.h"
#include "Materials/MaterialExpressionSceneTexture.h"
#include "Materials/MaterialExpressionScalarParameter.h"
#include "Materials/MaterialExpressionVectorParameter.h"
#include "Materials/MaterialExpressionComponentMask.h"
#include "Materials/MaterialExpressionScreenPosition.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "UObject/Package.h"
#include "UObject/SavePackage.h"

// 콘솔 명령
static FAutoConsoleCommand CmdGeneratePP(
	TEXT("Helluna.GenerateTimeDistortionPP"),
	TEXT("TimeDistortion PP Material 자동 생성"),
	FConsoleCommandDelegate::CreateLambda([]()
	{
		UMaterialInterface* Result = UTimeDistortionMaterialGenerator::GenerateDesaturationMaterial();
		if (Result)
		{
			UE_LOG(LogTemp, Log, TEXT("[TDZ] Material ready: %s"), *Result->GetPathName());
		}
	})
);
#endif // WITH_EDITOR

UMaterialInterface* UTimeDistortionMaterialGenerator::GenerateDesaturationMaterial()
{
#if WITH_EDITOR
	const FString AssetPath = TEXT("/Game/BossEvent/Materials/M_PP_TimeDistortion");
	const FString AssetName = TEXT("M_PP_TimeDistortion");

	if (UMaterialInterface* Existing = LoadObject<UMaterialInterface>(nullptr, *AssetPath))
	{
		UE_LOG(LogTemp, Warning, TEXT("[TDZ] 이미 존재: %s — 삭제 후 다시 실행하세요."), *AssetPath);
		return Existing;
	}

	UPackage* Package = CreatePackage(*AssetPath);
	if (!Package) return nullptr;

	// ─────────────────────────────────────────────────────────────
	// 머티리얼 기본 설정
	// ─────────────────────────────────────────────────────────────
	UMaterial* Mat = NewObject<UMaterial>(Package, *AssetName, RF_Public | RF_Standalone);
	Mat->MaterialDomain = MD_PostProcess;
	Mat->BlendableLocation = BL_AfterTonemapping;

	auto& Exprs = Mat->GetExpressionCollection().Expressions;

	auto SetPos = [](UMaterialExpression* E, int32 X, int32 Y)
	{
		E->MaterialExpressionEditorX = X;
		E->MaterialExpressionEditorY = Y;
	};

	// =============================================================
	// 입력 노드들
	// =============================================================

	// 1. SceneTexture: PostProcessInput0 (원본 색상)
	auto* SceneColor = NewObject<UMaterialExpressionSceneTexture>(Mat);
	SceneColor->SceneTextureId = PPI_PostProcessInput0;
	SetPos(SceneColor, -600, -100);
	Exprs.Add(SceneColor);

	// 2. SceneTexture: SceneDepth (선형 깊이)
	auto* SceneDepth = NewObject<UMaterialExpressionSceneTexture>(Mat);
	SceneDepth->SceneTextureId = PPI_SceneDepth;
	SetPos(SceneDepth, -600, -250);
	Exprs.Add(SceneDepth);

	// 3. SceneTexture: CustomDepth (CustomDepth 오브젝트 깊이)
	auto* CustomDepthTex = NewObject<UMaterialExpressionSceneTexture>(Mat);
	CustomDepthTex->SceneTextureId = PPI_CustomDepth;
	SetPos(CustomDepthTex, -600, -400);
	Exprs.Add(CustomDepthTex);

	// 4. ScreenPosition (뷰포트 UV 0~1)
	auto* ScreenUV = NewObject<UMaterialExpressionScreenPosition>(Mat);
	SetPos(ScreenUV, -600, -550);
	Exprs.Add(ScreenUV);

	// 5. VectorParameter: ZoneCenter → RGB 추출
	auto* ZoneCenter = NewObject<UMaterialExpressionVectorParameter>(Mat);
	ZoneCenter->ParameterName = FName("ZoneCenter");
	ZoneCenter->DefaultValue = FLinearColor(0.f, 0.f, 0.f, 0.f);
	SetPos(ZoneCenter, -600, -700);
	Exprs.Add(ZoneCenter);

	auto* CenterRGB = NewObject<UMaterialExpressionComponentMask>(Mat);
	CenterRGB->Input.Expression = ZoneCenter;
	CenterRGB->R = true;
	CenterRGB->G = true;
	CenterRGB->B = true;
	CenterRGB->A = false;
	SetPos(CenterRGB, -400, -700);
	Exprs.Add(CenterRGB);

	// 6. ScalarParameter: ZoneRadius
	auto* ZoneRadius = NewObject<UMaterialExpressionScalarParameter>(Mat);
	ZoneRadius->ParameterName = FName("ZoneRadius");
	ZoneRadius->DefaultValue = 1000.f;
	SetPos(ZoneRadius, -600, -850);
	Exprs.Add(ZoneRadius);

	// 7. ScalarParameter: Strength
	auto* StrengthParam = NewObject<UMaterialExpressionScalarParameter>(Mat);
	StrengthParam->ParameterName = FName("Strength");
	StrengthParam->DefaultValue = 0.8f;
	SetPos(StrengthParam, -600, -1000);
	Exprs.Add(StrengthParam);

	// =============================================================
	// 핵심: 하나의 Custom HLSL 노드에서 전부 처리
	// =============================================================
	auto* MainNode = NewObject<UMaterialExpressionCustom>(Mat);
	MainNode->Description = TEXT("TimeDistortion_AllInOne");
	MainNode->OutputType = CMOT_Float3;

	MainNode->Code = TEXT(
		"// ── 1. 월드 좌표 복원 (TranslatedWorld 공간) ──\n"
		"float2 SP = UV * float2(2.0, -2.0) + float2(-1.0, 1.0);\n"
		"float D = Depth;\n"
		"float4 TW = mul(float4(SP * D, D, 1.0),\n"
		"                ResolvedView.ScreenToTranslatedWorld);\n"
		"float3 PixelTW = TW.xyz / TW.w;\n"
		"\n"
		"// ZoneCenter → TranslatedWorld 공간 변환\n"
		"float3 CenterTW = Center\n"
		"    + LWCHackToFloat(ResolvedView.PreViewTranslation);\n"
		"\n"
		"// ── 2. 존 마스크: 0=안쪽(회색), 1=바깥(원본) ──\n"
		"float Dist = distance(PixelTW, CenterTW);\n"
		"float Ratio = Dist / max(Radius, 1.0);\n"
		"float ZoneMask = smoothstep(0.85, 1.0, Ratio);\n"
		"\n"
		"// ── 3. 제외 마스크: CustomDepth vs SceneDepth 비교 ──\n"
		"// bRenderCustomDepth=true인 오브젝트: CD ≈ Depth (차이 작음)\n"
		"// 일반 오브젝트: CD = 원거리 클리어값 (차이 매우 큼)\n"
		"float RelDiff = abs(CD - D) / max(D, 1.0);\n"
		"float ExcludeMask = (RelDiff < 0.05) ? 1.0 : 0.0;\n"
		"\n"
		"// ── 4. 최종 마스크 ──\n"
		"float Mask = max(ZoneMask, ExcludeMask);\n"
		"\n"
		"// ── 5. 채도 제거 ──\n"
		"float3 Color = SceneColor.rgb;\n"
		"float Luma = dot(Color, float3(0.2126, 0.7152, 0.0722));\n"
		"float3 Grey = float3(Luma, Luma, Luma);\n"
		"float3 Desaturated = lerp(Color, Grey, Strength);\n"
		"\n"
		"// ── 6. 블렌딩 ──\n"
		"return lerp(Desaturated, Color, Mask);\n"
	);

	// 입력 7개 연결
	MainNode->Inputs.Empty();
	{
		FCustomInput In;

		In.InputName = FName("SceneColor");
		In.Input.Expression = SceneColor;
		In.Input.OutputIndex = 0;
		MainNode->Inputs.Add(In);

		In.InputName = FName("Depth");
		In.Input.Expression = SceneDepth;
		In.Input.OutputIndex = 0;
		MainNode->Inputs.Add(In);

		In.InputName = FName("CD");
		In.Input.Expression = CustomDepthTex;
		In.Input.OutputIndex = 0;
		MainNode->Inputs.Add(In);

		In.InputName = FName("UV");
		In.Input.Expression = ScreenUV;
		In.Input.OutputIndex = 0;
		MainNode->Inputs.Add(In);

		In.InputName = FName("Center");
		In.Input.Expression = CenterRGB;
		In.Input.OutputIndex = 0;
		MainNode->Inputs.Add(In);

		In.InputName = FName("Radius");
		In.Input.Expression = ZoneRadius;
		In.Input.OutputIndex = 0;
		MainNode->Inputs.Add(In);

		In.InputName = FName("Strength");
		In.Input.Expression = StrengthParam;
		In.Input.OutputIndex = 0;
		MainNode->Inputs.Add(In);
	}

	SetPos(MainNode, 0, -300);
	Exprs.Add(MainNode);

	// =============================================================
	// Emissive Color 출력
	// =============================================================
	if (UMaterialEditorOnlyData* EditorData = Mat->GetEditorOnlyData())
	{
		EditorData->EmissiveColor.Expression = MainNode;
		EditorData->EmissiveColor.OutputIndex = 0;
	}

	// =============================================================
	// 컴파일 & 저장
	// =============================================================
	Mat->PreEditChange(nullptr);
	Mat->PostEditChange();

	FAssetRegistryModule::AssetCreated(Mat);
	Package->MarkPackageDirty();

	const FString PackageFilename = FPackageName::LongPackageNameToFilename(
		AssetPath, FPackageName::GetAssetPackageExtension());

	FSavePackageArgs SaveArgs;
	SaveArgs.TopLevelFlags = RF_Public | RF_Standalone;
	UPackage::SavePackage(Package, Mat, *PackageFilename, SaveArgs);

	UE_LOG(LogTemp, Log, TEXT("[TDZ] PP Material 생성 완료: %s"), *AssetPath);
	return Mat;

#else
	UE_LOG(LogTemp, Warning, TEXT("[TDZ] Editor 전용 기능입니다."));
	return nullptr;
#endif
}
