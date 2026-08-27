#include "Interaction/UEGT2InteractableActor.h"

#include "Components/StaticMeshComponent.h"
#include "Kismet/GameplayStatics.h"
#include "Sound/SoundBase.h"
#include "Engine/StaticMesh.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "UEGT2LogChannels.h"
#include "UI/UEGT2HUD.h"

AUEGT2InteractableActor::AUEGT2InteractableActor()
{
	PrimaryActorTick.bCanEverTick = false;

	MeshComponent = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Mesh"));
	RootComponent = MeshComponent;
	MeshComponent->SetMobility(EComponentMobility::Movable);
	MeshComponent->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
	MeshComponent->SetCollisionProfileName(TEXT("BlockAllDynamic"));
	// The interaction probe traces on Visibility; make sure we are seen.
	MeshComponent->SetCollisionResponseToChannel(ECC_Visibility, ECR_Block);
}

void AUEGT2InteractableActor::SetInteractableMesh(UStaticMesh* Mesh)
{
	if (MeshComponent && Mesh)
	{
		MeshComponent->SetStaticMesh(Mesh);
	}
}

FText AUEGT2InteractableActor::GetInteractionPrompt(const AActor* Interactor) const
{
	return PromptText;
}

bool AUEGT2InteractableActor::CanInteract(const AActor* Interactor) const
{
	return !(bSingleUse && bUsed);
}

void AUEGT2InteractableActor::Interact(AActor* Interactor)
{
	if (!CanInteract(Interactor))
	{
		return;
	}
	bUsed = true;

	if (USoundBase* Click = LoadObject<USoundBase>(nullptr, TEXT("/Game/Fairhaven/Audio/S_Interact")))
	{
		UGameplayStatics::PlaySoundAtLocation(this, Click, GetActorLocation(), 0.6f);
	}
	OnInteract(Interactor);
}

void AUEGT2InteractableActor::SetInteractionFocus(bool bInFocused)
{
	bFocused = bInFocused;
	if (MeshComponent)
	{
		// Ready for a post-process outline; harmless without one.
		MeshComponent->SetRenderCustomDepth(bInFocused);
	}
}

FVector AUEGT2InteractableActor::GetInteractionPoint() const
{
	return MeshComponent ? MeshComponent->Bounds.Origin : GetActorLocation();
}

void AUEGT2InteractableActor::ShowHudMessage(AActor* Interactor, const FText& Message,
	float Duration) const
{
	const APawn* Pawn = Cast<APawn>(Interactor);
	const APlayerController* Controller = Pawn ? Cast<APlayerController>(Pawn->GetController()) : nullptr;
	if (AUEGT2HUD* Hud = Controller ? Cast<AUEGT2HUD>(Controller->GetHUD()) : nullptr)
	{
		Hud->ShowMessage(Message, Duration);
	}
	else
	{
		UE_LOG(LogUEGT2Interaction, Log, TEXT("%s"), *Message.ToString());
	}
}
