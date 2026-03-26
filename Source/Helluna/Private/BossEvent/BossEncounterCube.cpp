// Source/Helluna/Private/BossEvent/BossEncounterCube.cpp

#include "BossEvent/BossEncounterCube.h"
#include "Widgets/HoldInteractWidget.h"
#include "Components/SphereComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/WidgetComponent.h"
#include "Net/UnrealNetwork.h"
#include "Character/HellunaHeroCharacter.h"
#include "Controller/HellunaHeroController.h"

// ============================================================================
// 생성자
// ============================================================================

ABossEncounterCube::ABossEncounterCube()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.bStartWithTickEnabled = true;

	bReplicates = true;

	// 루트 메시 (에디터에서 스태틱메시 수동 할당)
	MeshComp = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("MeshComp"));
	SetRootComponent(MeshComp);

	// 상호작용 구체 (기본 반경 500)
	InteractionSphere = CreateDefaultSubobject<USphereComponent>(TEXT("InteractionSphere"));
	InteractionSphere->SetSphereRadius(500.f);
	InteractionSphere->SetCollisionProfileName(TEXT("OverlapAllDynamic"));
	InteractionSphere->SetGenerateOverlapEvents(true);
	InteractionSphere->SetupAttachment(MeshComp);

	// 3D 상호작용 위젯
	InteractWidgetComp = CreateDefaultSubobject<UWidgetComponent>(TEXT("InteractWidgetComp"));
	InteractWidgetComp->SetupAttachment(MeshComp);
	InteractWidgetComp->SetRelativeLocation(FVector(0.f, 0.f, 120.f));
	InteractWidgetComp->SetDrawSize(FVector2D(200.f, 60.f));
	InteractWidgetComp->SetWidgetSpace(EWidgetSpace::Screen);
	InteractWidgetComp->SetVisibility(false);
	InteractWidgetComp->SetCollisionEnabled(ECollisionEnabled::NoCollision);
}

// ============================================================================
// 라이프사이클
// ============================================================================

void ABossEncounterCube::BeginPlay()
{
	Super::BeginPlay();

	// 3D 상호작용 위젯 클래스 설정 (클라이언트+리슨서버)
	if (InteractWidgetComp && InteractWidgetClass)
	{
		InteractWidgetComp->SetWidgetClass(InteractWidgetClass);
	}
}

void ABossEncounterCube::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	DOREPLIFETIME(ABossEncounterCube, bActivated);
}

// ============================================================================
// Tick — 클라이언트 위젯 + F키 홀드 프로그레스
// ============================================================================

void ABossEncounterCube::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

	// 데디케이티드 서버에서는 위젯 처리 불필요
	if (IsRunningDedicatedServer())
	{
		return;
	}

	UpdateInteractWidgetVisibility();

	// F키 홀드 프로그레스 업데이트
	if (bInteractWidgetVisible && InteractWidgetInstance)
	{
		UWorld* World = GetWorld();
		if (!World) { return; }

		APlayerController* PC = World->GetFirstPlayerController();
		if (!PC) { return; }

		AHellunaHeroCharacter* HeroChar = Cast<AHellunaHeroCharacter>(PC->GetPawn());
		AHellunaHeroController* HeroPC = Cast<AHellunaHeroController>(PC);

		if (HeroChar && HeroChar->IsHoldingInteraction())
		{
			// 프로그레스 증가
			LocalHoldProgress = FMath::Min(1.f, LocalHoldProgress + DeltaTime / HoldDuration);
			InteractWidgetInstance->SetProgress(LocalHoldProgress);

			if (LocalHoldProgress >= 1.f && !bHoldCompleted)
			{
				bHoldCompleted = true;
				InteractWidgetInstance->ShowCompleted();

				// Server RPC 호출
				if (HeroPC)
				{
					HeroPC->Server_BossEncounterActivate();
					UE_LOG(LogTemp, Warning, TEXT("[BossEncounterCube] Hold complete — Server_BossEncounterActivate called"));
				}
			}
		}
		else
		{
			// F키 안 누르면 프로그레스 빠르게 감소
			if (LocalHoldProgress > 0.f)
			{
				LocalHoldProgress = FMath::Max(0.f, LocalHoldProgress - DeltaTime * 3.f);
				InteractWidgetInstance->SetProgress(LocalHoldProgress);
			}
		}
	}
}

// ============================================================================
// 서버: 활성화
// ============================================================================

bool ABossEncounterCube::TryActivate(AController* Activator)
{
	if (!HasAuthority())
	{
		return false;
	}

	if (bActivated)
	{
		UE_LOG(LogTemp, Warning, TEXT("[BossEncounterCube] Already activated"));
		return false;
	}

	bActivated = true;
	UE_LOG(LogTemp, Warning, TEXT("[BossEncounterCube] Activated by %s"),
		Activator ? *Activator->GetName() : TEXT("nullptr"));

	// TODO: Step 3+ — 보스 스폰, 영역 흑백 전환, 이벤트 시작
	return true;
}

// ============================================================================
// 상호작용 반경
// ============================================================================

float ABossEncounterCube::GetInteractionRadius() const
{
	if (InteractionSphere)
	{
		return InteractionSphere->GetScaledSphereRadius();
	}
	return 500.f;
}

// ============================================================================
// 3D 위젯 표시/숨김
// ============================================================================

void ABossEncounterCube::UpdateInteractWidgetVisibility()
{
	if (!InteractWidgetComp) { return; }

	UWorld* World = GetWorld();
	if (!World) { return; }

	APlayerController* PC = World->GetFirstPlayerController();
	if (!PC || !PC->IsLocalController()) { return; }

	APawn* Pawn = PC->GetPawn();
	if (!Pawn) { return; }

	// 조건 1: 거리 체크
	const float Dist = FVector::Dist(Pawn->GetActorLocation(), GetActorLocation());
	const bool bInRange = Dist < GetInteractionRadius();

	// 조건 2: 아직 활성화되지 않음
	const bool bNotActivated = !bActivated;

	// 조건 3: 카메라 뷰에 보이는지
	const bool bOnScreen = WasRecentlyRendered(0.2f);

	const bool bShouldShow = bInRange && bNotActivated && bOnScreen;

	if (bShouldShow && !bInteractWidgetVisible)
	{
		// 위젯 표시
		InteractWidgetComp->SetVisibility(true);
		bInteractWidgetVisible = true;

		// 위젯 인스턴스 캐시
		if (!InteractWidgetInstance)
		{
			InteractWidgetInstance = Cast<UHoldInteractWidget>(InteractWidgetComp->GetWidget());
		}
		if (InteractWidgetInstance)
		{
			InteractWidgetInstance->ResetState();
		}

		UE_LOG(LogTemp, Log, TEXT("[BossEncounterCube] 3D Interact widget shown (dist=%.0f)"), Dist);
	}
	else if (!bShouldShow && bInteractWidgetVisible)
	{
		// 위젯 숨김
		InteractWidgetComp->SetVisibility(false);
		bInteractWidgetVisible = false;
		LocalHoldProgress = 0.f;
		bHoldCompleted = false;

		UE_LOG(LogTemp, Log, TEXT("[BossEncounterCube] 3D Interact widget hidden"));
	}
}
