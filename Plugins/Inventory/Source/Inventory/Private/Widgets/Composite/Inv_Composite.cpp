// Gihyeon's Inventory Project

#include "Widgets/Composite/Inv_Composite.h"
#include "Blueprint/WidgetTree.h"

void UInv_Composite::NativeOnInitialized() // 위젯 초기화 메서드
{
	Super::NativeOnInitialized(); // 부모 클래스의 초기화 메서드 호출

	WidgetTree->ForEachWidget([this](UWidget* Widget) // 모든 위젯에 대해 반복
	{
		if (UInv_CompositeBase* Composite = Cast<UInv_CompositeBase>(Widget); IsValid(Composite)) // 위젯이 UInv_CompositeBase 타입인지 확인
		{
			Children.Add(Composite); // 자식 컴포지트 배열에 추가
			Composite->Collapse(); // 자식 컴포지트를 접음
		}
	});
}

void UInv_Composite::ApplyFunction(FuncType Function) // 함수 적용 메서드
{
	for (auto& Child : Children) // 모든 자식 컴포지트에 대해 반복
	{
		Child->ApplyFunction(Function); // 각 자식 컴포지트에 함수 적용
	}
}

void UInv_Composite::Collapse() // 접기 메서드
{
	for (auto& Child : Children) // 모든 자식 컴포지트에 대해 반복
	{
		Child->Collapse(); // 모든 자식 컴포지트를 접음
	}
}
