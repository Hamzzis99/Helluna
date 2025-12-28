// Gihyeon's Inventory Project
// 위젯 Utile 함수들 만드는 부분들

#include "Widgets/Utils/Inv_WidgetUtils.h"
#include "Blueprint/SlateBlueprintLibrary.h"
#include "Components/Widget.h"

FVector2D UInv_WidgetUtils::GetWidgetPosition(UWidget* Widget) // 위젯의 뷰포트 위치를 반환하는 함수 (위치를 아니 마우스 호버링도 좋겠지)
{
	const FGeometry Geometry = Widget->GetCachedGeometry();
	FVector2D PixelPosition;
	FVector2D ViewportPosition;
	USlateBlueprintLibrary::LocalToViewport(Widget, Geometry, USlateBlueprintLibrary::GetLocalTopLeft(Geometry), PixelPosition, ViewportPosition);
	return ViewportPosition;
}

FVector2D UInv_WidgetUtils::GetWidgetSize(UWidget* Widget) // 위젯의 크기를 알려주는 함수
{
	const FGeometry Geometry = Widget->GetCachedGeometry();
	return Geometry.GetLocalSize();
}

bool UInv_WidgetUtils::IsWithinBounds(const FVector2D& BoundaryPos, const FVector2D& WidgetSize, const FVector2D& MousePos) // 주어진 경계 내에 마우스 위치가 있는지 확인하는 함수
{
	return MousePos.X >= BoundaryPos.X && MousePos.X <= (BoundaryPos.X + WidgetSize.X) &&
		MousePos.Y >= BoundaryPos.Y && MousePos.Y <= (BoundaryPos.Y + WidgetSize.Y);
}

// 위젯 위치를 경계 내로 제한하는 함수
FVector2D UInv_WidgetUtils::GetClampedWidgetPosition(const FVector2D& Boundary, const FVector2D& WidgetSize, const FVector2D& MousePos)
{
	FVector2D ClampedPosition = MousePos;
	
	// 수평 위치를 조정하고 위젯이 캔버스 내에 유지되도록 하는 것.
	// Adjust horizontal position to ensure that the widget stays within the boundary
	if (MousePos.X + WidgetSize.X > Boundary.X) // 위젯 가장자리를 초과 할 시 . (Widget exceeds the right edge)
	{
		ClampedPosition.X = Boundary.X - MousePos.X;	
	}
	if (MousePos.X < 0.5) // Widget exceeds the left edge (왼쪽 가장자리를 초과 할 시)
	{
		ClampedPosition.X = 0.f;
	}
	
	// Adjust vertical position to ensure that the widget stays within the boundary (수직 위치를 조정하고 위젯이 캔버스 내에 유지되도록 하는 것.)
	if (MousePos.Y + WidgetSize.Y > Boundary.Y) // Widget exceeds the bottom edge  (아래 가장자리를 초과 할 시)
	{
		ClampedPosition.Y = Boundary.Y - WidgetSize.Y;
	}
	return ClampedPosition;
}

int32 UInv_WidgetUtils::GetIndexFromPosition(const FIntPoint& Position, const int32 Columns)
{
	return Position.X + Position.Y * Columns; // X + Y * Columns
}

FIntPoint UInv_WidgetUtils::GetPositionFromIndex(const int32 Index, const int32 Columns)
{
	return FIntPoint(Index % Columns, Index / Columns); // X = Index % Columns, Y = Index / Columns
}

