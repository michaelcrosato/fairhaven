#include "Interaction/UEGT2WorldInteractables.h"

#include "Components/PointLightComponent.h"
#include "Components/StaticMeshComponent.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "UEGT2LogChannels.h"

#define LOCTEXT_NAMESPACE "UEGT2Interactables"

// ---------------------------------------------------------------------------
// Sign
// ---------------------------------------------------------------------------
AUEGT2Sign::AUEGT2Sign()
{
	PromptText = LOCTEXT("ReadSign", "Read");
	SignText = LOCTEXT("DefaultSign", "Fairhaven");
}

void AUEGT2Sign::OnInteract(AActor* Interactor)
{
	ShowHudMessage(Interactor, SignText, 5.0f);
	UE_LOG(LogUEGT2Interaction, Log, TEXT("Sign read: %s"), *SignText.ToString());
}

// ---------------------------------------------------------------------------
// Door
// ---------------------------------------------------------------------------
AUEGT2Door::AUEGT2Door()
{
	PrimaryActorTick.bCanEverTick = true;
	PromptText = LOCTEXT("OpenDoor", "Open door");
}

FText AUEGT2Door::GetInteractionPrompt(const AActor* Interactor) const
{
	return bOpen ? LOCTEXT("CloseDoor", "Close door") : LOCTEXT("OpenDoor2", "Open door");
}

void AUEGT2Door::OnInteract(AActor* Interactor)
{
	if (CurrentAngle == 0.0f && !bOpen)
	{
		ClosedYaw = GetActorRotation().Yaw;
	}
	bOpen = !bOpen;
	SetActorTickEnabled(true);
	UE_LOG(LogUEGT2Interaction, Log, TEXT("Door %s: %s"),
		*GetName(), bOpen ? TEXT("opening") : TEXT("closing"));
}

void AUEGT2Door::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	const float Target = bOpen ? OpenAngle : 0.0f;
	if (FMath::IsNearlyEqual(CurrentAngle, Target, 0.15f))
	{
		CurrentAngle = Target;
		SetActorTickEnabled(false);
		return;
	}
	CurrentAngle = FMath::FInterpConstantTo(CurrentAngle, Target, DeltaSeconds, SwingSpeed);

	FRotator Rotation = GetActorRotation();
	Rotation.Yaw = ClosedYaw + CurrentAngle;
	SetActorRotation(Rotation);
}

// ---------------------------------------------------------------------------
// Lamp
// ---------------------------------------------------------------------------
AUEGT2Lamp::AUEGT2Lamp()
{
	PromptText = LOCTEXT("ToggleLamp", "Toggle lamp");

	Light = CreateDefaultSubobject<UPointLightComponent>(TEXT("Light"));
	Light->SetupAttachment(MeshComponent);
	Light->SetRelativeLocation(FVector(0.0f, 0.0f, 470.0f));
	Light->SetMobility(EComponentMobility::Movable);
	// Unshadowed on purpose: dozens of these should cost close to nothing.
	Light->SetCastShadows(false);
	Light->SetLightColor(FLinearColor(1.0f, 0.86f, 0.62f));
}

void AUEGT2Lamp::BeginPlay()
{
	Super::BeginPlay();
	bOn = bStartsOn;
	ApplyState();
}

FText AUEGT2Lamp::GetInteractionPrompt(const AActor* Interactor) const
{
	return bOn ? LOCTEXT("LampOff", "Turn lamp off") : LOCTEXT("LampOn", "Turn lamp on");
}

void AUEGT2Lamp::ApplyState()
{
	if (Light)
	{
		Light->SetIntensity(bOn ? Brightness : 0.0f);
		Light->SetAttenuationRadius(Radius);
		Light->SetVisibility(bOn);
	}
}

void AUEGT2Lamp::OnInteract(AActor* Interactor)
{
	bOn = !bOn;
	ApplyState();
	UE_LOG(LogUEGT2Interaction, Log, TEXT("Lamp %s switched %s"),
		*GetName(), bOn ? TEXT("on") : TEXT("off"));
}

// ---------------------------------------------------------------------------
// Pickup
// ---------------------------------------------------------------------------
AUEGT2Pickup::AUEGT2Pickup()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.bStartWithTickEnabled = false;
	PromptText = LOCTEXT("PickUp", "Pick up");

	MeshComponent->SetMobility(EComponentMobility::Movable);
	MeshComponent->SetCollisionProfileName(TEXT("PhysicsActor"));
	MeshComponent->SetCollisionResponseToChannel(ECC_Visibility, ECR_Block);
	// Physics state is set up in BeginPlay, not here: touching body instances
	// during CDO construction warns and the mesh is not assigned yet anyway.
}

void AUEGT2Pickup::BeginPlay()
{
	Super::BeginPlay();
	MeshComponent->SetMassOverrideInKg(NAME_None, 18.0f, true);
	MeshComponent->SetLinearDamping(0.4f);
	MeshComponent->SetAngularDamping(1.2f);
	MeshComponent->SetSimulatePhysics(true);
}

FText AUEGT2Pickup::GetInteractionPrompt(const AActor* Interactor) const
{
	return IsCarried() ? LOCTEXT("Throw", "Throw") : LOCTEXT("PickUp2", "Pick up");
}

void AUEGT2Pickup::OnInteract(AActor* Interactor)
{
	if (IsCarried())
	{
		Drop(true);
		return;
	}
	Carrier = Interactor;
	MeshComponent->SetSimulatePhysics(false);
	MeshComponent->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	SetActorTickEnabled(true);
	UE_LOG(LogUEGT2Interaction, Log, TEXT("Picked up %s"), *GetName());
}

void AUEGT2Pickup::Drop(bool bThrow)
{
	AActor* Thrower = Carrier;
	Carrier = nullptr;
	SetActorTickEnabled(false);

	MeshComponent->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
	MeshComponent->SetSimulatePhysics(true);

	if (bThrow && Thrower)
	{
		FVector ViewLocation;
		FRotator ViewRotation;
		if (const APawn* Pawn = Cast<APawn>(Thrower))
		{
			if (const AController* Controller = Pawn->GetController())
			{
				Controller->GetPlayerViewPoint(ViewLocation, ViewRotation);
				MeshComponent->AddImpulse(ViewRotation.Vector() * ThrowImpulse);
			}
		}
	}
	UE_LOG(LogUEGT2Interaction, Log, TEXT("Released %s (%s)"),
		*GetName(), bThrow ? TEXT("thrown") : TEXT("dropped"));
}

void AUEGT2Pickup::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	if (!Carrier)
	{
		return;
	}

	const APawn* Pawn = Cast<APawn>(Carrier);
	const AController* Controller = Pawn ? Pawn->GetController() : nullptr;
	if (!Controller)
	{
		Drop(false);
		return;
	}

	FVector ViewLocation;
	FRotator ViewRotation;
	Controller->GetPlayerViewPoint(ViewLocation, ViewRotation);
	const FVector Target = ViewLocation + ViewRotation.Vector() * CarryDistance;

	// Smoothed so carrying feels physical rather than welded to the camera.
	const FVector NewLocation = FMath::VInterpTo(GetActorLocation(), Target, DeltaSeconds, 18.0f);
	SetActorLocation(NewLocation, true);

	// Drop it if the player walks it into something or gets too far away.
	if (FVector::DistSquared(NewLocation, ViewLocation) > FMath::Square(CarryDistance * 3.0f))
	{
		Drop(false);
	}
}

// ---------------------------------------------------------------------------
// Landmark
// ---------------------------------------------------------------------------
int32 AUEGT2Landmark::DiscoveredCount = 0;
int32 AUEGT2Landmark::TotalCount = 0;

AUEGT2Landmark::AUEGT2Landmark()
{
	PromptText = LOCTEXT("Survey", "Survey");
	LandmarkName = LOCTEXT("DefaultLandmark", "Landmark");
	bSingleUse = true;
	MeshComponent->SetMobility(EComponentMobility::Static);
}

void AUEGT2Landmark::BeginPlay()
{
	Super::BeginPlay();
	++TotalCount;
}

void AUEGT2Landmark::EndPlay(const EEndPlayReason::Type Reason)
{
	TotalCount = 0;
	DiscoveredCount = 0;
	Super::EndPlay(Reason);
}

FText AUEGT2Landmark::GetInteractionPrompt(const AActor* Interactor) const
{
	return FText::Format(LOCTEXT("SurveyNamed", "Survey {0}"), LandmarkName);
}

void AUEGT2Landmark::OnInteract(AActor* Interactor)
{
	++DiscoveredCount;
	const FText Message = FText::Format(
		LOCTEXT("Discovered", "{0} surveyed  ({1} of {2})"),
		LandmarkName, FText::AsNumber(DiscoveredCount), FText::AsNumber(TotalCount));
	ShowHudMessage(Interactor, Message, 5.0f);
	UE_LOG(LogUEGT2Interaction, Log, TEXT("Landmark surveyed: %s (%d/%d)"),
		*LandmarkName.ToString(), DiscoveredCount, TotalCount);
}

#undef LOCTEXT_NAMESPACE
