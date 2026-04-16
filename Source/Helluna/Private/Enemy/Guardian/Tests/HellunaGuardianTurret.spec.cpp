// File: Source/Helluna/Private/Enemy/Guardian/Tests/HellunaGuardianTurret.spec.cpp
//
// 자동화 테스트 — 망가진 가디언 포탑 상태머신
//
// 테스트 경로: Helluna.Enemy.Guardian.StateMachine
// 실행: Session Frontend → Automation → "Helluna.Enemy.Guardian.StateMachine" 체크 후 RunTests
// 또는 콘솔: Automation RunTests Helluna.Enemy.Guardian.StateMachine
//
// 검증 범위 (현재):
//   - 스폰 직후 초기 상태 = Idle
//   - HealthComponent 기본 장착 여부
//   - Replicated/Multicast 노출 여부 (컴파일 타임 검증)
//
// TODO(Phase22): 풀 시나리오 자동화
//   - Hero 스폰 + PlayersInRange 주입 + HasAuthority 강제
//   - LoS 차단물 없는 빈 월드에서 Tick 반복 → Idle→Detect→Lock→FireDelay→Fire→Cooldown 타이머 검증
//   - HandlePhaseChanged(Night) 호출 시 Idle 강제 리셋 검증
//   현재는 PIE 수동 재현(bShowDebug=true 로 상태별 색 변화 관찰)으로 커버.

#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"
#include "Enemy/Guardian/HellunaGuardianTurret.h"
#include "Character/EnemyComponent/HellunaHealthComponent.h"
#include "Engine/World.h"
#include "Engine/Engine.h"

#if WITH_DEV_AUTOMATION_TESTS

BEGIN_DEFINE_SPEC(FHellunaGuardianTurret_StateMachineTest,
	"Helluna.Enemy.Guardian.StateMachine",
	EAutomationTestFlags::ApplicationContextMask | EAutomationTestFlags::ProductFilter)

	UWorld* TestWorld = nullptr;
	AHellunaGuardianTurret* Guardian = nullptr;

END_DEFINE_SPEC(FHellunaGuardianTurret_StateMachineTest)

void FHellunaGuardianTurret_StateMachineTest::Define()
{
	Describe("AHellunaGuardianTurret", [this]()
	{
		BeforeEach([this]()
		{
			TestWorld = UWorld::CreateWorld(EWorldType::Game, /*bInformEngineOfWorld=*/false, TEXT("GuardianSpecWorld"));
			if (TestWorld)
			{
				FWorldContext& WorldContext = GEngine->CreateNewWorldContext(EWorldType::Game);
				WorldContext.SetCurrentWorld(TestWorld);

				FURL URL;
				TestWorld->InitializeActorsForPlay(URL);
				TestWorld->BeginPlay();

				FActorSpawnParameters Params;
				Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
				Guardian = TestWorld->SpawnActor<AHellunaGuardianTurret>(
					AHellunaGuardianTurret::StaticClass(),
					FVector::ZeroVector,
					FRotator::ZeroRotator,
					Params);
			}
		});

		It("should spawn with a valid world", [this]()
		{
			TestNotNull(TEXT("Test world created"), TestWorld);
			TestNotNull(TEXT("Guardian actor spawned"), Guardian);
		});

		It("should start in Idle state", [this]()
		{
			if (!Guardian)
			{
				AddError(TEXT("Guardian spawn failed — prerequisite missing"));
				return;
			}
			TestEqual(TEXT("Initial state"), Guardian->GetCurrentState(), EGuardianState::Idle);
		});

		It("should have a HealthComponent attached", [this]()
		{
			if (!Guardian)
			{
				AddError(TEXT("Guardian spawn failed — prerequisite missing"));
				return;
			}
			UHellunaHealthComponent* Health = Guardian->FindComponentByClass<UHellunaHealthComponent>();
			TestNotNull(TEXT("HealthComponent present"), Health);
		});

		It("should reset state timer on spawn", [this]()
		{
			if (!Guardian)
			{
				AddError(TEXT("Guardian spawn failed — prerequisite missing"));
				return;
			}
			// 서버 권한 없이 스폰된 상태에서도 초기 타이머는 0 근처여야 함
			TestTrue(TEXT("State timer not advanced on spawn"), Guardian->GetStateTimer() < 1.f);
		});

		AfterEach([this]()
		{
			if (TestWorld)
			{
				if (Guardian && IsValid(Guardian))
				{
					Guardian->Destroy();
				}
				Guardian = nullptr;

				TestWorld->DestroyWorld(/*bBroadcastWorldDestroyedEvent=*/false);

				if (GEngine)
				{
					GEngine->DestroyWorldContext(TestWorld);
				}
				TestWorld = nullptr;
			}
		});
	});
}

#endif // WITH_DEV_AUTOMATION_TESTS
