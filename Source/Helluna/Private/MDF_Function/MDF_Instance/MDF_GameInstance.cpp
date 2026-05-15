// MDF_GameInstance.cpp
// 게임 인스턴스 구현
// 
// ============================================
// 📌 역할:
// - Seamless Travel에서도 유지되는 데이터 관리
// - 로그인 플레이어 목록 관리 (동시 접속 방지)
// 
// ============================================
// 📌 로그인 시스템에서의 역할:
// ============================================
// 
// LoggedInPlayerIds (TSet<FString>)
//   - 현재 접속 중인 플레이어 ID 목록
//   - 예: {"playerA", "playerB", "playerC"}
// 
// [동시 접속 방지 흐름]
// 
// 1. 플레이어 A 로그인 시도 (ID: "test123")
// 2. DefenseGameMode::ProcessLogin()에서:
//    GameInstance->IsPlayerLoggedIn("test123") 체크
//    └─ false → 로그인 진행
// 3. OnLoginSuccess()에서:
//    GameInstance->RegisterLogin("test123")
//    └─ LoggedInPlayerIds에 추가
// 
// 4. 플레이어 B가 같은 ID로 로그인 시도
// 5. IsPlayerLoggedIn("test123") → true
//    └─ 로그인 거부! "이미 접속 중인 계정입니다"
// 
// 6. 플레이어 A 로그아웃 (연결 끊김)
// 7. DefenseGameMode::Logout()에서:
//    GameInstance->RegisterLogout("test123")
//    └─ LoggedInPlayerIds에서 제거
// 
// 8. 이제 플레이어 B가 "test123"으로 로그인 가능!
// 
// ============================================
// 📌 Seamless Travel과의 관계:
// ============================================
// - GameInstance는 맵 이동해도 파괴되지 않음
// - 따라서 LoggedInPlayerIds도 맵 이동 후에도 유지
// - 맵 이동 후에도 동시 접속 방지 정상 작동
// 
// 📌 작성자: Gihyeon
// ============================================

#include "MDF_Function/MDF_Instance/MDF_GameInstance.h"
#include "Login/Widget/HellunaLoadingWidget.h"
#include "Loading/SLoadingSnapshotWidget.h"
#include "Blueprint/UserWidget.h"
#include "Engine/LocalPlayer.h"
#include "Engine/Texture2D.h"
#include "Engine/GameViewportClient.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "TimerManager.h"
#include "MoviePlayer.h"
#include "UnrealClient.h"
#include "PixelFormat.h"
#include "RenderingThread.h"
#include "TextureResource.h"
#include "RHI.h"
#include "Engine/Engine.h"  // [§17 3-Layer] StreamingPause delegate

// [§17++ Phase 2] AsyncLoadingScreen plugin 통합 (TargetType=Server는 dependency 제외됨, 가드)
#if !UE_SERVER
#include "AsyncLoadingScreen.h"
#include "AsyncLoadingScreenLibrary.h"
#endif

// ============================================
// 🔐 RegisterLogin - 로그인 등록
// ============================================
// 📌 호출 시점: DefenseGameMode::OnLoginSuccess() 에서 호출
// 📌 역할: 접속자 목록에 PlayerId 추가
// ============================================
void UMDF_GameInstance::RegisterLogin(const FString& PlayerId)
{
	if (!PlayerId.IsEmpty())
	{
		LoggedInPlayerIds.Add(PlayerId);
		UE_LOG(LogTemp, Warning, TEXT("[GameInstance] ★ RegisterLogin: '%s' (현재 접속자 %d명)"), *PlayerId, LoggedInPlayerIds.Num());
		
		// 디버깅: 현재 접속자 목록 출력
		UE_LOG(LogTemp, Warning, TEXT("[GameInstance] 접속자 목록:"));
		for (const FString& Id : LoggedInPlayerIds)
		{
			UE_LOG(LogTemp, Warning, TEXT("[GameInstance]   - '%s'"), *Id);
		}
	}
}

// ============================================
// 🔐 RegisterLogout - 로그아웃 등록
// ============================================
// 📌 호출 시점: DefenseGameMode::Logout() 에서 호출
// 📌 역할: 접속자 목록에서 PlayerId 제거
// 📌 효과: 다른 클라이언트가 같은 ID로 로그인 가능해짐
// ============================================
void UMDF_GameInstance::RegisterLogout(const FString& PlayerId)
{
	if (LoggedInPlayerIds.Contains(PlayerId))
	{
		LoggedInPlayerIds.Remove(PlayerId);
		UE_LOG(LogTemp, Warning, TEXT("[GameInstance] ★ RegisterLogout: '%s' (현재 접속자 %d명)"), *PlayerId, LoggedInPlayerIds.Num());
	}
}

// ============================================
// 🔐 IsPlayerLoggedIn - 동시 접속 체크
// ============================================
// 📌 호출 시점: DefenseGameMode::ProcessLogin() 에서 호출
// 📌 역할: 해당 PlayerId가 이미 접속 중인지 확인
// 📌 반환값:
//    - true: 이미 접속 중 → 로그인 거부해야 함
//    - false: 접속 안 함 → 로그인 진행 가능
// ============================================
bool UMDF_GameInstance::IsPlayerLoggedIn(const FString& PlayerId) const
{
	bool bResult = LoggedInPlayerIds.Contains(PlayerId);
	UE_LOG(LogTemp, Warning, TEXT("[GameInstance] IsPlayerLoggedIn('%s') = %s (접속자 %d명)"), 
		*PlayerId, bResult ? TEXT("TRUE") : TEXT("FALSE"), LoggedInPlayerIds.Num());
	return bResult;
}

int32 UMDF_GameInstance::GetLoggedInPlayerCount() const
{
	return LoggedInPlayerIds.Num();
}

// ============================================
// 로딩 화면 (Loading Screen)
// ============================================

void UMDF_GameInstance::ShowLoadingScreen(const FString& Message)
{
	// 이미 유효한 로딩 위젯이 있으면 메시지만 갱신
	if (IsValid(LoadingWidget))
	{
		LoadingWidget->SetLoadingMessage(Message);
		UE_LOG(LogTemp, Log, TEXT("[GameInstance] 로딩 화면 메시지 갱신: '%s'"), *Message);
		return;
	}

	if (!LoadingWidgetClass)
	{
		UE_LOG(LogTemp, Warning, TEXT("[GameInstance] LoadingWidgetClass 미설정! BP에서 설정 필요"));
		return;
	}

	// 첫 번째 로컬 플레이어 컨트롤러로 위젯 생성
	APlayerController* PC = GetFirstLocalPlayerController();
	if (!PC)
	{
		UE_LOG(LogTemp, Warning, TEXT("[GameInstance] 로딩 화면 표시 실패 - LocalPlayerController 없음"));
		return;
	}

	LoadingWidget = CreateWidget<UHellunaLoadingWidget>(PC, LoadingWidgetClass);
	if (!LoadingWidget)
	{
		UE_LOG(LogTemp, Error, TEXT("[GameInstance] 로딩 위젯 생성 실패!"));
		return;
	}

	LoadingWidget->SetLoadingMessage(Message);
	LoadingWidget->AddToViewport(9999);  // 최상위 ZOrder

	UE_LOG(LogTemp, Log, TEXT("[GameInstance] 로딩 화면 표시: '%s'"), *Message);
}

void UMDF_GameInstance::HideLoadingScreen()
{
	if (!IsValid(LoadingWidget))
	{
		// 맵 전환 등으로 이미 파괴됨 → 포인터만 정리
		LoadingWidget = nullptr;
		return;
	}

	LoadingWidget->RemoveFromParent();
	LoadingWidget = nullptr;

	UE_LOG(LogTemp, Log, TEXT("[GameInstance] 로딩 화면 숨김"));
}

// ════════════════════════════════════════════════════════════════════════════════
// §13 v2.1 — Loading Barrier A/B/C 연속 로딩
// ════════════════════════════════════════════════════════════════════════════════

void UMDF_GameInstance::Init()
{
	Super::Init();

	if (!IsRunningDedicatedServer())
	{
		FCoreUObjectDelegates::PreLoadMap.AddUObject(this, &UMDF_GameInstance::OnPreLoadMap);
		FCoreUObjectDelegates::PostLoadMapWithWorld.AddUObject(this, &UMDF_GameInstance::OnPostLoadMapWithWorld);

#if !UE_SERVER
		// [§17 plugin fix] AsyncLoadingScreen은 default로 모든 LoadMap에 trigger되어 빈 검은 화면을 띄움.
		// 우주선 핸드오프(SetupSnapshotLoadingScreen) 시점에만 활성화하도록 기본 비활성화.
		UAsyncLoadingScreenLibrary::SetEnableLoadingScreen(false);
		UE_LOG(LogTemp, Warning, TEXT("[LoadingDbg][GI] Init — AsyncLoadingScreen plugin 기본 비활성화 (우주선 핸드오프 시에만 활성화)"));

		// [§17 3-Layer] Engine StreamingPause delegate를 우리 것으로 교체.
		// default StreamingPauseRendering 모듈은 이미 등록됐지만 RegisterBegin/EndStreamingPauseRenderingDelegate는
		// 단순 assignment라 우리 delegate가 덮어쓴다 → engine throbber 대신 우리 SLoadingSnapshotWidget 표시.
		if (GEngine)
		{
			FBeginStreamingPauseDelegate* BeginDel = new FBeginStreamingPauseDelegate;
			BeginDel->BindUObject(this, &UMDF_GameInstance::OnEngineStreamingPauseBegin);
			GEngine->RegisterBeginStreamingPauseRenderingDelegate(BeginDel);
			StreamingPauseBeginDelegateRaw = BeginDel;

			FEndStreamingPauseDelegate* EndDel = new FEndStreamingPauseDelegate;
			EndDel->BindUObject(this, &UMDF_GameInstance::OnEngineStreamingPauseEnd);
			GEngine->RegisterEndStreamingPauseRenderingDelegate(EndDel);
			StreamingPauseEndDelegateRaw = EndDel;

			UE_LOG(LogTemp, Warning, TEXT("[LoadingDbg][GI] Init — StreamingPause delegate 교체 완료 (engine throbber 비활성화)"));
		}
#endif
	}
}

void UMDF_GameInstance::Shutdown()
{
	if (!IsRunningDedicatedServer())
	{
		FCoreUObjectDelegates::PreLoadMap.RemoveAll(this);
		FCoreUObjectDelegates::PostLoadMapWithWorld.RemoveAll(this);

#if !UE_SERVER
		// [§17 3-Layer] StreamingPause delegate 해제 (UEngine은 단순 pointer 보관이라 nullptr로 reset)
		if (GEngine)
		{
			GEngine->RegisterBeginStreamingPauseRenderingDelegate(nullptr);
			GEngine->RegisterEndStreamingPauseRenderingDelegate(nullptr);
		}
		if (StreamingPauseBeginDelegateRaw)
		{
			delete static_cast<FBeginStreamingPauseDelegate*>(StreamingPauseBeginDelegateRaw);
			StreamingPauseBeginDelegateRaw = nullptr;
		}
		if (StreamingPauseEndDelegateRaw)
		{
			delete static_cast<FEndStreamingPauseDelegate*>(StreamingPauseEndDelegateRaw);
			StreamingPauseEndDelegateRaw = nullptr;
		}
#endif

		if (UGameViewportClient* VC = GetGameViewportClient())
		{
			if (CachedScreenshotHandle.IsValid())
			{
				VC->OnScreenshotCaptured().Remove(CachedScreenshotHandle);
				CachedScreenshotHandle.Reset();
			}
		}
	}

	Super::Shutdown();
}

void UMDF_GameInstance::CaptureLoadingSnapshot(FSimpleDelegate OnComplete)
{
	if (IsRunningDedicatedServer())
	{
		OnComplete.ExecuteIfBound();
		return;
	}

	if (bCaptureInProgress)
	{
		UE_LOG(LogTemp, Warning, TEXT("[LoadingDbg][GI] CaptureLoadingSnapshot 중복 호출 무시"));
		OnComplete.ExecuteIfBound();
		return;
	}

	UGameViewportClient* VC = GetGameViewportClient();
	if (!VC)
	{
		UE_LOG(LogTemp, Warning, TEXT("[LoadingDbg][GI] CaptureLoadingSnapshot — GameViewportClient null"));
		OnComplete.ExecuteIfBound();
		return;
	}

	bCaptureInProgress = true;
	PendingCaptureCallback = OnComplete;

	if (CachedScreenshotHandle.IsValid())
	{
		VC->OnScreenshotCaptured().Remove(CachedScreenshotHandle);
		CachedScreenshotHandle.Reset();
	}
	CachedScreenshotHandle = VC->OnScreenshotCaptured().AddUObject(this, &UMDF_GameInstance::OnScreenshotCaptured);

	FScreenshotRequest::RequestScreenshot(false);
	UE_LOG(LogTemp, Log, TEXT("[LoadingDbg][GI] CaptureLoadingSnapshot 요청 — RequestScreenshot 호출"));

	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().SetTimer(CaptureTimeoutHandle, this,
			&UMDF_GameInstance::OnSnapshotCaptureTimeout, 0.5f, false);
	}
}

void UMDF_GameInstance::OnScreenshotCaptured(int32 W, int32 H, const TArray<FColor>& Bitmap)
{
	if (UGameViewportClient* VC = GetGameViewportClient())
	{
		if (CachedScreenshotHandle.IsValid())
		{
			VC->OnScreenshotCaptured().Remove(CachedScreenshotHandle);
			CachedScreenshotHandle.Reset();
		}
	}
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(CaptureTimeoutHandle);
	}

	bCaptureInProgress = false;

	if (W <= 0 || H <= 0 || Bitmap.Num() <= 0)
	{
		UE_LOG(LogTemp, Warning, TEXT("[LoadingDbg][GI] OnScreenshotCaptured — 빈 비트맵 (%dx%d, Pixels=%d)"), W, H, Bitmap.Num());
		LoadingSnapshotTexture = nullptr;
		FSimpleDelegate Cb = PendingCaptureCallback;
		PendingCaptureCallback.Unbind();
		Cb.ExecuteIfBound();
		return;
	}

	LoadingSnapshotTexture = UTexture2D::CreateTransient(W, H, PF_B8G8R8A8);
	if (!LoadingSnapshotTexture)
	{
		UE_LOG(LogTemp, Warning, TEXT("[LoadingDbg][GI] CreateTransient 실패"));
		FSimpleDelegate Cb = PendingCaptureCallback;
		PendingCaptureCallback.Unbind();
		Cb.ExecuteIfBound();
		return;
	}

	LoadingSnapshotTexture->SRGB = true;
	if (FTexturePlatformData* PD = LoadingSnapshotTexture->GetPlatformData())
	{
		FTexture2DMipMap& Mip = PD->Mips[0];
		void* Data = Mip.BulkData.Lock(LOCK_READ_WRITE);
		FMemory::Memcpy(Data, Bitmap.GetData(), Bitmap.Num() * sizeof(FColor));
		Mip.BulkData.Unlock();
	}
	LoadingSnapshotTexture->UpdateResource();

	UE_LOG(LogTemp, Log, TEXT("[LoadingDbg][GI] OnScreenshotCaptured OK — %dx%d (%d pixels)"), W, H, Bitmap.Num());

	FSimpleDelegate Cb = PendingCaptureCallback;
	PendingCaptureCallback.Unbind();
	Cb.ExecuteIfBound();
}

void UMDF_GameInstance::OnSnapshotCaptureTimeout()
{
	if (!bCaptureInProgress)
	{
		return;
	}

	UE_LOG(LogTemp, Warning, TEXT("[LoadingDbg][GI] OnSnapshotCaptureTimeout — 0.5s 내 캡처 실패, 폴백 진행"));

	if (UGameViewportClient* VC = GetGameViewportClient())
	{
		if (CachedScreenshotHandle.IsValid())
		{
			VC->OnScreenshotCaptured().Remove(CachedScreenshotHandle);
			CachedScreenshotHandle.Reset();
		}
	}

	bCaptureInProgress = false;
	LoadingSnapshotTexture = nullptr;

	FSimpleDelegate Cb = PendingCaptureCallback;
	PendingCaptureCallback.Unbind();
	Cb.ExecuteIfBound();
}

void UMDF_GameInstance::SetupSnapshotLoadingScreen()
{
#if !UE_SERVER
	if (IsRunningDedicatedServer())
	{
		return;
	}
	if (!LoadingSnapshotTexture)
	{
		UE_LOG(LogTemp, Warning, TEXT("[LoadingDbg][GI] SetupSnapshotLoadingScreen — Snapshot null, 스킵"));
		return;
	}

	// [§17++ Phase 2] AsyncLoadingScreen plugin에 우리 SLoadingSnapshotWidget을 ExternalLoadingWidget으로 등록.
	// plugin이 PreLoadMap에서 자동으로 OnPrepareLoadingScreen → SetupLoadingScreen 호출하면서
	// ExternalWidget을 WidgetLoadingScreen으로 사용 → 우주선 화면 풀스크린 표시.
	//
	// DefaultGame.ini 설정:
	//   bWaitForManualStop=true   → BeginPlay에서 StopLoadingScreen() 호출까지 화면 유지
	//   bAllowEngineTick=true     → GameThread block 회피 → PendingNetGame Tick 정상 → NMT_Join 송신 → deadlock 회피
	//   MinimumLoadingScreenDisplayTime=-1
	TSharedRef<SLoadingSnapshotWidget> SnapshotWidget =
		SNew(SLoadingSnapshotWidget).SnapshotTexture(LoadingSnapshotTexture);

	FAsyncLoadingScreenModule::SetExternalLoadingWidget(SnapshotWidget);
	UAsyncLoadingScreenLibrary::SetEnableLoadingScreen(true);

	UE_LOG(LogTemp, Warning, TEXT("[LoadingDbg][GI] SetupSnapshotLoadingScreen — AsyncLoadingScreen plugin에 우주선 widget 등록 완료"));
#endif
}

void UMDF_GameInstance::ClearLoadingHandoffState()
{
	SavedShipTimeAccum = 0.f;
	SavedShipAmpScale = 0.f;
	SavedFakeProgress = 0.f;
	bHasSavedShipPose = false;
}

void UMDF_GameInstance::EnsureGameViewportOverlay()
{
	// [§17 3-Layer] 우리 SLoadingSnapshotWidget을 GameViewport overlay에 보장.
	//   - 없으면 신규 추가
	//   - 있으면 force Re-Add (SOverlay layout/paint 재계산, plugin/streaming pause 종료 시 invalidate 회피)
	if (IsRunningDedicatedServer())
	{
		return;
	}
	if (!LoadingSnapshotTexture)
	{
		return;
	}

	UGameViewportClient* VC = GetGameViewportClient();
	if (!VC)
	{
		return;
	}

	if (!PostLoadOverlayWidget.IsValid())
	{
		PostLoadOverlayWidget = SNew(SLoadingSnapshotWidget).SnapshotTexture(LoadingSnapshotTexture);
		VC->AddViewportWidgetContent(PostLoadOverlayWidget.ToSharedRef(), 9999);
		UE_LOG(LogTemp, Warning, TEXT("[LoadingDbg][GI] EnsureGameViewportOverlay — overlay 신규 추가"));
	}
	else
	{
		VC->RemoveViewportWidgetContent(PostLoadOverlayWidget.ToSharedRef());
		VC->AddViewportWidgetContent(PostLoadOverlayWidget.ToSharedRef(), 9999);
		UE_LOG(LogTemp, Warning, TEXT("[LoadingDbg][GI] EnsureGameViewportOverlay — overlay 강제 Re-Add"));
	}
}

void UMDF_GameInstance::OnPreLoadMap(const FString& MapName)
{
	// [§17 3-Layer] LoadMap 시작 시점 — Lobby unload되기 전에 overlay 보장.
	UE_LOG(LogTemp, Warning, TEXT("[LoadingDbg][GI] OnPreLoadMap — Map=%s"), *MapName);
	EnsureGameViewportOverlay();
}

void UMDF_GameInstance::OnPostLoadMapWithWorld(UWorld* LoadedWorld)
{
	// [§17 3-Layer] World 생성 직후 — plugin 자동 종료되는 시점, overlay force redraw로 invalidate 회피.
	UE_LOG(LogTemp, Warning, TEXT("[LoadingDbg][GI] OnPostLoadMapWithWorld"));
	EnsureGameViewportOverlay();
}

void UMDF_GameInstance::OnEngineStreamingPauseBegin(FViewport* Viewport)
{
	// [§17 3-Layer] World Partition cell 로드 대기(BlockTillLevelStreamingCompleted) 시 발동.
	// engine throbber 대신 우리 SLoadingSnapshotWidget이 화면 가림.
	// 이 시점 이후 GameThread block이라 Slate frame 안 그려짐 → overlay는 직전 frame에 이미 그려져 있어야 함.
	UE_LOG(LogTemp, Warning, TEXT("[LoadingDbg][GI] StreamingPauseBegin — overlay 보장"));
	EnsureGameViewportOverlay();
}

void UMDF_GameInstance::OnEngineStreamingPauseEnd()
{
	// [§17 3-Layer] streaming pause 종료. overlay는 ClearPostLoadOverlay에서 정리.
	UE_LOG(LogTemp, Warning, TEXT("[LoadingDbg][GI] StreamingPauseEnd"));
}

// ════════════════════════════════════════════════════════════════════════════════
// [§17++] ClearPostLoadOverlay
// ════════════════════════════════════════════════════════════════════════════════
//   Client_EnterLoadingScene이 LoadingHUD를 추가한 직후 호출.
//   MainMap C구간 우주선 + LoadingHUD가 화면을 가리고 있으니 풀스크린 오버레이 제거 안전.
// ════════════════════════════════════════════════════════════════════════════════
void UMDF_GameInstance::ClearPostLoadOverlay()
{
#if !UE_SERVER
	// [§17++ Phase 2] plugin StopLoadingScreen 호출로 변경.
	// bWaitForManualStop=true + bAllowEngineTick=true 조합에서 MoviePlayer를 명시 종료해야 화면 사라짐.
	UAsyncLoadingScreenLibrary::StopLoadingScreen();
	FAsyncLoadingScreenModule::ClearExternalLoadingWidget();
	UE_LOG(LogTemp, Warning, TEXT("[LoadingDbg][GI] ClearPostLoadOverlay — plugin StopLoadingScreen + ExternalWidget 해제"));
#endif

	// 레거시 GameViewport overlay 잔재 정리 (안전망)
	if (PostLoadOverlayWidget.IsValid())
	{
		if (UGameViewportClient* VC = GetGameViewportClient())
		{
			VC->RemoveViewportWidgetContent(PostLoadOverlayWidget.ToSharedRef());
		}
		PostLoadOverlayWidget.Reset();
	}
}
