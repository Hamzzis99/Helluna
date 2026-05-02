// Source/Helluna/Public/Minwoo/BossSummonCinematicTrigger.h

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "BossSummonCinematicTrigger.generated.h"

class APawn;
class ACameraActor;
class ULevelSequence;
class ULevelSequencePlayer;
class ALevelSequenceActor;
class UBossDialogueWidget;
class UCameraShakeBase;

/**
 * [PortalCutsV1] 시네마틱 카메라 컷 1단위.
 *   보스 메시의 특정 소켓을 앵커로 잡고, 보스 액터 로컬 좌표계(Forward/Right/Up)
 *   기준으로 카메라 위치/시선을 결정. 매 Tick 클라가 직접 SetActorLocationAndRotation 으로
 *   갱신하므로 LevelSequence 없이도 보스가 움직이는 동안 컷이 따라간다.
 */
USTRUCT(BlueprintType)
struct FBossCinematicCut
{
	GENERATED_BODY()

	/** 부착 기준 소켓/본 이름 (Manny 예: foot_r, hand_r, head). None 이면 보스 액터 루트. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cut")
	FName Socket = NAME_None;

	/** 컷 지속시간 (초). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cut", meta = (ClampMin = "0.1"))
	float Duration = 3.f;

	/** 보스 로컬축 기준 카메라 오프셋 — X 정면, Y 우측, Z 위. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cut")
	FVector CameraOffset = FVector(120.f, 60.f, 30.f);

	/** 보스 로컬축 기준 시선 보정. 0 이면 소켓 자체를 응시. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cut")
	FVector LookAtOffset = FVector::ZeroVector;
};

/**
 * 보스 소환 시네마틱 트리거 (Minwoo 브랜치 전용 — BossEncounterCube와 무관한 독립 액터)
 *
 * 역할: 우주선 수리 완료 등 게임 이벤트 시점에 호출되어,
 *       이미 월드에 존재하는 보스 액터를 향해 카메라를 블렌드하고
 *       소환 몽타주가 끝나면 다시 플레이어로 복귀시킨다.
 *
 * 책임 분리: 이 액터는 "카메라 블렌드 + 보스 AI 일시정지 + 몽타주 동기화" 만 담당.
 *           흑백 전환·꽃 컬러웨이브·보스 스폰 등은 BossEncounterCube에 그대로 두고 손대지 않음.
 *
 * 트리거 방법 (택1):
 *   1. bAutoActivateOnBeginPlay = true → 레벨 시작 후 AutoActivateDelay 초 뒤 자동 발동 (테스트용)
 *   2. BP에서 TryActivate(보스 Pawn 또는 nullptr) 호출 → 우주선 수리 이벤트 등에서 사용
 *
 * 보스 지정 우선순위:
 *   1. TryActivate에 인자로 넘겨준 Pawn
 *   2. ExistingBossActorTag로 월드에서 검색
 *   3. TargetBossActor 인스턴스 참조 (레벨 배치 보스용)
 *   → 셋 다 실패하면 활성화 실패
 *
 * 보스 캐릭터 요구사항: AHellunaEnemyCharacter 상속 (Multicast_PlaySummonMontage / OnSummonMontageFinished 사용)
 *                     아니어도 카메라 블렌드는 동작하지만 몽타주는 재생 안 되고 SummonMaxDuration 후 자동 종료.
 */
UCLASS(Blueprintable)
class HELLUNA_API ABossSummonCinematicTrigger : public AActor
{
	GENERATED_BODY()

public:
	ABossSummonCinematicTrigger();

	// =========================================================================================
	// 카메라 / 시네마틱 설정
	// =========================================================================================

	/**
	 * 보스 로컬 좌표 기준 카메라 오프셋:
	 *   X > 0 → 보스 정면(Forward)으로 N cm
	 *   Y > 0 → 보스 오른쪽(Right)으로 N cm
	 *   Z > 0 → 보스 위쪽(Up)으로 N cm
	 * 기본값(450, 150, 100): 정면-우측 약간-위 → 3/4 정면 시점.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "BossSummon|Camera",
		meta = (DisplayName = "Camera Offset (보스 로컬 좌표)"))
	FVector CameraOffset = FVector(450.f, 150.f, 100.f);

	/** 플레이어 → 보스 카메라 블렌드 시간 (초) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "BossSummon|Camera",
		meta = (DisplayName = "Camera Blend In (블렌드 인 시간)", ClampMin = "0.05", ClampMax = "3.0"))
	float CameraBlendIn = 0.5f;

	/** 보스 → 플레이어 카메라 복귀 시간 (초) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "BossSummon|Camera",
		meta = (DisplayName = "Camera Blend Out (블렌드 아웃 시간)", ClampMin = "0.05", ClampMax = "3.0"))
	float CameraBlendOut = 0.4f;

	/**
	 * 카메라 최소 유지 시간 (초) — 몽타주가 없거나 짧아도 이 시간만큼은 보스를 비춘다.
	 * 시네마틱 종료 조건: 이 시간 경과 AND 몽타주 종료(또는 몽타주 없음).
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "BossSummon|Camera",
		meta = (DisplayName = "Min Hold Duration (최소 유지 시간)", ClampMin = "0.5", ClampMax = "15.0"))
	float MinCinematicHoldDuration = 4.5f;

	/** 시네마틱 최대 지속시간 (Failsafe) — 몽타주가 너무 길거나 콜백 누락 시 강제 종료 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "BossSummon|Camera",
		meta = (DisplayName = "Max Duration (최대 지속시간)", ClampMin = "1.0", ClampMax = "30.0"))
	float MaxDuration = 12.0f;

	// =========================================================================================
	// [CinematicShakeV1] 시네마틱 동안 World Camera Shake 반복
	//   포탈/소환 임팩트 대용으로 시네마틱 시작 직후부터 주기적으로 PlayWorldCameraShake 호출.
	//   각 클라 로컬에서만 발화 (각자 자기 PC 카메라 기준), 데디 서버는 스킵.
	// =========================================================================================

	/** 시네마틱 동안 반복 재생할 카메라 쉐이크. None 이면 쉐이크 비활성. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "BossSummon|Shake",
		meta = (DisplayName = "시네마틱 카메라 쉐이크"))
	TSubclassOf<UCameraShakeBase> CinematicShakeClass;

	/** 쉐이크 반복 간격 (초). 0 또는 음수면 시작 시 1회만. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "BossSummon|Shake",
		meta = (DisplayName = "반복 간격 (초)", ClampMin = "0.0", ClampMax = "5.0"))
	float CinematicShakeInterval = 0.4f;

	/** 풀 강도 거리 (cm). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "BossSummon|Shake",
		meta = (DisplayName = "Inner Radius (cm)", ClampMin = "0.0"))
	float CinematicShakeInnerRadius = 0.f;

	/** 감쇠 종료 거리 (cm). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "BossSummon|Shake",
		meta = (DisplayName = "Outer Radius (cm)", ClampMin = "0.0"))
	float CinematicShakeOuterRadius = 6000.f;

	/** 거리별 감쇠 지수. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "BossSummon|Shake",
		meta = (DisplayName = "Falloff", ClampMin = "0.1", ClampMax = "10.0"))
	float CinematicShakeFalloff = 1.f;

	/**
	 * 카메라 연출 LevelSequence (선택사항).
	 * 설정 시: 이 시퀀스가 카메라 위치/회전을 제어 (Camera Cuts 트랙으로 ViewTarget 자동 전환).
	 * 미설정 + CameraCuts 비어있음: 기존 방식대로 보스에 카메라 부착 + CameraOffset 적용.
	 * 미설정 + CameraCuts 있음: 코드 기반 컷 시퀀스가 카메라를 매 Tick 갱신 (소켓별 클로즈업).
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "BossSummon|Sequence",
		meta = (DisplayName = "Camera Level Sequence (시네마틱 시퀀스)"))
	TObjectPtr<ULevelSequence> CameraSequence;

	// =========================================================================================
	// [PortalCutsV1] 코드 기반 카메라 컷 시퀀스 — 보스 소켓 앵커 + 로컬축 오프셋 매 Tick 갱신
	//   LevelSequence 와 상호배타: CameraSequence 가 설정되어 있으면 이 배열은 무시됨.
	//   기본값: foot_r → hand_r → head, 각 3초 (총 9초).
	//   MinCinematicHoldDuration 은 컷 합계로 자동 확장된다.
	// =========================================================================================

	/** 컷 시퀀스. 비어있으면 단일 카메라 폴백 모드. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "BossSummon|Cuts",
		meta = (DisplayName = "Camera Cuts (코드 기반 컷 시퀀스)"))
	TArray<FBossCinematicCut> CameraCuts;

	// =========================================================================================
	// [PortalCutsV1] 보스 등장 포탈 — 시네마틱 시작 시 클라마다 로컬 스폰, 종료 시 지연 후 파괴
	//   각 클라가 자기 머신에서만 스폰하므로 네트워크 트래픽 없음. 데디 서버는 자동 스킵.
	// =========================================================================================

	/** 시네마틱 시작 시 보스 뒤에 스폰할 포탈 액터 BP. None 이면 포탈 비활성. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "BossSummon|Portal",
		meta = (DisplayName = "Portal Class (포탈 BP)"))
	TSubclassOf<AActor> PortalClass;

	/** 보스 로컬축 기준 포탈 스폰 오프셋 (X 정면, Y 우측, Z 위). 기본 -150 → 보스 뒤 1.5m. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "BossSummon|Portal",
		meta = (DisplayName = "Spawn Offset (보스 로컬)"))
	FVector PortalSpawnOffset = FVector(-150.f, 0.f, 0.f);

	/** 포탈 회전 보정. 기본 Yaw=180 → 포탈 정면이 보스를 바라봄(보스가 걸어나오는 방향). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "BossSummon|Portal",
		meta = (DisplayName = "Spawn Rotation (보스 기준 추가 회전)"))
	FRotator PortalSpawnRotation = FRotator(0.f, 180.f, 0.f);

	/** 시네마틱 종료 후 포탈 파괴 지연 (초). 보스가 빠져나오는 끝 잔상용. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "BossSummon|Portal",
		meta = (DisplayName = "Destroy Delay (종료 후 파괴 지연)", ClampMin = "0.0", ClampMax = "5.0"))
	float PortalDestroyDelay = 1.0f;

	/** [PortalRevealV1] 포탈 액터 스폰 스케일 (BP_Portal 기본 크기 대비 배율). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "BossSummon|Portal",
		meta = (DisplayName = "Spawn Scale (포탈 크기 배율)"))
	FVector PortalSpawnScale = FVector(2.f, 2.f, 2.f);

	/**
	 * [PortalRevealV1] 포탈만 보이고 보스는 숨긴 상태로 유지하는 시간 (초).
	 *   이 시간 동안 카메라는 플레이어를 비추고 포탈 VFX 만 화면에 등장.
	 *   경과 후 보스 메시 visibility 복원 + 시네마틱 카메라(컷) + walk 시작.
	 *   0 으로 두면 즉시 보스 + 카메라 시작 (기존 동작).
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "BossSummon|Portal",
		meta = (DisplayName = "Reveal Delay (포탈만 보이는 시간)", ClampMin = "0.0", ClampMax = "10.0"))
	float PortalRevealDelay = 1.5f;

	/**
	 * [GracePeriodV1] 시네마틱 끝나고 보스 AI 재개 + 무적 해제까지 유예 (초).
	 *   카메라가 player 로 블렌드되는 동안 보스가 즉시 공격하면 플레이어가 반응 못 함.
	 *   기본 1초 — 카메라 블렌드 0.5s + 0.5s 여유.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "BossSummon|Pacing",
		meta = (DisplayName = "Post Cinematic Grace (종료 후 보스 행동 지연)", ClampMin = "0.0", ClampMax = "5.0"))
	float PostCinematicGracePeriod = 1.0f;

	// =========================================================================================
	// 보스 대사 자막 (선택사항) — 시네마틱 동안 화면 하단에 표시
	// =========================================================================================

	/**
	 * 대사 자막 위젯 클래스 (BP of UBossDialogueWidget).
	 * 비워두면 자막 생략. 로컬 플레이어 카메라에 Multicast_Start 시 추가, End 시 페이드 아웃.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "BossSummon|Dialogue",
		meta = (DisplayName = "대사 위젯 클래스"))
	TSubclassOf<UBossDialogueWidget> DialogueWidgetClass;

	/** 화자 이름 (예: "DESPAIR", "파괴자"). 비우면 헤더 줄 숨김. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "BossSummon|Dialogue",
		meta = (DisplayName = "화자 이름"))
	FText BossSpeakerName;

	/** 보스가 말하는 대사 본문 (한 줄). 비우면 자막 생략. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "BossSummon|Dialogue",
		meta = (DisplayName = "대사 본문", MultiLine = true))
	FText BossDialogueLine;

	// =========================================================================================
	// 보스 HP 바 / 후속 HUD 연출 — 시네마틱 종료 시 BP가 스폰
	// =========================================================================================

	/**
	 * 시네마틱 종료 시점(Multicast_End 클라 경로)에 호출되는 BP 이벤트.
	 * C++ 기본 동작(HP 바 스폰) 후 추가 연출을 BP에서 얹고 싶을 때 사용.
	 * 서버 호출 없이 로컬 플레이어 머신에서만 발화되므로 AddToViewport 안전.
	 */
	UFUNCTION(BlueprintImplementableEvent, Category = "BossSummon|HUD",
		meta = (DisplayName = "시네마틱 종료 후 HUD 후처리"))
	void OnCinematicEndedClient(APawn* Boss);

	/**
	 * 보스 HP 바 위젯 클래스. WBP_BossHealthBar 할당.
	 * 위젯 BP에 Actor Reference 변수 'BossActor' (Expose on Spawn)가 있어야 자동 연결됨.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "BossSummon|HUD",
		meta = (DisplayName = "보스 HP 바 위젯 클래스"))
	TSubclassOf<UUserWidget> BossHealthBarWidgetClass;

	/** ZOrder (HUD 위에 표시되려면 기본 HUD ZOrder보다 높게). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "BossSummon|HUD",
		meta = (DisplayName = "HP 바 ZOrder", ClampMin = "0", ClampMax = "1000"))
	int32 BossHealthBarZOrder = 5;

	// =========================================================================================
	// 보스 지정 (3가지 방법 중 택1, 우선순위: 인자 > Tag > Actor 참조)
	// =========================================================================================

	/** 월드에서 이 태그를 가진 첫 번째 Pawn을 보스로 지정 (예: "TestBoss") */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "BossSummon|Target",
		meta = (DisplayName = "Existing Boss Actor Tag (기존 보스 태그)"))
	FName ExistingBossActorTag = NAME_None;

	/** 레벨에 직접 배치한 보스 액터 인스턴스 참조 (Tag보다 우선순위 낮음) */
	UPROPERTY(EditInstanceOnly, BlueprintReadOnly, Category = "BossSummon|Target",
		meta = (DisplayName = "Target Boss Actor (보스 인스턴스)"))
	TObjectPtr<APawn> TargetBossActor;

	// =========================================================================================
	// 자동 발동 (테스트용)
	// =========================================================================================

	/** 레벨 시작 시 자동으로 시네마틱 발동 (서버 권한, 테스트 전용) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "BossSummon|Test",
		meta = (DisplayName = "Auto Activate On BeginPlay (자동 발동)"))
	bool bAutoActivateOnBeginPlay = false;

	/** 자동 발동까지 지연 시간 (초) — 폴백 모드(태그 보스 스폰 대기 비활성)일 때만 적용 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "BossSummon|Test",
		meta = (DisplayName = "Auto Activate Delay (자동 발동 지연)", ClampMin = "0.1", ClampMax = "10.0",
		EditCondition = "bAutoActivateOnBeginPlay && !bWaitForTaggedBossSpawn"))
	float AutoActivateDelay = 1.5f;

	/**
	 * 태그된 보스가 월드에 스폰되는 순간을 감지해 자동 발동 (이벤트 기반).
	 * true → AddOnActorSpawnedHandler로 스폰 이벤트 구독, 매칭되는 보스 발견 시 즉시 (PostSpawnDelay 후) 시네마틱 시작.
	 * false → AutoActivateDelay 타이머 방식 (시간 추측 — 게임모드 일정 변경에 취약).
	 *
	 * BeginPlay 시점에 이미 태그 보스가 월드에 있으면 즉시 폴백(타이머)로 진행.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "BossSummon|Test",
		meta = (DisplayName = "Wait For Tagged Boss Spawn (스폰 이벤트 대기)",
		EditCondition = "bAutoActivateOnBeginPlay"))
	bool bWaitForTaggedBossSpawn = true;

	/** 스폰 이벤트 감지 후 시네마틱 시작까지 유예 시간 (초) — 보스 BeginPlay/AI 초기화 완료 대기 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "BossSummon|Test",
		meta = (DisplayName = "Post Spawn Delay (스폰 후 유예시간)", ClampMin = "0.0", ClampMax = "5.0",
		EditCondition = "bAutoActivateOnBeginPlay && bWaitForTaggedBossSpawn"))
	float PostSpawnDelay = 0.3f;

	// =========================================================================================
	// [CinematicWalkV1] 시네마틱 중 보스 자체 전진
	//   AM_Boss_Walk 가 in-place 애니라 root motion 으로 못 움직임 → Tick 에서 AddMovementInput 으로 직접 전진.
	// =========================================================================================

	/**
	 * 시네마틱 동안 보스가 매 Tick AddMovementInput 으로 전진하는 속도 (cm/s).
	 *  0  : 전진 안 함 (기존 동작 유지)
	 *  >0 : 시네마틱 시작 시 보스 MaxWalkSpeed 를 이 값으로 잠시 덮고 매 Tick 전방으로 입력.
	 *       시네마틱 종료 시 원래 MaxWalkSpeed 로 복원.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "BossSummon|Movement",
		meta = (DisplayName = "시네마틱 중 보스 전진 속도 (cm/s)", ClampMin = "0.0", ClampMax = "1000.0"))
	float CinematicBossWalkSpeed = 200.f;

	/**
	 * 전진 방향:
	 *  true  : 보스의 ActorForwardVector 방향 (소환 몽타주가 향한 방향).
	 *  false : 플레이어/타깃 방향 (현재 가장 가까운 PlayerPawn).
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "BossSummon|Movement",
		meta = (DisplayName = "전진 방향: 보스 정면 (false=플레이어 방향)"))
	bool bCinematicWalkUseBossForward = false;

	// =========================================================================================
	// 공개 API
	// =========================================================================================

	/**
	 * 시네마틱 활성화 (서버 권한 필요).
	 * @param InTargetBoss  대상 보스. nullptr이면 ExistingBossActorTag → TargetBossActor 순으로 검색.
	 * @return 성공 여부
	 */
	UFUNCTION(BlueprintCallable, Category = "BossSummon")
	bool TryActivate(APawn* InTargetBoss = nullptr);

	/** 현재 시네마틱 진행 중인지 (서버 기준) */
	UFUNCTION(BlueprintPure, Category = "BossSummon")
	bool IsCinematicActive() const { return bCinematicActive; }

	// =========================================================================================
	// Multicast RPC — 서버 → 모든 클라이언트
	// =========================================================================================

	/** 시네마틱 시작 (클라: ViewTarget 전환) */
	UFUNCTION(NetMulticast, Reliable)
	void Multicast_StartCinematic(APawn* Boss);

	/** 시네마틱 종료 (클라: ViewTarget 복귀) */
	UFUNCTION(NetMulticast, Reliable)
	void Multicast_EndCinematic();

	/**
	 * 모든 클라에서 보스 SkelMesh의 bPauseAnims 토글.
	 * true: SummonMontage 끝난 후 보스가 Locomotion walk 애니로 복귀해 "러닝머신" 되는 걸 방지 — 마지막 포즈에 고정.
	 * false: 시네마틱 종료 시 해제.
	 */

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void Tick(float DeltaTime) override;

private:
	// =========================================================================================
	// 내부 헬퍼
	// =========================================================================================

	/** 자동 발동 타이머 콜백 */
	void HandleAutoActivate();

	/** 월드 액터 스폰 이벤트 콜백 — 태그 매칭 보스 발견 시 시네마틱 예약 */
	void OnActorSpawnedInWorld(AActor* SpawnedActor);

	/** 스폰 감지 핸들러 등록 해제 */
	void UnregisterActorSpawnHandler();

	/** 보스 검색 (Tag → Actor 참조 순서) */
	APawn* ResolveTargetBoss() const;

	/** 몽타주 종료 콜백 (서버) — 플래그만 세팅하고 종료 조건 평가 */
	void OnSummonMontageFinishedServer();

	/** 최소 유지 시간 경과 콜백 (서버) — 플래그만 세팅하고 종료 조건 평가 */
	void OnMinHoldElapsedServer();

	/** 종료 조건(둘 다 충족) 검사 후 시네마틱 종료 */
	void TryFinishCinematic();

	/** 서버: 시네마틱 종료 처리 (몽타주 + 최소시간 + Failsafe 공용) */
	void HandleCinematicCompletedServer();

	// =========================================================================================
	// 내부 상태
	// =========================================================================================

	/** 시네마틱 활성 (서버 기준 — 중복 시작 방지) */
	bool bCinematicActive = false;

	/** AI Brain을 우리가 멈췄는지 (복원용) */
	bool bAIStopped = false;

	/** 현재 시네마틱 대상 보스 (서버 전용) */
	TWeakObjectPtr<APawn> ActiveBoss;

	/** 클라이언트 로컬 시네마틱 카메라 액터 (Multicast 시작 시 스폰, 종료 시 파괴) */
	UPROPERTY()
	TWeakObjectPtr<ACameraActor> LocalCameraActor;

	/** 클라이언트 로컬 시퀀스 플레이어 (CameraSequence 사용 시) */
	UPROPERTY()
	TObjectPtr<ULevelSequencePlayer> LocalSequencePlayer;

	/** 클라이언트 로컬 시퀀스 액터 (CameraSequence 사용 시) */
	UPROPERTY()
	TObjectPtr<ALevelSequenceActor> LocalSequenceActor;

	/** 로컬 대사 자막 위젯 (Multicast_Start 시 생성, End 시 페이드 아웃 → Remove) */
	UPROPERTY()
	TObjectPtr<UBossDialogueWidget> LocalDialogueWidget;

	/** Multicast_Start에서 받은 보스 참조 — Multicast_End에서 BP 이벤트로 전달하기 위해 캐시. */
	UPROPERTY()
	TWeakObjectPtr<APawn> LocalCinematicBoss;

	/** 로컬 HP 바 위젯 (보스 전투 종료까지 유지). */
	UPROPERTY()
	TObjectPtr<UUserWidget> LocalBossHealthBar;

	/** 보스 OnDeath 바인딩용 — HP 바 제거. */
	UFUNCTION()
	void OnBossDiedClient(AActor* DeadActor, AActor* KillerActor);

	/**
	 * [ClientBossResolveV1] 멀티플레이에서 클라가 Multicast_Start 를 보스 리플리케이션 직전에 받으면
	 * Boss=NULL 로 도착해 시네마틱이 무음으로 끊기는 문제 → 태그 검색 폴백 + 짧은 폴링.
	 * 첫 시도가 NULL 이면 0.1s 간격으로 최대 RemainingRetries 회 재시도.
	 */
	void StartLocalCinematicWithRetry(APawn* Boss, int32 RemainingRetries);

	/** 시네마틱 본체 (CineCam 스폰 + ViewTarget + 자막). 보스 확보된 후 호출. */
	void StartLocalCinematicBody(APawn* Boss);

	/** [ClientBossResolveV1] 클라 폴링용 타이머 핸들 */
	FTimerHandle ClientCinematicRetryTimer;

	/** 자동 발동 타이머 핸들 */
	FTimerHandle AutoActivateTimer;

	/** OnActorSpawned 델리게이트 핸들 (해제용) */
	FDelegateHandle ActorSpawnedHandle;

	/** 최소 유지 시간 타이머 핸들 */
	FTimerHandle MinHoldTimer;

	/** Failsafe 타이머 핸들 (절대 상한) */
	FTimerHandle FailsafeTimer;

	/** [CinematicShakeV1] 시네마틱 동안 카메라 쉐이크 반복 타이머 (클라 로컬). */
	FTimerHandle CinematicShakeTimer;

	/** [CinematicShakeV1] 클라 로컬에서 1회 World Camera Shake 발화. */
	void TriggerLocalCinematicShake();

	/** 몽타주 종료됨 (또는 몽타주 없음) 플래그 */
	bool bMontageFinishedFlag = false;

	/** 최소 유지 시간 경과 플래그 */
	bool bMinHoldElapsedFlag = false;

	/** [CinematicWalkV1] 시네마틱 시작 직전 보스 MaxWalkSpeed 백업 — 종료 시 복원용. -1 = 미백업. */
	float SavedBossMaxWalkSpeed = -1.f;

	// =========================================================================================
	// [PortalCutsV1] 코드 기반 카메라 컷 + 포탈 스폰 (클라 로컬 상태)
	// =========================================================================================

	/** 클라 로컬 스폰된 포탈 액터. Multicast_End 에서 PortalDestroyDelay 후 파괴. */
	UPROPERTY()
	TWeakObjectPtr<AActor> LocalPortalActor;

	/** 현재 활성 컷 인덱스 (-1 = 미초기화). Multicast_End 시 -1 로 리셋. */
	int32 CurrentCutIndex = -1;

	/** 컷 시퀀스 시작 후 누적 시간 (초). */
	float CutsElapsedTime = 0.f;

	/** 클라 로컬 카메라 위치/회전을 현재 컷에 맞춰 갱신 (매 Tick). */
	void UpdateCameraForCurrentCut();

	/** [GracePeriodV1] PostCinematicGracePeriod 경과 후 보스 무적 해제 + AI 재개. */
	void OnGracePeriodElapsed();

	/** [GracePeriodV1] grace 타이머 핸들. */
	FTimerHandle GraceTimer;

	// =========================================================================================
	// [PortalRevealV1] 포탈만 먼저 보이고 잠시 후 보스 + 카메라 컷 시작
	// =========================================================================================

	/** 서버: PortalRevealDelay 경과 후 walk + 몽타주 시작. */
	void OnPortalRevealElapsedServer();

	/** 클라: PortalRevealDelay 경과 후 보스 메시 표시 + 카메라 스폰 + 컷 시작. */
	void StartCinematicCameraAfterReveal();

	/** 서버 reveal 타이머 핸들. */
	FTimerHandle PortalRevealTimerServer;

	/** 클라 reveal 타이머 핸들. */
	FTimerHandle PortalRevealTimerLocal;

	/** 서버: 포탈 reveal 후에만 walk Tick 실행. */
	bool bCinematicWalkActive = false;
};
