// Gihyeon's Inventory Project


#include "Widgets/Composite/Inv_Leaf_Text.h"

#include "Components/Textblock.h"

void UInv_Leaf_Text::SetText(const FText& Text) const
{
	Text_LeafText->SetText(Text);
}

void UInv_Leaf_Text::NativePreConstruct()
{
	Super::NativePreConstruct();
	
	//폰트사이즈 지정하기
	FSlateFontInfo FontInfo = Text_LeafText->GetFont();
	FontInfo.Size = FontSize;
	
	Text_LeafText->SetFont(FontInfo);
}
