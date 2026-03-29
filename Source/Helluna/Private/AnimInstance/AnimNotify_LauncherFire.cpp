// Capstone Project Helluna - Launcher Fire Notify

#include "AnimInstance/AnimNotify_LauncherFire.h"
#include "Character/HellunaHeroCharacter.h"
#include "Weapon/HeroWeapon_Launcher.h"

void UAnimNotify_LauncherFire::Notify(USkeletalMeshComponent* MeshComp, UAnimSequenceBase* Animation,
	const FAnimNotifyEventReference& EventReference)
{
	Super::Notify(MeshComp, Animation, EventReference);

	if (!MeshComp) return;

	AHellunaHeroCharacter* Hero = Cast<AHellunaHeroCharacter>(MeshComp->GetOwner());
	if (!Hero) return;

	// 서버에서만 실제 발사
	if (!Hero->HasAuthority()) return;

	AHeroWeapon_Launcher* Launcher = Cast<AHeroWeapon_Launcher>(Hero->GetCurrentWeapon());
	if (!Launcher) return;

	AController* Controller = Hero->GetController();
	if (!Controller) return;

	Launcher->Fire(Controller);
}
