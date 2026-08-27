#include "Interaction/UEGT2InteractionComponent.h"

#include "DrawDebugHelpers.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "Interaction/UEGT2Interactable.h"
#include "UEGT2LogChannels.h"

namespace
{
	static TAutoConsoleVariable<int32> CVarDrawInteraction(
		TEXT("uegt2.Debug.DrawInteraction"), 0,
		TEXT("Draw the interaction probe from the player camera."),
		ECVF_Cheat);
}

UUEGT2InteractionComponent::UUEGT2InteractionComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	// 20 Hz is far more than enough and keeps the probe off the render thread's back.
	PrimaryComponentTick.TickInterval = 0.05f;
}

bool UUEGT2InteractionComponent::GetViewPoint(FVector& OutLocation, FRotator& OutRotation) const
{
	const APawn* Pawn = Cast<APawn>(GetOwner());
	if (!Pawn)
	{
		return false;
	}
	if (const AController* Controller = Pawn->GetController())
	{
		Controller->GetPlayerViewPoint(OutLocation, OutRotation);
		return true;
	}
	return false;
}

AActor* UUEGT2InteractionComponent::ProbeForInteractable(FText& OutPrompt) const
{
	FVector ViewLocation;
	FRotator ViewRotation;
	if (!GetViewPoint(ViewLocation, ViewRotation))
	{
		return nullptr;
	}

	const FVector End = ViewLocation + ViewRotation.Vector() * Reach;

	FCollisionQueryParams Params(SCENE_QUERY_STAT(UEGT2Interaction), false, GetOwner());
	Params.bTraceComplex = false;

	FHitResult Hit;
	const bool bHit = GetWorld()->SweepSingleByChannel(
		Hit, ViewLocation, End, FQuat::Identity, ECC_Visibility,
		FCollisionShape::MakeSphere(ProbeRadius), Params);

	if (CVarDrawInteraction.GetValueOnGameThread() != 0)
	{
		DrawDebugLine(GetWorld(), ViewLocation, End,
			bHit ? FColor::Green : FColor::Silver, false, 0.06f, 0, 0.6f);
	}

	if (!bHit)
	{
		return nullptr;
	}

	// Walk up the attachment chain: props are often meshes under an owning actor.
	for (AActor* Candidate = Hit.GetActor(); Candidate; Candidate = Candidate->GetAttachParentActor())
	{
		const IUEGT2Interactable* Interactable = Cast<IUEGT2Interactable>(Candidate);
		if (!Interactable)
		{
			continue;
		}
		if (!Interactable->CanInteract(GetOwner()))
		{
			return nullptr;
		}
		OutPrompt = Interactable->GetInteractionPrompt(GetOwner());
		return Candidate;
	}
	return nullptr;
}

void UUEGT2InteractionComponent::UpdateFocus()
{
	FText Prompt;
	AActor* Found = ProbeForInteractable(Prompt);

	if (Found == FocusedActor.Get())
	{
		FocusedPrompt = Prompt;
		return;
	}

	if (AActor* Previous = FocusedActor.Get())
	{
		if (IUEGT2Interactable* Interactable = Cast<IUEGT2Interactable>(Previous))
		{
			Interactable->SetInteractionFocus(false);
		}
	}

	FocusedActor = Found;
	FocusedPrompt = Prompt;

	if (Found)
	{
		if (IUEGT2Interactable* Interactable = Cast<IUEGT2Interactable>(Found))
		{
			Interactable->SetInteractionFocus(true);
		}
		UE_LOG(LogUEGT2Interaction, Verbose, TEXT("Focus: %s (%s)"),
			*Found->GetName(), *Prompt.ToString());
	}

	OnFocusChanged.Broadcast(Found, FocusedPrompt);
}

void UUEGT2InteractionComponent::TickComponent(float DeltaTime, ELevelTick TickType,
	FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
	UpdateFocus();
}

bool UUEGT2InteractionComponent::TryInteract()
{
	AActor* Target = FocusedActor.Get();
	if (!Target)
	{
		return false;
	}
	IUEGT2Interactable* Interactable = Cast<IUEGT2Interactable>(Target);
	if (!Interactable || !Interactable->CanInteract(GetOwner()))
	{
		return false;
	}

	UE_LOG(LogUEGT2Interaction, Log, TEXT("Interacted with %s."), *Target->GetName());
	Interactable->Interact(GetOwner());

	// The interaction may have changed the prompt (a door that just opened).
	UpdateFocus();
	return true;
}
