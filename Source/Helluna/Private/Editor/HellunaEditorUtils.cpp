// Capstone Project Helluna

#include "Editor/HellunaEditorUtils.h"
#include "Animation/Skeleton.h"
#include "Engine/SkeletalMeshSocket.h"

bool UHellunaEditorUtils::AddSocketToSkeleton(
	USkeleton* Skeleton,
	FName SocketName,
	FName BoneName,
	FVector RelativeLocation,
	FRotator RelativeRotation)
{
	if (!Skeleton) return false;
	if (SocketName.IsNone()) return false;

	// 중복 체크 (정식 FindSocket API)
	if (Skeleton->FindSocket(SocketName)) return false;

	USkeletalMeshSocket* NewSocket = NewObject<USkeletalMeshSocket>(Skeleton);
	if (!NewSocket) return false;

	NewSocket->SocketName       = SocketName;
	NewSocket->BoneName         = BoneName;
	NewSocket->RelativeLocation = RelativeLocation;
	NewSocket->RelativeRotation = RelativeRotation;
	NewSocket->RelativeScale    = FVector::OneVector;

	Skeleton->Sockets.Add(NewSocket);
	Skeleton->MarkPackageDirty();

	return true;
}
