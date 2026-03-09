// ============================================================================
// HellunaGameServerManager.h
// ============================================================================
//
// [Phase 16] 동적 게임서버 프로세스 관리
//
// 사용처:
//   - HellunaLobbyGameMode (소유, BeginPlay에서 Initialize)
//
// 역할:
//   - 매칭 완료 시 게임서버 프로세스를 FPlatformProcess::CreateProc으로 스폰
//   - 서버 레지스트리 폴링으로 준비 상태 확인
//   - 포트 자동 할당 (7778~7798 범위)
//   - 종료된 프로세스 주기적 정리
//
// ============================================================================

#pragma once

#include "CoreMinimal.h"
#include "UObject/NoExportTypes.h"
#include "HellunaGameServerManager.generated.h"

UCLASS()
class HELLUNA_API UHellunaGameServerManager : public UObject
{
	GENERATED_BODY()

public:
	/** 초기화 (LobbyGameMode::BeginPlay에서 호출) */
	void Initialize(UWorld* InWorld, const FString& InRegistryDir, const FString& InLobbyReturnURL = FString());

	/** 서버 프로세스 스폰 -- 포트 자동 할당, 맵 경로 지정. 실패 시 -1 반환 */
	int32 SpawnGameServer(const FString& MapPath);

	/** 해당 포트의 서버가 ready(registry에 empty 상태 + 60초 이내) 인지 확인 */
	bool IsServerReady(int32 Port) const;

	/** [Phase 19] 빈 서버 종료 후 같은 포트에 새 맵으로 재스폰. 실패 시 -1 반환 */
	int32 RespawnGameServer(int32 Port, const FString& NewMapPath);

	/** [Phase 19] 해당 포트의 서버가 ready + 지정 맵인지 확인 */
	bool IsServerReadyForMap(int32 Port, const FString& MapKey) const;

	/** 포트 할당: 레지스트리 + ActiveServers 스캔 -> [7778, 7798] 범위에서 미사용 포트 */
	int32 AllocatePort() const;

	/** 프로세스 정리 (EndPlay 시 전체 종료) */
	void ShutdownAll();

	/** 종료된 프로세스 정리 (주기적 호출) */
	void CleanupTerminatedProcesses();

private:
	/** 활성 서버 프로세스 정보 */
	struct FActiveServerInfo
	{
		FProcHandle ProcessHandle;
		int32 Port = 0;
		FString MapPath;
		double SpawnTime = 0.0;
	};

	/** 활성 서버 프로세스 목록 */
	TArray<FActiveServerInfo> ActiveServers;

	/** World 약참조 (타이머용) */
	TWeakObjectPtr<UWorld> WorldRef;

	/** 레지스트리 경로 캐시 */
	FString RegistryDir;
	FString LobbyReturnURL;

	/** 서버 실행 파일 경로 (에디터 vs 패키징 자동 감지) */
	FString GetServerExecutablePath() const;

	/** 지정 포트에 서버 프로세스 스폰 (내부 전용) */
	int32 SpawnGameServerOnPort(int32 Port, const FString& MapPath);

	/** 정리 타이머 */
	FTimerHandle CleanupTimer;

	/** 포트 범위 상수 */
	static constexpr int32 MinPort = 7778;
	static constexpr int32 MaxPort = 7798;
};
