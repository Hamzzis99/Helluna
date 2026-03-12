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
	Args = FString::Printf(TEXT("\"%s\" %s -server -port=%d -log"),
		*ProjectPath, *MapPath, Port);
#else
	// 패키징 모드
	Args = FString::Printf(TEXT("%s -server -port=%d -log"),
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
	Args = FString::Printf(TEXT("\"%s\" %s -server -port=%d -log"),
		*ProjectPath, *MapPath, Port);
#else
	Args = FString::Printf(TEXT("%s -server -port=%d -log"),
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
				UE_LOG(LogHellunaLobby, Log, TEXT("[ServerManager] 기존 프로세스 종료 | Port=%d"), Port);
			}
			FPlatformProcess::CloseProc(ActiveServers[i].ProcessHandle);
			ActiveServers.RemoveAt(i);
			break;
		}
	}

	// 2. 레지스트리 파일 삭제 (새 서버가 깨끗하게 시작하도록)
	const FString RegistryFile = FPaths::Combine(RegistryDir, FString::Printf(TEXT("channel_%d.json"), Port));
	if (IFileManager::Get().FileExists(*RegistryFile))
	{
		IFileManager::Get().Delete(*RegistryFile);
		UE_LOG(LogHellunaLobby, Log, TEXT("[ServerManager] 레지스트리 파일 삭제 | %s"), *RegistryFile);
	}

	// 3. 같은 포트에 새 맵으로 스폰
	return SpawnGameServerOnPort(Port, NewMapPath);
}

// ============================================================================
// IsServerReady
// ============================================================================

bool UHellunaGameServerManager::IsServerReady(int32 Port) const
{
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

	// lastUpdate 60초 이내 확인
	const FString LastUpdateStr = JsonObj->GetStringField(TEXT("lastUpdate"));
	FDateTime LastUpdate;
	if (FDateTime::ParseIso8601(*LastUpdateStr, LastUpdate))
	{
		const FTimespan Age = FDateTime::UtcNow() - LastUpdate;
		if (Age.GetTotalSeconds() > 60.0)
		{
			return false;
		}
	}

	return true;
}

// ============================================================================
// [Phase 19] IsServerReadyForMap
// ============================================================================

bool UHellunaGameServerManager::IsServerReadyForMap(int32 Port, const FString& MapKey) const
{
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

	// lastUpdate 60초 이내 확인
	const FString LastUpdateStr = JsonObj->GetStringField(TEXT("lastUpdate"));
	FDateTime LastUpdate;
	if (FDateTime::ParseIso8601(*LastUpdateStr, LastUpdate))
	{
		const FTimespan Age = FDateTime::UtcNow() - LastUpdate;
		if (Age.GetTotalSeconds() > 60.0)
		{
			return false;
		}
	}

	return true;
}

// ============================================================================
// AllocatePort
// ============================================================================

int32 UHellunaGameServerManager::AllocatePort() const
{
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

		// 레지스트리 파일 확인 (Offline이 아닌 서버가 있으면 사용 중)
		const FString FilePath = FPaths::Combine(RegistryDir, FString::Printf(TEXT("channel_%d.json"), Port));
		if (IFileManager::Get().FileExists(*FilePath))
		{
			FString JsonString;
			if (FFileHelper::LoadFileToString(JsonString, *FilePath))
			{
				TSharedPtr<FJsonObject> JsonObj;
				TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(JsonString);
				if (FJsonSerializer::Deserialize(Reader, JsonObj) && JsonObj.IsValid())
				{
					const FString Status = JsonObj->GetStringField(TEXT("status"));
					const FString LastUpdateStr = JsonObj->GetStringField(TEXT("lastUpdate"));

					// 유효한 서버가 점유 중이면 스킵
					FDateTime LastUpdate;
					if (FDateTime::ParseIso8601(*LastUpdateStr, LastUpdate))
					{
						const FTimespan Age = FDateTime::UtcNow() - LastUpdate;
						if (Age.GetTotalSeconds() <= 60.0)
						{
							// 아직 살아있는 서버 → 사용 불가
							continue;
						}
					}
					// 60초 초과 = Offline → 사용 가능
				}
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
		if (FPlatformProcess::IsProcRunning(Info.ProcessHandle))
		{
			FPlatformProcess::TerminateProc(Info.ProcessHandle, true);
			UE_LOG(LogHellunaLobby, Log, TEXT("[ServerManager] 프로세스 종료 | Port=%d"), Info.Port);
		}
		FPlatformProcess::CloseProc(Info.ProcessHandle);
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
			UE_LOG(LogHellunaLobby, Log, TEXT("[ServerManager] 종료된 프로세스 정리 | Port=%d"), ActiveServers[i].Port);
			FPlatformProcess::CloseProc(ActiveServers[i].ProcessHandle);
			ActiveServers.RemoveAt(i);
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
