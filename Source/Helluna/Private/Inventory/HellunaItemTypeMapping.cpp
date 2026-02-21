// Helluna Inventory Save System - Phase 1: DataTable Mapping

#include "Inventory/HellunaItemTypeMapping.h"

TSubclassOf<AActor> UHellunaItemTypeMapping::GetActorClassFromItemType(
	const UDataTable* DataTable,
	const FGameplayTag& ItemType)
{
	// 유효성 검사
	if (!IsValid(DataTable))
	{
		UE_LOG(LogTemp, Error, TEXT("[ItemTypeMapping] DataTable이 nullptr입니다!"));
		return nullptr;
	}

	if (!ItemType.IsValid())
	{
		UE_LOG(LogTemp, Error, TEXT("[ItemTypeMapping] ItemType이 유효하지 않습니다!"));
		return nullptr;
	}

	// DataTable의 모든 Row 순회
	const FString ContextString(TEXT("ItemTypeMapping"));
	TArray<FItemTypeToActorMapping*> AllRows;
	DataTable->GetAllRows<FItemTypeToActorMapping>(ContextString, AllRows);

	for (const FItemTypeToActorMapping* Row : AllRows)
	{
		if (Row && Row->ItemType.MatchesTagExact(ItemType))
		{
			if (Row->ItemActorClass)
			{
				UE_LOG(LogTemp, Log, TEXT("[ItemTypeMapping] 매핑 성공: %s → %s"),
					*ItemType.ToString(),
					*Row->ItemActorClass->GetName());
				return Row->ItemActorClass;
			}
			else
			{
				UE_LOG(LogTemp, Warning, TEXT("[ItemTypeMapping] ItemType '%s'의 ActorClass가 nullptr입니다!"),
					*ItemType.ToString());
				return nullptr;
			}
		}
	}

	// 매핑을 찾지 못함
	UE_LOG(LogTemp, Warning, TEXT("[ItemTypeMapping] ItemType '%s'에 대한 매핑을 찾을 수 없습니다!"),
		*ItemType.ToString());
	return nullptr;
}

void UHellunaItemTypeMapping::DebugPrintAllMappings(const UDataTable* DataTable)
{
	if (!IsValid(DataTable))
	{
		UE_LOG(LogTemp, Error, TEXT("[ItemTypeMapping] DebugPrint - DataTable이 nullptr입니다!"));
		return;
	}

	UE_LOG(LogTemp, Warning, TEXT(""));
	UE_LOG(LogTemp, Warning, TEXT("╔════════════════════════════════════════════════════════════╗"));
	UE_LOG(LogTemp, Warning, TEXT("║         [ItemTypeMapping] DataTable 매핑 목록              ║"));
	UE_LOG(LogTemp, Warning, TEXT("╠════════════════════════════════════════════════════════════╣"));

	const FString ContextString(TEXT("ItemTypeMappingDebug"));
	TArray<FItemTypeToActorMapping*> AllRows;
	DataTable->GetAllRows<FItemTypeToActorMapping>(ContextString, AllRows);

	int32 Index = 0;
	for (const FItemTypeToActorMapping* Row : AllRows)
	{
		if (Row)
		{
			FString ItemTypeStr = Row->ItemType.IsValid() ? Row->ItemType.ToString() : TEXT("(Invalid Tag)");
			FString ActorClassStr = Row->ItemActorClass ? Row->ItemActorClass->GetName() : TEXT("(nullptr)");
			
			UE_LOG(LogTemp, Warning, TEXT("║ [%d] %s"), Index, *ItemTypeStr);
			UE_LOG(LogTemp, Warning, TEXT("║     → %s"), *ActorClassStr);
			Index++;
		}
	}

	UE_LOG(LogTemp, Warning, TEXT("╠════════════════════════════════════════════════════════════╣"));
	UE_LOG(LogTemp, Warning, TEXT("║ 총 %d개의 매핑"), AllRows.Num());
	UE_LOG(LogTemp, Warning, TEXT("╚════════════════════════════════════════════════════════════╝"));
	UE_LOG(LogTemp, Warning, TEXT(""));
}
