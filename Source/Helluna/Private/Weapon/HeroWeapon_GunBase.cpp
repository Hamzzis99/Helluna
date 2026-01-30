// Capstone Project Helluna


#include "Weapon/HeroWeapon_GunBase.h"
#include "GameFramework/Controller.h"
#include "GameFramework/Pawn.h"
#include "Kismet/GameplayStatics.h"
#include "Engine/World.h"
#include "DrawDebugHelpers.h"
#include "NiagaraFunctionLibrary.h"			
#include "NiagaraSystem.h"
#include "Net/UnrealNetwork.h"
#include "Character/HellunaHeroCharacter.h"

#include "DebugHelper.h"


void AHeroWeapon_GunBase::BeginPlay()
{
	Super::BeginPlay();

	CurrentMag = MaxMag;
}

bool AHeroWeapon_GunBase::CanFire() const
{
	return CurrentMag > 0;
}

bool AHeroWeapon_GunBase::CanReload() const
{
	// 무한탄: 탄창이 덜 찼으면 언제든 장전 가능
	return CurrentMag < MaxMag;
}

void AHeroWeapon_GunBase::Reload()
{
	// 서버가 최종 탄약 변경
	if (HasAuthority())
	{
		Reload_Internal();
	}
	else
	{
		ServerReload();
	}
}

void AHeroWeapon_GunBase::ServerReload_Implementation()
{
	Reload_Internal();
}

void AHeroWeapon_GunBase::Reload_Internal()
{
	if (!HasAuthority())
		return;

	if (!CanReload())
		return;

	// 무한탄: 그냥 풀 장전
	CurrentMag = MaxMag;

	BroadcastAmmoChanged();
}

void AHeroWeapon_GunBase::OnRep_CurrentAmmoInMag()
{
	BroadcastAmmoChanged();
}

void AHeroWeapon_GunBase::BroadcastAmmoChanged()
{
	OnAmmoChanged.Broadcast(CurrentMag, MaxMag);
}



void AHeroWeapon_GunBase::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	DOREPLIFETIME(AHeroWeapon_GunBase, CurrentMag);
}

void AHeroWeapon_GunBase::Fire(AController* InstigatorController)
{
	if (!HasAuthority())
	{
		Debug::Print(TEXT("Warning: AHeroWeapon_GunBase::Fire called Client"), FColor::Red);
		return;
	}

	if (!InstigatorController)
		return;

	APawn* Pawn = InstigatorController->GetPawn();
	if (!Pawn)
		return;

	// (선택) 탄약 체크를 여기서도 한번 방어
	if (!CanFire())
		return;

	FVector ViewLoc;
	FRotator ViewRot;
	InstigatorController->GetPlayerViewPoint(ViewLoc, ViewRot);  // 카메라 기준

	const FVector TraceStart = ViewLoc;
	const FVector TraceEnd = TraceStart + (ViewRot.Vector() * Range);

	// =========================
	// [MOD] 서버에서만 탄 소비 + 데미지
	// =========================
	CurrentMag = FMath::Max(0, CurrentMag - 1);
	BroadcastAmmoChanged();

	DoLineTraceAndDamage(InstigatorController, TraceStart, TraceEnd);
}

void AHeroWeapon_GunBase::DoLineTraceAndDamage(AController* InstigatorController, const FVector& TraceStart, const FVector& TraceEnd)
{
	// “실제 히트판정 + 데미지 적용” 핵심 함수

	UWorld* World = GetWorld();
	if (!World || !InstigatorController)
		return;

	FCollisionQueryParams Params(SCENE_QUERY_STAT(AR_LineTrace), false);
	Params.AddIgnoredActor(this);

	APawn* Pawn = InstigatorController->GetPawn();
	if (Pawn)
	{
		Params.AddIgnoredActor(Pawn); // 자기 자신(발사자) 맞는 것 방지
	}

	FHitResult Hit;
	const bool bHit = World->LineTraceSingleByChannel(
		Hit,
		TraceStart,
		TraceEnd,
		TraceChannel,
		Params
	);

	// 맞았으면 히트 위치, 아니면 끝점
	const FVector HitLocation = bHit ? Hit.ImpactPoint : TraceEnd;

	if (bHit)
	{
		AActor* HitActor = Hit.GetActor();
		if (HitActor)
		{
			UGameplayStatics::ApplyPointDamage(
				HitActor,
				Damage,
				(TraceEnd - TraceStart).GetSafeNormal(),
				Hit,
				InstigatorController,
				this,
				UDamageType::StaticClass()
			);
		}
	}

	// 서버에서 FX 동기화 호출 (Unreliable: 총알 FX는 손실돼도 큰 문제 없음)
	MulticastFireFX(TraceStart, TraceEnd, bHit, HitLocation);

}

void AHeroWeapon_GunBase::MulticastFireFX_Implementation(FVector_NetQuantize TraceStart, FVector_NetQuantize TraceEnd, bool bHit, FVector_NetQuantize HitLocation)
{
	// “모든 클라이언트에서 보이는 연출” 전용

	const FVector SpawnLoc = bHit ? (FVector)HitLocation : (FVector)TraceEnd;
	if (ImpactFX)
	{
		UNiagaraFunctionLibrary::SpawnSystemAtLocation(
			this,
			ImpactFX,
			SpawnLoc,
			FRotator::ZeroRotator,
			ImpactFXScale,   // 에디터에서 조절 가능
			true,
			true,
			ENCPoolMethod::AutoRelease
		);
	}
}

void AHeroWeapon_GunBase::ApplyRecoil(AHellunaHeroCharacter* TargetCharacter)
{
	// 로컬 플레이어 카메라만 움직여야 함(다른 클라는 각자 처리)
	if (!TargetCharacter) return;
	if (!TargetCharacter->IsLocallyControlled()) return;

	UWorld* World = GetWorld();
	if (!World) return;

	// Tick에서 계속 쓰기 위해 타겟 저장
	RecoilTarget = TargetCharacter;

	// 무기 데이터(디자이너 값)에서 반동 크기 가져오기
	const float PitchKick = ReboundUp;
	const float YawKick = FMath::RandRange(-ReboundLeftRight, ReboundLeftRight);

	// 큐를 쌓지 않고 "목표 오프셋"만 누적
	// (연사해도 꼬리처럼 길게 남지 않고 자연스럽게 누적됨)
	RecoilTargetPitch += PitchKick;
	RecoilTargetYaw += YawKick;

	// 총알 이펙트가 먼저 보이고 카메라가 살짝 뒤에 올라가는 느낌(고정값)
	constexpr float FixedStartDelay = 0.03f;

	// 타이머가 꺼져있을 때만 시작(연사 중 매번 새 타이머/딜레이 걸지 않음)
	if (!World->GetTimerManager().IsTimerActive(RecoilTimerHandle))
	{
		RecoilStartDelayRemaining = FixedStartDelay;

		// Delta 계산 기준(첫 틱에 튀는 현상 방지)
		RecoilPrevPitch = RecoilCurrentPitch;
		RecoilPrevYaw = RecoilCurrentYaw;

		// Tick 대신 타이머로 로컬에서만 일정 간격으로 반동 처리
		World->GetTimerManager().SetTimer(
			RecoilTimerHandle,
			this,
			&AHeroWeapon_GunBase::TickRecoil,
			FMath::Max(RecoilTickInterval, 0.001f),
			true
		);
	}
}

void AHeroWeapon_GunBase::TickRecoil()
{
	UWorld* World = GetWorld();
	AHellunaHeroCharacter* TargetCharacter = RecoilTarget.Get();

	// 타겟/월드가 없으면 정리
	if (!World || !TargetCharacter)
	{
		if (World) World->GetTimerManager().ClearTimer(RecoilTimerHandle);

		RecoilTargetPitch = RecoilTargetYaw = 0.f;
		RecoilCurrentPitch = RecoilCurrentYaw = 0.f;
		RecoilPrevPitch = RecoilPrevYaw = 0.f;
		RecoilStartDelayRemaining = 0.f;
		return;
	}

	const float dt = FMath::Max(RecoilTickInterval, 0.001f);

	// 첫 발만 딜레이(연사 중에는 추가 딜레이 없음)
	if (RecoilStartDelayRemaining > 0.f)
	{
		RecoilStartDelayRemaining -= dt;
		if (RecoilStartDelayRemaining > 0.f)
			return;

		// 딜레이 끝나는 프레임에 Delta 튐 방지
		RecoilPrevPitch = RecoilCurrentPitch;
		RecoilPrevYaw = RecoilCurrentYaw;
	}

	// 선형 속도 모델:
	// RefKick을 RefRiseTime 동안 올리는 속도로 환산해서,
	// 반동 크기가 달라도 "일관된 속도"로 올라가게 함
	constexpr float RefKick = 10.0f;
	constexpr float RefRiseTime = 0.20f;

	const float SpeedPerSec = (RefRiseTime > 0.f) ? (RefKick / RefRiseTime) : 0.f;
	const float Step = SpeedPerSec * dt;

	// Current를 Target으로 한 번에 점프시키지 않고 Step만큼만 따라가게(부드럽게)
	{
		const float DiffPitch = RecoilTargetPitch - RecoilCurrentPitch;
		const float DiffYaw = RecoilTargetYaw - RecoilCurrentYaw;

		RecoilCurrentPitch += FMath::Clamp(DiffPitch, -Step, Step);
		RecoilCurrentYaw += FMath::Clamp(DiffYaw, -Step, Step);
	}

	// "이번 프레임에 얼마나 변했는지(Delta)"만 컨트롤러 입력으로 적용
	const float DeltaPitch = RecoilCurrentPitch - RecoilPrevPitch;
	const float DeltaYaw = RecoilCurrentYaw - RecoilPrevYaw;

	RecoilPrevPitch = RecoilCurrentPitch;
	RecoilPrevYaw = RecoilCurrentYaw;

	// Pitch는 위로 튀는 느낌을 위해 부호를 반대로 적용
	TargetCharacter->AddControllerPitchInput(-DeltaPitch);
	TargetCharacter->AddControllerYawInput(DeltaYaw);

	// 목표에 도달하면 타이머 종료(불필요한 업데이트 방지)
	if (FMath::Abs(RecoilCurrentPitch - RecoilTargetPitch) < 0.001f &&
		FMath::Abs(RecoilCurrentYaw - RecoilTargetYaw) < 0.001f)
	{
		World->GetTimerManager().ClearTimer(RecoilTimerHandle);
	}
}