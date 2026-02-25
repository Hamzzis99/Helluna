// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/NoExportTypes.h"
#include "HAL/Runnable.h"
#include "HAL/ThreadSafeBool.h"
#include "Containers/Queue.h"
#include "Sockets.h"
#include "Http.h"
#include "Misc/ScopedSlowTask.h"
#include "Misc/EngineVersionComparison.h"
#include "MeshyBridge.generated.h"

// UE 5.6/5.7 版本兼容性宏
#ifndef UE_5_07_OR_LATER
    #if ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION >= 7
        #define UE_5_07_OR_LATER 1
    #else
        #define UE_5_07_OR_LATER 0
    #endif
#endif

USTRUCT()
struct FMeshTransfer
{
    GENERATED_BODY()

    UPROPERTY()
    FString FileFormat = TEXT("");

    UPROPERTY()
    FString Path = TEXT("");

    UPROPERTY()
    FString Name = TEXT("");

    UPROPERTY()
    int32 FrameRate = 30;
    
    FMeshTransfer()
        : FileFormat(TEXT(""))
        , Path(TEXT(""))
        , Name(TEXT(""))
        , FrameRate(30)
    {}
};

class MESHY_API FMeshyBridge : public FRunnable
{
public:
    static FMeshyBridge& Get()
    {
        static FMeshyBridge Instance;
        return Instance;
    }

    FMeshyBridge();
    virtual ~FMeshyBridge();

    // FRunnable interface
    virtual bool Init() override;
    virtual uint32 Run() override;
    virtual void Stop() override;
    virtual void Exit() override;

    void StartServer();
    void StopServer();
    bool IsRunning() const { return bIsRunning; }

    // 处理HTTP回调的函数，正确声明参数类型
    void OnFileDownloaded(FHttpRequestPtr Request, FHttpResponsePtr Response, bool bSucceeded, FString SaveFilePath, FString Format, FString Name, int32 FrameRate);
    
    // 处理导入队列
    void ProcessImportQueue();

    /** 放置静态网格体到世界中（静态辅助函数） */
    static void PlaceStaticMeshInWorldStatic(UStaticMesh* StaticMesh);
    
    /** 放置骨骼网格体到世界中（静态辅助函数） */
    static void PlaceSkeletalMeshInWorldStatic(USkeletalMesh* SkeletalMesh, UAnimSequence* Animation);
    
    // 简化进度显示方法
    void ShowDownloadProgress(const FString& ModelName);
    void UpdateDownloadProgress(float Progress);
    void CompleteDownloadProgress();

private:
    void ProcessClientRequest(class FSocket* ClientSocket);
    void SendResponse(FSocket* ClientSocket, const FString& Response);
    void ProcessImportRequest(const FString& RequestData);
    void ImportModel(const FMeshTransfer& Transfer);
    void CleanupTempFile(const FString& Path);
    
    // 添加ZIP文件解压方法
    bool ExtractZipFile(const FString& ZipFilePath, const FString& ExtractDir);
    
    // 添加导入和放置资产的辅助方法
    void ImportAndPlaceAsset(const FString& FilePath, const FString& AssetPath);

    // 添加从ZIP提取模型文件的方法（改名以避免与之前定义冲突）
    bool ExtractModelFromZip(const FString& ZipFilePath, const FString& ExtractDir, FString& OutModelFilePath);

    // 服务器状态
    bool bIsRunning = false;
    bool bShouldStop = false;
    
    // 线程和套接字
    FRunnableThread* ServerThread = nullptr;
    FSocket* ListenerSocket = nullptr;

    // 导入队列
    FCriticalSection ImportQueueLock;
    TQueue<FMeshTransfer> ImportQueue;
    
    // 下载进度相关
    int32 TotalBytesToReceive = 0;
    TSharedPtr<FScopedSlowTask> ProgressTask;

    static const int32 ServerPort = 5327;
    static const TArray<FString> AllowedOrigins;

    // 添加清理JSON字符串的辅助函数
    FString SanitizeJsonString(const FString& InputJson);
}; 