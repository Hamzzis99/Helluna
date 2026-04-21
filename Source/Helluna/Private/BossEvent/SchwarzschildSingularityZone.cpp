// Capstone Project Helluna

#include "BossEvent/SchwarzschildSingularityZone.h"

#include "Character/HellunaHeroCharacter.h"
#include "Character/HellunaEnemyCharacter.h"
#include "NiagaraFunctionLibrary.h"
#include "NiagaraComponent.h"
#include "NiagaraSystem.h"
#include "Components/StaticMeshComponent.h"
#include "Components/SceneCaptureComponent2D.h"
#include "Components/PointLightComponent.h"
#include "Components/PrimitiveComponent.h"
#include "Components/PostProcessComponent.h"
#include "PhysicsEngine/RadialForceComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Engine/Scene.h"
#include "Engine/OverlapResult.h"
#include "WorldCollision.h"
#include "Materials/MaterialInterface.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Kismet/GameplayStatics.h"
#include "Camera/CameraComponent.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "EngineUtils.h"
#include "UObject/ConstructorHelpers.h"

ASchwarzschildSingularityZone::ASchwarzschildSingularityZone()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.bStartWithTickEnabled = false; // BP editor preview에서 자동 tick 금지

	HorizonMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("HorizonMesh"));
	RootComponent = HorizonMesh;
	HorizonMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	HorizonMesh->SetMobility(EComponentMobility::Movable);
	HorizonMesh->SetCastShadow(false);

	// 기본 구체 메시 자동 로드 (사건 지평선 본체)
	static ConstructorHelpers::FObjectFinder<UStaticMesh> SphereMesh(TEXT("/Engine/BasicShapes/Sphere.Sphere"));
	if (SphereMesh.Succeeded())
	{
		HorizonMesh->SetStaticMesh(SphereMesh.Object);
	}

	// ═══════════════════════════════════════════════════════════
	// 고급 컴포넌트 서브오브젝트
	// ═══════════════════════════════════════════════════════════

	SceneCapture = CreateDefaultSubobject<USceneCaptureComponent2D>(TEXT("SceneCapture"));
	SceneCapture->SetupAttachment(HorizonMesh);
	SceneCapture->CaptureSource = ESceneCaptureSource::SCS_FinalColorLDR;
	SceneCapture->bCaptureEveryFrame = false;
	SceneCapture->bCaptureOnMovement = false;
	SceneCapture->FOVAngle = 120.f;

	RadialForce = CreateDefaultSubobject<URadialForceComponent>(TEXT("RadialForce"));
	RadialForce->SetupAttachment(HorizonMesh);
	RadialForce->Radius = 3000.f;
	RadialForce->ForceStrength = -50000.f;
	RadialForce->Falloff = RIF_Linear;
	RadialForce->bImpulseVelChange = false;
	RadialForce->bIgnoreOwningActor = true;
	RadialForce->bAutoActivate = false;

	PostProcess = CreateDefaultSubobject<UPostProcessComponent>(TEXT("PostProcess"));
	PostProcess->SetupAttachment(HorizonMesh);
	PostProcess->bUnbound = false;
	PostProcess->BlendRadius = 3000.f;
	PostProcess->BlendWeight = 0.f;

	AccretionLight = CreateDefaultSubobject<UPointLightComponent>(TEXT("AccretionLight"));
	AccretionLight->SetupAttachment(HorizonMesh);
	AccretionLight->SetIntensity(0.f);
	AccretionLight->SetLightColor(FLinearColor(1.f, 0.6f, 0.3f));
	AccretionLight->AttenuationRadius = 4000.f;
	AccretionLight->SetCastShadows(false);
}

void ASchwarzschildSingularityZone::ActivateZone()
{
	if (!OwnerEnemy) { NotifyPatternFinished(false); return; }

	bZoneActive = true;
	PatternElapsed = 0.0;
	CurrentPhase = EPhase::Formation;
	EffectiveRs = 0.0;

	InitializePhotons();
	InitializeDiskParticles();

	if (HorizonVFX)
	{
		// Attached 대신 Location 기반 스폰 — 메시 스케일 변화에 영향받지 않음
		HorizonVFXComp = UNiagaraFunctionLibrary::SpawnSystemAtLocation(
			GetWorld(), HorizonVFX, GetActorLocation(), FRotator::ZeroRotator,
			FVector(FMath::Max(1.0f, SchwarzschildRadius / 100.f)),
			false, true, ENCPoolMethod::None);
	}

	// 지평선 메시: 엔진 기본 구체 반지름 50cm → r_s cm 매칭
	const float MeshScale = FMath::Max(0.1f, SchwarzschildRadius / 50.f);
	HorizonMesh->SetWorldScale3D(FVector(MeshScale * 0.1f)); // 초기 10% → Formation에서 100%로 성장

	// 블랙 머티리얼 런타임 적용 + MID로 베이스컬러 강제 검게
	if (HorizonBlackMaterial)
	{
		UMaterialInstanceDynamic* MID = UMaterialInstanceDynamic::Create(HorizonBlackMaterial, this);
		if (MID)
		{
			MID->SetVectorParameterValue(TEXT("BaseColor"), FLinearColor::Black);
			MID->SetVectorParameterValue(TEXT("Color"), FLinearColor::Black);
			MID->SetScalarParameterValue(TEXT("Emissive"), 0.f);
			HorizonMesh->SetMaterial(0, MID);
		}
	}


	UE_LOG(LogTemp, Warning, TEXT("[Schwarzschild] ActivateZone at %s | Rs=%.0f | Photons=%d | Disk=%d | Duration=%.2f"),
		*GetActorLocation().ToString(), SchwarzschildRadius, PhotonCount, DiskParticleCount, PatternDuration);

	// 렌즈링 PostProcess MID 생성 (선택)
	if (LensingMaterial)
	{
		LensingMID = UMaterialInstanceDynamic::Create(LensingMaterial, this);
		if (LensingMID)
		{
			LensingMID->SetScalarParameterValue(TEXT("SchwarzschildRadius"), 0.f);
		}
	}

	SetActorTickEnabled(true);

	// ═══════════════════════════════════════════════════════════
	// 런타임 RenderTarget 생성 + SceneCapture 연결
	// ═══════════════════════════════════════════════════════════
	if (SceneCapture)
	{
		CaptureRenderTarget = NewObject<UTextureRenderTarget2D>(this);
		CaptureRenderTarget->RenderTargetFormat = RTF_RGBA8;
		CaptureRenderTarget->InitAutoFormat(CaptureResolution, CaptureResolution);
		CaptureRenderTarget->UpdateResourceImmediate(true);
		SceneCapture->TextureTarget = CaptureRenderTarget;
		SceneCapture->ShowFlags.SetAtmosphere(true);
		SceneCapture->ShowFlags.SetFog(true);

		// MID에 캡처 텍스처 텍스처 파라미터로 전달 (머티리얼이 "HorizonCapture" 파라미터 사용 시)
		if (UMaterialInterface* Mat = HorizonMesh->GetMaterial(0))
		{
			if (UMaterialInstanceDynamic* MID = Cast<UMaterialInstanceDynamic>(Mat))
			{
				MID->SetTextureParameterValue(TEXT("HorizonCapture"), CaptureRenderTarget);
			}
		}
	}

	// RadialForceComponent 활성화 + 반경을 r_s×10 으로 설정
	if (RadialForce && bApplyPhysicsForce)
	{
		RadialForce->Radius = SchwarzschildRadius * 10.f;
		RadialForce->ForceStrength = -GravitationalParameter * 0.01f;
		RadialForce->Activate(true);
	}

	// PostProcess 설정 — 색수차 / 비네트 베이스라인
	if (PostProcess && bApplyPostProcess)
	{
		PostProcess->BlendRadius = SchwarzschildRadius * 10.f;
		PostProcess->Settings.bOverride_SceneFringeIntensity = true;
		PostProcess->Settings.bOverride_VignetteIntensity = true;
		PostProcess->Settings.bOverride_ColorSaturation = true;
		PostProcess->Settings.SceneFringeIntensity = 0.f;
		PostProcess->Settings.VignetteIntensity = 0.f;
		PostProcess->Settings.ColorSaturation = FVector4(1, 1, 1, 1);
	}

	// 포인트 라이트 ON
	if (AccretionLight)
	{
		AccretionLight->SetIntensity(5000.f);
		AccretionLight->AttenuationRadius = SchwarzschildRadius * 12.f;
	}

	// 피격 피드백 바인드 (플레이어가 블랙홀을 쏘면 리플 트리거)
	HorizonMesh->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
	HorizonMesh->SetCollisionObjectType(ECC_WorldDynamic);
	HorizonMesh->SetCollisionResponseToAllChannels(ECR_Overlap);
	HorizonMesh->SetCollisionResponseToChannel(ECC_Visibility, ECR_Block);
	OnTakeAnyDamage.AddDynamic(this, &ASchwarzschildSingularityZone::HandleDamageFeedback);

	GetWorldTimerManager().SetTimer(PatternEndTimerHandle, [this]()
	{
		DeactivateZone();
	}, PatternDuration, false);
}

void ASchwarzschildSingularityZone::DeactivateZone()
{
	if (!bZoneActive) return;
	bZoneActive = false;
	SetActorTickEnabled(false);

	DestroyAllVFX();

	// 플레이어 시간 지연 복원
	for (auto& Pair : OriginalTimeDilation)
	{
		if (Pair.Key.IsValid())
		{
			Pair.Key->CustomTimeDilation = Pair.Value;
		}
	}
	OriginalTimeDilation.Empty();

	// 물리 스케일 복원 (조석 분리된 액터들)
	for (auto& Pair : OriginalPhysicsScale)
	{
		if (Pair.Key.IsValid())
		{
			Pair.Key->SetWorldScale3D(Pair.Value);
		}
	}
	OriginalPhysicsScale.Empty();

	if (RadialForce) RadialForce->Deactivate();
	if (AccretionLight) AccretionLight->SetIntensity(0.f);
	if (PostProcess) PostProcess->BlendWeight = 0.f;

	OnTakeAnyDamage.RemoveDynamic(this, &ASchwarzschildSingularityZone::HandleDamageFeedback);

	GetWorldTimerManager().ClearTimer(PatternEndTimerHandle);

	UWorld* World = GetWorld();
	if (!World || World->bIsTearingDown) return;
	NotifyPatternFinished(false);
}

void ASchwarzschildSingularityZone::DestroyAllVFX()
{
	for (FPhoton& P : Photons)
	{
		if (P.VFXComp.IsValid()) P.VFXComp->DestroyComponent();
	}
	Photons.Empty();

	for (FDiskParticle& D : DiskParticles)
	{
		if (D.VFXComp.IsValid()) D.VFXComp->DestroyComponent();
	}
	DiskParticles.Empty();

	if (HorizonVFXComp.IsValid())
	{
		HorizonVFXComp->DestroyComponent();
	}
}

// ═════════════════════════════════════════════════════════════
// 광자 (Photon Sphere) 초기화
// ═════════════════════════════════════════════════════════════
void ASchwarzschildSingularityZone::InitializePhotons()
{
	Photons.Empty();
	Photons.Reserve(PhotonCount);

	const double Rs = SchwarzschildRadius;
	const double Rph = 1.5 * Rs;              // 광자구
	const double u_ph = 1.0 / Rph;

	for (int32 i = 0; i < PhotonCount; ++i)
	{
		FPhoton P;
		// 초기: 광자구 근방 (u = u_ph + 작은 섭동)
		// 불안정 궤도라서 작은 섭동이 발산 → 궤도 탈출 or 낙하 연출
		const double Perturb = (FMath::FRand() - 0.5) * 2.0 * PhotonInitialPerturbation;
		P.U = u_ph + Perturb * u_ph;
		P.DU = 0.0;
		P.Phi = (2.0 * PI / PhotonCount) * i;

		// 각 광자는 서로 다른 궤도 평면 — 구면 분포
		// InclinationAxis: z축 회전각. 각 광자의 궤도면을 z축 기준으로 회전시킴.
		P.InclinationAxis = (2.0 * PI / PhotonCount) * i * 0.5 +
			FMath::FRand() * PI;

		P.CurrentPosition = PhotonStateToWorld(P);

		if (PhotonVFX)
		{
			UNiagaraComponent* NC = UNiagaraFunctionLibrary::SpawnSystemAtLocation(
				GetWorld(), PhotonVFX, P.CurrentPosition, FRotator::ZeroRotator,
				FVector(PhotonVFXScale), false, true, ENCPoolMethod::None);
			P.VFXComp = NC;
		}

		Photons.Add(P);
	}
}

// ═════════════════════════════════════════════════════════════
// 강착원반 초기화 (Kepler ω = √(GM/r³))
// ═════════════════════════════════════════════════════════════
void ASchwarzschildSingularityZone::InitializeDiskParticles()
{
	DiskParticles.Empty();
	DiskParticles.Reserve(DiskParticleCount);

	const double Rs = SchwarzschildRadius;
	const double R_in = 3.0 * Rs;                       // ISCO
	const double R_out = DiskOuterRadiusRs * Rs;
	const double GM = static_cast<double>(GravitationalParameter);

	for (int32 i = 0; i < DiskParticleCount; ++i)
	{
		FDiskParticle D;
		// 균일한 면적 밀도: r ∝ √(rand)
		const double T = FMath::FRand();
		D.R = FMath::Sqrt(R_in * R_in + T * (R_out * R_out - R_in * R_in));
		D.Phi = FMath::FRand() * 2.0 * PI;
		// Kepler 각속도 ω = √(GM / r³)
		D.Omega = FMath::Sqrt(GM / (D.R * D.R * D.R));
		D.ZOffset = (FMath::FRand() - 0.5) * 2.0 * DiskThickness;

		const FVector Center = GetActorLocation();
		D.CurrentPosition = Center + FVector(
			FMath::Cos(D.Phi) * D.R, FMath::Sin(D.Phi) * D.R, D.ZOffset);

		if (DiskParticleVFX)
		{
			UNiagaraComponent* NC = UNiagaraFunctionLibrary::SpawnSystemAtLocation(
				GetWorld(), DiskParticleVFX, D.CurrentPosition, FRotator::ZeroRotator,
				FVector(DiskParticleVFXScale), false, true, ENCPoolMethod::None);
			D.VFXComp = NC;
		}

		DiskParticles.Add(D);
	}
}

// ═════════════════════════════════════════════════════════════
// 고전 RK4: d²u/dφ² + u = (3/2) r_s u²
//   상태 [u, v=du/dφ], 독립변수 φ
//   du/dφ = v
//   dv/dφ = -u + (3/2) r_s u²
// ═════════════════════════════════════════════════════════════
void ASchwarzschildSingularityZone::IntegratePhotonGeodesic(FPhoton& P, double DPhi)
{
	const double Rs = (EffectiveRs > KINDA_SMALL_NUMBER) ? EffectiveRs : SchwarzschildRadius;
	const double h = DPhi / static_cast<double>(RK4Substeps);
	const double Coef = 1.5 * Rs;

	auto F = [Coef](double U, double V, double& DU, double& DV)
	{
		DU = V;
		DV = -U + Coef * U * U;
	};

	for (int32 s = 0; s < RK4Substeps; ++s)
	{
		double k1u, k1v, k2u, k2v, k3u, k3v, k4u, k4v;
		F(P.U, P.DU, k1u, k1v);
		F(P.U + 0.5 * h * k1u, P.DU + 0.5 * h * k1v, k2u, k2v);
		F(P.U + 0.5 * h * k2u, P.DU + 0.5 * h * k2v, k3u, k3v);
		F(P.U + h * k3u, P.DU + h * k3v, k4u, k4v);

		P.U += (h / 6.0) * (k1u + 2.0 * k2u + 2.0 * k3u + k4u);
		P.DU += (h / 6.0) * (k1v + 2.0 * k2v + 2.0 * k3v + k4v);
		P.Phi += h;
	}

	// u 발산 방지: 지평선 안쪽으로 빠지면 재투입
	const double u_max = 2.0 / FMath::Max(0.01, Rs); // r < 0.5 r_s 이면 리셋
	if (P.U > u_max || !FMath::IsFinite(P.U) || !FMath::IsFinite(P.DU))
	{
		P.U = 1.0 / (1.5 * Rs);
		P.DU = 0.0;
		P.Phi = FMath::FRand() * 2.0 * PI;
	}
	// r 너무 커지면 재투입 (궤도 탈출)
	if (P.U < 1e-6)
	{
		P.U = 1.0 / (1.5 * Rs);
		P.DU = 0.0;
	}
}

FVector ASchwarzschildSingularityZone::PhotonStateToWorld(const FPhoton& P) const
{
	const double R = 1.0 / FMath::Max(P.U, 1e-6);
	const double X = R * FMath::Cos(P.Phi);
	const double Y = R * FMath::Sin(P.Phi);
	// 궤도면 기울이기 — InclinationAxis만큼 z축 주변에서 x축을 회전시킨 평면
	const double C = FMath::Cos(P.InclinationAxis);
	const double S = FMath::Sin(P.InclinationAxis);
	const FVector Local(
		static_cast<float>(X * C),
		static_cast<float>(X * S),
		static_cast<float>(Y));  // Y 성분을 z로 보내 구면 분포 생성
	return GetActorLocation() + Local;
}

// ═════════════════════════════════════════════════════════════
// Tick 광자 업데이트
// ═════════════════════════════════════════════════════════════
void ASchwarzschildSingularityZone::UpdatePhotons(float DeltaTime)
{
	const double DPhi = static_cast<double>(PhotonPhiRate) * static_cast<double>(DeltaTime);

	for (FPhoton& P : Photons)
	{
		IntegratePhotonGeodesic(P, DPhi);
		P.CurrentPosition = PhotonStateToWorld(P);
		if (P.VFXComp.IsValid())
		{
			P.VFXComp->SetWorldLocation(P.CurrentPosition);
		}
	}
}

// ═════════════════════════════════════════════════════════════
// 강착원반 업데이트 + 도플러 색상
// ═════════════════════════════════════════════════════════════
void ASchwarzschildSingularityZone::UpdateDiskParticles(float DeltaTime)
{
	const FVector Center = GetActorLocation();

	// 관측자(= 활성 플레이어) 방향 — 도플러 편이 기준
	FVector ObserverDir = FVector::ForwardVector;
	if (UWorld* World = GetWorld())
	{
		if (APlayerController* PC = UGameplayStatics::GetPlayerController(World, 0))
		{
			if (APawn* Pawn = PC->GetPawn())
			{
				ObserverDir = (Pawn->GetActorLocation() - Center).GetSafeNormal2D();
			}
		}
	}

	const double Rs = (EffectiveRs > KINDA_SMALL_NUMBER) ? EffectiveRs : SchwarzschildRadius;

	for (FDiskParticle& D : DiskParticles)
	{
		// Kepler 궤도: φ += ω·dt
		D.Phi += D.Omega * static_cast<double>(DeltaTime);

		const double Cf = FMath::Cos(D.Phi);
		const double Sf = FMath::Sin(D.Phi);
		D.CurrentPosition = Center + FVector(
			static_cast<float>(Cf * D.R),
			static_cast<float>(Sf * D.R),
			static_cast<float>(D.ZOffset));

		if (D.VFXComp.IsValid())
		{
			D.VFXComp->SetWorldLocation(D.CurrentPosition);

			// 속도 벡터 (접선 방향, 반시계)
			const FVector Velocity(
				static_cast<float>(-Sf * D.R * D.Omega),
				static_cast<float>(Cf * D.R * D.Omega),
				0.f);
			const FVector VelDir = Velocity.GetSafeNormal();
			// 관측자 쪽으로 다가오면 양수 (blueshift), 멀어지면 음수 (redshift)
			const float Approach = FVector::DotProduct(VelDir, ObserverDir);

			// 중력 적색편이: (1 - r_s / r)^(1/2) ≤ 1
			const double GravRedshift = FMath::Sqrt(FMath::Max(0.0, 1.0 - Rs / D.R));

			// Niagara user params 로 전달 (컬러 시프트)
			D.VFXComp->SetVariableFloat(FName("DopplerShift"), Approach);
			D.VFXComp->SetVariableFloat(FName("GravRedshift"), static_cast<float>(GravRedshift));
		}
	}
}

// ═════════════════════════════════════════════════════════════
// 플레이어 중력 (GR 1차 보정) + 사건 지평선 / 조석력 데미지
// ═════════════════════════════════════════════════════════════
void ASchwarzschildSingularityZone::ApplyPlayerGravity(float DeltaTime)
{
	UWorld* World = GetWorld();
	if (!World) return;

	const double Rs = (EffectiveRs > KINDA_SMALL_NUMBER) ? EffectiveRs : 0.0;
	if (Rs <= 0.0) return;

	const FVector Center = GetActorLocation();
	const double GM = static_cast<double>(GravitationalParameter);
	const double Rph = 1.5 * Rs;
	const double Now = World->GetTimeSeconds();

	for (TActorIterator<AHellunaHeroCharacter> It(World); It; ++It)
	{
		AHellunaHeroCharacter* Hero = *It;
		if (!IsValid(Hero)) continue;

		const FVector HeroPos = Hero->GetActorLocation();
		const FVector Delta = Center - HeroPos;
		const double R = Delta.Size();
		if (R < KINDA_SMALL_NUMBER) continue;

		// 사건 지평선 안쪽 → 즉사
		if (R < Rs)
		{
			UGameplayStatics::ApplyDamage(
				Hero, HorizonKillDamage,
				OwnerEnemy ? OwnerEnemy->GetController() : nullptr,
				OwnerEnemy, UDamageType::StaticClass());
			continue;
		}

		// 가속도: a = GM/r² × (1 + 3 r_s/r) — 선행 GR 보정
		const double GRFactor = 1.0 + 3.0 * Rs / R;
		const double Accel = GM / (R * R) * GRFactor * static_cast<double>(PlayerPullMultiplier);

		const FVector Dir = Delta.GetSafeNormal();
		Hero->LaunchCharacter(Dir * static_cast<float>(Accel * DeltaTime), false, false);

		// 광자구 안쪽 → 조석력 DoT (틱 1초 주기)
		if (R < Rph)
		{
			double& LastTick = LastTidalTick.FindOrAdd(Hero);
			if (Now - LastTick >= 1.0)
			{
				// 조석력 ~ 1/r³ — r이 r_s에 가까울수록 급증
				const double TidalScale = (Rs * Rs * Rs) / (R * R * R);
				const float Dmg = TidalDamagePerSecond * static_cast<float>(TidalScale);
				UGameplayStatics::ApplyDamage(
					Hero, Dmg,
					OwnerEnemy ? OwnerEnemy->GetController() : nullptr,
					OwnerEnemy, UDamageType::StaticClass());
				LastTick = Now;
			}
		}
	}
}

// ═════════════════════════════════════════════════════════════
// 페이즈 업데이트 + EffectiveRs 보간
// ═════════════════════════════════════════════════════════════
void ASchwarzschildSingularityZone::UpdatePhase(float DeltaTime)
{
	PatternElapsed += DeltaTime;
	const double T = PatternElapsed / FMath::Max(0.01, static_cast<double>(PatternDuration));

	const double FormEnd = FormationPhaseRatio;
	const double EvapStart = 1.0 - EvaporationPhaseRatio;

	if (T < FormEnd)
	{
		CurrentPhase = EPhase::Formation;
		// r_s: 0 → SchwarzschildRadius (easeInOut)
		const double Alpha = FMath::Clamp(T / FormEnd, 0.0, 1.0);
		const double Eased = Alpha * Alpha * (3.0 - 2.0 * Alpha); // smoothstep
		EffectiveRs = Eased * static_cast<double>(SchwarzschildRadius);
	}
	else if (T < EvapStart)
	{
		CurrentPhase = EPhase::Stable;
		EffectiveRs = static_cast<double>(SchwarzschildRadius);
	}
	else
	{
		if (CurrentPhase != EPhase::Evaporation)
		{
			CurrentPhase = EPhase::Evaporation;
			if (EvaporationFlashVFX)
			{
				UNiagaraFunctionLibrary::SpawnSystemAtLocation(
					GetWorld(), EvaporationFlashVFX, GetActorLocation(), FRotator::ZeroRotator,
					FVector(1.f), true, true, ENCPoolMethod::AutoRelease);
			}
		}
		const double Alpha = FMath::Clamp((T - EvapStart) / FMath::Max(0.001, 1.0 - EvapStart), 0.0, 1.0);
		// r_s: SchwarzschildRadius → 0 (호킹 증발)
		EffectiveRs = (1.0 - Alpha) * static_cast<double>(SchwarzschildRadius);
	}

	// 지평선 메시 스케일 업데이트: 엔진 기본 구체(반지름 50cm) → EffectiveRs cm
	if (HorizonMesh)
	{
		const float S = FMath::Max(0.1f, static_cast<float>(EffectiveRs) / 50.f);
		HorizonMesh->SetWorldScale3D(FVector(S));
	}

	// 렌즈링 PostProcess 파라미터 갱신
	if (LensingMID)
	{
		LensingMID->SetScalarParameterValue(TEXT("SchwarzschildRadius"), static_cast<float>(EffectiveRs));
		LensingMID->SetVectorParameterValue(TEXT("BlackHoleLocation"),
			FLinearColor(GetActorLocation()));
	}
}

// ═════════════════════════════════════════════════════════════
// Tick
// ═════════════════════════════════════════════════════════════
void ASchwarzschildSingularityZone::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);
	if (!bZoneActive) return;

	UpdatePhase(DeltaTime);
	UpdatePhotons(DeltaTime);
	UpdateDiskParticles(DeltaTime);
	ApplyPlayerGravity(DeltaTime);

	// 신규 고급 상호작용 — 언리얼 기능 총동원
	UpdateSceneCapture();
	ApplyPostProcessToPlayer(DeltaTime);
	ApplyTimeDilationToPlayer();
	ApplyPhysicsForces();
	TryGravitationalSlingshot();
	ApplyTidalDisruption(DeltaTime);
}

// ═════════════════════════════════════════════════════════════
// SceneCapture: 블랙홀에서 환경 실시간 캡처 → MID 텍스처 파라미터 갱신
// ═════════════════════════════════════════════════════════════
void ASchwarzschildSingularityZone::UpdateSceneCapture()
{
	if (!SceneCapture || !CaptureRenderTarget) return;
	// 수동 캡처 (매 프레임 비싸니 2프레임에 1번)
	static int32 Counter = 0;
	if ((Counter++ & 1) == 0)
	{
		SceneCapture->CaptureScene();
	}

	// MID 파라미터 갱신 (r_s, 시간, 리플 시간)
	if (UMaterialInstanceDynamic* MID = Cast<UMaterialInstanceDynamic>(HorizonMesh->GetMaterial(0)))
	{
		MID->SetScalarParameterValue(TEXT("SchwarzschildRadius"), static_cast<float>(EffectiveRs));
		MID->SetScalarParameterValue(TEXT("Time"), GetWorld()->GetTimeSeconds());
		MID->SetScalarParameterValue(TEXT("LastRippleTime"), static_cast<float>(LastRippleTime));
	}
}

// ═════════════════════════════════════════════════════════════
// 플레이어 포스트프로세스: 거리 기반 색수차/비네트/채도
// ═════════════════════════════════════════════════════════════
void ASchwarzschildSingularityZone::ApplyPostProcessToPlayer(float DeltaTime)
{
	if (!bApplyPostProcess || !PostProcess) return;
	UWorld* World = GetWorld();
	if (!World) return;

	APlayerController* PC = UGameplayStatics::GetPlayerController(World, 0);
	if (!PC || !PC->GetPawn()) return;

	const double Rs = (EffectiveRs > KINDA_SMALL_NUMBER) ? EffectiveRs : SchwarzschildRadius;
	const double Rph = 1.5 * Rs;
	const double Rmax = 10.0 * Rs;
	const FVector Center = GetActorLocation();
	const double R = FVector::Dist(PC->GetPawn()->GetActorLocation(), Center);

	// 블렌드: r_max 밖 → 0, r_ph 안쪽 → 1
	const double Alpha = FMath::Clamp(1.0 - (R - Rph) / FMath::Max(1.0, Rmax - Rph), 0.0, 1.0);
	PostProcess->BlendWeight = static_cast<float>(Alpha);

	// 색수차 강도 (ISCO 근처부터 서서히, r_s 접근 시 최대)
	const double FringeAlpha = FMath::Clamp(1.0 - R / Rmax, 0.0, 1.0);
	PostProcess->Settings.SceneFringeIntensity = static_cast<float>(FringeAlpha * 5.0);
	PostProcess->Settings.VignetteIntensity = static_cast<float>(FringeAlpha * 1.5);
	// 채도 하락 (r_s 접근 시 흑백)
	const float Sat = static_cast<float>(FMath::Clamp(R / Rmax, 0.0, 1.0));
	PostProcess->Settings.ColorSaturation = FVector4(Sat, Sat, Sat, 1.f);
}

// ═════════════════════════════════════════════════════════════
// 진짜 슈바르츠실트 시간 지연: Δτ/Δt = √(1 - r_s/r)
// 플레이어 개인 시간이 블랙홀에 가까울수록 느려짐
// ═════════════════════════════════════════════════════════════
void ASchwarzschildSingularityZone::ApplyTimeDilationToPlayer()
{
	if (!bApplyTimeDilation) return;
	UWorld* World = GetWorld();
	if (!World) return;

	const double Rs = (EffectiveRs > KINDA_SMALL_NUMBER) ? EffectiveRs : 0.0;
	if (Rs <= 0.0) return;
	const FVector Center = GetActorLocation();

	for (TActorIterator<AHellunaHeroCharacter> It(World); It; ++It)
	{
		AHellunaHeroCharacter* Hero = *It;
		if (!IsValid(Hero)) continue;

		const double R = FVector::Dist(Hero->GetActorLocation(), Center);
		// 원본 저장
		if (!OriginalTimeDilation.Contains(Hero))
		{
			OriginalTimeDilation.Add(Hero, Hero->CustomTimeDilation);
		}

		if (R <= Rs)
		{
			Hero->CustomTimeDilation = 0.1f; // 지평선 안쪽: 극단적 시간 지연
		}
		else
		{
			// Schwarzschild 시간 지연 계수, 클램프
			const double Factor = FMath::Sqrt(FMath::Max(0.01, 1.0 - Rs / R));
			Hero->CustomTimeDilation = FMath::Lerp(0.3f, 1.0f,
				static_cast<float>(FMath::Clamp(Factor, 0.0, 1.0)));
		}
	}
}

// ═════════════════════════════════════════════════════════════
// 물리 시뮬 액터 방사형 인력 (RadialForceComponent 위치 동기화)
// ═════════════════════════════════════════════════════════════
void ASchwarzschildSingularityZone::ApplyPhysicsForces()
{
	if (!bApplyPhysicsForce || !RadialForce) return;
	RadialForce->Radius = SchwarzschildRadius * 10.f;
	RadialForce->ForceStrength = -GravitationalParameter * 0.01f;
	RadialForce->FireImpulse(); // 즉시 적용 (지속 force는 이미 enabled)
}

// ═════════════════════════════════════════════════════════════
// 중력 새총: ISCO 대역에서 접선 속도 높으면 부스트
// Newton's cannonball — 실제 궤도역학 응용
// ═════════════════════════════════════════════════════════════
void ASchwarzschildSingularityZone::TryGravitationalSlingshot()
{
	UWorld* World = GetWorld();
	if (!World) return;

	const double Rs = (EffectiveRs > KINDA_SMALL_NUMBER) ? EffectiveRs : 0.0;
	if (Rs <= 0.0) return;
	const double Rin = 3.0 * Rs;   // ISCO
	const double Rout = 6.0 * Rs;
	const FVector Center = GetActorLocation();

	for (TActorIterator<AHellunaHeroCharacter> It(World); It; ++It)
	{
		AHellunaHeroCharacter* Hero = *It;
		if (!IsValid(Hero)) continue;
		const FVector Pos = Hero->GetActorLocation();
		const FVector ToCenter = Center - Pos;
		const double R = ToCenter.Size();
		if (R < Rin || R > Rout) continue;

		UCharacterMovementComponent* Move = Hero->GetCharacterMovement();
		if (!Move) continue;
		const FVector Vel = Move->Velocity;
		const float Speed = Vel.Size();
		if (Speed < SlingshotMinSpeed) continue;

		// 접선 성분 = 전체 속도에서 반경 방향 성분 제거
		const FVector RadialDir = ToCenter.GetSafeNormal();
		const FVector RadialComp = FVector::DotProduct(Vel, RadialDir) * RadialDir;
		const FVector TangentialComp = Vel - RadialComp;
		const float TangentialSpeed = TangentialComp.Size();
		if (TangentialSpeed < SlingshotMinSpeed * 0.5f) continue;

		// 접선 방향으로 부스트 — 궤도 이탈 속도
		const FVector Boost = TangentialComp.GetSafeNormal() * TangentialSpeed * SlingshotBoost;
		Hero->LaunchCharacter(Boost, true, false);
	}
}

// ═════════════════════════════════════════════════════════════
// 조석 분리: 광자구 내 물리 액터 반경 방향으로 스파게티화
// ═════════════════════════════════════════════════════════════
void ASchwarzschildSingularityZone::ApplyTidalDisruption(float DeltaTime)
{
	UWorld* World = GetWorld();
	if (!World) return;
	const double Rs = (EffectiveRs > KINDA_SMALL_NUMBER) ? EffectiveRs : 0.0;
	if (Rs <= 0.0) return;
	const double Rph = 1.5 * Rs;
	const FVector Center = GetActorLocation();

	TArray<FOverlapResult> Overlaps;
	FCollisionShape Sphere = FCollisionShape::MakeSphere(static_cast<float>(Rph));
	FCollisionQueryParams Params;
	Params.AddIgnoredActor(this);
	if (!World->OverlapMultiByObjectType(
		Overlaps, Center, FQuat::Identity,
		FCollisionObjectQueryParams(ECC_PhysicsBody), Sphere, Params))
	{
		return;
	}

	for (const FOverlapResult& Hit : Overlaps)
	{
		UPrimitiveComponent* Prim = Hit.GetComponent();
		if (!Prim || !Prim->IsSimulatingPhysics()) continue;

		if (!OriginalPhysicsScale.Contains(Prim))
		{
			OriginalPhysicsScale.Add(Prim, Prim->GetComponentScale());
		}
		const FVector OrigScale = OriginalPhysicsScale[Prim];

		const FVector CompPos = Prim->GetComponentLocation();
		const double R = FVector::Dist(CompPos, Center);
		const double Factor = FMath::Clamp(1.0 - R / Rph, 0.0, 1.0);
		const float Stretch = 1.f + static_cast<float>(Factor * TidalDisruptionStrength);

		// 반경 방향으로 stretch, 수직 방향으로 squeeze (조석력 특징)
		FVector NewScale = OrigScale;
		NewScale.X *= Stretch;
		NewScale.Y /= FMath::Max(0.1f, Stretch);
		NewScale.Z /= FMath::Max(0.1f, Stretch);
		Prim->SetWorldScale3D(NewScale);
	}
}

// ═════════════════════════════════════════════════════════════
// 피격 피드백: 플레이어가 블랙홀을 공격하면 리플 파라미터 트리거
// ═════════════════════════════════════════════════════════════
void ASchwarzschildSingularityZone::HandleDamageFeedback(AActor* DamagedActor, float Damage,
	const UDamageType* DamageType, AController* InstigatedBy, AActor* DamageCauser)
{
	UWorld* World = GetWorld();
	if (!World) return;
	LastRippleTime = World->GetTimeSeconds();

	// 정보성 로그
	UE_LOG(LogTemp, Warning, TEXT("[Schwarzschild] Damage feedback ripple! Damage=%.1f"), Damage);
}
