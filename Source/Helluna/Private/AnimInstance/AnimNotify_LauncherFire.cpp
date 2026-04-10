// Capstone Project Helluna - Launcher Fire Notify

#include "AnimInstance/AnimNotify_LauncherFire.h"
#include "Character/HellunaHeroCharacter.h"
#include "Weapon/HeroWeapon_GunBase.h"

void UAnimNotify_LauncherFire::Notify(USkeletalMeshComponent* MeshComp, UAnimSequenceBase* Animation,
	const FAnimNotifyEventReference& EventReference)
{
	Super::Notify(MeshComp, Animation, EventReference);

	if (!MeshComp) return;

	AHellunaHeroCharacter* Hero = Cast<AHellunaHeroCharacter>(MeshComp->GetOwner());
	if (!Hero) return;

	AHeroWeapon_GunBase* Weapon = Cast<AHeroWeapon_GunBase>(Hero->GetCurrentWeapon());
	if (!Weapon) return;

	// 서버: 발사 (GunBase→라인트레이스 / Launcher→프로젝타일)
	if (Hero->HasAuthority())
	{
		AController* Controller = Hero->GetController();
		if (Controller)
		{
			Weapon->Fire(Controller);
		}
	}

	// 로컬: 반동 적용 (발사 타이밍에 맞춰서)
	if (Hero->IsLocallyControlled())
	{
		Weapon->ApplyRecoil(Hero);
	}
}
