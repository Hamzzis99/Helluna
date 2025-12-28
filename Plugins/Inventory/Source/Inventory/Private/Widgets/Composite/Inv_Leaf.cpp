// Gihyeon's Inventory Project
// HUD의 잎사귀들을 만드는 건데. 트리 구조 기억하지 원소를 Leaf라고 부르잖아. 그거 만드는거임.

#include "Widgets/Composite/Inv_Leaf.h"

void UInv_Leaf::ApplyFunction(FuncType Function) // 함수 적용 메서드
{
	Function(this); 
	
}
