// Gihyeon's Inventory Project
// Leaf Image란? UI에서 딸려오는 이미지 부분이다. 트리 구조라 생각하자.

#include "Widgets/Composite/Inv_Leaf_Image.h"

#include "Components/Image.h"
#include "Components/SizeBox.h"

void UInv_Leaf_Image::SetImage(UTexture2D* Texture) const
{
	Image_Icon->SetBrushFromTexture(Texture);
}

void UInv_Leaf_Image::SetBoxSize(const FVector2D& Size) const
{
	SizeBox_Icon->SetWidthOverride(Size.X);
	SizeBox_Icon->SetHeightOverride(Size.Y);
}

void UInv_Leaf_Image::SetImageSize(const FVector2D& Size) const
{
	Image_Icon->SetDesiredSizeOverride(Size);	
}

FVector2D UInv_Leaf_Image::GetImageSize() const
{
	return Image_Icon->GetDesiredSize();
}
