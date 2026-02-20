// Gihyeon's Inventory Project


#include "Widgets/CharacterDisplay/Inv_CharacterDisplay.h"

#include "Blueprint/WidgetLayoutLibrary.h"
#include "Interaction/ProxyMesh/Inv_ProxyMesh.h"
#include "Kismet/GameplayStatics.h"

FReply UInv_CharacterDisplay::NativeOnMouseButtonDown(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	CurrentPosition = UWidgetLayoutLibrary::GetMousePositionOnViewport(GetOwningPlayer());
	LastPosition = CurrentPosition;
	
	bIsDragging = true;
	return FReply::Handled();
}

FReply UInv_CharacterDisplay::NativeOnMouseButtonUp(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent)
{
	bIsDragging = false;
	return Super::NativeOnMouseButtonUp(InGeometry, InMouseEvent);
}

void UInv_CharacterDisplay::NativeOnMouseLeave(const FPointerEvent& InMouseEvent)
{
	Super::NativeOnMouseLeave(InMouseEvent);
	bIsDragging = false;
}

void UInv_CharacterDisplay::NativeOnInitialized()
{
	Super::NativeOnInitialized();

	TArray<AActor*> Actors;
	UGameplayStatics::GetAllActorsOfClass(this, AInv_ProxyMesh::StaticClass(), Actors);

	if (!Actors.IsValidIndex(0)) return;

	AInv_ProxyMesh* ProxyMesh = Cast<AInv_ProxyMesh>(Actors[0]);
	if (!IsValid(ProxyMesh)) return;

	Mesh = ProxyMesh->GetMesh();
}

void UInv_CharacterDisplay::NativeTick(const FGeometry& MyGeometry, float InDeltaTime)
{
	Super::NativeTick(MyGeometry, InDeltaTime);
	if (!bIsDragging) return;

	LastPosition = CurrentPosition;
	CurrentPosition = UWidgetLayoutLibrary::GetMousePositionOnViewport(GetOwningPlayer());

	const float HorizontalDelta = LastPosition.X - CurrentPosition.X;

	if (!Mesh.IsValid()) return;
	Mesh->AddRelativeRotation(FRotator(0.f, HorizontalDelta, 0.f));
}