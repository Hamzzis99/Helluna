// Source/Helluna/Public/Puzzle/PuzzleInteractWidget.h

#pragma once

#include "CoreMinimal.h"
#include "Widgets/HoldInteractWidget.h"
#include "PuzzleInteractWidget.generated.h"

/**
 * 퍼즐 큐브 전용 3D 상호작용 위젯
 * UHoldInteractWidget 상속 — DefaultActionText를 "HOLD to hack"으로 설정.
 *
 * [보스전 로드맵]
 * 보스 버전에서도 UHoldInteractWidget을 상속한 별도 BP 위젯을 사용.
 * PuzzleShieldComponent의 WidgetComponent에 부착하면 됨.
 */
UCLASS()
class HELLUNA_API UPuzzleInteractWidget : public UHoldInteractWidget
{
	GENERATED_BODY()

public:
	UPuzzleInteractWidget(const FObjectInitializer& ObjectInitializer);
};
