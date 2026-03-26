// Source/Helluna/Private/BossEvent/BossEncounterCube.cpp

#include "BossEvent/BossEncounterCube.h"
#include "Widgets/HoldInteractWidget.h"
#include "Components/SphereComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/WidgetComponent.h"
#include "Components/PrimitiveComponent.h"
#include "Net/UnrealNetwork.h"
#include "Character/HellunaHeroCharacter.h"
#include "Character/HellunaEnemyCharacter.h"
#include "Controller/HellunaHeroController.h"
#include "Character/EnemyComponent/HellunaHealthComponent.h"
#include "Kismet/KismetMaterialLibrary.h"
#include "Materials/MaterialParameterCollection.h"
#include "EngineUtils.h"

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

	// 늦은 접속 클라이언트
	if (!IsRunningDedicatedServer() && bActivated && MPC_BossEncounter)
	{
		if (bBossDefeated)
		{
			// 보스 이미 처치됨 → 컬러 유지 + 꽃 표시 (ColorWaveRadius 최대)
			UKismetMaterialLibrary::SetScalarParameterValue(
				this, MPC_BossEncounter, TEXT("DesatAmount"), 0.f);
			UKismetMaterialLibrary::SetScalarParameterValue(
				this, MPC_BossEncounter, TEXT("ColorWaveRadius"), ColorWaveMaxRadius);
			UE_LOG(LogTemp, Log, TEXT("[BossEncounterCube] Late join — boss defeated, color + flowers restored"));
		}
		else
		{
			// 보스 전투 진행 중 → 즉시 흑백 + Custom Depth
			UKismetMaterialLibrary::SetScalarParameterValue(
				this, MPC_BossEncounter, TEXT("DesatAmount"), 1.f);
			DesatProgress = 1.f;

			FTimerHandle CustomDepthTimerHandle;
			GetWorldTimerManager().SetTimer(CustomDepthTimerHandle, this,
				&ABossEncounterCube::EnableBossEncounterCustomDepth, 1.f, false);

			UE_LOG(LogTemp, Log, TEXT("[BossEncounterCube] Late join — MPC DesatAmount snapped to 1.0"));
		}
	}
}

void ABossEncounterCube::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	DOREPLIFETIME(ABossEncounterCube, bActivated);
	DOREPLIFETIME(ABossEncounterCube, bBossDefeated);
}

// ============================================================================
// Tick — 클라이언트 위젯 + F키 홀드 프로그레스
// ============================================================================

void ABossEncounterCube::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

	// 데디케이티드 서버에서는 위젯/MPC 처리 불필요
	if (IsRunningDedicatedServer())
	{
		return;
	}

	// [Step 3] 흑백 전환 MPC 업데이트 (클라이언트)
	TickDesaturation(DeltaTime);

	// [Step 5] 컬러 웨이브 MPC 업데이트 (클라이언트)
	TickColorWave(DeltaTime);

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

	// ── [Step 2] 보스 스폰 (서버) ──
	if (!BossClass)
	{
		UE_LOG(LogTemp, Warning, TEXT("[BossEncounterCube] BossClass가 설정되지 않았습니다! BP에서 할당 필요"));
		return false;
	}

	UWorld* World = GetWorld();
	if (!World)
	{
		return false;
	}

	FActorSpawnParameters Param;
	Param.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AdjustIfPossibleButAlwaysSpawn;

	const FVector SpawnLocation = GetActorLocation() + BossSpawnOffset;
	const FRotator SpawnRotation = GetActorRotation();

	APawn* Boss = World->SpawnActor<APawn>(BossClass, SpawnLocation, SpawnRotation, Param);

	if (!IsValid(Boss))
	{
		UE_LOG(LogTemp, Error, TEXT("[BossEncounterCube] SpawnActor 실패 — Class: %s"),
			*BossClass->GetName());
		return false;
	}

	SpawnedBoss = Boss;
	bActivated = true;

	// ── [Step 5] 보스 사망 감지 바인딩 (서버) ──
	if (UHellunaHealthComponent* HealthComp = Boss->FindComponentByClass<UHellunaHealthComponent>())
	{
		HealthComp->OnDeath.AddUniqueDynamic(this, &ABossEncounterCube::OnSpawnedBossDied);
		UE_LOG(LogTemp, Log, TEXT("[BossEncounterCube] Bound to boss OnDeath delegate"));
	}
	else
	{
		UE_LOG(LogTemp, Warning, TEXT("[BossEncounterCube] Boss has no HellunaHealthComponent — death detection disabled"));
	}

	UE_LOG(LogTemp, Warning, TEXT("[BossEncounterCube] Boss spawned: %s at %s by %s"),
		*Boss->GetName(), *SpawnLocation.ToString(),
		Activator ? *Activator->GetName() : TEXT("nullptr"));

	// ── [Step 3] 흑백 전환 시작 알림 (서버 → 모든 클라이언트) ──
	Multicast_BossEncounterStarted();

	return true;
}

// ============================================================================
// Multicast: 보스 조우 시작 (클라이언트에서 흑백 전환 시작)
// ============================================================================

void ABossEncounterCube::Multicast_BossEncounterStarted_Implementation()
{
	// 데디케이티드 서버에서는 시각 처리 불필요
	if (IsRunningDedicatedServer())
	{
		return;
	}

	// 이미 전환 중이면 스킵
	if (bDesatTransitioning || DesatProgress >= 1.f)
	{
		return;
	}

	bDesatTransitioning = true;
	DesatProgress = 0.f;

	// [Step 4] 플레이어·보스 메시에 Custom Depth/Stencil 활성화
	EnableBossEncounterCustomDepth();

	UE_LOG(LogTemp, Warning, TEXT("[BossEncounterCube] Multicast received — starting desaturation transition (%.1fs)"),
		DesatTransitionDuration);
}

// ============================================================================
// [Step 3] 흑백 전환 Tick (클라이언트)
// ============================================================================

void ABossEncounterCube::TickDesaturation(float DeltaTime)
{
	if (!bDesatTransitioning)
	{
		return;
	}

	if (!MPC_BossEncounter)
	{
		UE_LOG(LogTemp, Warning, TEXT("[BossEncounterCube] MPC_BossEncounter 미설정 — 흑백 전환 불가"));
		bDesatTransitioning = false;
		return;
	}

	// 프로그레스 증가
	const float Duration = FMath::Max(DesatTransitionDuration, 0.1f);
	DesatProgress = FMath::Min(1.f, DesatProgress + DeltaTime / Duration);

	// MPC 업데이트
	UKismetMaterialLibrary::SetScalarParameterValue(
		this, MPC_BossEncounter, TEXT("DesatAmount"), DesatProgress);

	if (DesatProgress >= 1.f)
	{
		bDesatTransitioning = false;
		UE_LOG(LogTemp, Warning, TEXT("[BossEncounterCube] Desaturation complete — fully grayscale"));
	}
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
// [Step 4] Custom Depth / Stencil — 플레이어·보스 컬러 유지
// ============================================================================

void ABossEncounterCube::SetCustomDepthOnActor(AActor* Actor, bool bEnable, int32 StencilValue)
{
	if (!IsValid(Actor))
	{
		return;
	}

	// 액터 자신의 모든 PrimitiveComponent
	TInlineComponentArray<UPrimitiveComponent*> Prims;
	Actor->GetComponents<UPrimitiveComponent>(Prims);
	for (UPrimitiveComponent* Prim : Prims)
	{
		if (Prim && Prim->IsRegistered())
		{
			Prim->SetRenderCustomDepth(bEnable);
			if (bEnable)
			{
				Prim->SetCustomDepthStencilValue(StencilValue);
			}
		}
	}

	// 부착된 자식 액터에도 재귀 적용 (무기, 장비 등)
	TArray<AActor*> AttachedActors;
	Actor->GetAttachedActors(AttachedActors);
	for (AActor* Attached : AttachedActors)
	{
		SetCustomDepthOnActor(Attached, bEnable, StencilValue);
	}
}

void ABossEncounterCube::EnableBossEncounterCustomDepth()
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	int32 Count = 0;

	// 플레이어 캐릭터 전체
	for (TActorIterator<AHellunaHeroCharacter> It(World); It; ++It)
	{
		SetCustomDepthOnActor(*It, true, BossEncounterStencilValue);
		++Count;
	}

	// 보스 캐릭터: SpawnedBoss 우선, 없으면 SemiBoss/Boss 등급 검색
	if (SpawnedBoss.IsValid())
	{
		SetCustomDepthOnActor(SpawnedBoss.Get(), true, BossEncounterStencilValue);
		++Count;
	}
	else
	{
		for (TActorIterator<AHellunaEnemyCharacter> It(World); It; ++It)
		{
			if (It->EnemyGrade == EEnemyGrade::SemiBoss || It->EnemyGrade == EEnemyGrade::Boss)
			{
				SetCustomDepthOnActor(*It, true, BossEncounterStencilValue);
				++Count;
			}
		}
	}

	UE_LOG(LogTemp, Log, TEXT("[BossEncounterCube] Custom Depth enabled on %d actors (Stencil=%d)"),
		Count, BossEncounterStencilValue);
}

// ============================================================================
// [Step 5] 보스 사망 → 컬러 웨이브
// ============================================================================

void ABossEncounterCube::OnSpawnedBossDied(AActor* DeadActor, AActor* KillerActor)
{
	if (!HasAuthority())
	{
		return;
	}

	FVector DeathLocation = GetActorLocation(); // 폴백: 큐브 위치
	if (IsValid(DeadActor))
	{
		DeathLocation = DeadActor->GetActorLocation();
	}

	bBossDefeated = true;

	UE_LOG(LogTemp, Warning, TEXT("[BossEncounterCube] Boss defeated at %s — starting color wave"),
		*DeathLocation.ToString());

	Multicast_BossDefeated(DeathLocation);
}

void ABossEncounterCube::Multicast_BossDefeated_Implementation(FVector DeathLocation)
{
	if (IsRunningDedicatedServer())
	{
		return;
	}

	WaveOrigin = DeathLocation;
	bColorWaveActive = true;
	ColorWaveRadius = 0.f;

	// MPC에 웨이브 원점 설정
	if (MPC_BossEncounter)
	{
		UKismetMaterialLibrary::SetScalarParameterValue(
			this, MPC_BossEncounter, TEXT("WaveOriginX"), WaveOrigin.X);
		UKismetMaterialLibrary::SetScalarParameterValue(
			this, MPC_BossEncounter, TEXT("WaveOriginY"), WaveOrigin.Y);
		UKismetMaterialLibrary::SetScalarParameterValue(
			this, MPC_BossEncounter, TEXT("WaveOriginZ"), WaveOrigin.Z);
		UKismetMaterialLibrary::SetScalarParameterValue(
			this, MPC_BossEncounter, TEXT("ColorWaveRadius"), 0.f);
	}

	UE_LOG(LogTemp, Warning, TEXT("[BossEncounterCube] Multicast — color wave started from %s"),
		*DeathLocation.ToString());
}

void ABossEncounterCube::TickColorWave(float DeltaTime)
{
	if (!bColorWaveActive)
	{
		return;
	}

	if (!MPC_BossEncounter)
	{
		UE_LOG(LogTemp, Warning, TEXT("[BossEncounterCube] MPC_BossEncounter 미설정 — 컬러 웨이브 불가"));
		bColorWaveActive = false;
		return;
	}

	// 반경 확장
	ColorWaveRadius += ColorWaveSpeed * DeltaTime;

	UKismetMaterialLibrary::SetScalarParameterValue(
		this, MPC_BossEncounter, TEXT("ColorWaveRadius"), ColorWaveRadius);

	// 최대 반경 도달 → 완전 컬러 복원
	if (ColorWaveRadius >= ColorWaveMaxRadius)
	{
		bColorWaveActive = false;

		// 흑백 해제 (DesatAmount=0 → PP Material이 원본 컬러 출력)
		UKismetMaterialLibrary::SetScalarParameterValue(
			this, MPC_BossEncounter, TEXT("DesatAmount"), 0.f);

		// ⚠️ ColorWaveRadius는 리셋하지 않음 — 꽃 WPO가 이 값을 참조하므로
		// MaxRadius 유지해야 꽃이 올라온 상태를 유지한다

		// Custom Depth 해제 (더 이상 불필요)
		DisableBossEncounterCustomDepth();

		UE_LOG(LogTemp, Warning, TEXT("[BossEncounterCube] Color wave complete — full color restored (flowers persist)"));
	}
}

void ABossEncounterCube::DisableBossEncounterCustomDepth()
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	for (TActorIterator<AHellunaHeroCharacter> It(World); It; ++It)
	{
		SetCustomDepthOnActor(*It, false, 0);
	}

	for (TActorIterator<AHellunaEnemyCharacter> It(World); It; ++It)
	{
		SetCustomDepthOnActor(*It, false, 0);
	}

	UE_LOG(LogTemp, Log, TEXT("[BossEncounterCube] Custom Depth disabled on all characters"));
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
