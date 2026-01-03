
#include "Object/ResourceUsingObject/HellunaBaseResourceUsingObject.h"
#include "Components/BoxComponent.h"
#include "Components/StaticMeshComponent.h"

AHellunaBaseResourceUsingObject::AHellunaBaseResourceUsingObject()
{
	PrimaryActorTick.bCanEverTick = false;
	
	ResouceUsingMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("WeaponMesh"));
	SetRootComponent(ResouceUsingMesh);
	ResouceUsingMesh->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);

	ResouceUsingCollisionBox = CreateDefaultSubobject<UBoxComponent>(TEXT("ResouceUsingCollisionBox"));
	ResouceUsingCollisionBox->SetupAttachment(ResouceUsingMesh);
	ResouceUsingCollisionBox->OnComponentBeginOverlap.AddUniqueDynamic(this, &ThisClass::CollisionBoxBeginOverlap);

}

void AHellunaBaseResourceUsingObject::CollisionBoxBeginOverlap(UPrimitiveComponent* OverlappedComponent, AActor* OtherActor, UPrimitiveComponent* OtherComp, int32 OtherBodyIndex, bool bFromSweep, const FHitResult& SweepResult)
{

}
