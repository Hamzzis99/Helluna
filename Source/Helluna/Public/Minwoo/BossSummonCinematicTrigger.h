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

	/**
	 * [PortalAnchorV1] true 면 anchor 가 보스 대신 LocalPortalActor (포탈) 의 위치/회전 사용.
	 *   포탈은 시네마틱 동안 고정이라 카메라가 이동하지 않음 (정적 와이드샷에 유리).
	 *   CameraOffset/LookAtOffset 은 포탈 로컬축 기준으로 해석됨.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cut")
	bool bAnchorToPortal = false;
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
	 * [CloseUpShakeV1] Cut 0 와이드 컷 끝난 후 (발/팔/얼굴 클로즈업 동안) 사용할 약한 쉐이크.
	 *   None 이면 클로즈업 동안 쉐이크 없음.
	 *   CinematicShakeClass 가 단발 (interval=0) 이고 OscillationDuration 이 Cut 0 길이만큼이면,
	 *   이 쉐이크가 그 직후 자동 발화되어 클로즈업 동안 계속 진동.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "BossSummon|Shake",
		meta = (DisplayName = "Close-Up Shake (클로즈업 동안 약한 쉐이크)"))
	TSubclassOf<UCameraShakeBase> CloseUpShakeClass;

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

	// =========================================================================================
	// [PortalBackdropV1] 포탈 뒤 배경 메시 — 회의 피드백 "포탈 뒤에 배경 넣으면 예쁠 거 같다"
	//   포탈과 동시 spawn / 같이 destroy. 클라 로컬 (포탈과 동일 흐름). 메인맵 스카이/우주 톤
	//   머터리얼을 BP CDO 에서 wiring. None 이면 backdrop 비활성.
	// =========================================================================================

	/** 포탈 뒤에 배치할 배경 메시. None 이면 backdrop 비활성. 큰 plane (예: /Engine/BasicShapes/Plane) 권장. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "BossSummon|Portal|Backdrop",
		meta = (DisplayName = "Backdrop Mesh (포탈 뒤 배경 메시)"))
	TObjectPtr<UStaticMesh> BackdropMesh = nullptr;

	/** Backdrop 메시에 덮어쓸 머터리얼. None 이면 메시 default. 메인맵 스카이/우주 톤 권장. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "BossSummon|Portal|Backdrop",
		meta = (DisplayName = "Backdrop Material (배경 머터리얼)"))
	TObjectPtr<UMaterialInterface> BackdropMaterial = nullptr;

	/** Backdrop 위치 — 포탈 로컬축 기준 오프셋. 기본 -300 = 포탈 뒤 3m. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "BossSummon|Portal|Backdrop",
		meta = (DisplayName = "Backdrop Offset (포탈 로컬)"))
	FVector BackdropOffset = FVector(-300.f, 0.f, 0.f);

	/** Backdrop 회전 보정 — 포탈 기준 추가 회전. 기본 Yaw=180 → 포탈 정면이 backdrop 정면 (보스 쪽). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "BossSummon|Portal|Backdrop",
		meta = (DisplayName = "Backdrop Rotation (추가 회전)"))
	FRotator BackdropRotation = FRotator(90.f, 180.f, 0.f);

	/** Backdrop 스케일. 메인맵 톤이 portal 주변을 충분히 채우려면 크게. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "BossSummon|Portal|Backdrop",
		meta = (DisplayName = "Backdrop Scale (메시 크기 배율)"))
	FVector BackdropScale = FVector(20.f, 20.f, 1.f);

	/** Backdrop collision 비활성 (시각용). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "BossSummon|Portal|Backdrop",
		meta = (DisplayName = "Backdrop Collision Off"))
	bool bBackdropCollisionOff = true;

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
	 * [BossEmergeV1] 시네마틱 카메라가 시작된 후 보스가 포탈에서 나타나기까지 시간 (초).
	 *   이 시간 동안 시네마틱 카메라는 이미 켜져 있지만 보스 메시는 숨김 + 정지.
	 *   경과 후 보스 visible + walk + 몽타주 시작 → 카메라가 emergence 포착.
	 *   PortalRevealDelay 와 합쳐서 총 보스 등장까지 = PortalRevealDelay + BossEmergeDelay.
	 *   0 이면 카메라 시작 즉시 보스 등장 (이전 동작).
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "BossSummon|Portal",
		meta = (DisplayName = "Boss Emerge Delay (카메라 시작 후 보스 등장까지)", ClampMin = "0.0", ClampMax = "10.0"))
	float BossEmergeDelay = 1.0f;

	/**
	 * [WideCutSlowMoV1] 시네마틱 시작 직후 일정 시간 동안 글로벌 타임 디레이션 적용.
	 *   "보스가 위엄있게 등장하는" 슬로우 모션 효과. 1.0 = 정상, 0.5 = 절반 속도.
	 *   각 클라 로컬에서 SetGlobalTimeDilation 호출 (멀티플레이 자동 동기화 안 함, 머신별 시각 효과).
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "BossSummon|Pacing",
		meta = (DisplayName = "Wide Cut TimeDilation (슬로우 강도)", ClampMin = "0.05", ClampMax = "1.0"))
	float WideCutTimeDilation = 0.5f;

	/**
	 * [WideCutSlowMoV1] 슬로우 모션 지속 시간 (게임 시간 초). 0 이면 비활성.
	 *   기본 2.5s = Cut 0 와이드 컷 길이와 동일.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "BossSummon|Pacing",
		meta = (DisplayName = "Wide Cut SlowMo Duration (지속 시간)", ClampMin = "0.0", ClampMax = "10.0"))
	float WideCutSlowMoDuration = 2.5f;

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

	/**
	 * [DialogueLine2V1] 두 번째 대사 (선택). 비어있지 않고 SecondLineDelay > 0 이면
	 * 시네마틱 시작 후 SecondLineDelay 초 시점에 PlayDialogue 재호출 → 두 번째 줄로 교체.
	 * UBossDialogueWidget::PlayDialogue 는 매번 페이드인+타이핑 새로 시작하므로 원위젯 재사용 가능.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "BossSummon|Dialogue",
		meta = (DisplayName = "두 번째 대사 (선택)", MultiLine = true))
	FText BossDialogueLine2;

	/** 두 번째 대사 등장 시점 (시네마틱 시작 후 초). 0 이면 두 번째 대사 비활성. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "BossSummon|Dialogue",
		meta = (DisplayName = "두 번째 대사 시점(초)", ClampMin = "0.0", ClampMax = "30.0"))
	float BossDialogueLine2Delay = 0.f;

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

	/**
	 * [DisableCinematic_OnlyHealthBarV1] 시네마틱 logic 모두 skip + HP 바만 spawn — 디버그/테스트용.
	 *   true 시: 카메라 전환·대사·포탈·몽타주 모두 안 발동, HP 바만 즉시 spawn.
	 *   false (default): 정상 시네마틱 + HP 바.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "BossSummon|Test",
		meta = (DisplayName = "Disable Cinematic — HP Bar Only (디버그용)"))
	bool bDisableCinematic_OnlyHealthBar = false;

	/**
	 * [BPDefaultSyncV1] BeginPlay 시 BP CDO 의 Edit-가능 property 를 instance 에 강제 sync.
	 *   true (default): placement instance 의 override 무시, BP CDO 변경이 모든 곳에 자동 적용.
	 *   false: instance 값 그대로 유지 (디자이너가 인스턴스별 다른 값 원할 시).
	 *   Sync 제외 항목: 보스 참조 (Tag/TargetBossActor), 자동 발동 토글, 디버그 토글.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "BossSummon|Sync",
		meta = (DisplayName = "BP CDO 자동 동기화 (insance override 무시)"))
	bool bSyncFromBPDefault = true;

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

	/**
	 * 시네마틱 시작 (클라: ViewTarget 전환).
	 * @param bSkipVisuals  서버에서 결정된 skip 여부 — 클라는 BP default 가 아닌 이 값으로 판정해야
	 *                       GM 토글 (서버 only) 도 클라에 전파됨.
	 * @param WalkDirYaw    [PortalWalkDirRpcV1] 서버가 결정한 보스 walk direction yaw (= ClosestPlayer 방향).
	 *                       클라는 PC iteration 대신 이 값으로 portal forward 계산 — server/client 일관성 보장.
	 * @param BossLocation  [PortalEarlySpawnV1] 서버 측 보스 위치. 메인맵처럼 보스가 멀리 소환되면
	 *                       클라에 보스 actor replication 이 지연되는데, 포탈 spawn 이 보스 actor 에
	 *                       의존하면 retry abort 시 포탈이 안 뜸. 이 값으로 보스 actor 없이 즉시 spawn.
	 */
	UFUNCTION(NetMulticast, Reliable)
	void Multicast_StartCinematic(APawn* Boss, bool bSkipVisuals, float WalkDirYaw, FVector BossLocation);

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

	/**
	 * [BPDefaultSyncV1] BP CDO 의 Edit-가능 property 를 instance 에 강제 복사.
	 *   instance override 가 BP CDO 변경을 가리는 문제 fix.
	 *   Skip 리스트: 보스 참조, 자동 발동 토글, 디버그 토글, 본 sync 토글.
	 */
	void SyncFromBPDefault();

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

	/**
	 * [CinematicWalkYawV1] 서버 face 결정 시점에 저장한 walk yaw — Tick walk 함수가 이 값으로 통일 사용.
	 * 기존: Tick 마다 PC iteration 으로 player 검색 → bOrientRotationToMovement=true 와 결합되어
	 *       face 가 walk dir 로 자동 회전 → portal visual 과 어긋남. 서버가 한 번 결정한 yaw 로 고정.
	 * Player 검색 실패 케이스도 placement yaw 그대로 유지되어 face=walk=portal 일관성 보장.
	 */
	float CinematicWalkYaw = 0.f;

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
	void StartLocalCinematicWithRetry(APawn* Boss, int32 RemainingRetries, float WalkDirYaw);

	/** 시네마틱 본체 (CineCam 스폰 + ViewTarget + 자막). 보스 확보된 후 호출. */
	void StartLocalCinematicBody(APawn* Boss, float WalkDirYaw);

	/**
	 * [BossAIEarlyStopV1] 보스 spawn 직후 AI(StateTree brain)를 즉시 정지.
	 *   StateTreeAIComponent 가 bStartLogicAutomatically=true 라 possess 즉시 AI 가 도는데,
	 *   TryActivate 는 PostSpawnDelay(1.5s) 후 호출이라 그 공백 동안 보스가 행동(공격/패턴)한다.
	 *   그 사이 보스가 건 슬로우(AnimNotify TimeDilation, 시간왜곡 패턴 등)가 시네마틱 종료
	 *   후에도 안 풀리고 다음 시네마틱이 막히는 버그 원인. spawn 감지 즉시 정지시켜 차단.
	 *   controller/brain 이 아직 없으면 0.05s 간격으로 RemainingRetries 회 재시도.
	 */
	void SuppressBossAIImmediate(APawn* Boss, int32 RemainingRetries);

	/**
	 * [PortalEarlySpawnV1] 보스 등장 포탈을 클라에 로컬 spawn.
	 *   보스 actor 가 아니라 위치값만 받으므로 replication 지연과 무관하게 즉시 호출 가능.
	 *   LocalPortalActor 가 이미 유효하면 중복 spawn 방지로 무시.
	 */
	void SpawnEntrancePortal(const FVector& BossLoc, float WalkDirYaw);

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

	/** [CloseUpShakeV1] 클라 로컬에서 약한 close-up 쉐이크 발화. */
	void TriggerLocalCloseUpShake();

	/** [CloseUpShakeV1] Cut 0 끝난 시점에 close-up 쉐이크 시작 타이머. */
	FTimerHandle CloseUpShakeTimer;

	/** [DialogueLine2V1] 두 번째 대사 전환 타이머 핸들 (클라 로컬). */
	FTimerHandle SecondDialogueTimer;

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

	/** [PortalBackdropV1] 클라 로컬 스폰된 배경 메시 액터. 포탈과 함께 destroy. */
	UPROPERTY()
	TWeakObjectPtr<AActor> LocalBackdropActor;

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

	// =========================================================================================
	// [BossEmergeV1] 시네마틱 카메라 시작 후 BossEmergeDelay 만큼 보스 hidden + 정지
	// =========================================================================================

	/** 서버: emerge 후 walk + 몽타주 활성화. */
	void OnBossEmergeElapsedServer();

	/** 클라: emerge 후 보스 mesh visibility 복원. */
	void OnBossEmergeElapsedLocal();

	/** 서버 emerge 타이머. */
	FTimerHandle BossEmergeTimerServer;

	/** 클라 emerge 타이머. */
	FTimerHandle BossEmergeTimerLocal;

	/** [WideCutSlowMoV1] 클라 slow-mo 종료 타이머. */
	FTimerHandle SlowMoRestoreTimerLocal;

	/** [WideCutSlowMoV1] slow-mo 종료 — 글로벌 타임 디레이션 1.0 으로 복원. */
	void OnSlowMoElapsedLocal();

	/** [WalkStopV1] Cut 0 와이드 컷 끝나면 보스 walk 정지 — 클로즈업이 정적이 되도록. */
	FTimerHandle WalkStopTimerServer;

	/** [WalkStopV1] walk 정지 콜백 — bCinematicWalkActive=false + StopMovementImmediately. */
	void OnWalkStopElapsedServer();
};
