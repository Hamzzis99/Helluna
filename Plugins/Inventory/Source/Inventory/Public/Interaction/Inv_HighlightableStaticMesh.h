// Gihyeon's Inventory Project

#pragma once

#include "CoreMinimal.h"
#include "Inv_Highlightable.h"
#include "Components/StaticMeshComponent.h"
#include "Inv_HighlightableStaticMesh.generated.h"

/**
 * 하이라이트 가능한 스태틱 메시 컴포넌트
 * 블루프린트에서 외곽선 굵기 조절 가능
 */
UCLASS(ClassGroup=(Inventory), meta=(BlueprintSpawnableComponent))
class INVENTORY_API UInv_HighlightableStaticMesh : public UStaticMeshComponent, public IInv_Highlightable
{
	GENERATED_BODY()
public:
	UInv_HighlightableStaticMesh();
	
	virtual void Highlight_Implementation() override;
	virtual void UnHighlight_Implementation() override;

	// ════════════════════════════════════════════════════════════════
	// 📌 외곽선 굵기 조절 (블루프린트에서 호출 가능)
	// ════════════════════════════════════════════════════════════════
	
	/**
	 * 외곽선 굵기 설정
	 * @param NewThickness - 새로운 굵기 값 (0.0 ~ 10.0 권장)
	 */
	UFUNCTION(BlueprintCallable, Category = "상호작용|하이라이트", meta = (DisplayName = "외곽선 굵기 설정"))
	void SetOutlineThickness(float NewThickness);

	/**
	 * 현재 외곽선 굵기 반환
	 */
	UFUNCTION(BlueprintPure, Category = "상호작용|하이라이트", meta = (DisplayName = "외곽선 굵기 가져오기"))
	float GetOutlineThickness() const { return OutlineThickness; }

	/**
	 * 외곽선 색상 설정
	 * @param NewColor - 새로운 외곽선 색상
	 */
	UFUNCTION(BlueprintCallable, Category = "상호작용|하이라이트", meta = (DisplayName = "외곽선 색상 설정"))
	void SetOutlineColor(FLinearColor NewColor);

	/**
	 * 현재 외곽선 색상 반환
	 */
	UFUNCTION(BlueprintPure, Category = "상호작용|하이라이트", meta = (DisplayName = "외곽선 색상 가져오기"))
	FLinearColor GetOutlineColor() const { return OutlineColor; }

private:
	// 원본 머티리얼 (에디터에서 설정)
	UPROPERTY(EditAnywhere, Category = "상호작용|하이라이트", meta = (DisplayName = "하이라이트 머티리얼", Tooltip = "하이라이트 시 적용할 외곽선 머티리얼. 런타임에 동적 머티리얼 인스턴스로 생성됩니다."))
	TObjectPtr<UMaterialInterface> HighlightMaterial;

	// 동적 머티리얼 인스턴스 (런타임 생성)
	UPROPERTY()
	TObjectPtr<UMaterialInstanceDynamic> DynamicHighlightMaterial;

	// ════════════════════════════════════════════════════════════════
	// 📌 블루프린트에서 설정 가능한 파라미터들
	// ════════════════════════════════════════════════════════════════
	
	// 외곽선 굵기 (머티리얼의 "OutlineThickness" 파라미터에 전달됨)
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "상호작용|하이라이트",
		meta = (DisplayName = "외곽선 굵기", Tooltip = "하이라이트 외곽선의 두께. 0.0~10.0 범위로 조절 가능합니다.", ClampMin = "0.0", ClampMax = "10.0", AllowPrivateAccess = "true"))
	float OutlineThickness = 1.0f;

	// 외곽선 색상 (머티리얼의 "OutlineColor" 파라미터에 전달됨)
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "상호작용|하이라이트",
		meta = (DisplayName = "외곽선 색상", Tooltip = "하이라이트 외곽선의 색상. 기본값은 황금색입니다.", AllowPrivateAccess = "true"))
	FLinearColor OutlineColor = FLinearColor(1.0f, 0.8f, 0.0f, 1.0f); // 기본: 황금색

	// 머티리얼 파라미터 이름 (머티리얼 그래프와 일치해야 함)
	UPROPERTY(EditAnywhere, AdvancedDisplay, Category = "상호작용|하이라이트",
		meta = (DisplayName = "굵기 파라미터 이름", Tooltip = "머티리얼 그래프의 외곽선 굵기 스칼라 파라미터 이름. 머티리얼과 일치해야 합니다."))
	FName ThicknessParameterName = TEXT("OutlineThickness");

	UPROPERTY(EditAnywhere, AdvancedDisplay, Category = "상호작용|하이라이트",
		meta = (DisplayName = "색상 파라미터 이름", Tooltip = "머티리얼 그래프의 외곽선 색상 벡터 파라미터 이름. 머티리얼과 일치해야 합니다."))
	FName ColorParameterName = TEXT("OutlineColor");

	// 동적 머티리얼 생성 및 파라미터 적용
	void CreateDynamicMaterial();
	void ApplyMaterialParameters();
};
