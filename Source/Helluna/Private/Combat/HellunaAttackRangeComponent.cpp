// Capstone Project Helluna

#include "Combat/HellunaAttackRangeComponent.h"

UHellunaAttackRangeComponent::UHellunaAttackRangeComponent()
{
	// Tick 꺼둠 — 물리 엔진의 overlap system 이 알아서 처리
	PrimaryComponentTick.bCanEverTick          = false;
	PrimaryComponentTick.bStartWithTickEnabled = false;

	// 초기: 공격 안 할 때 완전 꺼짐 (broad-phase 트리 미등록)
	SetCollisionEnabled(ECollisionEnabled::NoCollision);

	// 활성화 시 사용할 채널 프리셋.
	// - Pawn: Player/Turret 같은 Pawn 계열 (Capsule 이 보통 ECC_Pawn)
	// 우주선/타워의 DynamicMesh(WorldStatic, BlockAll) 는 Overlap 이벤트를 못 받음 (Block 우선).
	// 해결은 HellunaBaseResourceUsingObject 쪽에서 DynamicMesh 가 WorldDynamic(=이 박스)에
	// Overlap 응답하도록 설정해 줌. 박스 쪽은 Pawn 만 켜두고, WorldDynamic 은 Ignore 유지해도
	// 상대가 Overlap 응답이면 overlap event 는 발동됨.
	SetCollisionObjectType(ECC_WorldDynamic);
	SetCollisionResponseToAllChannels(ECR_Ignore);
	SetCollisionResponseToChannel(ECC_Pawn, ECR_Overlap);

	// Overlap 이벤트 기본 ON (활성화만 Collision 이 막고 있음)
	SetGenerateOverlapEvents(true);
	SetCanEverAffectNavigation(false);

	// 시각화 기본값.
	// SetBoxExtent() 는 내부에서 BodySetup 을 NewObject 로 만들려 시도해
	// UObject 생성자 체인에서 호출 시 크래시. BoxExtent 멤버에 직접 할당.
	BoxExtent      = FVector(40.f, 40.f, 40.f);
	ShapeColor     = FColor(255, 50, 50, 255); // 빨강
	LineThickness  = 1.5f;

	// 복제 불필요 — 서버만 Collision 제어, 클라는 시각화만
	SetIsReplicatedByDefault(false);
}
