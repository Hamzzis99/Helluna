#pragma once

#include "CoreMinimal.h"
#include "Animation/AnimInstance.h"
#include "HellunaPreviewAnimInstance.generated.h"

/**
 * ============================================
 * 📌 HellunaPreviewAnimInstance
 * ============================================
 *
 * 캐릭터 선택 프리뷰 전용 AnimInstance
 * UAnimInstance를 직접 상속 (BaseAnimInstance의 GameplayTag 로직 불필요)
 *
 * ============================================
 * 📌 역할:
 * ============================================
 * 1. bIsHovered 변수를 AnimBP State Machine에 제공
 * 2. AnimBP(ABP_CharacterPreview)에서 트랜지션 룰 참조용
 *
 * ============================================
 * 📌 State Machine 설계 (AnimBP에서 구현):
 * ============================================
 * - [Idle] → [HoverEnter]: bIsHovered == true
 * - [HoverEnter] → [HoverIdle]: Automatic (재생 완료)
 * - [HoverEnter] → [HoverExit]: bIsHovered == false (중간 인터럽트)
 * - [HoverIdle] → [HoverExit]: bIsHovered == false
 * - [HoverExit] → [Idle]: Automatic (재생 완료)
 *
 * 📌 작성자: Gihyeon
 */
UCLASS()
class HELLUNA_API UHellunaPreviewAnimInstance : public UAnimInstance
{
	GENERATED_BODY()

public:
	// ============================================
	// 📌 AnimBP에서 참조하는 변수
	// ============================================

	/** Hover 상태 - State Machine 트랜지션 룰에서 참조 */
	UPROPERTY(VisibleAnywhere, BlueprintReadWrite, Category = "Preview (프리뷰)")
	bool bIsHovered = false;

protected:
	// ============================================
	// 📌 확장 슬롯 (필요 시 Override)
	// ============================================

	virtual void NativeInitializeAnimation() override;
	virtual void NativeUpdateAnimation(float DeltaSeconds) override;
};
