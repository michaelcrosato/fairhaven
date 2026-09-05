#include "Contracts/UEGT2SurveyContract.h"

#include "Components/StaticMeshComponent.h"
#include "Contracts/UEGT2SurveyContractSubsystem.h"
#include "Player/UEGT2Character.h"
#include "Player/UEGT2PlayerController.h"

#define LOCTEXT_NAMESPACE "UEGT2SurveyContractBoard"

AUEGT2SurveyContract::AUEGT2SurveyContract()
{
	PromptText = LOCTEXT("Read", "Read town survey contract");
	// This new sign is query-only. It neither obstructs the player nor becomes
	// an elevated floor for NPC WorldStatic ground probes.
	MeshComponent->SetMobility(EComponentMobility::Static);
	MeshComponent->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	MeshComponent->SetCollisionObjectType(ECC_WorldDynamic);
	MeshComponent->SetCollisionResponseToAllChannels(ECR_Ignore);
	MeshComponent->SetCollisionResponseToChannel(ECC_Visibility, ECR_Block);
	MeshComponent->SetGenerateOverlapEvents(false);
}

bool AUEGT2SurveyContract::CanInteract(const AActor* Interactor) const
{
	const AUEGT2Character* Player = Cast<AUEGT2Character>(Interactor);
	const AUEGT2PlayerController* Controller = Player ? Cast<AUEGT2PlayerController>(Player->GetController()) : nullptr;
	if (!IsValid(Player) || Player->IsActorBeingDestroyed() || Player->GetWorld() != GetWorld()
		|| !IsValid(Controller) || !Controller->IsLocalController() || Controller->GetPawn() != Player
		|| Controller->GetMenuState() != EUEGT2MenuState::None || IsActorBeingDestroyed()) { return false; }
	const double DistanceSquared = FVector::DistSquared(Player->GetActorLocation(), GetActorLocation());
	return FMath::IsFinite(DistanceSquared) && FMath::IsFinite(UseRange) && UseRange > 0.0f
		&& DistanceSquared <= FMath::Square(static_cast<double>(UseRange));
}

void AUEGT2SurveyContract::OnInteract(AActor* Interactor)
{
	const AUEGT2Character* Player = Cast<AUEGT2Character>(Interactor);
	AUEGT2PlayerController* Controller = Player ? Cast<AUEGT2PlayerController>(Player->GetController()) : nullptr;
	const UUEGT2SurveyContractSubsystem* Contract = UUEGT2SurveyContractSubsystem::Get(GetWorld());
	FText Reason;
	if (Contract && Contract->CanOpenAt(Controller, this, Reason) && Controller->OpenSurveyContract(this)) { return; }
	ShowHudMessage(Interactor, Reason.IsEmpty()
		? LOCTEXT("Unavailable", "Town Survey Contract is unavailable here.") : Reason);
}

#undef LOCTEXT_NAMESPACE
