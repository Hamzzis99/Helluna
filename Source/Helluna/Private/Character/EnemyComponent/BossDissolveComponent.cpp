// Capstone Project Helluna

#include "Character/EnemyComponent/BossDissolveComponent.h"

#include "Components/SkeletalMeshComponent.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "NiagaraComponent.h"
#include "NiagaraFunctionLibrary.h"
#include "NiagaraSystem.h"
#include "Engine/World.h"
#include "TimerManager.h"
#include "Net/UnrealNetwork.h"

#include "Camera/CameraActor.h"
#include "Camera/CameraComponent.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/Character.h"
#include "Engine/Engine.h"

#include "Character/HellunaEnemyCharacter.h"

UBossDissolveComponent::UBossDissolveComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.bStartWithTickEnabled = false;
	SetIsReplicatedByDefault(true);
}

// ============================================================
// 공개 API — server 에서 호출
// ============================================================
void UBossDissolveComponent::TriggerDissolve()
{
	if (!GetOwner() || !GetOwner()->HasAuthority()) return;
	if (bDissolveActive) return;

	UE_LOG(LogTemp, Warning, TEXT("[BossDissolveComp] TriggerDissolve on %s"),
		*GetNameSafe(GetOwner()));

	// [BossDissolveComponentV1] Stage 순서 — Stage2 (보라톤 dissolve material + 구멍/녹는) 가
	//   TriggerDissolve 시점에 즉시 시작. Stage1 (가루 particles) 은 Stage2DelaySeconds 후 spawn.
	//   StartDissolveLocal 에서 material setup + camera + Stage1 deferred timer 처리.
	//   Stage2 niagara 는 별도 Multicast 로 즉시 spawn.
	Multicast_StartDissolve();

	if (VFX_Stage2Asset)
	{
		Multicast_ActivateStage2();
	}

	UWorld* World = GetWorld();
	if (!World) return;

	if (DissolveDuration > 0.f)
	{
		World->GetTimerManager().SetTimer(CompleteTimer, this,
			&UBossDissolveComponent::CompleteAndDestroy,
			DissolveDuration, false);
	}
}

// ============================================================
// Multicast — 모든 머신에서 dissolve 시작 (mesh swap + Stage1 VFX + tick on)
// ============================================================
void UBossDissolveComponent::Multicast_StartDissolve_Implementation()
{
	StartDissolveLocal();
}

void UBossDissolveComponent::Multicast_ActivateStage2_Implementation()
{
	if (bStage2Activated) return;
	bStage2Activated = true;

	AActor* Owner = GetOwner();
	if (!Owner) return;
	USkeletalMeshComponent* Mesh = Owner->FindComponentByClass<USkeletalMeshComponent>();
	if (!Mesh) return;

	if (VFX_Stage2Asset)
	{
		SpawnedVFX_Stage2 = UNiagaraFunctionLibrary::SpawnSystemAttached(
			VFX_Stage2Asset, Mesh, NAME_None,
			FVector::ZeroVector, FRotator::ZeroRotator,
			EAttachLocation::SnapToTarget, true);
		UE_LOG(LogTemp, Warning,
			TEXT("[BossDissolveComp] Stage2 VFX spawned (Auth=%d) on %s"),
			Owner->HasAuthority() ? 1 : 0, *GetNameSafe(Owner));
	}
}

// ============================================================
// 로컬 dissolve 시작 — 각 머신에서 자기 mesh + VFX 처리
// ============================================================
void UBossDissolveComponent::StartDissolveLocal()
{
	if (bDissolveActive) return;
	bDissolveActive = true;

	AActor* Owner = GetOwner();
	if (!Owner) return;

	UWorld* World = GetWorld();
	if (!World) return;

	DissolveStartWallTime = World->GetTimeSeconds();

	// Actor TimeDilation 슬로우 — anim/niagara 시각 효과만.
	Owner->CustomTimeDilation = FMath::Clamp(SlowMotionDilation, 0.05f, 1.f);

	USkeletalMeshComponent* Mesh = Owner->FindComponentByClass<USkeletalMeshComponent>();
	if (!Mesh)
	{
		UE_LOG(LogTemp, Warning, TEXT("[BossDissolveComp] No SkelMesh on %s"), *GetNameSafe(Owner));
		return;
	}

	// 머티리얼 swap (각 슬롯에 dissolve MID 적용)
	DissolveMIDs.Reset();
	const int32 SlotCount = Mesh->GetNumMaterials();
	for (int32 i = 0; i < SlotCount; ++i)
	{
		UMaterialInterface* SrcMat = (DissolveMaterialOverrides.IsValidIndex(i))
			? DissolveMaterialOverrides[i] : nullptr;
		if (!SrcMat) continue;

		UMaterialInstanceDynamic* MID = UMaterialInstanceDynamic::Create(SrcMat, Owner);
		if (!MID) continue;

		Mesh->SetMaterial(i, MID);
		DissolveMIDs.Add(MID);

		MID->SetVectorParameterValue(TEXT("Edge Color"), DissolveEdgeColor);
		MID->SetScalarParameterValue(DissolveAnimationParamName, 0.f);
	}

	// [BossDissolveComponentV1] Stage1 (가루 particles) + Top-down 카메라 — Stage2DelaySeconds 후 deferred.
	//   Stage2 (구멍/녹는) + 머티리얼 보라화는 즉시 시작 (이미 위에서 처리). Stage1 niagara 와
	//   카메라 구도 변경은 묶어서 delayed — 사용자 의도: 카메라 전환 시점 = Stage1 시작 시점.
	//   각 머신이 자기 local timer 로 처리.
	{
		TWeakObjectPtr<UBossDissolveComponent> WeakSelf(this);
		TWeakObjectPtr<USkeletalMeshComponent> WeakMesh(Mesh);

		auto Stage1AndCameraLambda = [WeakSelf, WeakMesh]()
		{
			UBossDissolveComponent* Self = WeakSelf.Get();
			USkeletalMeshComponent* M = WeakMesh.Get();
			if (!Self || !M) return;
			AActor* O = Self->GetOwner();
			if (!O) return;
			UWorld* W = Self->GetWorld();
			if (!W) return;

			// --- Stage1 niagara spawn ---
			if (Self->VFX_Stage1Asset)
			{
				Self->SpawnedVFX_Stage1 = UNiagaraFunctionLibrary::SpawnSystemAttached(
					Self->VFX_Stage1Asset, M, NAME_None,
					FVector::ZeroVector, FRotator::ZeroRotator,
					EAttachLocation::SnapToTarget, true);

				if (Self->SpawnedVFX_Stage1)
				{
					Self->SpawnedVFX_Stage1->SetVariableLinearColor(FName(TEXT("User.Edge Color")), Self->DissolveEdgeColor);
					Self->SpawnedVFX_Stage1->SetVariableFloat(FName(TEXT("User.Animation")), 0.f);

					// Retrigger timer — 3초 주기로 burst 재실행.
					TWeakObjectPtr<UNiagaraComponent> WeakNS = Self->SpawnedVFX_Stage1;
					TSharedPtr<FTimerHandle> RetriggerH = MakeShared<FTimerHandle>();
					constexpr float RetriggerInterval = 3.0f;
					W->GetTimerManager().SetTimer(*RetriggerH,
						FTimerDelegate::CreateLambda([WeakNS, RetriggerH]()
						{
							if (UNiagaraComponent* NS = WeakNS.Get())
							{
								NS->Activate(true);
							}
						}),
						RetriggerInterval, /*bLoop=*/true);

					UE_LOG(LogTemp, Warning,
						TEXT("[BossDissolveComp] Stage1 VFX spawned (Auth=%d) on %s"),
						O->HasAuthority() ? 1 : 0, *GetNameSafe(O));
				}
			}

			// --- Top-down 카메라 spawn + ViewTarget blend ---
			if (Self->bUseTopDownCamera)
			{
				FVector Anchor = O->GetActorLocation() + FVector(0.f, 0.f, 90.f);
				static const FName HeadBoneName(TEXT("head"));
				if (M->DoesSocketExist(HeadBoneName))
				{
					Anchor = M->GetSocketLocation(HeadBoneName);
				}

				const FVector BossFwd = O->GetActorForwardVector();
				const FVector BossRight = O->GetActorRightVector();
				const FVector BossUp = FVector::UpVector;
				const FVector CamLoc = Anchor
					+ BossFwd * Self->TopDownCameraOffset.X
					+ BossRight * Self->TopDownCameraOffset.Y
					+ BossUp * Self->TopDownCameraOffset.Z;

				const FRotator LookAtRot = (Anchor - CamLoc).Rotation();
				FRotator CamRot = LookAtRot + Self->TopDownCameraRotation;
				FActorSpawnParameters CamParams;
				CamParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
				ACameraActor* NewCam = W->SpawnActor<ACameraActor>(
					ACameraActor::StaticClass(), CamLoc, CamRot, CamParams);
				if (NewCam)
				{
					if (UCameraComponent* CC = NewCam->GetCameraComponent())
					{
						CC->SetConstraintAspectRatio(false);
					}
					Self->TopDownCamera = NewCam;

					for (FConstPlayerControllerIterator It = W->GetPlayerControllerIterator(); It; ++It)
					{
						APlayerController* PC = It->Get();
						if (!PC || !PC->IsLocalPlayerController()) continue;
						PC->SetViewTargetWithBlend(NewCam, FMath::Max(0.f, Self->TopDownCameraBlendIn));
						break;
					}
					UE_LOG(LogTemp, Warning,
						TEXT("[BossDissolveComp] TopDownCamera at %s rot=%s (boss yaw=%.1f, offset=%s)"),
						*CamLoc.ToString(), *CamRot.ToString(),
						O->GetActorRotation().Yaw, *Self->TopDownCameraOffset.ToString());
				}
			}
		};

		if (Stage2DelaySeconds <= 0.f)
		{
			Stage1AndCameraLambda();
		}
		else
		{
			TSharedPtr<FTimerHandle> Stage1H = MakeShared<FTimerHandle>();
			World->GetTimerManager().SetTimer(*Stage1H,
				FTimerDelegate::CreateLambda([Stage1AndCameraLambda, Stage1H]()
				{
					Stage1AndCameraLambda();
				}),
				Stage2DelaySeconds, /*bLoop=*/false);
		}
	}

	// [BossDissolveComponentV1] CustomTimeDilation 진단 — Tick 시점에 매 verify
	UE_LOG(LogTemp, Warning,
		TEXT("[BossDissolveComp] CustomTimeDilation set to %.2f (verify=%.2f) on %s, Auth=%d"),
		SlowMotionDilation, Owner->CustomTimeDilation,
		*GetNameSafe(Owner), Owner->HasAuthority() ? 1 : 0);

	// Tick on — material Animation parameter 진행
	PrimaryComponentTick.SetTickFunctionEnable(true);

	UE_LOG(LogTemp, Warning,
		TEXT("[BossDissolveComp] StartDissolveLocal — %s, slots=%d, MIDs=%d, Auth=%d"),
		*GetNameSafe(Owner), SlotCount, DissolveMIDs.Num(),
		Owner->HasAuthority() ? 1 : 0);
}

// ============================================================
// Tick — Animation parameter 0→1 wall clock 기준 progression
// ============================================================
void UBossDissolveComponent::TickComponent(float DeltaTime, ELevelTick TickType,
	FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	if (!bDissolveActive) return;

	UWorld* World = GetWorld();
	if (!World) return;

	const float WallElapsed = static_cast<float>(World->GetTimeSeconds() - DissolveStartWallTime);
	const float Alpha = FMath::Clamp(WallElapsed / FMath::Max(0.1f, DissolveDuration), 0.f, 1.f);

	for (UMaterialInstanceDynamic* MID : DissolveMIDs)
	{
		if (MID)
		{
			MID->SetScalarParameterValue(DissolveAnimationParamName, Alpha);
		}
	}

	// [BossDissolveComponentV1] NS_BossDissolve_A01_Custom 의 Animation user param 도 동일 driving.
	//   이렇게 하면 dissolve 진행 내내 Niagara 가 Animation 0→1 따라 emit 지속 (보스 사라질
	//   때까지 자연스럽게 partikel 이 계속 발생). Tick 이 멈춰도 niagara 는 attached 라 component
	//   destroy 까지 살아있음.
	if (UNiagaraComponent* NS1 = SpawnedVFX_Stage1.Get())
	{
		NS1->SetVariableFloat(FName(TEXT("User.Animation")), Alpha);
	}

	// Alpha 가 1 도달해도 Tick 을 끄지 않음 — 보스 destroy 까지 niagara animation 1 로 유지하면
	//   emitter 가 마지막 상태 그대로 emit 지속 → 보스 사라질 때까지 효과 자연스럽게 이어짐.
}

// ============================================================
// Stage 2 — server timer 가 호출 → multicast
// ============================================================
void UBossDissolveComponent::ActivateStage2Local()
{
	if (!GetOwner() || !GetOwner()->HasAuthority()) return;
	Multicast_ActivateStage2();
}

// ============================================================
// 완료 — Mass entity 정리 + 보스 actor destroy (server 에서만 호출)
// ============================================================
void UBossDissolveComponent::CompleteAndDestroy()
{
	AActor* Owner = GetOwner();
	if (!Owner || !Owner->HasAuthority()) return;
	UE_LOG(LogTemp, Warning, TEXT("[BossDissolveComp] Complete — destroying %s"),
		*GetNameSafe(Owner));

	// Top-down 카메라 destroy (모든 머신이 자기 카메라 정리해야 — 일단 server 만 처리.
	//   client 의 카메라는 ViewTarget 자동 원복 시 leak 가능성 있어 추후 RPC 추가 검토).
	if (ACameraActor* Cam = TopDownCamera.Get())
	{
		Cam->Destroy();
	}

	// [BossDissolveComponentV1] ViewTarget 원복 — local PC 의 view 를 자기 Pawn 으로 복귀.
	//   server 측에서만 호출하지만 SetViewTargetWithBlend 가 server-side PC 만 영향. client 측은
	//   각자 Multicast_ResetViewTarget 같은 처리 필요하지만 일단 single-PC PIE 기준 Pawn 복귀.
	UWorld* World = Owner->GetWorld();
	if (World)
	{
		for (FConstPlayerControllerIterator It = World->GetPlayerControllerIterator(); It; ++It)
		{
			APlayerController* PC = It->Get();
			if (!PC) continue;
			if (APawn* PlayerPawn = PC->GetPawn())
			{
				PC->SetViewTargetWithBlend(PlayerPawn, 0.5f);
			}
		}
	}

	// AHellunaEnemyCharacter::DespawnMassEntityOnServer 가 Mass entity 정리 + actor destroy 처리.
	//   보스 BP CDO 의 bEnableDeathDissolve=false 면 즉시 destroy (다른 dissolve 효과 안 일어남).
	if (AHellunaEnemyCharacter* EnemyOwner = Cast<AHellunaEnemyCharacter>(Owner))
	{
		EnemyOwner->DespawnMassEntityOnServer(TEXT("BossDissolveComplete"));
	}
	else
	{
		Owner->Destroy();
	}
}
