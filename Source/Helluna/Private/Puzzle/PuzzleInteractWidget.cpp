// Source/Helluna/Private/Puzzle/PuzzleInteractWidget.cpp

#include "Puzzle/PuzzleInteractWidget.h"

UPuzzleInteractWidget::UPuzzleInteractWidget(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	DefaultActionText = FText::FromString(TEXT("HOLD to hack"));
	CompletedActionText = FText::FromString(TEXT("ACTIVATED"));
}
