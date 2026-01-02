// Fill out your copyright notice in the Description page of Project Settings.


#include "GameMode/TestLoseActor.h"

#include "Components/BoxComponent.h"
#include "Components/StaticMeshComponent.h"
#include "GameFramework/Pawn.h"
#include "GameMode/HellunaDefenseGameMode.h"

ATestLoseActor::ATestLoseActor()
{
	PrimaryActorTick.bCanEverTick = false;



	// 판정용 Box
	Box = CreateDefaultSubobject<UBoxComponent>(TEXT("Box"));
	SetRootComponent(Box);
	Box->SetBoxExtent(FVector(80.f, 80.f, 80.f));
	Box->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	Box->SetCollisionResponseToAllChannels(ECR_Ignore);
	Box->SetCollisionResponseToChannel(ECC_Pawn, ECR_Overlap);
	Box->OnComponentBeginOverlap.AddDynamic(this, &ThisClass::OnBoxBeginOverlap);

	// 시각화용 메쉬
	VisualMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("VisualMesh"));
	VisualMesh->SetupAttachment(Box);

	// 메쉬는 "보이기만" 하게: 콜리전 끄기 (판정은 Box가 담당)
	VisualMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	VisualMesh->SetGenerateOverlapEvents(false);
	VisualMesh->CastShadow = false; // 테스트용이면 그림자 꺼도 편함 (원하면 삭제)
}

void ATestLoseActor::OnBoxBeginOverlap(UPrimitiveComponent* OverlappedComponent,
	AActor* OtherActor, UPrimitiveComponent* OtherComp,
	int32 OtherBodyIndex, bool bFromSweep, const FHitResult& SweepResult)
{
	if (!HasAuthority())
		return;

	if (!OtherActor)
		return;

	if (!OtherActor->IsA<APawn>())
		return;

	ExecuteAction();
}	

void ATestLoseActor::ExecuteAction()
{
	AHellunaDefenseGameMode* GM = GetWorld() ? GetWorld()->GetAuthGameMode<AHellunaDefenseGameMode>() : nullptr;
	if (!GM)
		return;

	switch (Action)
	{
	case EDefenseTriggerAction::Lose:
		GM->TriggerLose();
		break;

	case EDefenseTriggerAction::SummonBoss:
		// 테스트용이면 BossReady 켜는 걸로 충분 (즉시 소환이 필요하면 GM에 ForceSummonBoss 추가)
		GM->SetBossReady(true);
		break;

	default:
		break;
	}
}