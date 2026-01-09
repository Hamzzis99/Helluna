// Gihyeon's Deformation Project (Helluna)

#include "MeshDeformation.h"

#define LOCTEXT_NAMESPACE "FMeshDeformationModule"

// 로그 카테고리 정의
DEFINE_LOG_CATEGORY(LogMeshDeform);

void FMeshDeformationModule::StartupModule()
{
	UE_LOG(LogMeshDeform, Log, TEXT("MeshDeformation 플러그인 모듈이 시작되었습니다."));
}

void FMeshDeformationModule::ShutdownModule()
{
	UE_LOG(LogMeshDeform, Log, TEXT("MeshDeformation 플러그인 모듈이 종료되었습니다."));
}

#undef LOCTEXT_NAMESPACE
    
IMPLEMENT_MODULE(FMeshDeformationModule, MeshDeformation)