// ════════════════════════════════════════════════════════════════════════════════
// HellunaLobbyCameraAnchor.h
// ════════════════════════════════════════════════════════════════════════════════
//
// 로비 배경 서브레벨에 배치하는 카메라 앵커 액터
// 에디터에서 Pilot Camera로 카메라 뷰를 시각적으로 확인 가능
// TabIndex + SlotIndex 조합으로 탭별 다중 카메라 지원
//
// 작성자: Gihyeon (Claude Code 보조)
// ════════════════════════════════════════════════════════════════════════════════

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "HellunaLobbyCameraAnchor.generated.h"

class UCameraComponent;

UCLASS()
class HELLUNA_API AHellunaLobbyCameraAnchor : public AActor
{
	GENERATED_BODY()

public:
	AHellunaLobbyCameraAnchor();

	// ════════════════════════════════════════════════════════════════
	// Getter
	// ════════════════════════════════════════════════════════════════

	int32 GetTabIndex() const { return TabIndex; }
	int32 GetSlotIndex() const { return SlotIndex; }
	float GetFOV() const { return FOV; }
	UCameraComponent* GetCameraComponent() const { return CameraComp; }

protected:
	/** 에디터에서 카메라 뷰 프리뷰용 (Pilot Camera 가능) */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "로비|카메라앵커",
		meta = (DisplayName = "Camera Component (카메라 컴포넌트)"))
	TObjectPtr<UCameraComponent> CameraComp;

	/** 어떤 탭용인지 (0=Play, 2=Character) — LoadBackgroundForTab과 동일한 인덱스 체계 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "로비|카메라앵커",
		meta = (DisplayName = "Tab Index (탭 인덱스)", Tooltip = "0=Play, 2=Character"))
	int32 TabIndex = 0;

	/** 같은 탭 내에서의 슬롯 번호 (0=기본 뷰, 1~N=추가 뷰)
	 *  스킨/무기 전환 등으로 카메라 앵글을 바꿀 때 사용 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "로비|카메라앵커",
		meta = (DisplayName = "Slot Index (슬롯 인덱스)", Tooltip = "같은 탭 내 카메라 순번. 0=기본 뷰"))
	int32 SlotIndex = 0;

	/** 카메라 시야각 (에디터에서 변경 시 CameraComp에 자동 동기화) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "로비|카메라앵커",
		meta = (DisplayName = "FOV (시야각)"))
	float FOV = 50.f;

#if WITH_EDITOR
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
#endif
};
