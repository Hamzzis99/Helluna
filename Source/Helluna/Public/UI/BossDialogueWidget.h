// Capstone Project Helluna

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "BossDialogueWidget.generated.h"

class UTextBlock;
class UBorder;
class UCanvasPanel;

/**
 * [BossDialogueV1] 보스 소환 시네마틱 중 화면 하단에 보스의 대사를 표시하는 자막 위젯.
 *
 * 사용법:
 *  1. 이 클래스를 부모로 WBP_BossDialogue 생성.
 *  2. 디자이너에서 최소 다음 위젯을 추가 (BindWidget 이름 일치 필수):
 *       - UTextBlock "SpeakerNameText"  (화자 이름, 상단 작게)
 *       - UTextBlock "DialogueText"     (대사 본문, 하단 크게)
 *     선택: UBorder "DialogueBackground" (배경 패널)
 *  3. BossSummonCinematicTrigger.DialogueWidgetClass 에 지정.
 *
 * 기능:
 *  - 대사 타이핑 효과 (문자 단위 revealing)
 *  - 페이드 인 / 페이드 아웃 (RenderOpacity 기반 — 애니메이션 없이도 부드럽게)
 */
UCLASS()
class HELLUNA_API UBossDialogueWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	/** 대사 재생 시작. 호출 시 자막 페이드 인 → 타이핑 효과 → (Hide 호출 전까지 유지). */
	UFUNCTION(BlueprintCallable, Category = "BossDialogue")
	void PlayDialogue(const FText& InSpeakerName, const FText& InDialogueLine);

	/** 자막 페이드 아웃 후 Remove. 외부 클리어 호출용. */
	UFUNCTION(BlueprintCallable, Category = "BossDialogue")
	void HideDialogue();

	/** 타이핑 속도 (초당 문자 수). 0 이하면 즉시 전체 표시. */
	UPROPERTY(EditDefaultsOnly, Category = "BossDialogue",
		meta = (DisplayName = "타이핑 속도 (char/sec)", ClampMin = "0.0", ClampMax = "120.0"))
	float TypingSpeed = 30.f;

	/** 페이드 인 시간 (초). */
	UPROPERTY(EditDefaultsOnly, Category = "BossDialogue",
		meta = (DisplayName = "페이드 인 (초)", ClampMin = "0.0", ClampMax = "3.0"))
	float FadeInDuration = 0.35f;

	/** 페이드 아웃 시간 (초). HideDialogue 호출 후 제거까지 걸리는 시간. */
	UPROPERTY(EditDefaultsOnly, Category = "BossDialogue",
		meta = (DisplayName = "페이드 아웃 (초)", ClampMin = "0.0", ClampMax = "3.0"))
	float FadeOutDuration = 0.25f;

protected:
	virtual void NativeConstruct() override;
	virtual void NativeTick(const FGeometry& MyGeometry, float InDeltaTime) override;
	virtual void NativeDestruct() override;

	// ===== BindWidget (디자이너에서 필수 구성) =====

	UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> SpeakerNameText = nullptr;

	UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> DialogueText = nullptr;

	UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional))
	TObjectPtr<UBorder> DialogueBackground = nullptr;

private:
	enum class EBossDialoguePhase : uint8
	{
		Idle,
		FadingIn,
		Typing,
		Holding,
		FadingOut,
	};

	EBossDialoguePhase Phase = EBossDialoguePhase::Idle;

	FString FullDialogue;
	float PhaseElapsed = 0.f;
	int32 LastRevealedChars = 0;
};
