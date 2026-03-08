#include "MeshyBridge.h"
#include "HAL/PlatformProcess.h"
#include "HAL/PlatformFileManager.h"
#include "HAL/FileManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Misc/EngineVersionComparison.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "Editor.h"
#include "Engine/StaticMesh.h"
#include "Json.h"
#include "JsonObjectConverter.h"
#include "Sockets.h"
#include "SocketSubsystem.h"
#include "IPlatformFilePak.h"
#include "Interfaces/IPv4/IPv4Address.h"
#include "Interfaces/IPv4/IPv4Endpoint.h"
#include "AssetToolsModule.h"
#include "Factories/FbxFactory.h"
#include "UObject/Package.h"
#include "UObject/SavePackage.h"
#include "Factories/Factory.h"
#include "Editor/UnrealEd/Public/Editor.h"
#include "Editor/UnrealEd/Public/EditorLevelUtils.h"
#include "HttpModule.h"
#include "Interfaces/IHttpRequest.h"
#include "Interfaces/IHttpResponse.h"
#include "EditorFramework/AssetImportData.h"
#include "AssetToolsModule.h"
#include "Factories/ImportSettings.h"
#include "AutomatedAssetImportData.h"
#include "Misc/CoreDelegates.h"
#include "TimerManager.h"
#include "Engine/StaticMeshActor.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "ContentBrowserModule.h"
#include "Misc/Compression.h"
#include "Misc/ScopedSlowTask.h"
#include "GenericPlatform/GenericPlatformFile.h"
#include "Async/TaskGraphInterfaces.h"
#include "Containers/Ticker.h"
#include "Materials/Material.h"
#include "Engine/Texture2D.h"
#include "Materials/MaterialExpressionTextureSample.h"
#include "Materials/MaterialInstance.h"
#include "Materials/MaterialInstanceConstant.h"

// UE 5.6/5.7 版本兼容性宏
#ifndef UE_5_07_OR_LATER
    #if ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION >= 7
        #define UE_5_07_OR_LATER 1
    #else
        #define UE_5_07_OR_LATER 0
    #endif
#endif

// Add minizip header files include - cross-platform support
#if PLATFORM_WINDOWS || PLATFORM_MAC
    #include "zlib.h"
    #include "unzip.h"
    #define MESHY_MINIZIP_SUPPORTED 1
#else
    #define MESHY_MINIZIP_SUPPORTED 0
    #pragma message("Warning: Meshy plugin minizip support is only available on Windows and Mac")
#endif

namespace
{
    bool GIsModelImporting = false;
    bool GHasDownloadedModel = false;
    FString GDownloadedFilePath;
    FCriticalSection GImportLock;
}

const TArray<FString> FMeshyBridge::AllowedOrigins = {
    TEXT("https://www.meshy.ai"),
    TEXT("https://app-staging.meshy.ai"),
    TEXT("http://localhost:3700")
};

FMeshyBridge::FMeshyBridge()
    : bIsRunning(false)
    , bShouldStop(false)
    , ServerThread(nullptr)
    , ListenerSocket(nullptr)
{
}

FMeshyBridge::~FMeshyBridge()
{
    StopServer();
}

bool FMeshyBridge::Init()
{
    return true;
}

uint32 FMeshyBridge::Run()
{
    while (!bShouldStop)
    {
        bool bHasPendingConnection = false;
        if (ListenerSocket && ListenerSocket->HasPendingConnection(bHasPendingConnection) && bHasPendingConnection)
        {
            FSocket* ClientSocket = ListenerSocket->Accept(TEXT("MeshyBridge Client"));
            if (ClientSocket)
            {
                ProcessClientRequest(ClientSocket);
                ClientSocket->Close();
                delete ClientSocket;
            }
        }
        FPlatformProcess::Sleep(0.1f);
    }
    return 0;
}

void FMeshyBridge::Stop()
{
    bShouldStop = true;
}

void FMeshyBridge::Exit()
{
    if (ListenerSocket)
    {
        ListenerSocket->Close();
        delete ListenerSocket;
        ListenerSocket = nullptr;
    }
}

void FMeshyBridge::StartServer()
{
    if (bIsRunning)
    {
        return;
    }

    ISocketSubsystem* SocketSubsystem = ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM);
    if (!SocketSubsystem)
    {
        UE_LOG(LogTemp, Error, TEXT("[Meshy Bridge] Failed to get socket subsystem"));
        return;
    }

    ListenerSocket = SocketSubsystem->CreateSocket(NAME_Stream, TEXT("MeshyBridge Server"), true);
    if (!ListenerSocket)
    {
        UE_LOG(LogTemp, Error, TEXT("[Meshy Bridge] Failed to create socket"));
        return;
    }

    FIPv4Address Address;
    FIPv4Address::Parse(TEXT("0.0.0.0"), Address);
    FIPv4Endpoint Endpoint(Address, ServerPort);

    if (!ListenerSocket->Bind(*Endpoint.ToInternetAddr()))
    {
        UE_LOG(LogTemp, Error, TEXT("[Meshy Bridge] Failed to bind socket"));
        delete ListenerSocket;
        ListenerSocket = nullptr;
        return;
    }

    bool bHasPendingConnection = false;
    if (!ListenerSocket->Listen(8))
    {
        UE_LOG(LogTemp, Error, TEXT("[Meshy Bridge] Failed to listen on socket"));
        delete ListenerSocket;
        ListenerSocket = nullptr;
        return;
    }

    bIsRunning = true;
    bShouldStop = false;
    ServerThread = FRunnableThread::Create(this, TEXT("MeshyBridge Server Thread"));
    
    UE_LOG(LogTemp, Log, TEXT("[Meshy Bridge] Listening on port %d"), ServerPort);
}

void FMeshyBridge::StopServer()
{
    if (!bIsRunning)
    {
        return;
    }

    bShouldStop = true;
    if (ServerThread)
    {
        ServerThread->WaitForCompletion();
        delete ServerThread;
        ServerThread = nullptr;
    }

    if (ListenerSocket)
    {
        ListenerSocket->Close();
        delete ListenerSocket;
        ListenerSocket = nullptr;
    }

    bIsRunning = false;
    
    UE_LOG(LogTemp, Log, TEXT("[Meshy Bridge] Server stopped"));
}

void FMeshyBridge::ProcessClientRequest(FSocket* ClientSocket)
{
    // 设置接收缓冲区大小
    int32 ActualBufferSize = 0;
    ClientSocket->SetReceiveBufferSize(65536, ActualBufferSize);
    
    // 累积接收的数据
    TArray<uint8> ReceivedData;
    const int32 BufferSize = 8192;
    TArray<uint8> TempBuffer;
    TempBuffer.SetNumUninitialized(BufferSize);
    
    // 读取 HTTP 头部 - 需要等待直到收到完整头部
    FString HeaderString;
    int32 HeaderEndIndex = INDEX_NONE;
    const double TimeoutSeconds = 5.0;
    double StartTime = FPlatformTime::Seconds();
    
    while (HeaderEndIndex == INDEX_NONE)
    {
        // 检查超时
        if (FPlatformTime::Seconds() - StartTime > TimeoutSeconds)
        {
            UE_LOG(LogTemp, Warning, TEXT("[Meshy Bridge] Timeout waiting for HTTP header"));
            return;
        }
        
        uint32 PendingDataSize = 0;
        if (ClientSocket->HasPendingData(PendingDataSize) && PendingDataSize > 0)
        {
            int32 BytesToRead = FMath::Min((int32)PendingDataSize, BufferSize);
            int32 BytesRead = 0;
            if (ClientSocket->Recv(TempBuffer.GetData(), BytesToRead, BytesRead))
            {
                if (BytesRead > 0)
                {
                    int32 OldSize = ReceivedData.Num();
                    ReceivedData.SetNumUninitialized(OldSize + BytesRead);
                    FMemory::Memcpy(ReceivedData.GetData() + OldSize, TempBuffer.GetData(), BytesRead);
                    
                    // 检查是否收到了完整的 HTTP 头部（以 \r\n\r\n 结束）
                    HeaderString = FString(UTF8_TO_TCHAR(ReceivedData.GetData()));
                    HeaderEndIndex = HeaderString.Find(TEXT("\r\n\r\n"));
                }
            }
        }
        else
        {
            FPlatformProcess::Sleep(0.01f);
        }
    }
    
    // 解析 Content-Length
    int32 ContentLength = 0;
    TArray<FString> HeaderLines;
    FString HeaderPart = HeaderString.Left(HeaderEndIndex);
    HeaderPart.ParseIntoArray(HeaderLines, TEXT("\r\n"), true);
    
    for (const FString& Line : HeaderLines)
    {
        if (Line.StartsWith(TEXT("Content-Length:"), ESearchCase::IgnoreCase))
        {
            FString LengthStr = Line.RightChop(15).TrimStartAndEnd();
            ContentLength = FCString::Atoi(*LengthStr);
            break;
        }
    }
    
    // 计算已接收的请求体大小
    int32 HeaderTotalLength = HeaderEndIndex + 4; // 包括 \r\n\r\n
    int32 BodyReceived = ReceivedData.Num() - HeaderTotalLength;
    
    // 如果有 Content-Length，继续读取直到收到完整请求体
    if (ContentLength > 0 && BodyReceived < ContentLength)
    {
        StartTime = FPlatformTime::Seconds();
        
        while (BodyReceived < ContentLength)
        {
            // 检查超时
            if (FPlatformTime::Seconds() - StartTime > TimeoutSeconds)
            {
                UE_LOG(LogTemp, Warning, TEXT("[Meshy Bridge] Timeout waiting for request body (received %d/%d bytes)"), 
                    BodyReceived, ContentLength);
                break;
            }
            
            uint32 PendingDataSize = 0;
            if (ClientSocket->HasPendingData(PendingDataSize) && PendingDataSize > 0)
            {
                int32 BytesToRead = FMath::Min((int32)PendingDataSize, BufferSize);
                int32 BytesRead = 0;
                if (ClientSocket->Recv(TempBuffer.GetData(), BytesToRead, BytesRead))
                {
                    if (BytesRead > 0)
                    {
                        int32 OldSize = ReceivedData.Num();
                        ReceivedData.SetNumUninitialized(OldSize + BytesRead);
                        FMemory::Memcpy(ReceivedData.GetData() + OldSize, TempBuffer.GetData(), BytesRead);
                        BodyReceived += BytesRead;
                    }
                }
            }
            else
            {
                FPlatformProcess::Sleep(0.01f);
            }
        }
    }
    
    // 确保字符串以 null 结尾
    ReceivedData.Add(0);
    FString RequestString = FString(UTF8_TO_TCHAR(ReceivedData.GetData()));
    
    UE_LOG(LogTemp, Log, TEXT("[Meshy Bridge] Received complete request: %d bytes, Content-Length: %d"), 
        ReceivedData.Num() - 1, ContentLength);
    
    TArray<FString> RequestLines;
    RequestString.ParseIntoArray(RequestLines, TEXT("\r\n"), true);

    if (RequestLines.Num() == 0)
    {
        SendResponse(ClientSocket, TEXT("HTTP/1.1 400 Bad Request\r\n\r\n"));
        return;
    }

    TArray<FString> RequestParts;
    RequestLines[0].ParseIntoArray(RequestParts, TEXT(" "), true);
    if (RequestParts.Num() < 2)
    {
        SendResponse(ClientSocket, TEXT("HTTP/1.1 400 Bad Request\r\n\r\n"));
        return;
    }

    FString Method = RequestParts[0];
    FString Path = RequestParts[1];

    // Handle OPTIONS request
    if (Method == TEXT("OPTIONS"))
    {
        // Find Origin header
        FString Origin;
        for (int i = 0; i < RequestLines.Num(); i++)
        {
            if (RequestLines[i].StartsWith(TEXT("Origin: ")))
            {
                Origin = RequestLines[i].RightChop(8).TrimEnd();
                break;
            }
        }
        
        // If Origin not found or not in allowed list, use default
        bool bOriginAllowed = false;
        for (const FString& AllowedOrigin : AllowedOrigins)
        {
            if (Origin == AllowedOrigin)
            {
                bOriginAllowed = true;
                break;
            }
        }

        if (!bOriginAllowed || Origin.IsEmpty())
        {
            Origin = AllowedOrigins[0]; // Use default value
        }
        
        FString Response = FString::Printf(TEXT("HTTP/1.1 200 OK\r\n"
            "Access-Control-Allow-Origin: %s\r\n"
            "Access-Control-Allow-Methods: POST, GET, OPTIONS\r\n"
            "Access-Control-Allow-Headers: *\r\n"
            "Access-Control-Max-Age: 86400\r\n\r\n"),
            *Origin);
        SendResponse(ClientSocket, Response);
        return;
    }

    // Handle GET status request
    if (Method == TEXT("GET") && (Path == TEXT("/status") || Path == TEXT("/ping")))
    {
        // Find Origin header
        FString Origin;
        for (int i = 0; i < RequestLines.Num(); i++)
        {
            if (RequestLines[i].StartsWith(TEXT("Origin: ")))
            {
                Origin = RequestLines[i].RightChop(8).TrimEnd();
                break;
            }
        }
        
        // If Origin not found or not in allowed list, use default
        bool bOriginAllowed = false;
        for (const FString& AllowedOrigin : AllowedOrigins)
        {
            if (Origin == AllowedOrigin)
            {
                bOriginAllowed = true;
                break;
            }
        }

        if (!bOriginAllowed || Origin.IsEmpty())
        {
            Origin = AllowedOrigins[0]; // Use default value
        }

        TSharedPtr<FJsonObject> StatusObject = MakeShared<FJsonObject>();
        StatusObject->SetStringField(TEXT("status"), TEXT("ok"));
        StatusObject->SetStringField(TEXT("dcc"), TEXT("unreal"));
        StatusObject->SetStringField(TEXT("version"), FEngineVersion::Current().ToString());

        FString JsonString;
        TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&JsonString);
        FJsonSerializer::Serialize(StatusObject.ToSharedRef(), Writer);

        FString Response = FString::Printf(TEXT("HTTP/1.1 200 OK\r\n"
            "Access-Control-Allow-Origin: %s\r\n"
            "Content-Type: application/json\r\n"
            "Content-Length: %d\r\n\r\n%s"),
            *Origin,
            JsonString.Len(),
            *JsonString);
        SendResponse(ClientSocket, Response);
        return;
    }

    // Handle POST import request
    if (Method == TEXT("POST") && Path == TEXT("/import"))
    {
        // Find Origin header
        FString Origin;
        for (int i = 0; i < RequestLines.Num(); i++)
        {
            if (RequestLines[i].StartsWith(TEXT("Origin: ")))
            {
                Origin = RequestLines[i].RightChop(8).TrimEnd();
                break;
            }
        }
        
        // If Origin not found or not in allowed list, use default
        bool bOriginAllowed = false;
        for (const FString& AllowedOrigin : AllowedOrigins)
        {
            if (Origin == AllowedOrigin)
            {
                bOriginAllowed = true;
                break;
            }
        }

        if (!bOriginAllowed || Origin.IsEmpty())
        {
            Origin = AllowedOrigins[0]; // Use default value
        }
        
        ProcessImportRequest(RequestString);
        FString Response = FString::Printf(TEXT("HTTP/1.1 200 OK\r\n"
            "Access-Control-Allow-Origin: %s\r\n"
            "Content-Type: application/json\r\n\r\n"
            "{\"status\":\"ok\",\"message\":\"File queued for import\"}"),
            *Origin);
        SendResponse(ClientSocket, Response);
        return;
    }

    SendResponse(ClientSocket, TEXT("HTTP/1.1 404 Not Found\r\n\r\n"));
}

void FMeshyBridge::SendResponse(FSocket* ClientSocket, const FString& Response)
{
    FTCHARToUTF8 Converter(*Response);
    int32 BytesSent = 0;
    ClientSocket->Send((uint8*)Converter.Get(), Converter.Length(), BytesSent);
}

void FMeshyBridge::ProcessImportRequest(const FString& RequestData)
{
    FScopeLock Lock(&GImportLock);
    
    // If already processing, return directly
    if (GIsModelImporting)
    {
        UE_LOG(LogTemp, Warning, TEXT("[Meshy Bridge] Already processing import, skipping request"));
        return;
    }
    
    // Try to parse JSON
    int32 JsonStart = RequestData.Find(TEXT("{"));
    if (JsonStart == INDEX_NONE)
    {
        UE_LOG(LogTemp, Error, TEXT("[Meshy Bridge] No JSON found in request"));
        return;
    }

    // Use manual search to find the last }
    int32 JsonEnd = INDEX_NONE;
    for (int32 i = RequestData.Len() - 1; i > JsonStart; i--)
    {
        if (RequestData[i] == '}')
        {
            JsonEnd = i;
            break;
        }
    }
    
    if (JsonEnd == INDEX_NONE || JsonEnd <= JsonStart)
    {
        UE_LOG(LogTemp, Error, TEXT("[Meshy Bridge] Unable to determine JSON end position"));
        return;
    }

    // Extract complete JSON string
    FString JsonString = RequestData.Mid(JsonStart, JsonEnd - JsonStart + 1);
    
    // Save original JSON to log file for debugging
    FString LogDirPath = FPaths::ProjectLogDir() / TEXT("MeshyBridge");
    IPlatformFile& PlatformFile = FPlatformFileManager::Get().GetPlatformFile();
    PlatformFile.CreateDirectoryTree(*LogDirPath);
    
    // Fix: Variable name conflict, change from LogPath to JsonLogPath
    FString JsonLogPath = LogDirPath / FString::Printf(TEXT("JsonRequest_%s.json"), *FDateTime::Now().ToString(TEXT("%Y%m%d_%H%M%S")));
    FFileHelper::SaveStringToFile(JsonString, *JsonLogPath);
    UE_LOG(LogTemp, Log, TEXT("[Meshy Bridge] Original JSON saved to: %s"), *JsonLogPath);
    
    // 预处理 JSON 字符串 - 移除所有控制字符（换行、回车等）
    // 因为 JSON 键值对不应该被控制字符分割
    FString CleanJsonString;
    CleanJsonString.Reserve(JsonString.Len());
    bool bInString = false;
    bool bEscaped = false;
    
    for (int32 i = 0; i < JsonString.Len(); i++)
    {
        TCHAR Char = JsonString[i];
        
        // 跟踪是否在字符串内部
        if (!bEscaped && Char == TEXT('"'))
        {
            bInString = !bInString;
        }
        bEscaped = (!bEscaped && Char == TEXT('\\'));
        
        // 移除控制字符（除了在字符串内部的转义序列）
        if (Char < 32 || Char == 127)
        {
            // 跳过所有控制字符（包括换行、回车）
            continue;
        }
        
        CleanJsonString.AppendChar(Char);
    }
    
    UE_LOG(LogTemp, Log, TEXT("[Meshy Bridge] Cleaned JSON: %s"), *CleanJsonString);
    
    TSharedPtr<FJsonObject> JsonObject;
    TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(CleanJsonString);
    if (!FJsonSerializer::Deserialize(Reader, JsonObject) || !JsonObject.IsValid())
    {
        UE_LOG(LogTemp, Error, TEXT("[Meshy Bridge] Failed to parse JSON: %s"), *CleanJsonString);
        
        // 保存失败的 JSON 用于调试
        FString FailedJsonPath = LogDirPath / FString::Printf(TEXT("FailedJson_%s.json"), *FDateTime::Now().ToString(TEXT("%Y%m%d_%H%M%S")));
        FFileHelper::SaveStringToFile(CleanJsonString, *FailedJsonPath);
        UE_LOG(LogTemp, Log, TEXT("[Meshy Bridge] Failed JSON saved to: %s"), *FailedJsonPath);
        return;
    }
    
    UE_LOG(LogTemp, Log, TEXT("[Meshy Bridge] JSON parsed successfully"));

    // Extract necessary fields
    if (!JsonObject->HasField(TEXT("url")))
    {
        UE_LOG(LogTemp, Error, TEXT("[Meshy Bridge] Missing required URL field"));
        return;
    }

    // Get URL
    FString Url = JsonObject->GetStringField(TEXT("url"));
    FString Name = JsonObject->HasField(TEXT("name")) ? 
        JsonObject->GetStringField(TEXT("name")) : TEXT("Meshy_Model");
    int32 FrameRate = JsonObject->HasField(TEXT("frameRate")) ? 
        JsonObject->GetIntegerField(TEXT("frameRate")) : 30;
    
    // Create temporary directory
    FString TempPath = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("Temp"), TEXT("Meshy"));
    PlatformFile.CreateDirectoryTree(*TempPath);

    // Create unique filename - don't specify extension first, detect after download
    static int32 DownloadCounter = 0;
    DownloadCounter++;
    
    FString FileName = FString::Printf(TEXT("meshy_download_%03d.tmp"), DownloadCounter);
    FString FilePath = FPaths::Combine(TempPath, FileName);

    UE_LOG(LogTemp, Log, TEXT("[Meshy Bridge] Starting file download: %s to %s"), *Url, *FilePath);
    
    // Start download
    GIsModelImporting = true;
    GHasDownloadedModel = false;
    
    // Initialize HTTP request
    TSharedRef<IHttpRequest, ESPMode::ThreadSafe> HttpRequest = FHttpModule::Get().CreateRequest();
    HttpRequest->SetURL(Url);
    HttpRequest->SetVerb(TEXT("GET"));
    
    // Convert string constant to FString to solve delegate binding problem
    FString UnknownFormat = TEXT("unknown");
    
    // Simply show progress bar
    ShowDownloadProgress(Name);
    
    // Download complete callback - fix delegate binding
    HttpRequest->OnProcessRequestComplete().BindRaw(
        this, 
        &FMeshyBridge::OnFileDownloaded,
        FilePath,
        UnknownFormat,  // Use FString variable instead of string constant
        Name,
        FrameRate
    );
    
    // Start download
    HttpRequest->ProcessRequest();
    UE_LOG(LogTemp, Log, TEXT("[Meshy Bridge] Download request started"));
}

// Ensure method signature matches header file definition exactly
void FMeshyBridge::OnFileDownloaded(FHttpRequestPtr Request, FHttpResponsePtr Response, bool bSucceeded, 
                                  FString SaveFilePath, FString Format, FString Name, int32 FrameRate)
{
    // Close progress bar
    CompleteDownloadProgress();
    
    FScopeLock Lock(&GImportLock);
    
    if (!bSucceeded || !Response.IsValid() || Response->GetResponseCode() != 200)
    {
        UE_LOG(LogTemp, Error, TEXT("[Meshy Bridge] File download failed"));
        GIsModelImporting = false;
        return;
    }
    
    // Save file
    TArray<uint8> FileData = Response->GetContent();
    if (FFileHelper::SaveArrayToFile(FileData, *SaveFilePath))
    {
        UE_LOG(LogTemp, Log, TEXT("[Meshy Bridge] File downloaded to: %s"), *SaveFilePath);
        
        // Set flags and path for import use
        GHasDownloadedModel = true;
        GDownloadedFilePath = SaveFilePath;
        
        // Detect file format (by file signature/Magic Number)
        FString DetectedFormat = TEXT("unknown");
        
        // Read first 4 bytes of file to determine file type
        TArray<uint8> Header;
        Header.SetNumUninitialized(4);
        
        IFileHandle* FileHandle = FPlatformFileManager::Get().GetPlatformFile().OpenRead(*SaveFilePath);
        if (FileHandle)
        {
            if (FileHandle->Read(Header.GetData(), 4))
            {
                // Check ZIP file signature (PK\x03\x04)
                if (Header[0] == 'P' && Header[1] == 'K' && Header[2] == 0x03 && Header[3] == 0x04)
                {
                    DetectedFormat = TEXT("zip");
                    UE_LOG(LogTemp, Log, TEXT("[Meshy Bridge] Detected ZIP file format"));
                }
                // Check GLB file signature (glTF)
                else if (Header[0] == 'g' && Header[1] == 'l' && Header[2] == 'T' && Header[3] == 'F')
                {
                    DetectedFormat = TEXT("glb");
                    UE_LOG(LogTemp, Log, TEXT("[Meshy Bridge] Detected GLB file format"));
                }
                else
                {
                    // Unknown format, default to FBX
                    DetectedFormat = TEXT("fbx");
                    UE_LOG(LogTemp, Warning, TEXT("[Meshy Bridge] Unknown file format signature, defaulting to FBX"));
                }
            }
            
            delete FileHandle;
        }
        
        // Import model, use simple member variables to pass data
        FMeshTransfer Transfer;
        Transfer.FileFormat = DetectedFormat;
        Transfer.Path = SaveFilePath;
        Transfer.Name = Name;
        Transfer.FrameRate = FrameRate;
        
        // Use this pointer to call non-static member function
        this->ImportModel(Transfer);
    }
    else
    {
        UE_LOG(LogTemp, Error, TEXT("[Meshy Bridge] Unable to save downloaded file"));
        GIsModelImporting = false;
    }

    UE_LOG(LogTemp, Log, TEXT("[Meshy Bridge] Import processing complete"));
}

void FMeshyBridge::ImportModel(const FMeshTransfer& Transfer)
{
    FScopeLock Lock(&GImportLock);
    
    // If already processing, or no downloaded file, return directly
    if (!GHasDownloadedModel || GDownloadedFilePath.IsEmpty())
    {
        UE_LOG(LogTemp, Warning, TEXT("[Meshy Bridge] No downloaded model to import"));
        GIsModelImporting = false;
        return;
    }
    
    // Create import directory
    FString ImportDir = TEXT("/Game/MeshyImports");
    FString ContentDir = FPaths::ProjectContentDir() / TEXT("MeshyImports");
    
    // Ensure directory exists
    IPlatformFile& PlatformFile = FPlatformFileManager::Get().GetPlatformFile();
    if (!PlatformFile.DirectoryExists(*ContentDir))
    {
        PlatformFile.CreateDirectoryTree(*ContentDir);
    }

    // Static counter to ensure unique filenames
    static int32 ImportCounter = 0;
    ImportCounter++;
    
    // Generate unique filename
    FString ModelName = Transfer.Name.IsEmpty() ? TEXT("Meshy_Model") : Transfer.Name;
    FString FileExtension;
    FString ExtractedPath = Transfer.Path;
    
    // Explicitly determine file format - check if ZIP or GLB first
    if (Transfer.FileFormat.Equals(TEXT("zip"), ESearchCase::IgnoreCase))
    {
        UE_LOG(LogTemp, Log, TEXT("[Meshy Bridge] Processing ZIP file: %s"), *Transfer.Path);
        
        // Create extraction directory
        FString ExtractDir = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("Temp"), TEXT("Meshy"), 
                                           FString::Printf(TEXT("Extract_%04d"), ImportCounter));
        PlatformFile.CreateDirectoryTree(*ExtractDir);
        
        // Execute extraction
        if (ExtractZipFile(Transfer.Path, ExtractDir))
        {
            UE_LOG(LogTemp, Log, TEXT("[Meshy Bridge] ZIP file extracted successfully to: %s"), *ExtractDir);
            
            // Search for model files in extraction directory
            TArray<FString> FoundFiles;
            
            // Search for all supported 3D model file formats
            IFileManager::Get().FindFilesRecursive(FoundFiles, *ExtractDir, TEXT("*.glb"), true, false, false);
            IFileManager::Get().FindFilesRecursive(FoundFiles, *ExtractDir, TEXT("*.fbx"), true, false, false);
            IFileManager::Get().FindFilesRecursive(FoundFiles, *ExtractDir, TEXT("*.obj"), true, false, false);
            IFileManager::Get().FindFilesRecursive(FoundFiles, *ExtractDir, TEXT("*.gltf"), true, false, false);
            
            UE_LOG(LogTemp, Log, TEXT("[Meshy Bridge] Found %d model files in extraction directory"), FoundFiles.Num());
            
            // If model files found, process them
            if (FoundFiles.Num() > 0)
            {
                bool bFoundValidModel = false;
                
                // Process each found model file
                for (const FString& ModelFile : FoundFiles)
                {
                    FString FileFormat = FPaths::GetExtension(ModelFile).ToLower();
                    if (FileFormat.IsEmpty())
                    {
                        continue;
                    }
                    
                    // Create unique filename
                    FString UniqueFileName = FString::Printf(TEXT("%s_Import%04d.%s"),
                        *ModelName,
                        ImportCounter++,
                        *FileFormat);
                    
                    // Target path
                    FString DestPath = ContentDir / UniqueFileName;
                    
                    // Copy file to Content directory
                    if (IFileManager::Get().Copy(*DestPath, *ModelFile) == COPY_OK)
                    {
                        UE_LOG(LogTemp, Log, TEXT("[Meshy Bridge] Model file copied to Content directory: %s"), *DestPath);
                        
                        // Create asset path
                        FString AssetPath = FString::Printf(TEXT("%s/%s"),
                            *ImportDir,
                            *(FPaths::GetBaseFilename(UniqueFileName)));
                        
                        // Import model
                        ImportAndPlaceAsset(DestPath, AssetPath);
                        bFoundValidModel = true;
                    }
                    else
                    {
                        UE_LOG(LogTemp, Error, TEXT("[Meshy Bridge] Unable to copy model file to Content directory"));
                    }
                }
                
                if (!bFoundValidModel)
                {
                    UE_LOG(LogTemp, Warning, TEXT("[Meshy Bridge] Failed to successfully import extracted model files"));
                }
            }
            else
            {
                UE_LOG(LogTemp, Warning, TEXT("[Meshy Bridge] No model files found in ZIP"));
            }
            
            // Keep extraction directory for inspection
            UE_LOG(LogTemp, Warning, TEXT("[Meshy Bridge] Extraction directory kept at: %s"), *ExtractDir);
            UE_LOG(LogTemp, Warning, TEXT("[Meshy Bridge] Please manually check files in this directory"));
        }
        else
        {
            UE_LOG(LogTemp, Error, TEXT("[Meshy Bridge] ZIP file extraction failed"));
        }
    }
    else
    {
        // Non-ZIP file formats (like GLB, FBX, etc.), import directly
        UE_LOG(LogTemp, Log, TEXT("[Meshy Bridge] Directly importing model file: %s, format: %s"), *Transfer.Path, *Transfer.FileFormat);
        
        // Get file extension
        FileExtension = TEXT(".") + Transfer.FileFormat;
        
        // Create unique filename
        FString UniqueFileName = FString::Printf(TEXT("%s_Import%04d%s"),
            *ModelName,
            ImportCounter,
            *FileExtension);
            
        // Target path
        FString DestPath = ContentDir / UniqueFileName;
        
        // Copy file to Content directory
        if (IFileManager::Get().Copy(*DestPath, *Transfer.Path) == COPY_OK)
        {
            UE_LOG(LogTemp, Log, TEXT("[Meshy Bridge] Model file copied to Content directory: %s"), *DestPath);
            
            // Create asset path
            FString AssetPath = FString::Printf(TEXT("%s/%s"),
                *ImportDir,
                *(FPaths::GetBaseFilename(UniqueFileName)));
                
            // Import model
            ImportAndPlaceAsset(DestPath, AssetPath);
        }
        else
        {
            UE_LOG(LogTemp, Error, TEXT("[Meshy Bridge] Unable to copy model file to Content directory"));
        }
    }
    
    // Clean up temporary files
    CleanupTempFile(Transfer.Path);
    
    // Clean up extracted files (if different from original file)
    if (ExtractedPath != Transfer.Path)
    {
        CleanupTempFile(ExtractedPath);
    }
    
    // Reset all flags and state
    GIsModelImporting = false;
    GHasDownloadedModel = false;
    GDownloadedFilePath.Empty();
    
    UE_LOG(LogTemp, Log, TEXT("[Meshy Bridge] Import processing complete"));
}

void FMeshyBridge::ImportAndPlaceAsset(const FString& FilePath, const FString& AssetPath)
{
    // FMeshyBridge is not a UObject, cannot use TWeakObjectPtr
    // We will use static functions to completely avoid this reference
    
    // Don't use WaitUntilTaskCompletes when operating on import directory, this might cause deadlock
    // Use delayed approach to execute asset import operations on game thread
    FString FilePathCopy = FilePath;
    FString AssetPathCopy = AssetPath;

    // Use GEditor->GetTimerManager to create a delegate to execute later on main thread
    FTimerHandle TimerHandle;
    GEditor->GetTimerManager()->SetTimerForNextTick([FilePathCopy, AssetPathCopy]()
    {
        // Execute asset import on main thread
        UE_LOG(LogTemp, Log, TEXT("[Meshy Bridge] Importing asset on main thread"));
        
        // Get asset tools module
        FAssetToolsModule& AssetToolsModule = FModuleManager::GetModuleChecked<FAssetToolsModule>("AssetTools");
        IAssetTools& AssetTools = AssetToolsModule.Get();
        
        // Ensure asset path has unique identifier
        FString ImportTimestamp = FDateTime::Now().ToString(TEXT("%Y%m%d_%H%M%S"));
        FString UniqueFolderPath = FString::Printf(TEXT("%s/Import_%s"), 
            *FPaths::GetPath(AssetPathCopy), 
            *ImportTimestamp);
        
        // Set import options, ensure asset path is unique
        UAutomatedAssetImportData* ImportData = NewObject<UAutomatedAssetImportData>();
        ImportData->bReplaceExisting = false; // Don't replace existing assets
        ImportData->DestinationPath = UniqueFolderPath; // Use unique folder
        ImportData->Filenames.Add(FilePathCopy);
        
        // 启用完整材质和贴图导入
        // 注意：UE5 的 Interchange 系统会自动处理 GLB/GLTF 的 PBR 材质
        // 但需要确保这些选项正确设置
        ImportData->bSkipReadOnly = false;
        
        // 记录文件格式以便调试
        FString FileExt = FPaths::GetExtension(FilePathCopy).ToLower();
        UE_LOG(LogTemp, Log, TEXT("[Meshy Bridge] Importing file with extension: %s"), *FileExt);
        
        // Execute import, now on main thread
        TArray<UObject*> ImportedAssets = AssetTools.ImportAssetsAutomated(ImportData);
        
        // 额外扫描导入目录中的所有资产，包括贴图
        // 因为 ImportAssetsAutomated 可能不会返回所有导入的资产
        FAssetRegistryModule& AssetRegistryModule = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry");
        
        // 强制扫描导入目录
        AssetRegistryModule.Get().ScanPathsSynchronous({UniqueFolderPath}, true);
        
        // 获取该目录下所有资产
        TArray<FAssetData> AllAssetsInFolder;
        AssetRegistryModule.Get().GetAssetsByPath(FName(*UniqueFolderPath), AllAssetsInFolder, true);
        
        UE_LOG(LogTemp, Log, TEXT("[Meshy Bridge] Found %d assets in import folder via AssetRegistry scan"), AllAssetsInFolder.Num());
        
        // 将扫描到的资产添加到 ImportedAssets（如果尚未包含）
        for (const FAssetData& AssetData : AllAssetsInFolder)
        {
            UObject* Asset = AssetData.GetAsset();
            if (Asset && !ImportedAssets.Contains(Asset))
            {
                ImportedAssets.Add(Asset);
                UE_LOG(LogTemp, Log, TEXT("[Meshy Bridge] Added asset from scan: %s (%s)"), 
                    *AssetData.AssetName.ToString(), 
                    *AssetData.AssetClassPath.ToString());
            }
        }
        UE_LOG(LogTemp, Log, TEXT("[Meshy Bridge] Asset import complete, imported %d objects"), ImportedAssets.Num());
        
        {
            // 使用之前已获取的 AssetRegistryModule
            TArray<FAssetData> AssetsInFolder;
            AssetRegistryModule.Get().GetAssetsByPath(FName(*UniqueFolderPath), AssetsInFolder, true);
            TArray<FAssetRenameData> Renames;
            const FString BaseName = FPaths::GetBaseFilename(FilePathCopy); // 如: MyModel_Import0001
            for (const FAssetData& AssetData : AssetsInFolder)
            {
                if (AssetData.AssetName == FName(TEXT("Material_001")))
                {
                    UObject* AssetObj = AssetData.GetAsset();
                    if (AssetObj && AssetObj->IsA<UMaterial>())
                    {
                        const FString DesiredName = BaseName + TEXT("_Mat");
                        FString OutPkg, OutName;
                        AssetTools.CreateUniqueAssetName(UniqueFolderPath + TEXT("/") + DesiredName, TEXT(""), OutPkg, OutName);
                        Renames.Emplace(AssetObj, FPaths::GetPath(OutPkg), OutName);
                    }
                }
            }
            if (Renames.Num() > 0)
            {
                AssetTools.RenameAssets(Renames);
            }
        }
   // Find valid static mesh or skeletal mesh assets        
        if (ImportedAssets.Num() > 0)
        {
            // Find valid static mesh or skeletal mesh assets
            UStaticMesh* ImportedStaticMesh = nullptr;
            USkeletalMesh* ImportedSkeletalMesh = nullptr;
            UAnimSequence* ImportedAnimation = nullptr;
            
            // Record imported asset types for debugging
            FString ImportedAssetList;
            
            // 收集导入的贴图资源
            TArray<UTexture2D*> ImportedTextures;
            TArray<UMaterial*> ImportedMaterials;
            
            // Iterate through imported assets to find meshes and animations
            for (UObject* Asset : ImportedAssets)
            {
                // Record all imported asset types
                ImportedAssetList += FString::Printf(TEXT("%s (%s), "), 
                    *Asset->GetName(), 
                    *Asset->GetClass()->GetName());
                
                if (UStaticMesh* Mesh = Cast<UStaticMesh>(Asset))
                {
                    ImportedStaticMesh = Mesh;
                }
                else if (USkeletalMesh* SkelMesh = Cast<USkeletalMesh>(Asset))
                {
                    ImportedSkeletalMesh = SkelMesh;
                }
                else if (UAnimSequence* AnimSeq = Cast<UAnimSequence>(Asset))
                {
                    ImportedAnimation = AnimSeq;
                }
                else if (UTexture2D* Texture = Cast<UTexture2D>(Asset))
                {
                    ImportedTextures.Add(Texture);
                    UE_LOG(LogTemp, Log, TEXT("[Meshy Bridge] Found texture: %s"), *Texture->GetName());
                }
                else if (UMaterial* Material = Cast<UMaterial>(Asset))
                {
                    ImportedMaterials.Add(Material);
                    UE_LOG(LogTemp, Log, TEXT("[Meshy Bridge] Found material: %s"), *Material->GetName());
                }
                else if (UMaterialInstanceConstant* MatInstance = Cast<UMaterialInstanceConstant>(Asset))
                {
                    // 材质实例 - 检查它使用的贴图
                    UE_LOG(LogTemp, Log, TEXT("[Meshy Bridge] Found material instance: %s"), *MatInstance->GetName());
                    
                    // 获取所有贴图参数
                    TArray<FMaterialParameterInfo> TextureParams;
                    TArray<FGuid> TextureGuids;
                    MatInstance->GetAllTextureParameterInfo(TextureParams, TextureGuids);
                    
                    UE_LOG(LogTemp, Log, TEXT("[Meshy Bridge]   Material instance has %d texture parameters:"), TextureParams.Num());
                    
                    for (const FMaterialParameterInfo& ParamInfo : TextureParams)
                    {
                        UTexture* ParamTexture = nullptr;
                        if (MatInstance->GetTextureParameterValue(ParamInfo, ParamTexture) && ParamTexture)
                        {
                            UE_LOG(LogTemp, Log, TEXT("[Meshy Bridge]     - %s: %s"), 
                                *ParamInfo.Name.ToString(), *ParamTexture->GetName());
                        }
                        else
                        {
                            UE_LOG(LogTemp, Log, TEXT("[Meshy Bridge]     - %s: (no texture assigned)"), 
                                *ParamInfo.Name.ToString());
                        }
                    }
                    
                    // 获取所有标量参数（包括 Metallic、Roughness 常量值）
                    TArray<FMaterialParameterInfo> ScalarParams;
                    TArray<FGuid> ScalarGuids;
                    MatInstance->GetAllScalarParameterInfo(ScalarParams, ScalarGuids);
                    
                    UE_LOG(LogTemp, Log, TEXT("[Meshy Bridge]   Material instance has %d scalar parameters:"), ScalarParams.Num());
                    
                    for (const FMaterialParameterInfo& ParamInfo : ScalarParams)
                    {
                        float ParamValue = 0.0f;
                        if (MatInstance->GetScalarParameterValue(ParamInfo, ParamValue))
                        {
                            UE_LOG(LogTemp, Log, TEXT("[Meshy Bridge]     - %s: %.3f"), 
                                *ParamInfo.Name.ToString(), ParamValue);
                        }
                    }
                }
            }
            
            // Output imported asset type list to help diagnose problems
            UE_LOG(LogTemp, Log, TEXT("[Meshy Bridge] Imported asset list: %s"), *ImportedAssetList);
            UE_LOG(LogTemp, Log, TEXT("[Meshy Bridge] Total textures: %d, Total materials: %d"), 
                ImportedTextures.Num(), ImportedMaterials.Num());
            
            // 诊断：检查每个贴图的名称，帮助识别金属度/粗糙度贴图
            for (UTexture2D* Tex : ImportedTextures)
            {
                FString TexName = Tex->GetName().ToLower();
                if (TexName.Contains(TEXT("metallic")) || TexName.Contains(TEXT("metal")))
                {
                    UE_LOG(LogTemp, Log, TEXT("[Meshy Bridge] Detected Metallic texture: %s"), *Tex->GetName());
                }
                else if (TexName.Contains(TEXT("roughness")) || TexName.Contains(TEXT("rough")))
                {
                    UE_LOG(LogTemp, Log, TEXT("[Meshy Bridge] Detected Roughness texture: %s"), *Tex->GetName());
                }
                else if (TexName.Contains(TEXT("normal")))
                {
                    UE_LOG(LogTemp, Log, TEXT("[Meshy Bridge] Detected Normal texture: %s"), *Tex->GetName());
                }
                else if (TexName.Contains(TEXT("basecolor")) || TexName.Contains(TEXT("albedo")) || TexName.Contains(TEXT("diffuse")))
                {
                    UE_LOG(LogTemp, Log, TEXT("[Meshy Bridge] Detected BaseColor texture: %s"), *Tex->GetName());
                }
                else if (TexName.Contains(TEXT("orm")) || TexName.Contains(TEXT("occlusionroughnessmetallic")))
                {
                    UE_LOG(LogTemp, Log, TEXT("[Meshy Bridge] Detected ORM (packed) texture: %s"), *Tex->GetName());
                }
                else
                {
                    // 记录所有其他贴图名称以便调试
                    UE_LOG(LogTemp, Log, TEXT("[Meshy Bridge] Other texture: %s"), *Tex->GetName());
                }
            }
            
            // 诊断导入的材质，检查是否正确连接了 PBR 贴图
            for (UMaterial* Mat : ImportedMaterials)
            {
                UE_LOG(LogTemp, Log, TEXT("[Meshy Bridge] Analyzing material: %s"), *Mat->GetName());
                
                // 检查材质的表达式节点
                TArray<UMaterialExpression*> Expressions;
                Mat->GetAllExpressionsInMaterialAndFunctionsOfType<UMaterialExpression>(Expressions);
                
                int32 TextureSampleCount = 0;
                for (UMaterialExpression* Expr : Expressions)
                {
                    if (UMaterialExpressionTextureSample* TexSample = Cast<UMaterialExpressionTextureSample>(Expr))
                    {
                        TextureSampleCount++;
                        if (TexSample->Texture)
                        {
                            UE_LOG(LogTemp, Log, TEXT("[Meshy Bridge]   - TextureSample using: %s"), 
                                *TexSample->Texture->GetName());
                        }
                    }
                }
                UE_LOG(LogTemp, Log, TEXT("[Meshy Bridge]   Total texture samples in material: %d"), TextureSampleCount);
            }
            
            // Prioritize skeletal meshes with animation
            if (ImportedSkeletalMesh != nullptr)
            {
                if (ImportedAnimation != nullptr)
                {
                    UE_LOG(LogTemp, Log, TEXT("[Meshy Bridge] Imported skeletal mesh with animation"));
                }
                else
                {
                    UE_LOG(LogTemp, Log, TEXT("[Meshy Bridge] Imported skeletal mesh without animation"));
                }
                
                // If skeletal mesh found, use skeletal mesh first
                FMeshyBridge::PlaceSkeletalMeshInWorldStatic(ImportedSkeletalMesh, ImportedAnimation);
            }
            else if (ImportedStaticMesh != nullptr)
            {
                // If only static mesh, use original static mesh logic
                UE_LOG(LogTemp, Log, TEXT("[Meshy Bridge] Imported static mesh"));
                FMeshyBridge::PlaceStaticMeshInWorldStatic(ImportedStaticMesh);
            }
            else
            {
                UE_LOG(LogTemp, Warning, TEXT("[Meshy Bridge] No meshes found in imported assets"));
            }
        }
        else
        {
            UE_LOG(LogTemp, Error, TEXT("[Meshy Bridge] Failed to import any assets"));
        }
    });

    // No longer wait for task completion, this avoids potential thread issues
    UE_LOG(LogTemp, Log, TEXT("[Meshy Bridge] Import operation scheduled for next frame"));
}

void FMeshyBridge::CleanupTempFile(const FString& Path)
{
    IPlatformFile& PlatformFile = FPlatformFileManager::Get().GetPlatformFile();
    if (PlatformFile.FileExists(*Path))
    {
        PlatformFile.DeleteFile(*Path);
    }
}

// Modified to static helper function to solve this reference problem in lambda
void FMeshyBridge::PlaceSkeletalMeshInWorldStatic(USkeletalMesh* SkeletalMesh, UAnimSequence* Animation)
{
    if (!SkeletalMesh || !GEditor || !GEditor->GetEditorWorldContext().World())
    {
        UE_LOG(LogTemp, Error, TEXT("[Meshy Bridge] Skeletal mesh placement failed: invalid mesh or world context"));
        return;
    }
    
    UWorld* World = GEditor->GetEditorWorldContext().World();
    if (!World || !World->PersistentLevel)
    {
        return;
    }
    
    // Get skeletal mesh Actor class - avoid using deprecated ANY_PACKAGE
    UClass* ActorClass = LoadClass<AActor>(nullptr, TEXT("/Script/Engine.SkeletalMeshActor"));
    if (!ActorClass)
    {
        UE_LOG(LogTemp, Error, TEXT("[Meshy Bridge] Cannot find SkeletalMeshActor class"));
        return;
    }
    
    // Create skeletal mesh Actor
    FActorSpawnParameters SpawnParameters;
    SpawnParameters.OverrideLevel = World->PersistentLevel;
    SpawnParameters.ObjectFlags = RF_Transactional;
    
    // Use correct SpawnActor call method
    const FVector Location(0.0f, 0.0f, 0.0f);
    const FRotator Rotation(0.0f, 0.0f, 0.0f);
    AActor* NewActor = World->SpawnActor(ActorClass, &Location, &Rotation, SpawnParameters);
    
    if (!NewActor)
    {
        UE_LOG(LogTemp, Error, TEXT("[Meshy Bridge] Cannot create skeletal mesh Actor"));
        return;
    }
    
    // Get skeletal mesh component - use GetComponentByClass instead
    USkeletalMeshComponent* SkeletalMeshComponent = nullptr;
    
    // Iterate through Actor's components to find skeletal mesh component
    TArray<UActorComponent*> Components;
    NewActor->GetComponents(USkeletalMeshComponent::StaticClass(), Components);
    
    if (Components.Num() > 0)
    {
        SkeletalMeshComponent = Cast<USkeletalMeshComponent>(Components[0]);
    }
    
    if (SkeletalMeshComponent)
    {
        // 1. Set mesh first to avoid problems later
        SkeletalMeshComponent->SetSkeletalMesh(SkeletalMesh);
        
        // 2. Basic settings
        SkeletalMeshComponent->SetMobility(EComponentMobility::Movable);
        SkeletalMeshComponent->SetCollisionProfileName(TEXT("BlockAll"));
        SkeletalMeshComponent->SetSimulatePhysics(false);
        SkeletalMeshComponent->SetEnableGravity(false);
        
        // 3. Set scale - adjust to appropriate size to match static mesh
        SkeletalMeshComponent->SetRelativeScale3D(FVector(1.0f, 1.0f, 1.0f));
        
        // 4. Set animation - simplified settings, avoid redundant API calls
        if (Animation)
        {
            // Record animation import info
            UE_LOG(LogTemp, Log, TEXT("[Meshy Bridge] Found animation asset: %s"), *Animation->GetName());
            
            // Set animation mode first, then set animation asset
            SkeletalMeshComponent->SetAnimationMode(EAnimationMode::AnimationSingleNode);
            
            // Set animation playback parameters
            SkeletalMeshComponent->AnimationData.AnimToPlay = Animation;
            SkeletalMeshComponent->AnimationData.bSavedLooping = true;
            SkeletalMeshComponent->AnimationData.bSavedPlaying = true;
            
            // Give Actor a name with animation
            FString AnimatedLabelName = FString::Printf(TEXT("Meshy_Animated_%s"), *SkeletalMesh->GetName());
            NewActor->SetActorLabel(AnimatedLabelName);
        }
        else
        {
            // Normal skeletal mesh, set corresponding label
            FString StaticLabelName = FString::Printf(TEXT("Meshy_SkeletalMesh_%s"), *SkeletalMesh->GetName());
            NewActor->SetActorLabel(StaticLabelName);
        }
        
        // 5. Improved material settings - ensure all materials are applied
        TArray<FSkeletalMaterial> MeshMaterials = SkeletalMesh->GetMaterials();
        int32 MaterialCount = MeshMaterials.Num();
        
        UE_LOG(LogTemp, Log, TEXT("[Meshy Bridge] Need to apply %d materials to skeletal mesh"), MaterialCount);
        
        // First directly copy all original materials
        for (int32 MaterialIndex = 0; MaterialIndex < MaterialCount; MaterialIndex++)
        {
            UMaterialInterface* OriginalMaterial = MeshMaterials[MaterialIndex].MaterialInterface;
            if (OriginalMaterial)
            {
                // Record material name and slot name
                FName SlotName = (MeshMaterials[MaterialIndex].MaterialSlotName.IsNone()) ?
                    FName(*FString::Printf(TEXT("Material_%d"), MaterialIndex)) :
                    MeshMaterials[MaterialIndex].MaterialSlotName;
                    
                UE_LOG(LogTemp, Log, TEXT("[Meshy Bridge] Applying material %s to slot %s (index %d)"),
                    *OriginalMaterial->GetName(), *SlotName.ToString(), MaterialIndex);
                
                // Set original material directly first
                SkeletalMeshComponent->SetMaterial(MaterialIndex, OriginalMaterial);
                
                // Then create dynamic material instance
                UMaterialInstanceDynamic* MaterialInstance = UMaterialInstanceDynamic::Create(
                    OriginalMaterial, 
                    SkeletalMeshComponent
                );
                
                if (MaterialInstance)
                {
                    SkeletalMeshComponent->SetMaterial(MaterialIndex, MaterialInstance);
                }
            }
            else
            {
                UE_LOG(LogTemp, Warning, TEXT("[Meshy Bridge] Material index %d is invalid"), MaterialIndex);
            }
        }
        
        // 6. After creating material instances and applying textures, force refresh and compile
        for (int32 MaterialIndex = 0; MaterialIndex < MaterialCount; MaterialIndex++)
        {
            UMaterialInstanceDynamic* MaterialInstance = Cast<UMaterialInstanceDynamic>(SkeletalMeshComponent->GetMaterial(MaterialIndex));
            if (MaterialInstance)
            {
                // Force update material
                MaterialInstance->MarkPackageDirty();
                
                // Still keep ForceRecompileForRendering
                MaterialInstance->ForceRecompileForRendering();
                
                // Manually trigger skeletal mesh update flags
                SkeletalMeshComponent->MarkRenderStateDirty();
                SkeletalMeshComponent->MarkRenderDynamicDataDirty();
            }
        }
        
        // 7. Force refresh content browser and material cache
        if (GEditor)
        {
            // Request editor to re-render all viewports
            GEditor->RedrawAllViewports();
            
            // Simplify ContentBrowser usage, avoid using IContentBrowserSingleton
            if (FModuleManager::Get().IsModuleLoaded("ContentBrowser"))
            {
                // Avoid using IContentBrowserSingleton directly
                // Just request re-render
                FContentBrowserModule& ContentBrowserModule = FModuleManager::GetModuleChecked<FContentBrowserModule>("ContentBrowser");
                // Don't call SyncBrowserToAssets, it might have issues in UE5.5
            }
        }
        
        // 8. Select Actor
        if (GEditor)
        {
            GEditor->SelectNone(false, true);
            GEditor->SelectActor(NewActor, true, true);
            GEditor->NoteSelectionChange();
        }
        
        UE_LOG(LogTemp, Log, TEXT("[Meshy Bridge] Skeletal model placed in scene, size adjusted to match static mesh"));
    }
    else
    {
        UE_LOG(LogTemp, Error, TEXT("[Meshy Bridge] Cannot find skeletal mesh component"));
        // Clean up failed Actor
        if (NewActor)
        {
            NewActor->Destroy();
        }
    }
}

// Implement static mesh placement function
void FMeshyBridge::PlaceStaticMeshInWorldStatic(UStaticMesh* StaticMesh)
{
    if (!StaticMesh || !GEditor || !GEditor->GetEditorWorldContext().World())
    {
        UE_LOG(LogTemp, Error, TEXT("[Meshy Bridge] Static mesh placement failed: invalid mesh or world context"));
        return;
    }
    
    UWorld* World = GEditor->GetEditorWorldContext().World();
    if (!World || !World->PersistentLevel)
    {
        return;
    }
    
    // Create static mesh Actor
    FActorSpawnParameters SpawnParameters;
    SpawnParameters.OverrideLevel = World->PersistentLevel;
    SpawnParameters.ObjectFlags = RF_Transactional;
    
    // Use correct SpawnActor call method
    const FVector Location(0.0f, 0.0f, 0.0f);
    const FRotator Rotation(0.0f, 0.0f, 0.0f);
    
    // Load static mesh Actor class
    UClass* ActorClass = LoadClass<AActor>(nullptr, TEXT("/Script/Engine.StaticMeshActor"));
    if (!ActorClass)
    {
        UE_LOG(LogTemp, Error, TEXT("[Meshy Bridge] Cannot find StaticMeshActor class"));
        return;
    }
    
    AActor* NewActor = World->SpawnActor(ActorClass, &Location, &Rotation, SpawnParameters);
    
    if (!NewActor)
    {
        UE_LOG(LogTemp, Error, TEXT("[Meshy Bridge] Cannot create static mesh Actor"));
        return;
    }
    
    // Get static mesh component
    UStaticMeshComponent* MeshComp = nullptr;
    
    // Iterate through Actor's components to find static mesh component
    TArray<UActorComponent*> Components;
    NewActor->GetComponents(UStaticMeshComponent::StaticClass(), Components);
    
    if (Components.Num() > 0)
    {
        MeshComp = Cast<UStaticMeshComponent>(Components[0]);
    }
    
    if (MeshComp)
    {
        // Set mesh
        MeshComp->SetStaticMesh(StaticMesh);
        
        // Set Actor label
        FString StaticLabelName = FString::Printf(TEXT("Meshy_StaticMesh_%s"), *StaticMesh->GetName());
        NewActor->SetActorLabel(StaticLabelName);
        
        // Set materials
        int32 MaterialCount = StaticMesh->GetStaticMaterials().Num();
        for (int32 MaterialIndex = 0; MaterialIndex < MaterialCount; MaterialIndex++)
        {
            UMaterialInterface* OriginalMaterial = StaticMesh->GetMaterial(MaterialIndex);
            if (OriginalMaterial)
            {
                // Create material instance
                UMaterialInstanceDynamic* MaterialInstance = UMaterialInstanceDynamic::Create(
                    OriginalMaterial, 
                    MeshComp
                );
                
                if (MaterialInstance)
                {
                    MeshComp->SetMaterial(MaterialIndex, MaterialInstance);
                }
            }
        }
        
        // Select Actor
        if (GEditor)
        {
            GEditor->SelectNone(false, true);
            GEditor->SelectActor(NewActor, true, true);
            GEditor->NoteSelectionChange();
        }
        
        UE_LOG(LogTemp, Log, TEXT("[Meshy Bridge] Static model placed in scene"));
    }
    else
    {
        UE_LOG(LogTemp, Error, TEXT("[Meshy Bridge] Cannot find static mesh component"));
        // Clean up failed Actor
        if (NewActor)
        {
            NewActor->Destroy();
        }
    }
}

bool FMeshyBridge::ExtractZipFile(const FString& ZipFilePath, const FString& ExtractDir)
{
#if MESHY_MINIZIP_SUPPORTED
    unzFile zipFile = unzOpen(TCHAR_TO_ANSI(*ZipFilePath));
    if (!zipFile)
    {
        UE_LOG(LogTemp, Error, TEXT("[Meshy Bridge] Cannot open ZIP file: %s"), *ZipFilePath);
        return false;
    }
    
    unz_global_info globalInfo;
    if (unzGetGlobalInfo(zipFile, &globalInfo) != UNZ_OK)
    {
        UE_LOG(LogTemp, Error, TEXT("[Meshy Bridge] Cannot get ZIP file info"));
        unzClose(zipFile);
        return false;
    }
    
    // Iterate through all files in ZIP file
    for (uLong i = 0; i < globalInfo.number_entry; ++i)
    {
        // Get current file info
        unz_file_info fileInfo;
        char filename[512];
        if (unzGetCurrentFileInfo(zipFile, &fileInfo, filename, sizeof(filename), NULL, 0, NULL, 0) != UNZ_OK)
        {
            UE_LOG(LogTemp, Warning, TEXT("[Meshy Bridge] Cannot get file info for file %d in ZIP"), i);
            continue;
        }
        
        FString OutputDirectory = ExtractDir;
        // 使用 UTF8_TO_TCHAR 正确处理中文文件名
        FString FileName = FString(UTF8_TO_TCHAR(filename));
        
        // If it's a directory, create it
        if (FileName.EndsWith("/"))
        {
            FString FullPath = FPaths::Combine(OutputDirectory, FileName);
            FPlatformFileManager::Get().GetPlatformFile().CreateDirectoryTree(*FullPath);
            continue;
        }
        
        // Prepare to extract file
        if (unzOpenCurrentFile(zipFile) != UNZ_OK)
        {
            UE_LOG(LogTemp, Warning, TEXT("[Meshy Bridge] Cannot open file in ZIP: %s"), *FileName);
            continue;
        }
        
        // Create target file path
        FString FullPath = FPaths::Combine(OutputDirectory, FileName);
        FString Directory = FPaths::GetPath(FullPath);
        
        // Ensure directory exists
        FPlatformFileManager::Get().GetPlatformFile().CreateDirectoryTree(*Directory);
        
        // Read and write file
        TArray<uint8> FileData;
        FileData.SetNumUninitialized(fileInfo.uncompressed_size);
        
        if (unzReadCurrentFile(zipFile, FileData.GetData(), FileData.Num()) != (int32)fileInfo.uncompressed_size)
        {
            UE_LOG(LogTemp, Warning, TEXT("[Meshy Bridge] Cannot read file in ZIP: %s"), *FileName);
            unzCloseCurrentFile(zipFile);
            continue;
        }
        
        // Write file
        if (!FFileHelper::SaveArrayToFile(FileData, *FullPath))
        {
            UE_LOG(LogTemp, Warning, TEXT("[Meshy Bridge] Cannot write file: %s"), *FullPath);
            unzCloseCurrentFile(zipFile);
            continue;
        }
        
        unzCloseCurrentFile(zipFile);
        
        // Go to next file
        if ((i + 1) < globalInfo.number_entry)
        {
            if (unzGoToNextFile(zipFile) != UNZ_OK)
            {
                UE_LOG(LogTemp, Warning, TEXT("[Meshy Bridge] Cannot go to next file"));
                break;
            }
        }
    }
    
    unzClose(zipFile);
    UE_LOG(LogTemp, Log, TEXT("[Meshy Bridge] ZIP file extracted successfully: %s"), *ZipFilePath);
    return true;
    
#else
    UE_LOG(LogTemp, Error, TEXT("[Meshy Bridge] ZIP extraction not supported on this platform"));
    return false;
#endif
}

void FMeshyBridge::ShowDownloadProgress(const FString& ModelName)
{
    if (!IsInGameThread())
    {
        AsyncTask(ENamedThreads::GameThread, [this, ModelName]() {
            ShowDownloadProgress(ModelName);
        });
        return;
    }

    ProgressTask = MakeShareable(new FScopedSlowTask(
        1.0f, // 总工作量为1.0
        FText::FromString(FString::Printf(TEXT("Downloading model: %s"), *ModelName)),
        true // 显示取消按钮
    ));
    
    if (ProgressTask.IsValid())
    {
        ProgressTask->MakeDialog(true); // 创建模态对话框
        UE_LOG(LogTemp, Log, TEXT("[Meshy Bridge] Displaying download progress - %s"), *ModelName);
    }
}

void FMeshyBridge::CompleteDownloadProgress()
{
    if (!IsInGameThread())
    {
        AsyncTask(ENamedThreads::GameThread, [this]() {
            CompleteDownloadProgress();
        });
        return;
    }

    if (ProgressTask.IsValid())
    {
        // 首先设置进度为100%
        float RemainingProgress = 1.0f - ProgressTask->CompletedWork;
        if (RemainingProgress > 0.0f)
        {
            ProgressTask->EnterProgressFrame(RemainingProgress);
        }
        
        // 设置一个短暂的延迟后再关闭进度条
        UE_LOG(LogTemp, Log, TEXT("[Meshy Bridge] Download complete - closing progress dialog shortly"));
        
        // 使用定时器在短暂延迟后销毁进度条
        // 捕获 ProgressTask 的副本以避免 this 指针问题
        TSharedPtr<FScopedSlowTask> TaskToClose = ProgressTask;
        ProgressTask.Reset(); // 立即重置，避免重复操作
        
        FTSTicker::GetCoreTicker().AddTicker(
            FTickerDelegate::CreateLambda([TaskToClose](float DeltaTime) -> bool {
                if (TaskToClose.IsValid())
                {
                    // FScopedSlowTask 会在析构时自动关闭对话框
                    // 在 UE 5.6 和 5.7 中都兼容
                    UE_LOG(LogTemp, Log, TEXT("[Meshy Bridge] Progress dialog closed"));
                }
                return false; // 返回false表示只执行一次
            }),
            1.5f // 延迟1.5秒后关闭
        );
    }
}

void FMeshyBridge::UpdateDownloadProgress(float Progress)
{
    if (!IsInGameThread())
    {
        AsyncTask(ENamedThreads::GameThread, [this, Progress]() {
            UpdateDownloadProgress(Progress);
        });
        return;
    }

    if (ProgressTask.IsValid())
    {
        float FrameProgress = Progress - ProgressTask->CompletedWork;
        if (FrameProgress > 0.0f)
        {
            ProgressTask->EnterProgressFrame(FrameProgress);
        }
    }
}

