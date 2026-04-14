// Fill out your copyright notice in the Description page of Project Settings.


#include "Object/ResourceUsingObject/ResourceUsingObject_SpaceShip.h"
#include "AI/SpaceShipAttackSlotManager.h"
#include "Character/HellunaHeroCharacter.h"
#include "AbilitySystem/HellunaAbilitySystemComponent.h"
#include "HellunaGameplayTags.h"
#include "Components/BoxComponent.h"
#include "Net/UnrealNetwork.h"
#include "GameMode/HellunaDefenseGameMode.h"
#include "GameMode/HellunaDefenseGameState.h"
#include "NavigationInvokerComponent.h"
#include "NavModifierComponent.h"
#include "NavigationSystem.h"
#include "NavMesh/RecastNavMesh.h"
#include "NavAreas/NavArea_Null.h"
#include "Components/DynamicMeshComponent.h"

#include "debughelper.h"

// [Phase18] 3D 상호작용 위젯
#include "Components/WidgetComponent.h"
#include "Widgets/HUD/Inv_InteractPromptWidget.h"


// 박스 범위내에 들어올시 수리 가능 범위 능력 활성화(UI)
void AResourceUsingObject_SpaceShip::CollisionBoxBeginOverlap(UPrimitiveComponent* OverlappedComponent, AActor* OtherActor, UPrimitiveComponent* OtherComp, int32 OtherBodyIndex, bool bFromSweep, const FHitResult& SweepResult)
{
	TArray<AActor*> Overlaps;
	ResouceUsingCollisionBox->GetOverlappingActors(Overlaps);

	if (AHellunaHeroCharacter* OverlappedHeroCharacter = Cast<AHellunaHeroCharacter>(OtherActor))
	{
		OverlappedHeroCharacter->GetHellunaAbilitySystemComponent()->TryActivateAbilityByTag(HellunaGameplayTags::Player_Ability_InRepair);

		// [Phase18] 3D 위젯 표시 (로컬 플레이어만, 수리 미완료 시)
		if (InteractWidgetComp && OverlappedHeroCharacter->IsLocallyControlled() && !IsRepaired())
		{
			InteractWidgetComp->SetVisibility(true);
			bInteractWidgetVisible = true;
		}
	}

}

// 박스 범위내에서 벗어날시 수리 가능 범위 능력 비활성화(UI)
void AResourceUsingObject_SpaceShip::CollisionBoxEndOverlap(UPrimitiveComponent* OverlappedComponent, AActor* OtherActor, UPrimitiveComponent* OtherComp, int32 OtherBodyIndex)
{
	if (AHellunaHeroCharacter* OverlappedHeroCharacter = Cast<AHellunaHeroCharacter>(OtherActor))
	{
		OverlappedHeroCharacter->GetHellunaAbilitySystemComponent()->CancelAbilityByTag(HellunaGameplayTags::Player_Ability_InRepair);

		// [Phase18] 3D 위젯 숨김
		if (InteractWidgetComp && OverlappedHeroCharacter->IsLocallyControlled())
		{
			InteractWidgetComp->SetVisibility(false);
			bInteractWidgetVisible = false;
		}
	}

}

//자원량을 더하는 함수 (실제 추가된 양 반환)
int32 AResourceUsingObject_SpaceShip::AddRepairResource(int32 Amount)
{
	UE_LOG(LogTemp, Warning, TEXT("=== [SpaceShip::AddRepairResource] 호출됨! ==="));
	UE_LOG(LogTemp, Warning, TEXT("  추가 요청 자원: %d"), Amount);
	UE_LOG(LogTemp, Warning, TEXT("  현재 상태: %d / %d"), CurrentResource, NeedResource);
	UE_LOG(LogTemp, Warning, TEXT("  서버 여부: %s"), HasAuthority() ? TEXT("서버 ✅") : TEXT("클라이언트 ❌"));

	if (!HasAuthority())
	{
		UE_LOG(LogTemp, Warning, TEXT("  ⚠️ 서버가 아니므로 종료!"));
		return 0;
	}
	
	if (Amount <= 0)
	{
		UE_LOG(LogTemp, Warning, TEXT("  ⚠️ Amount가 0 이하!"));
		return 0;
	}
	
	if (IsRepaired())
	{
		UE_LOG(LogTemp, Warning, TEXT("  ⚠️ 이미 수리 완료됨! 추가 불가"));
		return 0;
	}

	// ⭐ 실제로 추가 가능한 양 계산
	int32 RemainingSpace = NeedResource - CurrentResource;
	int32 ActualAddAmount = FMath::Min(Amount, RemainingSpace);

	UE_LOG(LogTemp, Warning, TEXT("  📊 남은 공간: %d, 실제 추가량: %d"), RemainingSpace, ActualAddAmount);

	int32 OldResource = CurrentResource;
	CurrentResource += ActualAddAmount;

	UE_LOG(LogTemp, Warning, TEXT("  ✅ 자원 추가 완료! %d → %d (실제 추가: +%d)"), 
		OldResource, CurrentResource, ActualAddAmount);

	OnRepairProgressChanged.Broadcast(CurrentResource, NeedResource);
	UE_LOG(LogTemp, Warning, TEXT("  📢 OnRepairProgressChanged 델리게이트 브로드캐스트!"));

	// 수리 완료 체크!
	if (IsRepaired())
	{
		UE_LOG(LogTemp, Warning, TEXT("=== 🎉 SpaceShip 수리 완료! CurrentResource: %d / NeedResource: %d ==="), CurrentResource, NeedResource);
		OnRepairCompleted();
	}

	UE_LOG(LogTemp, Warning, TEXT("  📤 실제 추가된 자원: %d 반환"), ActualAddAmount);
	UE_LOG(LogTemp, Warning, TEXT("=== [SpaceShip::AddRepairResource] 완료! ==="));
	return ActualAddAmount;
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

	// 공격 슬롯 매니저 자동 생성
	AttackSlotManager = CreateDefaultSubobject<USpaceShipAttackSlotManager>(TEXT("AttackSlotManager"));

	// World Partition NavMesh 스트리밍 보장:
	// 우주선 주변 NavMesh 데이터가 플레이어 접속 전에도 로드되도록 강제
	// TileGenerationRadius: SlotManager의 MaxRadius(600) + 여유분
	// TileRemovalRadius: 생성 반경보다 넓게 설정 (hysteresis)
	NavigationInvoker = CreateDefaultSubobject<UNavigationInvokerComponent>(TEXT("NavigationInvoker"));
	NavigationInvoker->SetGenerationRadii(3000.f, 4000.f);

	// NavModifier: 에디터에서 bUseNavModifier로 on/off 가능
	// ⚠ CanEverAffectNavigation은 construction 시점에 true여야 octree에 등록되고, 이후 runtime 토글이 즉시 반영된다.
	//    false로 시작하면 BeginPlay에서 true로 바꿔도 nav octree가 refresh되지 않아 에디터에서 액터를 움직이기 전까지 적용되지 않음.
	NavModifierComp = CreateDefaultSubobject<UNavModifierComponent>(TEXT("NavModifierComp"));
	NavModifierComp->SetAreaClass(UNavArea_Null::StaticClass());
	NavModifierComp->FailsafeExtent = NavModifierExtent;
	NavModifierComp->SetComponentTickEnabled(false);
	NavModifierComp->SetCanEverAffectNavigation(true);
	NavModifierComp->SetActive(false);
}

#if WITH_EDITOR
void AResourceUsingObject_SpaceShip::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);

	const FName PropName = PropertyChangedEvent.GetPropertyName();

	if (PropName == GET_MEMBER_NAME_CHECKED(AResourceUsingObject_SpaceShip, bUseNavModifier)
		|| PropName == GET_MEMBER_NAME_CHECKED(AResourceUsingObject_SpaceShip, NavModifierAreaClass)
		|| PropName == GET_MEMBER_NAME_CHECKED(AResourceUsingObject_SpaceShip, NavModifierExtent))
	{
		if (NavModifierComp)
		{
			NavModifierComp->SetActive(bUseNavModifier);
			NavModifierComp->SetCanEverAffectNavigation(bUseNavModifier);

			if (bUseNavModifier)
			{
				if (NavModifierAreaClass)
				{
					NavModifierComp->SetAreaClass(NavModifierAreaClass);
				}
				NavModifierComp->FailsafeExtent = NavModifierExtent;
			}
		}
	}
}
#endif

// 게임 시작시 게임 상태에 우주선 등록
void AResourceUsingObject_SpaceShip::BeginPlay()
{
	Super::BeginPlay();

	// 서버: GameState에 우주선 등록
	if (HasAuthority())
	{
		if (AHellunaDefenseGameState* GS = GetWorld()->GetGameState<AHellunaDefenseGameState>())
		{
			GS->RegisterSpaceShip(this);
		}
	}

	// ★ NavModifier 런타임 적용 (NavSystem 미준비 시 재시도)
	if (NavModifierComp)
	{
		ApplyNavModifierRuntime(0);
	}

	// ★ [NavMesh 수정] 우주선 DynamicMesh가 NavMesh 계산에서 제외되도록 설정
	// ComplexAsSimple + BlockAll 콜리전이 NavMesh에 거대한 구멍을 만들었음.
	// Nav 영향을 끄면 우주선 아래/주변에도 NavMesh가 정상 생성됨.
	// 몬스터가 우주선을 뚫고 지나가는 건 물리 콜리전(BlockAll)이 막아줌.
	if (DynamicMeshComponent)
	{
		DynamicMeshComponent->SetCanEverAffectNavigation(false);
	}

	// [Phase18] 3D 상호작용 위젯 생성 (클라이언트만)
	if (GetNetMode() != NM_DedicatedServer && InteractWidgetClass)
	{
		USceneComponent* RootComp = GetRootComponent();
		if (RootComp)
		{
			InteractWidgetComp = NewObject<UWidgetComponent>(this, UWidgetComponent::StaticClass(), TEXT("SpaceShipInteractWidgetComp"));
			if (InteractWidgetComp)
			{
				InteractWidgetComp->SetupAttachment(RootComp);
				InteractWidgetComp->SetRelativeLocation(FVector(0.f, 0.f, InteractWidgetZOffset));
				InteractWidgetComp->SetWidgetSpace(EWidgetSpace::Screen);
				InteractWidgetComp->SetDrawSize(FVector2D(200.f, 50.f));
				InteractWidgetComp->SetWidgetClass(InteractWidgetClass);
				InteractWidgetComp->SetVisibility(false);
				InteractWidgetComp->SetCollisionEnabled(ECollisionEnabled::NoCollision);
				InteractWidgetComp->RegisterComponent();

				// 텍스트 설정
				if (UInv_InteractPromptWidget* Prompt = Cast<UInv_InteractPromptWidget>(InteractWidgetComp->GetWidget()))
				{
					Prompt->SetKeyText(TEXT("F"));
					Prompt->SetItemName(TEXT("우주선 수리"));
					Prompt->SetActionText(TEXT(""));
				}

				UE_LOG(LogTemp, Log, TEXT("[Phase18] SpaceShip 3D 위젯 생성: %s"), *GetName());
			}
		}
	}
}

// 새로 추가: 수리 완료 처리
void AResourceUsingObject_SpaceShip::OnRepairCompleted_Implementation()
{
	UE_LOG(LogTemp, Warning, TEXT("=== [OnRepairCompleted] 수리 완료 이벤트 실행 ==="));

	// ⭐ 1. 델리게이트 브로드캐스트 (UI에서 승리 화면 표시)
	OnRepairCompleted_Delegate.Broadcast();
	UE_LOG(LogTemp, Warning, TEXT("  OnRepairCompleted_Delegate 브로드캐스트!"));

	// [Phase18] 3D 위젯 영구 숨김
	if (InteractWidgetComp)
	{
		InteractWidgetComp->SetVisibility(false);
		bInteractWidgetVisible = false;
	}

	UE_LOG(LogTemp, Warning, TEXT("=== [OnRepairCompleted] 완료 ==="));
}

// =========================================================
// [cheatdebug/nav] NavModifier 강제 적용 헬퍼
//  - BP 설정을 무시하고 항상 NavArea_Null을 적용 (NavArea_Default는 path를 안 막음)
//  - NavSystem 미준비(null)면 다음 틱에 재시도 (최대 10회)
// =========================================================
void AResourceUsingObject_SpaceShip::ApplyNavModifierRuntime(int32 RetryCount)
{
	if (!NavModifierComp) return;

	UE_LOG(LogTemp, Warning,
		TEXT("[cheatdebug/nav] ApplyNavModifierRuntime SpaceShip=%s bUseNavModifier=%d BP_AreaClass=%s Extent=(%s) Retry=%d"),
		*GetName(), (int32)bUseNavModifier,
		NavModifierAreaClass ? *NavModifierAreaClass->GetName() : TEXT("<null>"),
		*NavModifierExtent.ToString(), RetryCount);

	if (bUseNavModifier)
	{
		// BP가 NavArea_Default로 설정되어 있으면 path를 안 막으므로 항상 Null로 강제 덮어씀.
		TSubclassOf<UNavArea> AreaToSet = TSubclassOf<UNavArea>(UNavArea_Null::StaticClass());
		NavModifierComp->SetAreaClass(AreaToSet);
		NavModifierComp->FailsafeExtent = NavModifierExtent;
		NavModifierComp->SetActive(true);
		UE_LOG(LogTemp, Warning, TEXT("[cheatdebug/nav] AreaClass FORCED to NavArea_Null"));
	}
	else
	{
		NavModifierComp->SetActive(false);
	}
	NavModifierComp->RefreshNavigationModifiers();

	UNavigationSystemV1* NavSys = FNavigationSystem::GetCurrent<UNavigationSystemV1>(GetWorld());
	if (!NavSys)
	{
		UE_LOG(LogTemp, Warning, TEXT("[cheatdebug/nav] SpaceShip=%s NavSystem=null (retry %d/10)"),
			*GetName(), RetryCount);
		if (RetryCount < 10 && GetWorld())
		{
			GetWorld()->GetTimerManager().SetTimer(NavModifierRetryHandle,
				FTimerDelegate::CreateUObject(this, &AResourceUsingObject_SpaceShip::ApplyNavModifierRuntime, RetryCount + 1),
				0.5f, false);
		}
		return;
	}

	for (ANavigationData* NavData : NavSys->NavDataSet)
	{
		ARecastNavMesh* Recast = Cast<ARecastNavMesh>(NavData);
		if (!Recast) continue;
		UE_LOG(LogTemp, Warning,
			TEXT("[cheatdebug/nav] RecastNavMesh=%s RuntimeGeneration=%d (0=Static,1=DynModifiersOnly,2=Dynamic) bRebuildAtRuntime=%d"),
			*Recast->GetName(), (int32)Recast->GetRuntimeGenerationMode(),
			(int32)Recast->SupportsRuntimeGeneration());

		if (Recast->GetRuntimeGenerationMode() == ERuntimeGenerationType::Static)
		{
			UE_LOG(LogTemp, Warning,
				TEXT("[cheatdebug/nav] WARN: RecastNavMesh가 Static. 레벨 인스턴스 설정이 DefaultEngine.ini를 덮어씀."));
		}
	}

	NavSys->UpdateComponentInNavOctree(*NavModifierComp);

	FBox ActorBox = GetComponentsBoundingBox(true, true);
	if (!ActorBox.IsValid || ActorBox.GetExtent().IsNearlyZero())
	{
		ActorBox = FBox::BuildAABB(GetActorLocation(), NavModifierExtent);
	}
	const int32 DirtyFlags = (int32)ENavigationDirtyFlag::All;
	NavSys->AddDirtyArea(ActorBox, DirtyFlags, nullptr, FName(TEXT("SpaceShipNavModifier")));

	UE_LOG(LogTemp, Warning,
		TEXT("[cheatdebug/nav] SpaceShip=%s dirty area %s 적용 완료 (Retry=%d)"),
		*GetName(), *ActorBox.ToString(), RetryCount);
}