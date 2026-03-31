// Source/Helluna/Private/BossEvent/BossEncounterCube.cpp

#include "BossEvent/BossEncounterCube.h"
#include "Widgets/HoldInteractWidget.h"
#include "Components/SphereComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/WidgetComponent.h"
#include "Components/PrimitiveComponent.h"
#include "Components/PostProcessComponent.h"
#include "Camera/CameraComponent.h"
#include "Net/UnrealNetwork.h"
#include "Character/HellunaHeroCharacter.h"
#include "Character/HellunaEnemyCharacter.h"
#include "Controller/HellunaHeroController.h"
#include "Character/EnemyComponent/HellunaHealthComponent.h"
#include "Kismet/KismetMaterialLibrary.h"
#include "Kismet/GameplayStatics.h"
#include "Materials/MaterialParameterCollection.h"
#include "NiagaraFunctionLibrary.h"
#include "NiagaraComponent.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Materials/MaterialInterface.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Engine/Texture2D.h"
#include "EngineUtils.h"
#include "Engine/StaticMeshActor.h"
#include "Engine/PostProcessVolume.h"

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

	// PPV_BossEncounter 캐시 (클라이언트)
	if (!IsRunningDedicatedServer())
	{
		CacheBossPostProcessVolume();
	}

	// 평시 어두움: 보스 이벤트와 무관하게 세계가 어두움
	if (!IsRunningDedicatedServer() && MPC_BossEncounter)
	{
		UKismetMaterialLibrary::SetScalarParameterValue(
			this, MPC_BossEncounter, TEXT("DarknessAmount"), DarknessAmount);
		UE_LOG(LogTemp, Warning, TEXT("[BossEncounter_DIAG] BeginPlay — DarknessAmount=%.2f, bActivated=%d, bBossDefeated=%d, MPC=%s"),
			DarknessAmount, (int)bActivated, (int)bBossDefeated, *MPC_BossEncounter->GetName());
	}
	else if (!MPC_BossEncounter)
	{
		UE_LOG(LogTemp, Error, TEXT("[BossEncounter_DIAG] BeginPlay — MPC_BossEncounter is NULL! BP 설정 확인 필요"));
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
				this, MPC_BossEncounter, TEXT("DarknessAmount"), 0.f);
			UKismetMaterialLibrary::SetScalarParameterValue(
				this, MPC_BossEncounter, TEXT("ColorWaveRadius"), ColorWaveMaxRadius);
			AuraVFXSpawnedActors.Empty();
			AuraVFXSpawnedFoliageHashes.Empty();
			CachedFoliageLocations.Empty();
			bFoliageCached = false;
			ActiveWireframeOverlays.Empty();
			// PPV 비활성화 (보스 이미 처치 → PP 필요 없음)
			SetBossPostProcessEnabled(false);
			UE_LOG(LogTemp, Log, TEXT("[BossEncounterCube] Late join — boss defeated, color + flowers restored, PPV disabled"));
		}
		else
		{
			// 보스 전투 진행 중 → 즉시 흑백 + Custom Depth
			UKismetMaterialLibrary::SetScalarParameterValue(
				this, MPC_BossEncounter, TEXT("DesatAmount"), 1.f);
			DesatProgress = 1.f;

			// 보스 영역 중심/반경 MPC 설정 (늦은 접속)
			const FVector AreaCenter = GetActorLocation();
			UKismetMaterialLibrary::SetVectorParameterValue(
				this, MPC_BossEncounter, TEXT("BossAreaCenter"),
				FLinearColor(AreaCenter.X, AreaCenter.Y, AreaCenter.Z, 0.f));
			UKismetMaterialLibrary::SetScalarParameterValue(
				this, MPC_BossEncounter, TEXT("BossAreaRadius"), BossAreaRadius);

			FTimerHandle CustomDepthTimerHandle;
			GetWorldTimerManager().SetTimer(CustomDepthTimerHandle, this,
				&ABossEncounterCube::EnableBossEncounterCustomDepth, 1.f, false);

			// [BossRestore] 늦은 접속 시에도 메쉬 어둡게 처리
			DarkenEnvironmentMeshes();

			UE_LOG(LogTemp, Log, TEXT("[BossEncounterCube] Late join — MPC DesatAmount snapped to 1.0, BossArea set"));
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

	// [Post-Wave] 보스 처치 후 웨이브 완료 → MPC 강제 유지 (다른 큐브 등에 의한 덮어쓰기 방지)
	if (bBossDefeated && !bColorWaveActive && MPC_BossEncounter)
	{
		UKismetMaterialLibrary::SetScalarParameterValue(
			this, MPC_BossEncounter, TEXT("DesatAmount"), 0.f);
		UKismetMaterialLibrary::SetScalarParameterValue(
			this, MPC_BossEncounter, TEXT("DarknessAmount"), 0.f);
	}

	// [Cinematic] 시네마틱 Lerp 처리 (FOV·크로마·섬광·채도)
	TickCinematic(DeltaTime);

	// [Wave VFX] 와이어프레임 오버레이 페이드 애니메이션
	TickWireframeOverlays(DeltaTime);

	// [BossRestore] 환경 메쉬 컬러 복원 스캔 애니메이션
	TickBossRestore(DeltaTime);

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

		if (HeroChar && HeroChar->IsHoldingInteraction() && !HeroChar->IsReviving())
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
					UE_LOG(LogTemp, Verbose, TEXT("[BossEncounterCube] Hold complete — Server_BossEncounterActivate called"));
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
		UE_LOG(LogTemp, Verbose, TEXT("[BossEncounterCube] Already activated"));
		return false;
	}

	// ── [Step 2] 보스 스폰 (서버) ──
	if (!BossClass)
	{
		UE_LOG(LogTemp, Verbose, TEXT("[BossEncounterCube] BossClass가 설정되지 않았습니다! BP에서 할당 필요"));
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
		UE_LOG(LogTemp, Verbose, TEXT("[BossEncounterCube] Boss has no HellunaHealthComponent — death detection disabled"));
	}

	UE_LOG(LogTemp, Verbose, TEXT("[BossEncounterCube] Boss spawned: %s at %s by %s"),
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

	// PP 볼륨 활성화 (보스 이벤트 시작 시)
	SetBossPostProcessEnabled(true);

	// 보스 영역 중심/반경을 MPC에 설정 (PP 머티리얼에서 영역 마스킹에 사용)
	if (MPC_BossEncounter)
	{
		const FVector AreaCenter = GetActorLocation();
		UKismetMaterialLibrary::SetVectorParameterValue(
			this, MPC_BossEncounter, TEXT("BossAreaCenter"),
			FLinearColor(AreaCenter.X, AreaCenter.Y, AreaCenter.Z, 0.f));
		UKismetMaterialLibrary::SetScalarParameterValue(
			this, MPC_BossEncounter, TEXT("BossAreaRadius"), BossAreaRadius);
		UKismetMaterialLibrary::SetScalarParameterValue(
			this, MPC_BossEncounter, TEXT("DarknessAmount"), DarknessAmount);

		UE_LOG(LogTemp, Verbose, TEXT("[BossEncounterCube] BossArea set: Center=%s, Radius=%.0f, Darkness=%.2f"),
			*AreaCenter.ToString(), BossAreaRadius, DarknessAmount);
	}

	// [Step 4] 플레이어·보스 메시에 Custom Depth/Stencil 활성화
	EnableBossEncounterCustomDepth();

	// [BossRestore] 영역 내 메쉬 물리적 어둡게 처리
	DarkenEnvironmentMeshes();

	UE_LOG(LogTemp, Verbose, TEXT("[BossEncounterCube] Multicast received — starting desaturation transition (%.1fs)"),
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
		UE_LOG(LogTemp, Verbose, TEXT("[BossEncounterCube] MPC_BossEncounter 미설정 — 흑백 전환 불가"));
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
		UE_LOG(LogTemp, Warning, TEXT("[BossEncounter_DIAG] Desaturation COMPLETE — DesatAmount=1.0"));
		UE_LOG(LogTemp, Verbose, TEXT("[BossEncounterCube] Desaturation complete — fully grayscale"));
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

	UE_LOG(LogTemp, Warning, TEXT("[BossEncounter_DIAG] OnSpawnedBossDied — DeathLoc=%s, calling Multicast"),
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

	// 흑백 전환이 아직 진행 중이면 강제 완료 (컬러 웨이브가 DesatAmount를 관리함)
	if (bDesatTransitioning)
	{
		bDesatTransitioning = false;
		DesatProgress = 1.f;
		if (MPC_BossEncounter)
		{
			UKismetMaterialLibrary::SetScalarParameterValue(
				this, MPC_BossEncounter, TEXT("DesatAmount"), 1.f);
		}
	}

	// MPC에 웨이브 원점 설정
	if (MPC_BossEncounter)
	{
		UKismetMaterialLibrary::SetVectorParameterValue(
			this, MPC_BossEncounter, TEXT("WaveOrigin"),
			FLinearColor(WaveOrigin.X, WaveOrigin.Y, WaveOrigin.Z, 0.f));
		UKismetMaterialLibrary::SetScalarParameterValue(
			this, MPC_BossEncounter, TEXT("ColorWaveRadius"), 0.f);
	}

	// [Cinematic] 시네마틱 시퀀스 시작 (컬러 웨이브와 병행)
	StartCinematicSequence(DeathLocation);

	UE_LOG(LogTemp, Warning, TEXT("[BossEncounter_DIAG] Multicast_BossDefeated — bColorWaveActive=%d, bDesatTransitioning=%d, MPC=%s, WaveSpeed=%.0f, WaveMax=%.0f"),
		(int)bColorWaveActive, (int)bDesatTransitioning, MPC_BossEncounter ? TEXT("Valid") : TEXT("NULL"),
		ColorWaveSpeed, ColorWaveMaxRadius);

	// [Failsafe] 10초 후 MPC 값 강제 복원 — 웨이브가 정상 완료되면 타이머 자동 해제
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(MPC_FailsafeTimer);
		World->GetTimerManager().SetTimer(
			MPC_FailsafeTimer, this,
			&ABossEncounterCube::ForceRestoreMPCValues,
			10.f, false);
	}
}

void ABossEncounterCube::TickColorWave(float DeltaTime)
{
	if (!bColorWaveActive)
	{
		return;
	}

	if (!MPC_BossEncounter)
	{
		UE_LOG(LogTemp, Verbose, TEXT("[BossEncounterCube] MPC_BossEncounter 미설정 — 컬러 웨이브 불가"));
		bColorWaveActive = false;
		return;
	}

	// 반경 확장
	ColorWaveRadius += ColorWaveSpeed * DeltaTime;

	// 진단: 웨이브 진행률 (1초마다)
	static float DiagTimer = 0.f;
	DiagTimer += DeltaTime;
	if (DiagTimer >= 1.f)
	{
		DiagTimer = 0.f;
		UE_LOG(LogTemp, Warning, TEXT("[BossEncounter_DIAG] TickColorWave — Radius=%.0f / Max=%.0f (%.0f%%)"),
			ColorWaveRadius, ColorWaveMaxRadius, (ColorWaveRadius / ColorWaveMaxRadius) * 100.f);
	}

	UKismetMaterialLibrary::SetScalarParameterValue(
		this, MPC_BossEncounter, TEXT("ColorWaveRadius"), ColorWaveRadius);

	// [Wave VFX] 웨이브 경계 사물에 오라 스폰
	SpawnWaveAuraVFX(DeltaTime);

	// 최대 반경 도달 → 완전 컬러 복원
	if (ColorWaveRadius >= ColorWaveMaxRadius)
	{
		bColorWaveActive = false;

		// 혹시 남아있는 흑백 전환도 확실히 정지
		bDesatTransitioning = false;

		// 흑백 해제 (DesatAmount=0, DarknessAmount=0 → 전체 원색 복원)
		UKismetMaterialLibrary::SetScalarParameterValue(
			this, MPC_BossEncounter, TEXT("DesatAmount"), 0.f);
		UKismetMaterialLibrary::SetScalarParameterValue(
			this, MPC_BossEncounter, TEXT("DarknessAmount"), 0.f);

		// ⚠️ ColorWaveRadius는 리셋하지 않음 — 꽃 WPO가 이 값을 참조하므로
		// MaxRadius 유지해야 꽃이 올라온 상태를 유지한다

		// PP 볼륨 비활성화 (PP 머티리얼이 DesatAmount=0에서도 장면을 어둡게 하는 문제 방지)
		SetBossPostProcessEnabled(false);

		// Custom Depth 해제 (더 이상 불필요)
		DisableBossEncounterCustomDepth();

		// 오라 VFX 추적 초기화
		AuraVFXSpawnedActors.Empty();
		AuraVFXSpawnedFoliageHashes.Empty();
		CachedFoliageLocations.Empty();
		bFoliageCached = false;
		TotalHoloSpawned = 0;

		// 혹시 남아있는 오버레이 메시 정리
		for (auto& Overlay : ActiveWireframeOverlays)
		{
			if (Overlay.OverlayMesh.IsValid())
			{
				Overlay.OverlayMesh->DestroyComponent();
			}
		}
		ActiveWireframeOverlays.Empty();

		// [BossRestore] 남아있는 어두운 메쉬 전부 원본 복원
		for (FDarkenedMesh& DM : DarkenedMeshes)
		{
			RestoreDarkenedMesh(DM);
		}
		DarkenedMeshes.Empty();

		// [Failsafe] 정상 완료되었으므로 안전장치 타이머 해제
		if (UWorld* World = GetWorld())
		{
			World->GetTimerManager().ClearTimer(MPC_FailsafeTimer);
		}

		// MPC 값 읽기 검증 — 실제로 0이 되었는지 확인
		{
			const float ReadDesat = UKismetMaterialLibrary::GetScalarParameterValue(
				this, MPC_BossEncounter, TEXT("DesatAmount"));
			const float ReadDark = UKismetMaterialLibrary::GetScalarParameterValue(
				this, MPC_BossEncounter, TEXT("DarknessAmount"));
			const float ReadWave = UKismetMaterialLibrary::GetScalarParameterValue(
				this, MPC_BossEncounter, TEXT("ColorWaveRadius"));

			UE_LOG(LogTemp, Warning,
				TEXT("[BossEncounter_DIAG] Color wave COMPLETE — MPC READBACK: DesatAmount=%.4f, DarknessAmount=%.4f, ColorWaveRadius=%.1f, World=%s"),
				ReadDesat, ReadDark, ReadWave,
				GetWorld() ? *GetWorld()->GetName() : TEXT("NULL"));
		}
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
// [Failsafe] MPC 값 강제 복원
// ============================================================================

void ABossEncounterCube::ForceRestoreMPCValues()
{
	// 웨이브가 이미 완료되었으면 아무것도 안 함
	if (!bColorWaveActive && !bDesatTransitioning)
	{
		UE_LOG(LogTemp, Warning,
			TEXT("[BossEncounter_DIAG] Failsafe fired but wave already completed — skipping"));
		return;
	}

	UE_LOG(LogTemp, Error,
		TEXT("[BossEncounter_DIAG] ⚠️ Failsafe TRIGGERED — wave did NOT complete in 10s! bColorWaveActive=%d, bDesatTransitioning=%d, Radius=%.0f/%.0f"),
		(int)bColorWaveActive, (int)bDesatTransitioning, ColorWaveRadius, ColorWaveMaxRadius);

	// 강제로 웨이브 완료 상태로 설정
	bColorWaveActive = false;
	bDesatTransitioning = false;

	if (MPC_BossEncounter)
	{
		UKismetMaterialLibrary::SetScalarParameterValue(
			this, MPC_BossEncounter, TEXT("DesatAmount"), 0.f);
		UKismetMaterialLibrary::SetScalarParameterValue(
			this, MPC_BossEncounter, TEXT("DarknessAmount"), 0.f);
		UKismetMaterialLibrary::SetScalarParameterValue(
			this, MPC_BossEncounter, TEXT("ColorWaveRadius"), ColorWaveMaxRadius);

		UE_LOG(LogTemp, Warning,
			TEXT("[BossEncounter_DIAG] Failsafe — MPC forced: DesatAmount=0, DarknessAmount=0, ColorWaveRadius=%.0f"),
			ColorWaveMaxRadius);
	}
	else
	{
		UE_LOG(LogTemp, Error,
			TEXT("[BossEncounter_DIAG] Failsafe — MPC_BossEncounter is NULL! Cannot restore"));
	}

	// PP 볼륨 비활성화
	SetBossPostProcessEnabled(false);

	// Custom Depth 해제
	DisableBossEncounterCustomDepth();
}

// ============================================================================
// [PPV 제어] 보스 이벤트 PP 볼륨 탐색/활성화
// ============================================================================

void ABossEncounterCube::CacheBossPostProcessVolume()
{
	UWorld* World = GetWorld();
	if (!World) return;

	for (TActorIterator<APostProcessVolume> It(World); It; ++It)
	{
		APostProcessVolume* PPV = *It;
		if (!PPV) continue;

		// 1차: 에디터 라벨 검색 (PPV_BossEncounter)
#if WITH_EDITOR
		const FString Label = PPV->GetActorLabel();
		if (Label.Contains(TEXT("BossEncounter")))
		{
			CachedBossPPV = PPV;
			UE_LOG(LogTemp, Warning, TEXT("[BossEncounterCube] PPV cached (label match): Name=%s, Label=%s"),
				*PPV->GetName(), *Label);
			return;
		}
#endif

		// 2차: bUnbound + Blendable이 있는 PPV (PPV_BossEncounter는 bUnbound=true)
		if (PPV->bUnbound && PPV->Settings.WeightedBlendables.Array.Num() > 0)
		{
			CachedBossPPV = PPV;
			UE_LOG(LogTemp, Warning, TEXT("[BossEncounterCube] PPV cached (unbound+blendable): Name=%s"),
				*PPV->GetName());
			return;
		}
	}

	UE_LOG(LogTemp, Warning,
		TEXT("[BossEncounterCube] PPV NOT FOUND — PPV 비활성화 불가! 레벨에 bUnbound PPV with Blendable 필요"));
}

void ABossEncounterCube::SetBossPostProcessEnabled(bool bEnable)
{
	APostProcessVolume* PPV = CachedBossPPV.Get();

	// 캐시 미스 시 재탐색
	if (!PPV)
	{
		CacheBossPostProcessVolume();
		PPV = CachedBossPPV.Get();
	}

	if (PPV)
	{
		PPV->bEnabled = bEnable;
		PPV->BlendWeight = bEnable ? 1.f : 0.f;
		UE_LOG(LogTemp, Warning, TEXT("[BossEncounterCube] PPV %s (BlendWeight=%.1f, Name=%s)"),
			bEnable ? TEXT("ENABLED") : TEXT("DISABLED"), PPV->BlendWeight, *PPV->GetName());
	}
	else
	{
		UE_LOG(LogTemp, Warning, TEXT("[BossEncounterCube] SetBossPostProcessEnabled(%d) — PPV NULL!"),
			(int)bEnable);
	}
}

// ============================================================================
// [Wave VFX] 웨이브 경계 사물에 오라 VFX 스폰
// ============================================================================

void ABossEncounterCube::SpawnWaveAuraVFX(float DeltaTime)
{
	// VFX/머티리얼 전부 미할당이면 전체 스킵
	if (!WaveAuraVFX && !WaveHoloVFX && !WaveWireframeMaterial)
	{
		return;
	}

	// 데디서버 체크 (Tick 가드와 중복이지만 안전장치)
	if (IsRunningDedicatedServer())
	{
		return;
	}

	// 쿨다운 — 0.15초 간격으로 스폰 (0.1→0.15 완화)
	WaveVFXCooldown -= DeltaTime;
	if (WaveVFXCooldown > 0.f)
	{
		return;
	}
	WaveVFXCooldown = 0.15f;

	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	// 카메라 위치 — 카메라에서 먼 액터는 VFX 스킵
	FVector CameraLocation = FVector::ZeroVector;
	if (const APlayerController* PC = World->GetFirstPlayerController())
	{
		if (const APlayerCameraManager* CM = PC->PlayerCameraManager)
		{
			CameraLocation = CM->GetCameraLocation();
		}
	}
	const float MaxCameraDistSq = 7000.f * 7000.f; // 70m 이내만 VFX

	// 웨이브 경계 범위: [ColorWaveRadius - WaveEdgeThickness, ColorWaveRadius]
	const float InnerRadius = FMath::Max(0.f, ColorWaveRadius - WaveEdgeThickness);
	const float OuterRadius = ColorWaveRadius;

	int32 SpawnedThisFrame = 0;

	// ──────────────────────────────────────────────────────
	// Part A: StaticMeshActor만 순회 (AActor 전체 순회 → 대폭 축소)
	// ──────────────────────────────────────────────────────
	for (TActorIterator<AStaticMeshActor> It(World); It; ++It)
	{
		if (SpawnedThisFrame >= MaxVFXPerFrame)
		{
			break;
		}

		AStaticMeshActor* Actor = *It;
		if (!IsValid(Actor))
		{
			continue;
		}

		// 이미 스폰된 액터 스킵
		TWeakObjectPtr<AActor> WeakActor(Actor);
		if (AuraVFXSpawnedActors.Contains(WeakActor))
		{
			continue;
		}

		// 거리 체크: 웨이브 경계 밴드 안에 있는지
		const FVector ActorLoc = Actor->GetActorLocation();
		const float Dist = FVector::Dist(ActorLoc, WaveOrigin);
		if (Dist < InnerRadius || Dist > OuterRadius)
		{
			continue;
		}

		// 카메라 거리 필터: 70m 이내만 VFX 스폰
		if (FVector::DistSquared(ActorLoc, CameraLocation) > MaxCameraDistSq)
		{
			// 너무 멀면 VFX 없이 "처리됨" 등록 (다시 검사 방지)
			AuraVFXSpawnedActors.Add(WeakActor);
			continue;
		}

		// 최소 크기 필터: 50cm 미만 소형 액터(무기/아이템 등) 스킵
		FVector Origin, BoxExtent;
		Actor->GetActorBounds(false, Origin, BoxExtent);
		if (BoxExtent.GetMax() < 50.f)
		{
			AuraVFXSpawnedActors.Add(WeakActor);
			continue;
		}

		// 부적절 액터 블랙리스트 (이름 기반)
		const FString ActorName = Actor->GetName();
		if (ActorName.Contains(TEXT("Ultra_Dynamic")) ||
			ActorName.Contains(TEXT("Sky")) ||
			ActorName.Contains(TEXT("Weather")) ||
			ActorName.Contains(TEXT("Floor")) ||
			ActorName.Contains(TEXT("Landscape")))
		{
			AuraVFXSpawnedActors.Add(WeakActor);
			continue;
		}

		// [BossRestore] 이 액터가 DarkenedMeshes에 있으면 복원 시작
		bool bDarkenedRestoreTriggered = false;
		for (FDarkenedMesh& DM : DarkenedMeshes)
		{
			if (DM.TargetActor.IsValid() && DM.TargetActor.Get() == Actor && !DM.bRestoreStarted)
			{
				DM.bRestoreStarted = true;
				DM.ScanProgress = 0.f;
				bDarkenedRestoreTriggered = true;
				UE_LOG(LogTemp, Log, TEXT("[BossRestore] Restore started for: %s"), *Actor->GetName());
				break;
			}
		}

		ApplyWaveEffectsOnActor(Actor, World);
		AuraVFXSpawnedActors.Add(WeakActor);
		SpawnedThisFrame++;
	}

	// ──────────────────────────────────────────────────────
	// Part B: 폴리지 인스턴스 (InstancedStaticMesh)
	// ──────────────────────────────────────────────────────
	if (SpawnedThisFrame >= MaxVFXPerFrame)
	{
		return;
	}

	// 폴리지 위치 캐시 (웨이브 시작 후 최초 1회)
	if (!bFoliageCached)
	{
		CachedFoliageLocations.Empty();

		for (TActorIterator<AActor> It(World); It; ++It)
		{
			AActor* Actor = *It;
			if (!IsValid(Actor))
			{
				continue;
			}

			TInlineComponentArray<UInstancedStaticMeshComponent*> ISMs;
			Actor->GetComponents<UInstancedStaticMeshComponent>(ISMs);

			for (UInstancedStaticMeshComponent* ISM : ISMs)
			{
				if (!ISM)
				{
					continue;
				}

				const int32 Count = ISM->GetInstanceCount();
				for (int32 i = 0; i < Count; ++i)
				{
					if (FMath::FRand() > FoliageSampleRate)
					{
						continue;
					}

					FTransform InstanceTransform;
					if (ISM->GetInstanceTransform(i, InstanceTransform, true))
					{
						CachedFoliageLocations.Add(InstanceTransform.GetLocation());
					}
				}
			}
		}

		bFoliageCached = true;
		UE_LOG(LogTemp, Log, TEXT("[WaveVFX] Foliage cached: %d sampled locations (rate=%.0f%%)"),
			CachedFoliageLocations.Num(), FoliageSampleRate * 100.f);
	}

	// 캐시된 폴리지 위치 순회
	for (const FVector& FoliageLoc : CachedFoliageLocations)
	{
		if (SpawnedThisFrame >= MaxVFXPerFrame)
		{
			break;
		}

		const int32 Hash = GetTypeHash(FoliageLoc);
		if (AuraVFXSpawnedFoliageHashes.Contains(Hash))
		{
			continue;
		}

		const float Dist = FVector::Dist(FoliageLoc, WaveOrigin);
		if (Dist < InnerRadius || Dist > OuterRadius)
		{
			continue;
		}

		// 카메라 거리 필터 (폴리지도)
		if (FVector::DistSquared(FoliageLoc, CameraLocation) > MaxCameraDistSq)
		{
			AuraVFXSpawnedFoliageHashes.Add(Hash);
			continue;
		}

		UNiagaraSystem* FoliageNS = WaveHoloVFX ? WaveHoloVFX.Get() : WaveAuraVFX.Get();
		if (!FoliageNS)
		{
			AuraVFXSpawnedFoliageHashes.Add(Hash);
			SpawnedThisFrame++;
			continue;
		}

		UNiagaraComponent* SpawnedComp = UNiagaraFunctionLibrary::SpawnSystemAtLocation(
			World, FoliageNS, FoliageLoc,
			FRotator::ZeroRotator, FVector(WaveAuraScale * 0.6f),
			false, true);

		if (SpawnedComp)
		{
			TWeakObjectPtr<UNiagaraComponent> WeakComp(SpawnedComp);
			FTimerHandle DestroyTimerHandle;
			World->GetTimerManager().SetTimer(
				DestroyTimerHandle,
				FTimerDelegate::CreateLambda([WeakComp]()
				{
					if (WeakComp.IsValid())
					{
						WeakComp->DeactivateImmediate();
						WeakComp->DestroyComponent();
					}
				}),
				WaveAuraLifetime,
				false
			);
		}

		AuraVFXSpawnedFoliageHashes.Add(Hash);
		SpawnedThisFrame++;
	}
}

// ============================================================================
// [Wave VFX] 홀로그램 + 와이어프레임 효과 적용
// ============================================================================

void ABossEncounterCube::ApplyWaveEffectsOnActor(AActor* Actor, UWorld* World)
{
	if (!IsValid(Actor) || !World)
	{
		return;
	}

	// ── 바운딩 박스 크기 계산 ──
	FVector Origin, BoxExtent;
	Actor->GetActorBounds(false, Origin, BoxExtent);
	const float BaseSize = 100.f;
	FVector HoloScale(
		FMath::Max(BoxExtent.X * 2.f / BaseSize, 0.5f),
		FMath::Max(BoxExtent.Y * 2.f / BaseSize, 0.5f),
		FMath::Max(BoxExtent.Z * 2.f / BaseSize, 0.5f)
	);
	HoloScale *= WaveAuraScale;

	// ── Effect A: 홀로그램 나이아가라 (동시 30개 제한) ──
	if (WaveHoloVFX && TotalHoloSpawned < MaxTotalHoloVFX)
	{
		UNiagaraComponent* HoloComp = UNiagaraFunctionLibrary::SpawnSystemAtLocation(
			World, WaveHoloVFX, Origin,
			FRotator::ZeroRotator, HoloScale,
			false, true);

		if (HoloComp)
		{
			++TotalHoloSpawned;
			TWeakObjectPtr<UNiagaraComponent> WeakComp(HoloComp);
			TWeakObjectPtr<ABossEncounterCube> WeakSelf(this);
			const float Lifetime = WaveAuraLifetime;
			FTimerHandle H;
			World->GetTimerManager().SetTimer(H,
				FTimerDelegate::CreateLambda([WeakComp, WeakSelf]()
				{
					if (WeakSelf.IsValid()) { --WeakSelf->TotalHoloSpawned; }
					if (WeakComp.IsValid())
					{
						WeakComp->DeactivateImmediate();
						WeakComp->DestroyComponent();
					}
				}), Lifetime, false);
		}
	}
	else if (!WaveHoloVFX && WaveAuraVFX)
	{
		UNiagaraComponent* AuraComp = UNiagaraFunctionLibrary::SpawnSystemAtLocation(
			World, WaveAuraVFX, Actor->GetActorLocation(),
			FRotator::ZeroRotator, FVector(WaveAuraScale),
			false, true);

		if (AuraComp)
		{
			TWeakObjectPtr<UNiagaraComponent> WeakComp(AuraComp);
			const float Lifetime = WaveAuraLifetime;
			FTimerHandle H;
			World->GetTimerManager().SetTimer(H,
				FTimerDelegate::CreateLambda([WeakComp]()
				{
					if (WeakComp.IsValid())
					{
						WeakComp->DeactivateImmediate();
						WeakComp->DestroyComponent();
					}
				}), Lifetime, false);
		}
	}

	// ── Effect C: 와이어프레임 오버레이 (동시 15개 제한) ──
	if (WaveWireframeMaterial && ActiveWireframeOverlays.Num() < MaxActiveOverlays)
	{
		UStaticMeshComponent* OrigSMC = Actor->FindComponentByClass<UStaticMeshComponent>();
		if (OrigSMC && OrigSMC->GetStaticMesh())
		{
			FVector BoundsOrigin, BoundsExtent;
			Actor->GetActorBounds(false, BoundsOrigin, BoundsExtent);
			const float BottomZ = BoundsOrigin.Z - BoundsExtent.Z;
			const float TopZ = BoundsOrigin.Z + BoundsExtent.Z;

			UStaticMeshComponent* OverlayMesh = NewObject<UStaticMeshComponent>(Actor);
			if (OverlayMesh)
			{
				OverlayMesh->SetStaticMesh(OrigSMC->GetStaticMesh());
				OverlayMesh->SetWorldTransform(OrigSMC->GetComponentTransform());
				OverlayMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);

				UMaterialInstanceDynamic* DMI = UMaterialInstanceDynamic::Create(WaveWireframeMaterial, this);
				if (DMI)
				{
					DMI->SetScalarParameterValue(TEXT("ScanHeight"), BottomZ);
					const float BandWidth = FMath::Max((TopZ - BottomZ) * ScanBandRatio, 10.f);
					DMI->SetScalarParameterValue(TEXT("BandWidth"), BandWidth);

					const int32 NumMats = OrigSMC->GetNumMaterials();
					for (int32 i = 0; i < NumMats; ++i)
					{
						OverlayMesh->SetMaterial(i, DMI);
					}

					FVector OrigScale = OrigSMC->GetComponentScale();
					OverlayMesh->SetWorldScale3D(OrigScale * 1.02f);
					OverlayMesh->SetVisibility(true);
					OverlayMesh->RegisterComponent();
					if (Actor->GetRootComponent())
					{
						OverlayMesh->AttachToComponent(
							Actor->GetRootComponent(),
							FAttachmentTransformRules::KeepWorldTransform);
					}

					FWireframeOverlay Overlay;
					Overlay.OverlayMesh = OverlayMesh;
					Overlay.DMI = DMI;
					Overlay.ScanProgress = 0.f;
					Overlay.BottomZ = BottomZ;
					Overlay.TopZ = TopZ;
					Overlay.OrigScaleZ = OrigScale.X;
					Overlay.OrigLocation = OrigSMC->GetComponentLocation();
					Overlay.TargetActor = Actor;
					ActiveWireframeOverlays.Add(Overlay);
				}
			}
		}
	}
}

// ============================================================================
// [Wave VFX] 와이어프레임 오버레이 스캔 라인 애니메이션
// ============================================================================

void ABossEncounterCube::TickWireframeOverlays(float DeltaTime)
{
	for (int32 i = ActiveWireframeOverlays.Num() - 1; i >= 0; --i)
	{
		FWireframeOverlay& Overlay = ActiveWireframeOverlays[i];

		if (!Overlay.OverlayMesh.IsValid())
		{
			ActiveWireframeOverlays.RemoveAt(i);
			continue;
		}

		// 스캔 진행 (0→1)
		Overlay.ScanProgress += DeltaTime / ScanDuration;

		if (Overlay.ScanProgress >= 1.0f)
		{
			// 스캔 완료 — 오버레이 제거
			Overlay.OverlayMesh->DestroyComponent();
			ActiveWireframeOverlays.RemoveAt(i);
			continue;
		}

		// ── ScanHeight 계산: BottomZ → TopZ로 Lerp ──
		const float CurrentScanZ = FMath::Lerp(Overlay.BottomZ, Overlay.TopZ, Overlay.ScanProgress);

		// ── DMI에 ScanHeight 업데이트 ──
		if (Overlay.DMI)
		{
			Overlay.DMI->SetScalarParameterValue(TEXT("ScanHeight"), CurrentScanZ);

			// 밴드 두께: 메시 높이의 ScanBandRatio
			const float MeshHeight = Overlay.TopZ - Overlay.BottomZ;
			const float BandWidth = FMath::Max(MeshHeight * ScanBandRatio, 10.f);
			Overlay.DMI->SetScalarParameterValue(TEXT("BandWidth"), BandWidth);
		}

		UE_LOG(LogTemp, VeryVerbose, TEXT("[WaveVFX] Scan: progress=%.2f, scanZ=%.0f, actor=%s"),
			Overlay.ScanProgress, CurrentScanZ,
			Overlay.TargetActor.IsValid() ? *Overlay.TargetActor->GetName() : TEXT("None"));
	}
}

// ============================================================================
// [Cinematic] 보스 사망 시네마틱 연출
// ============================================================================

void ABossEncounterCube::CreateCinematicPostProcess()
{
	if (CinematicPostProcess)
	{
		return;
	}

	CinematicPostProcess = NewObject<UPostProcessComponent>(this, TEXT("CinematicPostProcess"));
	if (!CinematicPostProcess)
	{
		UE_LOG(LogTemp, Error, TEXT("[BossEncounterCube] Failed to create CinematicPostProcess"));
		return;
	}

	CinematicPostProcess->RegisterComponent();
	CinematicPostProcess->bUnbound = true; // 화면 전체에 적용
	CinematicPostProcess->Priority = 100.f; // 다른 PP보다 높은 우선순위

	UE_LOG(LogTemp, Log, TEXT("[BossEncounterCube] CinematicPostProcess created (unbound, priority=100)"));
}

void ABossEncounterCube::StartCinematicSequence(FVector DeathLocation)
{
	CinematicDeathLocation = DeathLocation;

	// Phase 0 즉시 시작
	CinematicPhase0_SlowMotion();

	UE_LOG(LogTemp, Verbose, TEXT("[BossEncounterCube] Cinematic sequence started at %s"),
		*DeathLocation.ToString());
}

void ABossEncounterCube::CinematicPhase0_SlowMotion()
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	// 슬로모션 적용
	UGameplayStatics::SetGlobalTimeDilation(World, SlowMotionScale);
	UE_LOG(LogTemp, Verbose, TEXT("[BossEncounterCube] Phase0: SlowMotion = %.2f for %.2fs (game time)"),
		SlowMotionScale, SlowMotionDuration);

	// Phase 1을 SlowMotionDuration 뒤에 호출
	// 타이머는 게임 시간 기준이므로 실제 체감은 더 길어짐 (0.3/0.3 ≈ 1초)
	World->GetTimerManager().SetTimer(
		CinematicTimer_Phase1,
		this,
		&ABossEncounterCube::CinematicPhase1_Explosion,
		SlowMotionDuration,
		false);
}

void ABossEncounterCube::CinematicPhase1_Explosion()
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	// ── 슬로모션 복원 (이후 모든 타이머는 정상 속도) ──
	UGameplayStatics::SetGlobalTimeDilation(World, 1.0f);
	UE_LOG(LogTemp, Verbose, TEXT("[BossEncounterCube] Phase1: TimeDilation restored to 1.0"));

	// ── 시네마틱 PostProcess 생성 ──
	CreateCinematicPostProcess();

	APlayerController* PC = World->GetFirstPlayerController();

	// ── 카메라 셰이크 ──
	if (PC && BossDeathCameraShake)
	{
		PC->ClientStartCameraShake(BossDeathCameraShake, 1.0f);
		UE_LOG(LogTemp, Log, TEXT("[BossEncounterCube] Phase1: Camera shake started"));
	}

	// ── FOV 펀치 ──
	if (PC)
	{
		APawn* Pawn = PC->GetPawn();
		if (Pawn)
		{
			UCameraComponent* Cam = Pawn->FindComponentByClass<UCameraComponent>();
			if (Cam)
			{
				CinematicOriginalFOV = Cam->FieldOfView;
				Cam->SetFieldOfView(CinematicOriginalFOV + FOVPunchAmount);
				bCinematicFOVActive = true;
				UE_LOG(LogTemp, Log, TEXT("[BossEncounterCube] Phase1: FOV punch %.1f → %.1f"),
					CinematicOriginalFOV, CinematicOriginalFOV + FOVPunchAmount);
			}
		}
	}

	// ── 크로매틱 수차 ──
	if (CinematicPostProcess)
	{
		CinematicPostProcess->Settings.bOverride_SceneFringeIntensity = true;
		CinematicPostProcess->Settings.SceneFringeIntensity = 5.0f;
		bCinematicChromaActive = true;
		UE_LOG(LogTemp, Log, TEXT("[BossEncounterCube] Phase1: Chromatic aberration = 5.0"));
	}

	// ── 보스 사망 VFX (나이아가라) ──
	if (BossDeathVFX)
	{
		UNiagaraFunctionLibrary::SpawnSystemAtLocation(
			World, BossDeathVFX, CinematicDeathLocation,
			FRotator::ZeroRotator, FVector(1.f), true, true);
		UE_LOG(LogTemp, Log, TEXT("[BossEncounterCube] Phase1: Boss death VFX spawned at %s"),
			*CinematicDeathLocation.ToString());
	}

	// ── 바람 VFX (나이아가라) ──
	if (WindVFX)
	{
		UNiagaraFunctionLibrary::SpawnSystemAtLocation(
			World, WindVFX, CinematicDeathLocation,
			FRotator::ZeroRotator, FVector(1.f), true, true);
		UE_LOG(LogTemp, Log, TEXT("[BossEncounterCube] Phase1: Wind VFX spawned at %s"),
			*CinematicDeathLocation.ToString());
	}

	// ── 사운드 ──
	if (BossDeathSound)
	{
		UGameplayStatics::PlaySoundAtLocation(
			World, BossDeathSound, CinematicDeathLocation);
		UE_LOG(LogTemp, Log, TEXT("[BossEncounterCube] Phase1: Boss death sound played"));
	}

	// ── 후속 Phase 타이머 체인 (정상 속도) ──
	// Phase 2: 0.5초 뒤 (순백 섬광)
	World->GetTimerManager().SetTimer(
		CinematicTimer_Phase2,
		this,
		&ABossEncounterCube::CinematicPhase2_WhiteFlash,
		0.5f,
		false);

	// Phase 4: 3.2초 뒤 (채도 오버슈트 — 웨이브 ~80%)
	World->GetTimerManager().SetTimer(
		CinematicTimer_Phase4,
		this,
		&ABossEncounterCube::CinematicPhase4_SaturationOvershoot,
		3.2f,
		false);

	// Phase 5: 4.7초 뒤 (전체 정리)
	World->GetTimerManager().SetTimer(
		CinematicTimer_Phase5,
		this,
		&ABossEncounterCube::CinematicPhase5_Cleanup,
		4.7f,
		false);

	UE_LOG(LogTemp, Verbose, TEXT("[BossEncounterCube] Phase1: Explosion complete — timers chained (Phase2=0.5s, Phase4=3.2s, Phase5=4.7s)"));
}

void ABossEncounterCube::CinematicPhase2_WhiteFlash()
{
	if (!CinematicPostProcess)
	{
		return;
	}

	// 순백 섬광: 과노출 화이트 (3,3,3,1) → Tick에서 (1,1,1,1)로 Lerp
	CinematicPostProcess->Settings.bOverride_SceneColorTint = true;
	CinematicPostProcess->Settings.SceneColorTint = FLinearColor(3.f, 3.f, 3.f, 1.f);

	bCinematicFlashActive = true;
	CinematicFlashAlpha = 1.f;

	UE_LOG(LogTemp, Verbose, TEXT("[BossEncounterCube] Phase2: White flash — SceneColorTint = (3,3,3)"));
}

void ABossEncounterCube::CinematicPhase4_SaturationOvershoot()
{
	if (!CinematicPostProcess)
	{
		return;
	}

	// 채도 오버슈트: SaturationOvershootScale → Tick에서 1.0으로 Lerp
	CinematicPostProcess->Settings.bOverride_ColorSaturation = true;
	CinematicPostProcess->Settings.ColorSaturation = FVector4(
		SaturationOvershootScale, SaturationOvershootScale, SaturationOvershootScale, 1.f);

	bCinematicSatBoostActive = true;

	UE_LOG(LogTemp, Verbose, TEXT("[BossEncounterCube] Phase4: Saturation overshoot = %.2f"), SaturationOvershootScale);
}

void ABossEncounterCube::CinematicPhase5_Cleanup()
{
	// ── 모든 시네마틱 플래그 리셋 ──
	bCinematicFOVActive = false;
	bCinematicChromaActive = false;
	bCinematicFlashActive = false;
	bCinematicSatBoostActive = false;

	// ── FOV 복원 ──
	UWorld* World = GetWorld();
	if (World)
	{
		APlayerController* PC = World->GetFirstPlayerController();
		if (PC)
		{
			APawn* Pawn = PC->GetPawn();
			if (Pawn)
			{
				UCameraComponent* Cam = Pawn->FindComponentByClass<UCameraComponent>();
				if (Cam)
				{
					Cam->SetFieldOfView(CinematicOriginalFOV);
				}
			}
		}

		// 슬로모션 안전 복원 (만약 Phase1이 실행되지 않았을 경우를 대비)
		UGameplayStatics::SetGlobalTimeDilation(World, 1.0f);
	}

	// ── PostProcess 제거 ──
	if (CinematicPostProcess)
	{
		CinematicPostProcess->DestroyComponent();
		CinematicPostProcess = nullptr;
	}

	// ── 타이머 전부 정리 ──
	if (World)
	{
		World->GetTimerManager().ClearTimer(CinematicTimer_Phase1);
		World->GetTimerManager().ClearTimer(CinematicTimer_Phase2);
		World->GetTimerManager().ClearTimer(CinematicTimer_Phase4);
		World->GetTimerManager().ClearTimer(CinematicTimer_Phase5);
	}

	UE_LOG(LogTemp, Verbose, TEXT("[BossEncounterCube] Phase5: Cinematic cleanup complete — all effects reset"));
}

void ABossEncounterCube::TickCinematic(float DeltaTime)
{
	// ── FOV Lerp (0.5초에 걸쳐 원래 FOV로 복귀) ──
	if (bCinematicFOVActive)
	{
		UWorld* World = GetWorld();
		if (World)
		{
			APlayerController* PC = World->GetFirstPlayerController();
			if (PC)
			{
				APawn* Pawn = PC->GetPawn();
				if (Pawn)
				{
					UCameraComponent* Cam = Pawn->FindComponentByClass<UCameraComponent>();
					if (Cam)
					{
						const float Current = Cam->FieldOfView;
						const float NewFOV = FMath::FInterpTo(Current, CinematicOriginalFOV, DeltaTime, 4.f); // ~0.5초
						Cam->SetFieldOfView(NewFOV);

						if (FMath::IsNearlyEqual(NewFOV, CinematicOriginalFOV, 0.1f))
						{
							Cam->SetFieldOfView(CinematicOriginalFOV);
							bCinematicFOVActive = false;
						}
					}
				}
			}
		}
	}

	// ── 크로매틱 수차 페이드아웃 (0.5초) ──
	if (bCinematicChromaActive && CinematicPostProcess)
	{
		float& Intensity = CinematicPostProcess->Settings.SceneFringeIntensity;
		Intensity = FMath::FInterpTo(Intensity, 0.f, DeltaTime, 4.f); // ~0.5초

		if (Intensity < 0.01f)
		{
			Intensity = 0.f;
			CinematicPostProcess->Settings.bOverride_SceneFringeIntensity = false;
			bCinematicChromaActive = false;
		}
	}

	// ── 순백 섬광 페이드아웃 (WhiteFlashDuration) ──
	if (bCinematicFlashActive && CinematicPostProcess)
	{
		const float FlashSpeed = 1.f / FMath::Max(WhiteFlashDuration, 0.01f);
		CinematicFlashAlpha = FMath::Max(0.f, CinematicFlashAlpha - DeltaTime * FlashSpeed);

		// (3,3,3) → (1,1,1) Lerp
		const float TintValue = FMath::Lerp(1.f, 3.f, CinematicFlashAlpha);
		CinematicPostProcess->Settings.SceneColorTint = FLinearColor(TintValue, TintValue, TintValue, 1.f);

		if (CinematicFlashAlpha <= 0.f)
		{
			CinematicPostProcess->Settings.SceneColorTint = FLinearColor(1.f, 1.f, 1.f, 1.f);
			CinematicPostProcess->Settings.bOverride_SceneColorTint = false;
			bCinematicFlashActive = false;
		}
	}

	// ── 채도 오버슈트 Lerp (1.5초에 걸쳐 1.0으로 복귀) ──
	if (bCinematicSatBoostActive && CinematicPostProcess)
	{
		FVector4& Sat = CinematicPostProcess->Settings.ColorSaturation;
		const float Current = Sat.X;
		const float NewVal = FMath::FInterpTo(Current, 1.f, DeltaTime, 1.5f); // ~1.5초

		Sat = FVector4(NewVal, NewVal, NewVal, 1.f);

		if (FMath::IsNearlyEqual(NewVal, 1.f, 0.005f))
		{
			Sat = FVector4(1.f, 1.f, 1.f, 1.f);
			CinematicPostProcess->Settings.bOverride_ColorSaturation = false;
			bCinematicSatBoostActive = false;
		}
	}
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

// ============================================================================
// [BossRestore] 보스 영역 내 환경 메쉬 어둡게 처리
// ============================================================================

void ABossEncounterCube::DarkenEnvironmentMeshes()
{
	if (!BossRestoreMaterial)
	{
		return;
	}

	// 이미 어둡게 처리된 상태면 스킵
	if (DarkenedMeshes.Num() > 0)
	{
		return;
	}

	// 데디 서버에서는 비주얼 불필요
	if (IsRunningDedicatedServer())
	{
		return;
	}

	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	const FVector AreaCenter = GetActorLocation();
	int32 DarkenedCount = 0;

	for (TActorIterator<AStaticMeshActor> It(World); It; ++It)
	{
		if (DarkenedCount >= MaxDarkenedMeshes)
		{
			break;
		}

		AStaticMeshActor* Actor = *It;
		if (!IsValid(Actor))
		{
			continue;
		}

		// 거리 체크
		const float Dist = FVector::Dist(Actor->GetActorLocation(), AreaCenter);
		if (Dist > BossAreaRadius)
		{
			continue;
		}

		// 블랙리스트 (하늘/날씨/바닥/랜드스케이프)
		const FString ActorName = Actor->GetName();
		if (ActorName.Contains(TEXT("Ultra_Dynamic")) ||
			ActorName.Contains(TEXT("Sky")) ||
			ActorName.Contains(TEXT("Weather")) ||
			ActorName.Contains(TEXT("Floor")) ||
			ActorName.Contains(TEXT("Landscape")))
		{
			continue;
		}

		// 최소 크기 필터
		FVector Origin, BoxExtent;
		Actor->GetActorBounds(false, Origin, BoxExtent);
		if (BoxExtent.GetMax() < 50.f)
		{
			continue;
		}

		UStaticMeshComponent* SMC = Actor->FindComponentByClass<UStaticMeshComponent>();
		if (!SMC || !SMC->GetStaticMesh())
		{
			continue;
		}

		// 바운딩 Z 계산
		const float BottomZ = Origin.Z - BoxExtent.Z;
		const float TopZ = Origin.Z + BoxExtent.Z;
		const float MeshHeight = TopZ - BottomZ;
		if (MeshHeight < 1.f)
		{
			continue;
		}

		// 원본 머티리얼 백업
		const int32 NumMats = SMC->GetNumMaterials();
		if (NumMats <= 0)
		{
			continue;
		}

		FDarkenedMesh DM;
		DM.TargetActor = Actor;
		DM.MeshComp = SMC;
		DM.BottomZ = BottomZ;
		DM.TopZ = TopZ;
		DM.BandWidth = FMath::Max(MeshHeight * ScanBandRatio, 10.f);

		for (int32 i = 0; i < NumMats; ++i)
		{
			DM.OriginalMaterials.Add(SMC->GetMaterial(i));
		}

		// DMI 생성
		UMaterialInstanceDynamic* DMI = UMaterialInstanceDynamic::Create(BossRestoreMaterial, this);
		if (!DMI)
		{
			continue;
		}

		// 텍스처 추출 시도 (슬롯 0의 원본 머티리얼에서)
		UTexture* BaseColorTex = ExtractBaseColorTexture(DM.OriginalMaterials.IsValidIndex(0) ? DM.OriginalMaterials[0] : nullptr);
		if (BaseColorTex)
		{
			DMI->SetTextureParameterValue(TEXT("BaseColorTexture"), BaseColorTex);
		}
		else
		{
			UE_LOG(LogTemp, Warning, TEXT("[BossRestore] Texture extraction failed for %s — using fallback"), *Actor->GetName());
		}

		// ScanHeight를 메쉬 바닥 아래로 설정 → 전체 어두움
		DMI->SetScalarParameterValue(TEXT("ScanHeight"), BottomZ - 100.f);
		DMI->SetScalarParameterValue(TEXT("BandWidth"), DM.BandWidth);

		DM.DMI = DMI;

		// 모든 슬롯에 DMI 적용
		for (int32 i = 0; i < NumMats; ++i)
		{
			SMC->SetMaterial(i, DMI);
		}

		DarkenedMeshes.Add(MoveTemp(DM));
		DarkenedCount++;
	}

	UE_LOG(LogTemp, Warning, TEXT("[BossRestore] Darkened %d meshes in boss area (Radius=%.0f)"),
		DarkenedCount, BossAreaRadius);
}

// ============================================================================
// [BossRestore] 머티리얼에서 BaseColor 텍스처 추출 시도
// ============================================================================

UTexture* ABossEncounterCube::ExtractBaseColorTexture(UMaterialInterface* Material)
{
	if (!Material)
	{
		return nullptr;
	}

	// 1순위: 일반적인 BaseColor 파라미터 이름으로 시도
	static const FName BaseColorNames[] = {
		TEXT("BaseColor"),
		TEXT("Base Color"),
		TEXT("Diffuse"),
		TEXT("Albedo"),
		TEXT("BaseColorTexture"),
		TEXT("Texture"),
	};

	UTexture* OutTexture = nullptr;
	for (const FName& ParamName : BaseColorNames)
	{
		FMaterialParameterInfo ParamInfo(ParamName);
		if (Material->GetTextureParameterValue(ParamInfo, OutTexture))
		{
			if (OutTexture)
			{
				return OutTexture;
			}
		}
	}

	// 2순위: 모든 텍스처 파라미터를 열거하여 첫 번째 반환
	TArray<FMaterialParameterInfo> TextureParams;
	TArray<FGuid> Guids;
	Material->GetAllTextureParameterInfo(TextureParams, Guids);

	for (const FMaterialParameterInfo& Info : TextureParams)
	{
		if (Material->GetTextureParameterValue(Info, OutTexture))
		{
			if (OutTexture)
			{
				return OutTexture;
			}
		}
	}

	// 3순위: 머티리얼이 사용하는 모든 텍스처에서 첫 번째 Texture2D
	TArray<UTexture*> UsedTextures;
	Material->GetUsedTextures(UsedTextures);

	for (UTexture* Tex : UsedTextures)
	{
		if (Tex && Tex->IsA<UTexture2D>())
		{
			return Tex;
		}
	}

	return nullptr;
}

// ============================================================================
// [BossRestore] 복원 스캔 Tick
// ============================================================================

void ABossEncounterCube::TickBossRestore(float DeltaTime)
{
	if (DarkenedMeshes.Num() == 0)
	{
		return;
	}

	for (int32 i = DarkenedMeshes.Num() - 1; i >= 0; --i)
	{
		FDarkenedMesh& DM = DarkenedMeshes[i];

		// 아직 웨이브 미도달
		if (!DM.bRestoreStarted)
		{
			continue;
		}

		// 타겟 유효성 체크
		if (!DM.MeshComp.IsValid())
		{
			DarkenedMeshes.RemoveAt(i);
			continue;
		}

		DM.ScanProgress += DeltaTime / FMath::Max(RestoreScanDuration, 0.1f);

		if (DM.ScanProgress >= 1.0f)
		{
			// 복원 완료 → 원본 머티리얼 복원
			RestoreDarkenedMesh(DM);
			DarkenedMeshes.RemoveAt(i);
			continue;
		}

		// ScanHeight 업데이트: BottomZ → TopZ
		if (DM.DMI)
		{
			const float CurrentScanZ = FMath::Lerp(DM.BottomZ, DM.TopZ, DM.ScanProgress);
			DM.DMI->SetScalarParameterValue(TEXT("ScanHeight"), CurrentScanZ);
		}
	}
}

// ============================================================================
// [BossRestore] 개별 메쉬 원본 머티리얼 복원
// ============================================================================

void ABossEncounterCube::RestoreDarkenedMesh(FDarkenedMesh& Mesh)
{
	if (!Mesh.MeshComp.IsValid())
	{
		return;
	}

	UStaticMeshComponent* SMC = Mesh.MeshComp.Get();
	const int32 NumSlots = FMath::Min(Mesh.OriginalMaterials.Num(), SMC->GetNumMaterials());

	for (int32 i = 0; i < NumSlots; ++i)
	{
		if (Mesh.OriginalMaterials[i])
		{
			SMC->SetMaterial(i, Mesh.OriginalMaterials[i]);
		}
	}

	UE_LOG(LogTemp, Log, TEXT("[BossRestore] Mesh restored: %s (%d slots)"),
		Mesh.TargetActor.IsValid() ? *Mesh.TargetActor->GetName() : TEXT("INVALID"),
		NumSlots);
}
