// ============================================================================
// HellunaGameServerManager.cpp
// ============================================================================

#include "Lobby/ServerManager/HellunaGameServerManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformProcess.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Dom/JsonObject.h"
#include "Lobby/HellunaLobbyLog.h"

namespace
{
FString NormalizeServerRegistryMapIdentifier(const FString& InValue)
{
	FString Normalized = InValue;
	Normalized.TrimStartAndEndInline();

	if (Normalized.IsEmpty())
	{
		return Normalized;
	}

	if (Normalized.Contains(TEXT("/")) || Normalized.Contains(TEXT("\\")))
	{
		Normalized = FPaths::GetBaseFilename(Normalized);
	}

	return Normalized;
}

bool DoesServerRegistryMapMatch(const FString& RegistryMapName, const FString& RequestedMapIdentifier)
{
	const FString NormalizedRegistry = NormalizeServerRegistryMapIdentifier(RegistryMapName);
	const FString NormalizedRequested = NormalizeServerRegistryMapIdentifier(RequestedMapIdentifier);

	return !NormalizedRegistry.IsEmpty()
		&& !NormalizedRequested.IsEmpty()
		&& NormalizedRegistry.Equals(NormalizedRequested, ESearchCase::IgnoreCase);
}
}

// ============================================================================
// Initialize
// ============================================================================

void UHellunaGameServerManager::Initialize(UWorld* InWorld, const FString& InRegistryDir, const FString& InLobbyReturnURL)
{
	WorldRef = InWorld;
	RegistryDir = InRegistryDir;
	LobbyReturnURL = InLobbyReturnURL;

	// 30초마다 종료된 프로세스 정리
	if (InWorld)
	{
		InWorld->GetTimerManager().SetTimer(
			CleanupTimer,
			[this]() { CleanupTerminatedProcesses(); },
			30.0f, true
		);
	}

	UE_LOG(LogHellunaLobby, Log, TEXT("[ServerManager] Initialize | RegistryDir=%s | LobbyReturnURL='%s'"), *RegistryDir, *LobbyReturnURL);
	if (LobbyReturnURL.IsEmpty())
	{
		UE_LOG(LogHellunaLobby, Warning,
			TEXT("[ServerManager] Initialize: LobbyReturnURL is empty. Spawned game servers will not receive an explicit return URL."));
	}
}

// ============================================================================
// SpawnGameServer
// ============================================================================

int32 UHellunaGameServerManager::SpawnGameServer(const FString& MapPath)
{
	const int32 Port = AllocatePort();
	if (Port < 0)
	{
		UE_LOG(LogHellunaLobby, Error, TEXT("[ServerManager] SpawnGameServer: 빈 포트 없음 (서버 용량 초과)"));
		return -1;
	}

	const FString ServerExe = GetServerExecutablePath();
	if (ServerExe.IsEmpty())
	{
		UE_LOG(LogHellunaLobby, Error, TEXT("[ServerManager] SpawnGameServer: 서버 실행 파일 경로를 찾을 수 없음"));
		return -1;
	}

	// LobbyURL 구성 (로비 서버 주소 전달)
	FString Args;
#if WITH_EDITOR
	// 에디터 모드: UE 에디터가 -server 모드로 재실행
	const FString ProjectPath = FPaths::ConvertRelativePathToFull(FPaths::GetProjectFilePath());
	Args = FString::Printf(TEXT("\"%s\" %s -server -port=%d -log -NoDatabaseOpen"),
		*ProjectPath, *MapPath, Port);
#else
	// 패키징 모드 — 게임서버는 -NoDatabaseOpen으로 SQLite 서브시스템이 DB를 열지 않도록 명시
	Args = FString::Printf(TEXT("%s -server -port=%d -log -NoDatabaseOpen"),
		*MapPath, Port);
#endif

	if (!LobbyReturnURL.IsEmpty())
	{
		Args += FString::Printf(TEXT(" -LobbyURL=%s"), *LobbyReturnURL);
	}
	else
	{
		UE_LOG(LogHellunaLobby, Warning,
			TEXT("[ServerManager] SpawnGameServer: launching without -LobbyURL | Port=%d | MapPath=%s"),
			Port, *MapPath);
	}

	UE_LOG(LogHellunaLobby, Log, TEXT("[ServerManager] SpawnGameServer | Exe=%s | Args=%s"), *ServerExe, *Args);

	FProcHandle Handle = FPlatformProcess::CreateProc(
		*ServerExe, *Args,
		true,   // bLaunchDetached
		false,  // bLaunchHidden
		false,  // bLaunchReallyHidden
		nullptr, // OutProcessID
		0,      // PriorityModifier
		nullptr, // OptionalWorkingDirectory
		nullptr  // PipeWriteChild
	);

	if (!Handle.IsValid())
	{
		UE_LOG(LogHellunaLobby, Error, TEXT("[ServerManager] SpawnGameServer: CreateProc 실패 | Port=%d"), Port);
		return -1;
	}

	FActiveServerInfo Info;
	Info.ProcessHandle = Handle;
	Info.Port = Port;
	Info.MapPath = MapPath;
	Info.SpawnTime = FPlatformTime::Seconds();
	ActiveServers.Add(MoveTemp(Info));

	UE_LOG(LogHellunaLobby, Log, TEXT("[ServerManager] SpawnGameServer 성공 | Port=%d | MapPath=%s | ActiveCount=%d"),
		Port, *MapPath, ActiveServers.Num());

	return Port;
}

// ============================================================================
// [Phase 19] SpawnGameServerOnPort — 지정 포트에 스폰
// ============================================================================

int32 UHellunaGameServerManager::SpawnGameServerOnPort(int32 Port, const FString& MapPath)
{
	const FString ServerExe = GetServerExecutablePath();
	if (ServerExe.IsEmpty())
	{
		UE_LOG(LogHellunaLobby, Error, TEXT("[ServerManager] SpawnGameServerOnPort: 서버 실행 파일 경로를 찾을 수 없음"));
		return -1;
	}

	FString Args;
#if WITH_EDITOR
	const FString ProjectPath = FPaths::ConvertRelativePathToFull(FPaths::GetProjectFilePath());
	Args = FString::Printf(TEXT("\"%s\" %s -server -port=%d -log -NoDatabaseOpen"),
		*ProjectPath, *MapPath, Port);
#else
	// 게임서버는 -NoDatabaseOpen으로 SQLite 서브시스템이 DB를 열지 않도록 명시
	Args = FString::Printf(TEXT("%s -server -port=%d -log -NoDatabaseOpen"),
		*MapPath, Port);
#endif

	if (!LobbyReturnURL.IsEmpty())
	{
		Args += FString::Printf(TEXT(" -LobbyURL=%s"), *LobbyReturnURL);
	}
	else
	{
		UE_LOG(LogHellunaLobby, Warning,
			TEXT("[ServerManager] SpawnGameServerOnPort: launching without -LobbyURL | Port=%d | MapPath=%s"),
			Port, *MapPath);
	}

	UE_LOG(LogHellunaLobby, Log, TEXT("[ServerManager] SpawnGameServerOnPort | Port=%d | Args=%s"), Port, *Args);

	FProcHandle Handle = FPlatformProcess::CreateProc(
		*ServerExe, *Args, true, false, false, nullptr, 0, nullptr, nullptr);

	if (!Handle.IsValid())
	{
		UE_LOG(LogHellunaLobby, Error, TEXT("[ServerManager] SpawnGameServerOnPort: CreateProc 실패 | Port=%d"), Port);
		return -1;
	}

	FActiveServerInfo Info;
	Info.ProcessHandle = Handle;
	Info.Port = Port;
	Info.MapPath = MapPath;
	Info.SpawnTime = FPlatformTime::Seconds();
	ActiveServers.Add(MoveTemp(Info));

	UE_LOG(LogHellunaLobby, Log, TEXT("[ServerManager] SpawnGameServerOnPort 성공 | Port=%d | MapPath=%s | ActiveCount=%d"),
		Port, *MapPath, ActiveServers.Num());

	return Port;
}

// ============================================================================
// [Phase 19] RespawnGameServer — 종료 후 같은 포트에 새 맵으로 재스폰
// ============================================================================

int32 UHellunaGameServerManager::RespawnGameServer(int32 Port, const FString& NewMapPath)
{
	UE_LOG(LogHellunaLobby, Log, TEXT("[ServerManager] RespawnGameServer 시작 | Port=%d | NewMapPath=%s"), Port, *NewMapPath);

	// 1. 기존 프로세스 종료 + ActiveServers에서 제거
	for (int32 i = ActiveServers.Num() - 1; i >= 0; --i)
	{
		if (ActiveServers[i].Port == Port)
		{
			if (FPlatformProcess::IsProcRunning(ActiveServers[i].ProcessHandle))
			{
				FPlatformProcess::TerminateProc(ActiveServers[i].ProcessHandle, true);
				// [#23-FIX] 같은 포트로 재기동하기 전에 프로세스가 완전히 종료돼 리슨 소켓이
				// OS에 반환될 때까지 대기한다 (강제 종료된 프로세스라 보통 즉시 반환).
				// 안 그러면 새 서버가 아직 점유 중인 포트에 bind 실패 → 고장난 채널로 출격.
				FPlatformProcess::WaitForProc(ActiveServers[i].ProcessHandle);
				UE_LOG(LogHellunaLobby, Log, TEXT("[ServerManager] 기존 프로세스 종료 | Port=%d"), Port);
			}
			FPlatformProcess::CloseProc(ActiveServers[i].ProcessHandle);
			ActiveServers.RemoveAt(i);
			break;
		}
	}

	// 2. 레지스트리 파일 삭제 (새 서버가 깨끗하게 시작하도록)
	RemoveRegistryFileForPort(Port);

	// 3. 같은 포트에 새 맵으로 스폰
	return SpawnGameServerOnPort(Port, NewMapPath);
}

void UHellunaGameServerManager::RemoveRegistryFileForPort(int32 Port)
{
	if (Port <= 0 || RegistryDir.IsEmpty())
	{
		return;
	}

	const FString RegistryFile = FPaths::Combine(RegistryDir, FString::Printf(TEXT("channel_%d.json"), Port));
	if (!IFileManager::Get().FileExists(*RegistryFile))
	{
		return;
	}

	if (IFileManager::Get().Delete(*RegistryFile))
	{
		UE_LOG(LogHellunaLobby, Log, TEXT("[ServerManager] Removed channel registry | Port=%d | Path=%s"), Port, *RegistryFile);
	}
	else
	{
		UE_LOG(LogHellunaLobby, Warning, TEXT("[ServerManager] Failed to remove channel registry | Port=%d | Path=%s"), Port, *RegistryFile);
	}
}

bool UHellunaGameServerManager::IsTrackedServerRunning(int32 Port)
{
	for (int32 i = ActiveServers.Num() - 1; i >= 0; --i)
	{
		FActiveServerInfo& Info = ActiveServers[i];
		if (Info.Port != Port)
		{
			continue;
		}

		if (FPlatformProcess::IsProcRunning(Info.ProcessHandle))
		{
			return true;
		}

		UE_LOG(LogHellunaLobby, Warning, TEXT("[ServerManager] Tracked server process is dead | Port=%d"), Port);
		FPlatformProcess::CloseProc(Info.ProcessHandle);
		ActiveServers.RemoveAt(i);
		RemoveRegistryFileForPort(Port);
		return false;
	}

	return false;
}

// ============================================================================
// IsServerReady
// ============================================================================

bool UHellunaGameServerManager::IsServerReady(int32 Port)
{
	if (!IsTrackedServerRunning(Port))
	{
		UE_LOG(LogHellunaLobby, Warning, TEXT("[ServerManager] IsServerReady rejected untracked/dead server | Port=%d"), Port);
		return false;
	}

	const FString FilePath = FPaths::Combine(RegistryDir, FString::Printf(TEXT("channel_%d.json"), Port));

	FString JsonString;
	if (!FFileHelper::LoadFileToString(JsonString, *FilePath))
	{
		return false;
	}

	TSharedPtr<FJsonObject> JsonObj;
	TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(JsonString);
	if (!FJsonSerializer::Deserialize(Reader, JsonObj) || !JsonObj.IsValid())
	{
		return false;
	}

	const FString Status = JsonObj->GetStringField(TEXT("status"));
	if (Status != TEXT("empty"))
	{
		return false;
	}

	// [#19-FIX] lastUpdate가 유효(ISO8601 파싱 성공)하고 60초 이내여야 ready로 인정한다.
	// 누락/파싱 실패 = 신선도 미상 → not ready (stale 레지스트리를 ready로 오인 방지).
	const FString LastUpdateStr = JsonObj->GetStringField(TEXT("lastUpdate"));
	FDateTime LastUpdate;
	if (!FDateTime::ParseIso8601(*LastUpdateStr, LastUpdate))
	{
		UE_LOG(LogHellunaLobby, Warning, TEXT("[ServerManager][#19-FIX] lastUpdate 파싱 실패 → not ready | Port=%d"), Port);
		return false;
	}
	if ((FDateTime::UtcNow() - LastUpdate).GetTotalSeconds() > 60.0)
	{
		return false;
	}

	return true;
}

// ============================================================================
// [Phase 19] IsServerReadyForMap
// ============================================================================

bool UHellunaGameServerManager::IsServerReadyForMap(int32 Port, const FString& MapKey)
{
	if (!IsTrackedServerRunning(Port))
	{
		UE_LOG(LogHellunaLobby, Warning, TEXT("[ServerManager] IsServerReadyForMap rejected untracked/dead server | Port=%d | MapKey=%s"), Port, *MapKey);
		return false;
	}

	const FString FilePath = FPaths::Combine(RegistryDir, FString::Printf(TEXT("channel_%d.json"), Port));

	FString JsonString;
	if (!FFileHelper::LoadFileToString(JsonString, *FilePath))
	{
		return false;
	}

	TSharedPtr<FJsonObject> JsonObj;
	TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(JsonString);
	if (!FJsonSerializer::Deserialize(Reader, JsonObj) || !JsonObj.IsValid())
	{
		return false;
	}

	const FString Status = JsonObj->GetStringField(TEXT("status"));
	if (Status != TEXT("empty"))
	{
		return false;
	}

	// 맵 일치 확인
	const FString MapName = JsonObj->GetStringField(TEXT("mapName"));
	if (!DoesServerRegistryMapMatch(MapName, MapKey))
	{
		return false;
	}

	// [#19-FIX] lastUpdate가 유효하고 60초 이내여야 ready로 인정 (누락/파싱 실패 → not ready).
	const FString LastUpdateStr = JsonObj->GetStringField(TEXT("lastUpdate"));
	FDateTime LastUpdate;
	if (!FDateTime::ParseIso8601(*LastUpdateStr, LastUpdate))
	{
		UE_LOG(LogHellunaLobby, Warning, TEXT("[ServerManager][#19-FIX] lastUpdate 파싱 실패 → not ready (map) | Port=%d"), Port);
		return false;
	}
	if ((FDateTime::UtcNow() - LastUpdate).GetTotalSeconds() > 60.0)
	{
		return false;
	}

	return true;
}

// ============================================================================
// AllocatePort
// ============================================================================

int32 UHellunaGameServerManager::AllocatePort()
{
	CleanupTerminatedProcesses();

	for (int32 Port = MinPort; Port <= MaxPort; ++Port)
	{
		// ActiveServers에 있는지 확인
		bool bInUse = false;
		for (const FActiveServerInfo& Info : ActiveServers)
		{
			if (Info.Port == Port)
			{
				bInUse = true;
				break;
			}
		}
		if (bInUse)
		{
			continue;
		}

		// [H5/#22-FIX] 레지스트리 파일이 존재하면 "확실히 비어있다고 증명될 때만" 포트를 할당한다.
		// 기존 코드는 lastUpdate 파싱 실패나 JSON deserialize 실패(부분 기록 레이스) 시 그대로
		// return Port 로 떨어져 점유 중인 포트를 빈 포트로 오인 → 이중배정/충돌이 발생했다.
		const FString FilePath = FPaths::Combine(RegistryDir, FString::Printf(TEXT("channel_%d.json"), Port));
		if (IFileManager::Get().FileExists(*FilePath))
		{
			bool bPortFree = false; // 기본: 파일이 있으면 점유로 간주 (free 증명 시에만 true)

			FString JsonString;
			if (FFileHelper::LoadFileToString(JsonString, *FilePath))
			{
				TSharedPtr<FJsonObject> JsonObj;
				TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(JsonString);
				if (FJsonSerializer::Deserialize(Reader, JsonObj) && JsonObj.IsValid())
				{
					const FString Status = JsonObj->GetStringField(TEXT("status"));
					const FString LastUpdateStr = JsonObj->GetStringField(TEXT("lastUpdate"));

					FDateTime LastUpdate;
					const bool bParsed = FDateTime::ParseIso8601(*LastUpdateStr, LastUpdate);
					const double AgeSec = bParsed ? (FDateTime::UtcNow() - LastUpdate).GetTotalSeconds() : -1.0;

					if (Status == TEXT("offline"))
					{
						// 명시적으로 오프라인 → 회수 가능
						bPortFree = true;
					}
					else if (bParsed && AgeSec > 60.0 && !IsTrackedServerRunning(Port))
					{
						// 60초 초과(heartbeat 끊김) + 추적 프로세스 없음 → stale 회수 가능
						bPortFree = true;
					}
					else
					{
						// 신선/파싱실패/추적중 등 → 점유로 간주하고 스킵
						UE_LOG(LogHellunaLobby, Warning,
							TEXT("[ServerManager][H5-FIX] 비어있음 미증명 레지스트리 → 포트 스킵 | Port=%d | Status=%s | Parsed=%d | Age=%.1f"),
							Port, *Status, bParsed ? 1 : 0, AgeSec);
					}
				}
				// JSON deserialize 실패(부분 기록 가능) → bPortFree=false (스킵)
			}
			// 파일 읽기 실패 → bPortFree=false (스킵)

			if (!bPortFree)
			{
				continue;
			}
		}

		return Port;
	}

	return -1;
}

// ============================================================================
// ShutdownAll
// ============================================================================

void UHellunaGameServerManager::ShutdownAll()
{
	UE_LOG(LogHellunaLobby, Log, TEXT("[ServerManager] ShutdownAll | ActiveCount=%d"), ActiveServers.Num());

	// 타이머 해제
	if (WorldRef.IsValid())
	{
		WorldRef->GetTimerManager().ClearTimer(CleanupTimer);
	}

	for (FActiveServerInfo& Info : ActiveServers)
	{
		const int32 Port = Info.Port;
		if (FPlatformProcess::IsProcRunning(Info.ProcessHandle))
		{
			FPlatformProcess::TerminateProc(Info.ProcessHandle, true);
			UE_LOG(LogHellunaLobby, Log, TEXT("[ServerManager] 프로세스 종료 | Port=%d"), Port);
		}
		FPlatformProcess::CloseProc(Info.ProcessHandle);
		RemoveRegistryFileForPort(Port);
	}
	ActiveServers.Empty();
}

// ============================================================================
// CleanupTerminatedProcesses
// ============================================================================

void UHellunaGameServerManager::CleanupTerminatedProcesses()
{
	for (int32 i = ActiveServers.Num() - 1; i >= 0; --i)
	{
		if (!FPlatformProcess::IsProcRunning(ActiveServers[i].ProcessHandle))
		{
			const int32 DeadPort = ActiveServers[i].Port;
			UE_LOG(LogHellunaLobby, Log, TEXT("[ServerManager] 종료된 프로세스 정리 | Port=%d"), DeadPort);
			FPlatformProcess::CloseProc(ActiveServers[i].ProcessHandle);
			ActiveServers.RemoveAt(i);
			RemoveRegistryFileForPort(DeadPort);
		}
	}
}

// ============================================================================
// GetServerExecutablePath
// ============================================================================

FString UHellunaGameServerManager::GetServerExecutablePath() const
{
#if WITH_EDITOR
	// 에디터: 자신의 실행 파일 사용 (UE 에디터가 -server 모드로 재실행)
	return FString(FPlatformProcess::ExecutablePath());
#else
	// 패키징: 같은 디렉토리의 HellunaServer.exe
	return FPaths::Combine(
		FPaths::GetPath(FString(FPlatformProcess::ExecutablePath())),
		TEXT("HellunaServer.exe"));
#endif
}
