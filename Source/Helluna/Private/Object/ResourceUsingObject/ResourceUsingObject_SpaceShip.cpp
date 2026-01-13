// Fill out your copyright notice in the Description page of Project Settings.


#include "Object/ResourceUsingObject/ResourceUsingObject_SpaceShip.h"
#include "Character/HellunaHeroCharacter.h"
#include "AbilitySystem/HellunaAbilitySystemComponent.h"
#include "HellunaGameplayTags.h"
#include "Components/BoxComponent.h"
#include "Net/UnrealNetwork.h"
#include "GameMode/HellunaDefenseGameMode.h"
#include "GameMode/HellunaDefenseGameState.h"

#include "debughelper.h"


// 박스 범위내에 들어올시 수리 가능 범위 능력 활성화(UI)
void AResourceUsingObject_SpaceShip::CollisionBoxBeginOverlap(UPrimitiveComponent* OverlappedComponent, AActor* OtherActor, UPrimitiveComponent* OtherComp, int32 OtherBodyIndex, bool bFromSweep, const FHitResult& SweepResult)
{
	TArray<AActor*> Overlaps;
	ResouceUsingCollisionBox->GetOverlappingActors(Overlaps);

	if (AHellunaHeroCharacter* OverlappedHeroCharacter = Cast<AHellunaHeroCharacter>(OtherActor))
	{
		OverlappedHeroCharacter->GetHellunaAbilitySystemComponent()->TryActivateAbilityByTag(HellunaGameplayTags::Player_Ability_InRepair);
	}

}

// 박스 범위내에서 벗어날시 수리 가능 범위 능력 비활성화(UI)
void AResourceUsingObject_SpaceShip::CollisionBoxEndOverlap(UPrimitiveComponent* OverlappedComponent, AActor* OtherActor, UPrimitiveComponent* OtherComp, int32 OtherBodyIndex)
{
	if (AHellunaHeroCharacter* OverlappedHeroCharacter = Cast<AHellunaHeroCharacter>(OtherActor))
	{
		OverlappedHeroCharacter->GetHellunaAbilitySystemComponent()->CancelAbilityByTag(HellunaGameplayTags::Player_Ability_InRepair);
	}

}

//자원량을 더하는 함수
bool AResourceUsingObject_SpaceShip::AddRepairResource(int32 Amount)
{
	if (!HasAuthority())
		return false;
	if (Amount <= 0) return false;
	if (IsRepaired()) return false;

	CurrentResource = FMath::Clamp(CurrentResource + Amount, 0, NeedResource);

	OnRepairProgressChanged.Broadcast(CurrentResource, NeedResource);

	// 수리 완료 체크!
	if (IsRepaired())
	{
		UE_LOG(LogTemp, Warning, TEXT("=== 🎉 SpaceShip 수리 완료! CurrentResource: %d / NeedResource: %d ==="), CurrentResource, NeedResource);
		OnRepairCompleted();
	}

	return true;
}

// UI위해 수리도를 퍼센트로 변환
float AResourceUsingObject_SpaceShip::GetRepairPercent() const
{
	return NeedResource > 0 ? (float)CurrentResource / (float)NeedResource : 1.f;
}

// 수리 완료 여부
bool AResourceUsingObject_SpaceShip::IsRepaired() const
{ 
	return CurrentResource >= NeedResource;
}

// 현재 수리량이 변경이 신호를 주는 함수
void AResourceUsingObject_SpaceShip::OnRep_CurrentResource()
{
	OnRepairProgressChanged.Broadcast(CurrentResource, NeedResource);
}

//서버에서 수리량이 바뀌면 클라이언트 갱신
void AResourceUsingObject_SpaceShip::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	DOREPLIFETIME(AResourceUsingObject_SpaceShip, CurrentResource);
}

//생성자 복제(서버에서 생성시 클라에서도 생성)
AResourceUsingObject_SpaceShip::AResourceUsingObject_SpaceShip()
{
	bReplicates = true;
	bAlwaysRelevant = true;
}

// 게임 시작시 게임 상태에 우주선 등록
void AResourceUsingObject_SpaceShip::BeginPlay()
{
	Super::BeginPlay();

	if (!HasAuthority())
		return;

	if (AHellunaDefenseGameState* GS = GetWorld()->GetGameState<AHellunaDefenseGameState>())
	{
		GS->RegisterSpaceShip(this);
	}
}

// 새로 추가: 수리 완료 처리
void AResourceUsingObject_SpaceShip::OnRepairCompleted_Implementation()
{
	UE_LOG(LogTemp, Warning, TEXT("=== [OnRepairCompleted] 수리 완료 이벤트 실행 ==="));

	// ⭐ 1. 델리게이트 브로드캐스트 (UI에서 승리 화면 표시)
	OnRepairCompleted_Delegate.Broadcast();
	UE_LOG(LogTemp, Warning, TEXT("  📢 OnRepairCompleted_Delegate 브로드캐스트!"));

	// ⭐ 2. GameMode에 알림 (보스 소환)
	if (AHellunaDefenseGameMode* GameMode = GetWorld()->GetAuthGameMode<AHellunaDefenseGameMode>())
	{
		UE_LOG(LogTemp, Warning, TEXT("  🔥 GameMode에 알림: 보스 소환 준비!"));
		GameMode->SetBossReady(true);  // ← 기존 함수 사용! (즉시 보스 소환)
	}

	UE_LOG(LogTemp, Warning, TEXT("=== [OnRepairCompleted] 완료 ==="));
}