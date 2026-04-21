// Fill out your copyright notice in the Description page of Project Settings.

#include "Cheat/HellunaCheatComponent.h"

#include "EngineUtils.h"
#include "Engine/DamageEvents.h"
#include "Engine/DataTable.h"
#include "Engine/World.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerController.h"
#include "Net/UnrealNetwork.h"

#include "Character/HellunaEnemyCharacter.h"
#include "Character/EnemyComponent/HellunaHealthComponent.h"
#include "GameMode/HellunaDefenseGameMode.h"
#include "GameMode/HellunaDefenseGameState.h"
#include "Inventory/HellunaItemTypeMapping.h"

#include "Items/Components/Inv_ItemComponent.h"
#include "Items/Manifest/Inv_ItemManifest.h"
#include "Items/Fragments/Inv_ItemFragment.h"
#include "InventoryManagement/Components/Inv_InventoryComponent.h"

UHellunaCheatComponent::UHellunaCheatComponent()
{
    PrimaryComponentTick.bCanEverTick = true;
    PrimaryComponentTick.bStartWithTickEnabled = true;
    SetIsReplicatedByDefault(true);

    DefaultItemTypeMappingPath = FSoftObjectPath(
        TEXT("/Game/Data/Inventory/DT_ItemTypeMapping.DT_ItemTypeMapping"));
}

void UHellunaCheatComponent::BeginPlay()
{
    Super::BeginPlay();

    // 데디서버는 이 컴포넌트의 Tick 자체가 불필요 (입력 폴링 전용)
    //   As-A-Client에선 서버 프로세스도 이 액터를 들고 Tick하므로 명시적으로 끈다.
    if (UWorld* W = GetWorld())
    {
        if (W->GetNetMode() == NM_DedicatedServer)
        {
            SetComponentTickEnabled(false);
            PrimaryComponentTick.bStartWithTickEnabled = false;
        }
    }

#if !UE_BUILD_SHIPPING
    UE_LOG(LogTemp, Verbose, TEXT("[cheatdebug] Cheat BeginPlay: NoclipSpeedMultiplier=%.1f (BP/C++ default)"),
        NoclipSpeedMultiplier);
#endif
}

void UHellunaCheatComponent::TickComponent(float DeltaTime, ELevelTick TickType,
    FActorComponentTickFunction* ThisTickFunction)
{
    Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

    if (!bNoclipOn) return;

    // 로컬 클라에서만 키 상태 읽어서 서버로 전달
    APawn* OwnerPawn = Cast<APawn>(GetOwner());
    if (!OwnerPawn || !OwnerPawn->IsLocallyControlled()) return;

    APlayerController* PC = Cast<APlayerController>(OwnerPawn->GetController());
    if (!PC) return;

    const bool bUp   = PC->IsInputKeyDown(EKeys::SpaceBar);
    const bool bDown = PC->IsInputKeyDown(EKeys::LeftControl);

    if (bUp && !bDown)       Server_NoclipAscend(+1.f);
    else if (bDown && !bUp)  Server_NoclipAscend(-1.f);
    else                     Server_NoclipAscend( 0.f);
}

void UHellunaCheatComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
    Super::GetLifetimeReplicatedProps(OutLifetimeProps);
    DOREPLIFETIME(UHellunaCheatComponent, bNoclipOn);
}

// ─────────────────────────────────────────────────────────────────
// 로컬 입력 → Server RPC
// ─────────────────────────────────────────────────────────────────
static FString CheatOwnerTag(const UObject* WorldCtx)
{
    if (!WorldCtx) return TEXT("<null>");
    const AActor* OwnerActor = Cast<AActor>(WorldCtx->GetOuter());
    const ENetRole LocalRole = OwnerActor ? OwnerActor->GetLocalRole() : ROLE_None;
    const ENetMode NetMode   = WorldCtx->GetWorld() ? WorldCtx->GetWorld()->GetNetMode() : NM_Standalone;
    return FString::Printf(TEXT("Owner=%s LocalRole=%d NetMode=%d"),
        *GetNameSafe(OwnerActor), (int32)LocalRole, (int32)NetMode);
}

void UHellunaCheatComponent::HandleKey_KillAll()
{
    UE_LOG(LogTemp, Warning, TEXT("[cheatdebug] HandleKey_KillAll pressed | %s"), *CheatOwnerTag(this));
    Server_KillAllEnemies();
}
void UHellunaCheatComponent::HandleKey_TimeFreeze()
{
    UE_LOG(LogTemp, Warning, TEXT("[cheatdebug] HandleKey_TimeFreeze pressed | %s"), *CheatOwnerTag(this));
    Server_ToggleTimeFreeze();
}
void UHellunaCheatComponent::HandleKey_Noclip()
{
    UE_LOG(LogTemp, Warning, TEXT("[cheatdebug] HandleKey_Noclip pressed | %s"), *CheatOwnerTag(this));
    Server_ToggleNoclip();
}
void UHellunaCheatComponent::HandleKey_GrantMaterials()
{
    UE_LOG(LogTemp, Warning, TEXT("[cheatdebug] HandleKey_GrantMaterials pressed | %s"), *CheatOwnerTag(this));
    Server_GrantAllMaterials();
}
void UHellunaCheatComponent::HandleKey_SpeedUp()
{
    UE_LOG(LogTemp, Warning, TEXT("[cheatdebug] HandleKey_SpeedUp pressed | %s"), *CheatOwnerTag(this));
    Server_AdjustNoclipSpeed(2.f);
}
void UHellunaCheatComponent::HandleKey_SpeedDown()
{
    UE_LOG(LogTemp, Warning, TEXT("[cheatdebug] HandleKey_SpeedDown pressed | %s"), *CheatOwnerTag(this));
    Server_AdjustNoclipSpeed(0.5f);
}


// ─────────────────────────────────────────────────────────────────
// F1 — 모든 몬스터 처치 (서버)
// ─────────────────────────────────────────────────────────────────
void UHellunaCheatComponent::Server_KillAllEnemies_Implementation()
{
    UWorld* World = GetWorld();
    UE_LOG(LogTemp, Warning, TEXT("[cheatdebug] Server_KillAllEnemies ENTER | World=%s Authority=%d"),
        *GetNameSafe(World), GetOwner() ? (int32)GetOwner()->HasAuthority() : -1);
    if (!World) { UE_LOG(LogTemp, Warning, TEXT("[cheatdebug] KillAll ABORT: World null")); return; }

    APawn* OwnerPawn = Cast<APawn>(GetOwner());
    UE_LOG(LogTemp, Warning, TEXT("[cheatdebug] KillAll OwnerPawn=%s"), *GetNameSafe(OwnerPawn));

    AHellunaDefenseGameMode* DefenseGM = World->GetAuthGameMode<AHellunaDefenseGameMode>();
    UE_LOG(LogTemp, Warning, TEXT("[cheatdebug] KillAll DefenseGM=%s"), *GetNameSafe(DefenseGM));

    int32 Seen = 0, Killed = 0, SkippedHidden = 0, SkippedAlreadyDead = 0, SkippedNoHC = 0, NotifiedGM = 0, PhantomRevived = 0;
    for (TActorIterator<AHellunaEnemyCharacter> It(World); It; ++It)
    {
        ++Seen;
        AHellunaEnemyCharacter* Enemy = *It;
        if (!IsValid(Enemy) || Enemy->IsActorBeingDestroyed()) continue;

        const bool bHidden = Enemy->IsHidden();
        const bool bColl = Enemy->GetActorEnableCollision();

        // [PhantomDiagV1] 반복되는 원거리 +1 유령 몬스터 추적용. 모든 이터레이션 로그.
        UE_LOG(LogTemp, Warning,
            TEXT("[cheatdebug][PhantomIter] Enemy=%s Class=%s Hidden=%d Coll=%d Loc=%s"),
            *GetNameSafe(Enemy), *Enemy->GetClass()->GetName(),
            (int32)bHidden, (int32)bColl,
            *Enemy->GetActorLocation().ToString());

        // 1) 숨김 상태(풀 대기)만 스킵. 콜리전 off 는 유령 상태로 간주하고 강제 복구 후 킬.
        if (bHidden)
        {
            ++SkippedHidden;
            continue;
        }

        if (!bColl)
        {
            UE_LOG(LogTemp, Warning,
                TEXT("[cheatdebug][PhantomFix] Enemy=%s Class=%s — Hidden=false 인데 Coll=false(유령 상태). 강제 enable 후 킬 진행"),
                *GetNameSafe(Enemy), *Enemy->GetClass()->GetName());
            Enemy->SetActorEnableCollision(true);
            ++PhantomRevived;
        }

        UHellunaHealthComponent* HC = Enemy->FindComponentByClass<UHellunaHealthComponent>();
        if (!HC)
        {
            ++SkippedNoHC;
            UE_LOG(LogTemp, Warning, TEXT("[cheatdebug] KillAll skip Enemy=%s reason=NoHealthComp"), *GetNameSafe(Enemy));
            continue;
        }

        if (HC->IsDead())
        {
            ++SkippedAlreadyDead;
            UE_LOG(LogTemp, Warning, TEXT("[cheatdebug] KillAll skip Enemy=%s reason=AlreadyDead"), *GetNameSafe(Enemy));
            continue;
        }

        UE_LOG(LogTemp, Warning,
            TEXT("[cheatdebug] KillAll ForceKill Enemy=%s Hidden=%d Coll=%d H=%.1f Downed=%d"),
            *GetNameSafe(Enemy), (int32)bHidden, (int32)bColl, HC->GetHealth(), (int32)HC->IsDowned());
        HC->Cheat_ForceKill(OwnerPawn);
        ++Killed;

        // [cheatdebug] 웨이브 카운터 차감 — 정상 Death GA 경로가 파이프라인에서 유실될 수 있어
        // GameMode::NotifyMonsterDied 를 직접 호출해 RemainingMonstersThisNight 를 확정적으로 감소
        if (DefenseGM)
        {
            DefenseGM->NotifyMonsterDied(Enemy);
            ++NotifiedGM;
        }
    }
    UE_LOG(LogTemp, Warning,
        TEXT("[cheatdebug] KillAll DONE Seen=%d Killed=%d (PhantomRevived=%d) NotifiedGM=%d SkippedHidden=%d SkippedAlreadyDead=%d SkippedNoHC=%d"),
        Seen, Killed, PhantomRevived, NotifiedGM, SkippedHidden, SkippedAlreadyDead, SkippedNoHC);

    if (AHellunaDefenseGameState* GS = World->GetGameState<AHellunaDefenseGameState>())
    {
        UE_LOG(LogTemp, Warning, TEXT("[cheatdebug] KillAll PostState AliveMonsterCount=%d"),
            GS->GetAliveMonsterCount());
    }
}

// ─────────────────────────────────────────────────────────────────
// F2 — 시간 정지 토글 (서버 → GameState가 모두에게 전파)
// ─────────────────────────────────────────────────────────────────
void UHellunaCheatComponent::Server_ToggleTimeFreeze_Implementation()
{
    UWorld* World = GetWorld();
    UE_LOG(LogTemp, Warning, TEXT("[cheatdebug] Server_ToggleTimeFreeze ENTER | World=%s Authority=%d"),
        *GetNameSafe(World), GetOwner() ? (int32)GetOwner()->HasAuthority() : -1);
    if (!World) { UE_LOG(LogTemp, Warning, TEXT("[cheatdebug] TimeFreeze ABORT: World null")); return; }

    AGameStateBase* RawGS = World->GetGameState();
    AHellunaDefenseGameState* GS = Cast<AHellunaDefenseGameState>(RawGS);
    UE_LOG(LogTemp, Warning, TEXT("[cheatdebug] TimeFreeze RawGS=%s CastedGS=%s"),
        *GetNameSafe(RawGS), *GetNameSafe(GS));

    if (!GS)
    {
        UE_LOG(LogTemp, Warning, TEXT("[cheatdebug] TimeFreeze ABORT: DefenseGameState cast failed"));
        return;
    }
    GS->Cheat_ToggleTimeFrozen();
}

// ─────────────────────────────────────────────────────────────────
// F3 — 노클립 토글 (서버 권한으로 이동 모드 변경)
// ─────────────────────────────────────────────────────────────────
void UHellunaCheatComponent::Server_ToggleNoclip_Implementation()
{
    bNoclipOn = !bNoclipOn;
    ApplyNoclipOnOwner(bNoclipOn);
    Multicast_ApplyNoclipState(bNoclipOn, NoclipSpeedMultiplier);
    UE_LOG(LogTemp, Warning, TEXT("[cheatdebug] Server_ToggleNoclip -> %s Mult=%.1f"),
        bNoclipOn ? TEXT("ON") : TEXT("OFF"), NoclipSpeedMultiplier);
}

void UHellunaCheatComponent::Multicast_ApplyNoclipState_Implementation(bool bEnable, float Multiplier)
{
    NoclipSpeedMultiplier = Multiplier;
    bNoclipOn = bEnable;
    // 서버는 이미 Server_ToggleNoclip에서 적용했으니 원격(클라) 인스턴스에서만 적용
    if (GetOwner() && GetOwner()->HasAuthority()) return;
    ApplyNoclipOnOwner(bEnable);
    UE_LOG(LogTemp, Warning, TEXT("[cheatdebug] Client MulticastApplyNoclip bEnable=%d Mult=%.1f"),
        (int32)bEnable, Multiplier);
}

void UHellunaCheatComponent::Server_AdjustNoclipSpeed_Implementation(float Scale)
{
    if (Scale <= 0.f) return;
    NoclipSpeedMultiplier = FMath::Clamp(NoclipSpeedMultiplier * Scale, 1.f, 10000.f);
    UE_LOG(LogTemp, Warning, TEXT("[cheatdebug] AdjustNoclipSpeed Scale=%.2f -> Multiplier=%.1f"),
        Scale, NoclipSpeedMultiplier);

    if (bNoclipOn)
    {
        ACharacter* Char = Cast<ACharacter>(GetOwner());
        if (!Char) return;
        UCharacterMovementComponent* Move = Char->GetCharacterMovement();
        if (!Move || !bCachedMovementValid) return;

        Move->MaxFlySpeed = CachedFlySpeed * NoclipSpeedMultiplier;
        Move->MaxWalkSpeed = CachedWalkSpeed * NoclipSpeedMultiplier;
        Move->MaxAcceleration = CachedAcceleration * NoclipSpeedMultiplier;
        Move->BrakingDecelerationFlying = CachedAcceleration * NoclipSpeedMultiplier;

        // 클라 예측과 맞추기 위해 전원 전파
        Multicast_ApplyNoclipState(true, NoclipSpeedMultiplier);
    }
}

void UHellunaCheatComponent::Server_NoclipAscend_Implementation(float Direction)
{
    if (!bNoclipOn) return;
    ACharacter* Char = Cast<ACharacter>(GetOwner());
    if (!Char) return;

    // 수직 입력은 액터에 직접 속도를 추가하는 방식이 가장 단순하고 즉각적이다.
    UCharacterMovementComponent* Move = Char->GetCharacterMovement();
    if (!Move) return;

    // 수직 속도는 F5/F6(NoclipSpeedMultiplier)와 분리. 캐시 기준(원본 MaxFlySpeed) × 고정 배수 사용.
    const float BaseFly = bCachedMovementValid && CachedFlySpeed > 0.f ? CachedFlySpeed : Move->MaxFlySpeed;
    const float VerticalSpeed = BaseFly * 3.f;
    FVector Vel = Move->Velocity;
    Vel.Z = Direction * VerticalSpeed;
    Move->Velocity = Vel;
}

void UHellunaCheatComponent::ApplyNoclipOnOwner(bool bEnable)
{
    ACharacter* Char = Cast<ACharacter>(GetOwner());
    if (!Char) return;

    UCharacterMovementComponent* Move = Char->GetCharacterMovement();
    if (!Move) return;

    if (bEnable)
    {
        if (!bCachedMovementValid)
        {
            CachedWalkSpeed = Move->MaxWalkSpeed;
            CachedFlySpeed = Move->MaxFlySpeed;
            CachedAcceleration = Move->MaxAcceleration;
            bCachedMovementValid = true;
        }
        Move->SetMovementMode(MOVE_Flying);
        Move->MaxFlySpeed = CachedFlySpeed * NoclipSpeedMultiplier;
        Move->MaxWalkSpeed = CachedWalkSpeed * NoclipSpeedMultiplier;
        Move->MaxAcceleration = CachedAcceleration * NoclipSpeedMultiplier;
        Move->BrakingDecelerationFlying = CachedAcceleration * NoclipSpeedMultiplier;
        Char->SetActorEnableCollision(false);
    }
    else
    {
        if (bCachedMovementValid)
        {
            Move->MaxFlySpeed = CachedFlySpeed;
            Move->MaxWalkSpeed = CachedWalkSpeed;
            Move->MaxAcceleration = CachedAcceleration;
            Move->BrakingDecelerationFlying = CachedAcceleration;
        }
        Move->SetMovementMode(MOVE_Walking);
        Char->SetActorEnableCollision(true);
    }
}

// ─────────────────────────────────────────────────────────────────
// F4 — ItemTypeMapping DataTable에서 재료 전부 99개 지급 (서버)
// ─────────────────────────────────────────────────────────────────
UInv_InventoryComponent* UHellunaCheatComponent::FindInventoryComponent() const
{
    APawn* OwnerPawn = Cast<APawn>(GetOwner());
    if (!OwnerPawn) return nullptr;

    APlayerController* PC = Cast<APlayerController>(OwnerPawn->GetController());
    if (!PC) return nullptr;

    return PC->FindComponentByClass<UInv_InventoryComponent>();
}

void UHellunaCheatComponent::Server_GrantAllMaterials_Implementation()
{
    UWorld* World = GetWorld();
    if (!World) return;

    if (!ItemTypeMappingDataTable && DefaultItemTypeMappingPath.IsValid())
    {
        ItemTypeMappingDataTable = Cast<UDataTable>(DefaultItemTypeMappingPath.TryLoad());
    }
    if (!ItemTypeMappingDataTable)
    {
        UE_LOG(LogTemp, Warning, TEXT("[Cheat] GrantMaterials: ItemTypeMappingDataTable이 할당되지 않음"));
        return;
    }

    UInv_InventoryComponent* InvComp = FindInventoryComponent();
    if (!InvComp)
    {
        UE_LOG(LogTemp, Warning, TEXT("[Cheat] GrantMaterials: InventoryComponent 없음"));
        return;
    }

    TArray<FItemTypeToActorMapping*> AllRows;
    ItemTypeMappingDataTable->GetAllRows<FItemTypeToActorMapping>(TEXT("CheatGrantMaterials"), AllRows);

    int32 GrantedCount = 0;
    for (const FItemTypeToActorMapping* Row : AllRows)
    {
        if (!Row) continue;
        if (!Row->ItemType.IsValid()) continue;
        if (!Row->ItemType.ToString().StartsWith(MaterialTagPrefix)) continue;
        if (!Row->ItemActorClass) continue;

        // 임시 픽업 액터 스폰 → ItemComponent 추출 → InventoryComp에 추가
        FActorSpawnParameters SpawnParams;
        SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
        SpawnParams.ObjectFlags |= RF_Transient;
        SpawnParams.bNoFail = true;

        // 월드 밖 먼 지점에 스폰(충돌/보이기 방지). 어차피 PickedUp에서 파괴된다.
        const FVector FarAway(0.f, 0.f, -1'000'000.f);
        AActor* TempActor = World->SpawnActor<AActor>(Row->ItemActorClass, FarAway, FRotator::ZeroRotator, SpawnParams);
        if (!TempActor) continue;

        TempActor->SetActorHiddenInGame(true);
        TempActor->SetActorEnableCollision(false);

        UInv_ItemComponent* ItemComp = TempActor->FindComponentByClass<UInv_ItemComponent>();
        if (!ItemComp)
        {
            TempActor->Destroy();
            continue;
        }

        // 스택 한계에 맞춰 개수 제한
        int32 StackToGrant = GrantStackCount;
        if (const FInv_StackableFragment* Stackable =
            ItemComp->GetItemManifest().GetFragmentOfType<FInv_StackableFragment>())
        {
            StackToGrant = FMath::Min(StackToGrant, Stackable->GetMaxStackSize());
        }
        else
        {
            StackToGrant = 1; // 스택 불가 아이템은 1개만
        }

        InvComp->Server_AddNewItem(ItemComp, StackToGrant, 0);
        ++GrantedCount;

        // Server_AddNewItem의 Remainder==0 경로에서 ItemComp->PickedUp()이 호출되어 액터가 파괴됨.
        // 혹시 모를 누락에 대비한 가드.
        if (IsValid(TempActor) && !TempActor->IsActorBeingDestroyed())
        {
            TempActor->Destroy();
        }
    }
    UE_LOG(LogTemp, Warning, TEXT("[Cheat] GrantMaterials: %d종 지급"), GrantedCount);
}
