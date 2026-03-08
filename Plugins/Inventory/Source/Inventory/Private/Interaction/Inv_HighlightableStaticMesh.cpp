// Gihyeon's Inventory Project


#include "Interaction/Inv_HighlightableStaticMesh.h"
#include "Inventory.h"
#include "Materials/MaterialInstanceDynamic.h"

UInv_HighlightableStaticMesh::UInv_HighlightableStaticMesh()
{
	// 기본값 설정
	OutlineThickness = 1.0f;
	OutlineColor = FLinearColor(1.0f, 0.8f, 0.0f, 1.0f); // 황금색
	ThicknessParameterName = TEXT("OutlineThickness");
	ColorParameterName = TEXT("OutlineColor");
}

void UInv_HighlightableStaticMesh::Highlight_Implementation()
{
	// 동적 머티리얼이 없으면 생성
	if (!IsValid(DynamicHighlightMaterial))
	{
		CreateDynamicMaterial();
	}

	// 파라미터 적용 후 오버레이 설정
	if (IsValid(DynamicHighlightMaterial))
	{
		ApplyMaterialParameters();
		SetOverlayMaterial(DynamicHighlightMaterial);
	}
	else if (IsValid(HighlightMaterial))
	{
		// 폴백: 동적 머티리얼 생성 실패 시 원본 사용
		SetOverlayMaterial(HighlightMaterial);
	}
}

void UInv_HighlightableStaticMesh::UnHighlight_Implementation()
{
	SetOverlayMaterial(nullptr);
}

void UInv_HighlightableStaticMesh::SetOutlineThickness(float NewThickness)
{
	OutlineThickness = FMath::Clamp(NewThickness, 0.0f, 10.0f);
	
	// 이미 하이라이트 중이면 즉시 적용
	if (IsValid(DynamicHighlightMaterial) && GetOverlayMaterial() != nullptr)
	{
		DynamicHighlightMaterial->SetScalarParameterValue(ThicknessParameterName, OutlineThickness);
	}
}

void UInv_HighlightableStaticMesh::SetOutlineColor(FLinearColor NewColor)
{
	OutlineColor = NewColor;
	
	// 이미 하이라이트 중이면 즉시 적용
	if (IsValid(DynamicHighlightMaterial) && GetOverlayMaterial() != nullptr)
	{
		DynamicHighlightMaterial->SetVectorParameterValue(ColorParameterName, OutlineColor);
	}
}

void UInv_HighlightableStaticMesh::CreateDynamicMaterial()
{
	if (!IsValid(HighlightMaterial))
	{
#if INV_DEBUG_INTERACTION
		UE_LOG(LogTemp, Warning, TEXT("[HighlightMesh] HighlightMaterial이 설정되지 않았습니다!"));
#endif
		return;
	}

	// 동적 머티리얼 인스턴스 생성
	DynamicHighlightMaterial = UMaterialInstanceDynamic::Create(HighlightMaterial, this);

	if (!IsValid(DynamicHighlightMaterial))
	{
#if INV_DEBUG_INTERACTION
		UE_LOG(LogTemp, Error, TEXT("[HighlightMesh] DynamicMaterial 생성 실패!"));
#endif
	}
}

void UInv_HighlightableStaticMesh::ApplyMaterialParameters()
{
	if (!IsValid(DynamicHighlightMaterial)) return;

	// 굵기 파라미터 적용
	DynamicHighlightMaterial->SetScalarParameterValue(ThicknessParameterName, OutlineThickness);
	
	// 색상 파라미터 적용
	DynamicHighlightMaterial->SetVectorParameterValue(ColorParameterName, OutlineColor);
}

