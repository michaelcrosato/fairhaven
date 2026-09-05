#include "Interaction/UEGT2Amenity.h"

#include "Components/BoxComponent.h"
#include "Components/StaticMeshComponent.h"
#include "GameFramework/Pawn.h"
#include "Player/UEGT2NeedsComponent.h"
#include "Player/UEGT2PlayerController.h"
#include "Rest/UEGT2RestSubsystem.h"
#include "UEGT2LogChannels.h"

#define LOCTEXT_NAMESPACE "UEGT2Amenity"

EUEGT2Activity UEGT2ActivityForAmenity(EUEGT2AmenityKind Kind)
{
	switch (Kind)
	{
	case EUEGT2AmenityKind::Food:     return EUEGT2Activity::Eat;
	case EUEGT2AmenityKind::Tavern:   return EUEGT2Activity::Tavern;
	case EUEGT2AmenityKind::Washroom: return EUEGT2Activity::Washroom;
	case EUEGT2AmenityKind::Seat:     return EUEGT2Activity::Rest;
	case EUEGT2AmenityKind::Bed:      return EUEGT2Activity::Sleep;
	// The scheduled meal, which is free for everybody because it happens at
	// home out of a larder nothing models. Without one of these the player is
	// the only person in Fairhaven who has to buy every meal they eat, which
	// is not the same life the town is living.
	case EUEGT2AmenityKind::Larder:   return EUEGT2Activity::Dinner;
	case EUEGT2AmenityKind::Work:     return EUEGT2Activity::Work;
	case EUEGT2AmenityKind::Market:   return EUEGT2Activity::Market;
	case EUEGT2AmenityKind::Worship:  return EUEGT2Activity::Worship;
	default:                          return EUEGT2Activity::Idle;
	}
}

const TCHAR* UEGT2AmenityKindName(EUEGT2AmenityKind Kind)
{
	switch (Kind)
	{
	case EUEGT2AmenityKind::Food:     return TEXT("Food");
	case EUEGT2AmenityKind::Tavern:   return TEXT("Tavern");
	case EUEGT2AmenityKind::Washroom: return TEXT("Washroom");
	case EUEGT2AmenityKind::Seat:     return TEXT("Seat");
	case EUEGT2AmenityKind::Bed:      return TEXT("Bed");
	case EUEGT2AmenityKind::Larder:   return TEXT("Larder");
	case EUEGT2AmenityKind::Work:     return TEXT("Work");
	case EUEGT2AmenityKind::Market:   return TEXT("Market");
	case EUEGT2AmenityKind::Worship:  return TEXT("Worship");
	default:                          return TEXT("None");
	}
}

// ---------------------------------------------------------------------------
AUEGT2Amenity::AUEGT2Amenity()
{
	PrimaryActorTick.bCanEverTick = false;

	// The base class makes a mesh component and roots the actor on it. This
	// class has no mesh - see the header - so it is switched off entirely
	// rather than left as an empty thing with a collision profile on it.
	if (MeshComponent)
	{
		MeshComponent->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		MeshComponent->SetHiddenInGame(true);
		MeshComponent->SetVisibility(false);
		MeshComponent->SetMobility(EComponentMobility::Static);
	}

	Volume = CreateDefaultSubobject<UBoxComponent>(TEXT("Volume"));
	Volume->SetupAttachment(MeshComponent);
	Volume->SetMobility(EComponentMobility::Static);
	Volume->SetBoxExtent(FVector(85.0f, 85.0f, 115.0f));
	// Chest height above whatever the amenity was placed on, which is the
	// ground: a volume centred on the actor is half buried and the probe from
	// eye level sails over it.
	Volume->SetRelativeLocation(FVector(0.0f, 0.0f, 115.0f));
	Volume->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	// WorldDynamic, and this is load bearing. AUEGT2NPCActor::GroundZAt traces
	// for the floor with LineTraceSingleByObjectType(ECC_WorldStatic), and an
	// object-type query matches on the object type alone - the per-channel
	// responses below do not enter into it. A WorldStatic volume here would be
	// a 2.3 m invisible box standing on every bench, stall front and doorstep
	// in both settlements, directly in the path of the trace that decides where
	// people's feet go. WorldDynamic is invisible to that query and still
	// perfectly visible to the player's Visibility sweep, which reads the
	// response and not the type.
	Volume->SetCollisionObjectType(ECC_WorldDynamic);
	Volume->SetCollisionResponseToAllChannels(ECR_Ignore);
	Volume->SetCollisionResponseToChannel(ECC_Visibility, ECR_Block);
	Volume->SetGenerateOverlapEvents(false);
	Volume->SetHiddenInGame(true);

	PromptText = LOCTEXT("Use", "Use");
}

void AUEGT2Amenity::ConfigureAmenity(EUEGT2AmenityKind InKind, const FString& InVenueName,
	EUEGT2NPCRole InJobRole)
{
	// FString rather than FText for the same reason ConfigureNPC takes one:
	// the content build hands these over from Python, and unreal.Text is not
	// available in every engine build the stage may run under.
	Kind = InKind;
	VenueName = InVenueName.IsEmpty() ? FText::GetEmpty() : FText::FromString(InVenueName);
	JobRole = InJobRole;
}

void AUEGT2Amenity::SetUseRange(float Range)
{
	UseRange = FMath::Max(120.0f, Range);
}

void AUEGT2Amenity::SetVolumeExtent(const FVector& Extent)
{
	if (Volume)
	{
		Volume->SetBoxExtent(Extent);
		Volume->SetRelativeLocation(FVector(0.0f, 0.0f, Extent.Z));
	}
}

FVector AUEGT2Amenity::GetInteractionPoint() const
{
	return Volume ? Volume->GetComponentLocation() : GetActorLocation();
}

FText AUEGT2Amenity::MoneyNote() const
{
	const float Wage = UEGT2WageFor(JobRole, GetActivity());
	if (Wage > 0.0f)
	{
		return FText::Format(LOCTEXT("Pays", "  (pays {0} an hour)"),
			FText::AsNumber(FMath::RoundToInt(Wage)));
	}
	// Price is asked at the player's own trade only for Market, and a place
	// that sells is never the same actor as a place that hires, so the venue's
	// own role is the right one to ask with.
	const float Price = UEGT2PriceFor(JobRole, GetActivity());
	if (Price > 0.0f)
	{
		return FText::Format(LOCTEXT("Costs", "  ({0} an hour)"),
			FText::AsNumber(FMath::RoundToInt(Price)));
	}
	return FText::GetEmpty();
}

FText AUEGT2Amenity::GetInteractionPrompt(const AActor* Interactor) const
{
	const UUEGT2NeedsComponent* Life = Interactor
		? Interactor->FindComponentByClass<UUEGT2NeedsComponent>() : nullptr;
	if (Life && Life->IsUsing(this))
	{
		switch (Kind)
		{
		case EUEGT2AmenityKind::Seat:   return LOCTEXT("StopSit", "Stand up");
		case EUEGT2AmenityKind::Bed:    return LOCTEXT("StopSleep", "Get up");
		case EUEGT2AmenityKind::Work:   return LOCTEXT("StopWork", "Knock off");
		case EUEGT2AmenityKind::Food:
		case EUEGT2AmenityKind::Larder: return LOCTEXT("StopEat", "Stop eating");
		case EUEGT2AmenityKind::Tavern: return LOCTEXT("StopDrink", "Settle up");
		case EUEGT2AmenityKind::Market: return LOCTEXT("StopStall", "Leave the stall");
		default:                        return LOCTEXT("StopUse", "Stop");
		}
	}

	const FText Named = VenueName.IsEmpty() ? FText::GetEmpty() : VenueName;
	FText Verb;
	switch (Kind)
	{
	case EUEGT2AmenityKind::Food:
		Verb = Named.IsEmpty() ? LOCTEXT("EatHere", "Eat here")
			: FText::Format(LOCTEXT("EatAt", "Eat at {0}"), Named);
		break;
	case EUEGT2AmenityKind::Tavern:
		Verb = Named.IsEmpty() ? LOCTEXT("DrinkHere", "Take a drink")
			: FText::Format(LOCTEXT("DrinkAt", "Take a drink at {0}"), Named);
		break;
	case EUEGT2AmenityKind::Washroom: Verb = LOCTEXT("UseWash", "Use the washroom"); break;
	case EUEGT2AmenityKind::Seat:     Verb = LOCTEXT("SitDown", "Sit down"); break;
	case EUEGT2AmenityKind::Bed:
	{
		const UUEGT2RestSubsystem* Rest = UUEGT2RestSubsystem::Get(GetWorld());
		Verb = Rest && Rest->IsEnabled() ? LOCTEXT("SleepUntil", "Sleep until...") : LOCTEXT("SleepHere", "Sleep");
		break;
	}
	case EUEGT2AmenityKind::Larder:   Verb = LOCTEXT("EatIn", "Eat at home"); break;
	case EUEGT2AmenityKind::Work:
		Verb = Named.IsEmpty() ? LOCTEXT("WorkHere", "Put in a shift")
			: FText::Format(LOCTEXT("WorkAt", "Work at {0}"), Named);
		break;
	case EUEGT2AmenityKind::Market:
		// The one place that is work from one side of the counter and shopping
		// from the other, exactly as it is for the town: UEGT2WageFor pays a
		// merchant for the same activity it charges everybody else for.
		Verb = UEGT2WageFor(JobRole, EUEGT2Activity::Market) > 0.0f
			? LOCTEXT("MindStall", "Mind the stall")
			: (Named.IsEmpty() ? LOCTEXT("ShopHere", "Browse the market")
				: FText::Format(LOCTEXT("ShopAt", "Browse {0}"), Named));
		break;
	case EUEGT2AmenityKind::Worship: Verb = LOCTEXT("Worship", "Sit in the church"); break;
	default:                         Verb = PromptText; break;
	}
	return FText::Format(LOCTEXT("PromptMoney", "{0}{1}"), Verb, MoneyNote());
}

void AUEGT2Amenity::OnInteract(AActor* Interactor)
{
	UUEGT2NeedsComponent* Life = Interactor
		? Interactor->FindComponentByClass<UUEGT2NeedsComponent>() : nullptr;
	if (!Life)
	{
		// Anything without needs cannot use a bakehouse. Not an error: a dev
		// camera or a future companion may well probe one.
		UE_LOG(LogUEGT2Interaction, Verbose, TEXT("%s used %s with no needs component."),
			Interactor ? *Interactor->GetName() : TEXT("nobody"), *GetName());
		return;
	}
	UUEGT2RestSubsystem* Rest = UUEGT2RestSubsystem::Get(GetWorld());
	if (Kind == EUEGT2AmenityKind::Bed && !Life->IsUsing(this) && Rest && Rest->IsEnabled())
	{
		const APawn* Pawn = Cast<APawn>(Interactor);
		AUEGT2PlayerController* PC = Pawn ? Cast<AUEGT2PlayerController>(Pawn->GetController()) : nullptr;
		FText Reason;
		if (!Rest->CanSleepAt(PC, this, Reason)) { ShowHudMessage(Interactor, Reason); }
		else if (!PC->OpenRestPanel(this)) { ShowHudMessage(Interactor, LOCTEXT("CannotPause", "The sleep panel could not be opened.")); }
		return;
	}
	Life->BeginActivity(GetActivity(), this, VenueName, JobRole, UseRange);
}

#undef LOCTEXT_NAMESPACE
