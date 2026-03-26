// Gihyeon's Inventory Project
// @author 김기현

#include "Building/Actor/Inv_BuildableActor.h"
#include "Inventory.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"

AInv_BuildableActor::AInv_BuildableActor()
{
	// 부모(Inv_BuildingActor)에서 BuildingMesh, 리플리케이션 등 이미 설정됨
}

UStaticMesh* AInv_BuildableActor::GetEffectivePreviewMesh() const
{
	// 1. 명시적 PreviewMesh가 있으면 사용
	if (IsValid(PreviewMesh))
	{
		UE_LOG(LogTemp, Warning, TEXT("[BuildableActor] PreviewMesh=%s (Source=명시적)"), *PreviewMesh->GetName());
		return PreviewMesh;
	}

	// 2. BuildingMesh 컴포넌트에서 가져오기 (부모 클래스의 멤버)
	if (IsValid(BuildingMesh))
	{
		UStaticMesh* Mesh = BuildingMesh->GetStaticMesh();
		if (IsValid(Mesh))
		{
			UE_LOG(LogTemp, Warning, TEXT("[BuildableActor] PreviewMesh=%s (Source=BuildingMesh)"), *Mesh->GetName());
			return Mesh;
		}
	}

	UE_LOG(LogTemp, Warning, TEXT("[BuildableActor] PreviewMesh=NULL (Source=없음)"));
	return nullptr;
}
