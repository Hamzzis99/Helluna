// Gihyeon's Inventory Project

#include "Items/Components/Inv_ItemComponent.h"
#include "Net/UnrealNetwork.h"
#include "Components/WidgetComponent.h"
#include "Widgets/HUD/Inv_InteractPromptWidget.h"

// Sets default values for this component's properties
UInv_ItemComponent::UInv_ItemComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
	PickupMessage = FString("E - Pick Up Item"); // 기본 픽업 메시지 설정 이거 한글로 되는지 한 번 나중에 검사 TEXT를 쓰라는데..
	SetIsReplicatedByDefault(true); // 기본적으로 복제 설정 (개수 버그 수정 시키는 것.)
}

void UInv_ItemComponent::BeginPlay()
{
	Super::BeginPlay();

	// InteractWidgetClass 미설정 시 3D 위젯 생성 안 함 → 기존 2D HUD fallback
	if (!InteractWidgetClass) return;

	AActor* OwnerActor = GetOwner();
	if (!OwnerActor) return;

	// 데디케이티드 서버에서는 위젯 불필요
	UWorld* World = GetWorld();
	if (!World) return;
	if (World->GetNetMode() == NM_DedicatedServer) return;

	// Owner의 RootComponent 검증
	USceneComponent* RootComp = OwnerActor->GetRootComponent();
	if (!RootComp)
	{
		UE_LOG(LogTemp, Warning, TEXT("[Phase18] InteractWidgetComp 생성 실패: Owner=%s RootComponent 없음"), *GetNameSafe(OwnerActor));
		return;
	}

	// WidgetComponent 동적 생성
	InteractWidgetComp = NewObject<UWidgetComponent>(OwnerActor, UWidgetComponent::StaticClass(), TEXT("InteractWidgetComp"));
	if (!InteractWidgetComp) return;

	InteractWidgetComp->SetupAttachment(RootComp);
	InteractWidgetComp->SetRelativeLocation(FVector(0.f, 0.f, 80.f));
	InteractWidgetComp->SetWidgetSpace(EWidgetSpace::Screen);
	InteractWidgetComp->SetDrawSize(FVector2D(250.f, 50.f));
	InteractWidgetComp->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	InteractWidgetComp->SetWidgetClass(InteractWidgetClass);
	InteractWidgetComp->SetVisibility(false);
	InteractWidgetComp->RegisterComponent();

	// 위젯 인스턴스 캐시 + 초기 텍스트 설정
	InteractWidgetInstance = Cast<UInv_InteractPromptWidget>(InteractWidgetComp->GetWidget());
	if (InteractWidgetInstance)
	{
		// PickupMessage에서 키 접두사 제거 ("E - 무기 집기!" → "무기 집기!")
		FString DisplayName = PickupMessage;
		int32 DashIdx = INDEX_NONE;
		if (DisplayName.FindChar(TEXT('-'), DashIdx) && DashIdx <= 3)
		{
			DisplayName = DisplayName.Mid(DashIdx + 1).TrimStart();
		}
		InteractWidgetInstance->SetItemName(DisplayName);
		InteractWidgetInstance->SetActionText(TEXT("줍기"));
		InteractWidgetInstance->ResetState();
	}

	UE_LOG(LogTemp, Log, TEXT("[Phase18] InteractWidgetComp 생성 성공: Owner=%s, WidgetClass=%s"),
		*GetNameSafe(OwnerActor), *GetNameSafe(InteractWidgetClass));
}

void UInv_ItemComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	DOREPLIFETIME(ThisClass, ItemManifest); // 아이템 매니페스트 복제 설정
}

void UInv_ItemComponent::PickedUp()
{
	// 3D 위젯 정리 (Destroy 전)
	HideInteractWidget();
	InteractWidgetComp = nullptr;
	InteractWidgetInstance = nullptr;

	OnPickedUp(); // 픽업 이벤트 호출
	GetOwner()->Destroy(); // 아이템을 줍게 되면 남아있는 떨어져있는 흔적을 지우게 만드는 것.
}

bool UInv_ItemComponent::ShowInteractWidget(const FString& KeyName)
{
	if (!InteractWidgetComp) return false;
	if (bInteractWidgetVisible) return true; // 이미 표시 중

	// 실제 바인딩 키 이름 설정
	if (InteractWidgetInstance)
	{
		InteractWidgetInstance->SetKeyText(KeyName);
	}

	InteractWidgetComp->SetVisibility(true);
	bInteractWidgetVisible = true;

	UE_LOG(LogTemp, Log, TEXT("[Phase18] 3D 위젯 표시: %s, Key=%s"), *GetNameSafe(GetOwner()), *KeyName);
	return true;
}

void UInv_ItemComponent::HideInteractWidget()
{
	if (!InteractWidgetComp || !bInteractWidgetVisible) return;

	InteractWidgetComp->SetVisibility(false);
	bInteractWidgetVisible = false;

	UE_LOG(LogTemp, Log, TEXT("[Phase18] 3D 위젯 숨김: %s"), *GetNameSafe(GetOwner()));
}

void UInv_ItemComponent::InitItemManifest(FInv_ItemManifest CopyOfManifest)
{
	ItemManifest = CopyOfManifest; // 아이템 매니페스트 복사본 설정
}
