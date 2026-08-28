#include "NPC/UEGT2NPCActor.h"

#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "Kismet/GameplayStatics.h"
#include "NPC/UEGT2NPCDirector.h"
#include "NPC/UEGT2NPCRoutines.h"
#include "NPC/UEGT2NPCSpeech.h"
#include "NPC/UEGT2RouteNetwork.h"
#include "Sound/SoundBase.h"
#include "UEGT2LogChannels.h"
#include "UI/UEGT2HUD.h"

#define LOCTEXT_NAMESPACE "UEGT2NPC"

// Named rather than anonymous: see the note in UEGT2NPCTypes.cpp.
namespace UEGT2NPCActorLocal
{
	/** Tick period for each tier. Near is every frame. */
	float TickIntervalFor(EUEGT2NPCLOD LOD)
	{
		switch (LOD)
		{
		case EUEGT2NPCLOD::Near: return 0.0f;
		case EUEGT2NPCLOD::Mid:  return 0.1f;
		case EUEGT2NPCLOD::Far:  return 0.5f;
		default:                 return 0.0f;
		}
	}

	/**
	 * How far an NPC spreads out around a shared anchor, in centimetres.
	 *
	 * These are generous because the alternative looks much worse than it
	 * sounds: ninety villagers all sent to "the market" with a five metre
	 * spread is not a crowd, it is a single writhing mass of overlapping
	 * people. A market is fifteen metres across; a square is thirty.
	 */
	float AnchorSpread(EUEGT2Anchor Anchor)
	{
		switch (Anchor)
		{
		case EUEGT2Anchor::Plaza:   return 1700.0f;
		case EUEGT2Anchor::Park:    return 1600.0f;
		case EUEGT2Anchor::Square:
		case EUEGT2Anchor::Market:  return 1500.0f;
		case EUEGT2Anchor::Pasture:
		case EUEGT2Anchor::Field:   return 1800.0f;
		case EUEGT2Anchor::Shore:   return 1200.0f;
		// A pier is 2.6 m wide. Spreading a crew of dockhands over ten metres
		// of it puts most of them on the water, where there is nothing to
		// stand on - they each pick their own dock section instead.
		case EUEGT2Anchor::Dock:    return 150.0f;
		case EUEGT2Anchor::Church:  return 1100.0f;
		case EUEGT2Anchor::Tavern:  return 550.0f;
		case EUEGT2Anchor::Home:    return 260.0f;
		case EUEGT2Anchor::Work:    return 520.0f;
		default:                    return 600.0f;
		}
	}

	/** Activities during which the NPC drifts about rather than standing still. */
	bool Drifts(EUEGT2Activity Activity)
	{
		switch (Activity)
		{
		case EUEGT2Activity::Work:
		case EUEGT2Activity::Market:
		case EUEGT2Activity::Stroll:
		case EUEGT2Activity::Play:
		case EUEGT2Activity::Patrol:
		case EUEGT2Activity::Graze:
		case EUEGT2Activity::Forage:
		case EUEGT2Activity::Scavenge:
		case EUEGT2Activity::Errand:
		case EUEGT2Activity::Idle:
			return true;
		default:
			return false;
		}
	}

	/** Bob height and stride length, per species. Birds hop; cows barely move. */
	void GaitFor(EUEGT2NPCSpecies Species, float& OutBob, float& OutStride, float& OutRoll)
	{
		switch (Species)
		{
		case EUEGT2NPCSpecies::Chicken:
		case EUEGT2NPCSpecies::Duck:
		case EUEGT2NPCSpecies::Seagull:
			OutBob = 5.0f;  OutStride = 26.0f;  OutRoll = 2.0f;  break;
		case EUEGT2NPCSpecies::Rabbit:
			OutBob = 9.0f;  OutStride = 34.0f;  OutRoll = 0.0f;  break;
		case EUEGT2NPCSpecies::Cat:
			OutBob = 2.5f;  OutStride = 40.0f;  OutRoll = 3.0f;  break;
		case EUEGT2NPCSpecies::Dog:
			OutBob = 3.5f;  OutStride = 46.0f;  OutRoll = 3.5f;  break;
		case EUEGT2NPCSpecies::Sheep:
		case EUEGT2NPCSpecies::Goat:
		case EUEGT2NPCSpecies::Pig:
			OutBob = 3.0f;  OutStride = 52.0f;  OutRoll = 2.5f;  break;
		case EUEGT2NPCSpecies::Cow:
		case EUEGT2NPCSpecies::Horse:
			OutBob = 3.5f;  OutStride = 86.0f;  OutRoll = 2.0f;  break;
		default:
			// A person: a 70 cm stride and a 3.5 cm rise at the top of it.
			OutBob = 3.5f;  OutStride = 70.0f;  OutRoll = 4.5f;  break;
		}
	}
}

AUEGT2NPCActor::AUEGT2NPCActor()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.bStartWithTickEnabled = false;

	// The mesh hangs off a scene root rather than being the root itself: the
	// walk cycle moves the mesh relative to the actor, and a relative move on
	// the root component is a world move. See the note in the header.
	Pivot = CreateDefaultSubobject<USceneComponent>(TEXT("Pivot"));
	RootComponent = Pivot;
	Pivot->SetMobility(EComponentMobility::Movable);

	Body = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Body"));
	Body->SetupAttachment(Pivot);
	Body->SetMobility(EComponentMobility::Movable);

	// Query only, and only against the interaction probe.
	//
	// The player walks through people on purpose. The town square is where the
	// player starts and where the crowd is thickest, and a solid crowd there
	// means getting wedged between four villagers with no way out - and it
	// means the packaged walk smoke can fail because somebody stood in front of
	// the pawn. Being able to look at and talk to them is what actually
	// matters, and that only needs the Visibility channel.
	Body->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	Body->SetCollisionObjectType(ECC_WorldDynamic);
	Body->SetCollisionResponseToAllChannels(ECR_Ignore);
	Body->SetCollisionResponseToChannel(ECC_Visibility, ECR_Block);
	Body->SetGenerateOverlapEvents(false);

	// Hundreds of these exist. Everything below is about them costing nothing
	// when they are not on screen.
	Body->SetCastShadow(true);
	Body->bCastDynamicShadow = true;
	Body->SetCullDistance(26000.0f);
}

void AUEGT2NPCActor::BeginPlay()
{
	Super::BeginPlay();

	SpawnLocation = GetActorLocation();
	GroundZ = SpawnLocation.Z;
	AnchorCentre = SpawnLocation;
	Destination = SpawnLocation;
	CurrentYaw = GetActorRotation().Yaw;
	Personality = FUEGT2Personality::FromSeed(Seed);
	Routes = AUEGT2RouteNetwork::Get(GetWorld());

	// Needs start staggered rather than full, so the first in-game hour is not
	// two hundred people all deciding they are hungry at the same moment.
	Needs.Energy = 0.55f + UEGT2HashUnit((uint32)Seed, 0x1111u) * 0.45f;
	Needs.Fed = 0.45f + UEGT2HashUnit((uint32)Seed, 0x2222u) * 0.5f;
	Needs.Company = 0.3f + UEGT2HashUnit((uint32)Seed, 0x3333u) * 0.6f;
	// Everyone starts somewhere different, so the town does not all get hungry
	// at once on the first morning.
	Needs.Relief = 0.35f + UEGT2HashUnit((uint32)Seed, 0x4444u) * 0.6f;

	if (UUEGT2NPCDirector* Director = UUEGT2NPCDirector::Get(GetWorld()))
	{
		Director->RegisterNPC(this);
	}
}

void AUEGT2NPCActor::EndPlay(const EEndPlayReason::Type Reason)
{
	if (UUEGT2NPCDirector* Director = UUEGT2NPCDirector::Get(GetWorld()))
	{
		Director->UnregisterNPC(this);
	}
	Super::EndPlay(Reason);
}

// ---------------------------------------------------------------------------
// Authoring
// ---------------------------------------------------------------------------
void AUEGT2NPCActor::ConfigureNPC(const FString& InDisplayName, EUEGT2NPCRole InRole,
	EUEGT2NPCSpecies InSpecies, int32 InSeed)
{
	DisplayName = FText::FromString(InDisplayName);
	NPCRole = InRole;
	Species = InSpecies;
	Seed = InSeed;
	Personality = FUEGT2Personality::FromSeed(Seed);

	// Draw cost scales with how big the thing actually is. A chicken's shadow
	// is not worth a virtual shadow map page, and a chicken at two hundred
	// metres is one pixel, so both are switched off well before a person's are.
	if (Body)
	{
		const bool bSmall = Species == EUEGT2NPCSpecies::Chicken
			|| Species == EUEGT2NPCSpecies::Duck
			|| Species == EUEGT2NPCSpecies::Cat
			|| Species == EUEGT2NPCSpecies::Rabbit
			|| Species == EUEGT2NPCSpecies::Seagull;
		Body->SetCastShadow(!bSmall);
		Body->SetCullDistance(bSmall ? 9000.0f
			: (IsAnimal() ? 17000.0f : 26000.0f));
	}
}

void AUEGT2NPCActor::SetNPCMesh(UStaticMesh* Mesh)
{
	if (Body && Mesh)
	{
		Body->SetStaticMesh(Mesh);
	}
}

void AUEGT2NPCActor::AddAnchor(EUEGT2Anchor Type, const FVector& Location)
{
	for (FUEGT2NPCAnchorPoint& Anchor : Anchors)
	{
		if (Anchor.Type == Type)
		{
			Anchor.Location = Location;
			return;
		}
	}
	FUEGT2NPCAnchorPoint Added;
	Added.Type = Type;
	Added.Location = Location;
	Anchors.Add(Added);
}

void AUEGT2NPCActor::SetWanderRadius(float Radius)
{
	WanderRadius = FMath::Max(Radius, 0.0f);
}

void AUEGT2NPCActor::SetBaseSpeed(float Speed)
{
	BaseSpeed = FMath::Max(Speed, 10.0f);
}

// ---------------------------------------------------------------------------
// Interaction
// ---------------------------------------------------------------------------
FText AUEGT2NPCActor::GetInteractionPrompt(const AActor* Interactor) const
{
	if (IsAnimal())
	{
		return FText::Format(LOCTEXT("PetAnimal", "Pet the {0}"), GetSpeciesDisplayName(Species));
	}
	return FText::Format(LOCTEXT("TalkTo", "Talk to {0}"),
		DisplayName.IsEmpty() ? GetRoleDisplayName(NPCRole) : DisplayName);
}

bool AUEGT2NPCActor::CanInteract(const AActor* Interactor) const
{
	return !bSuppressed && !bIndoors;
}

void AUEGT2NPCActor::Interact(AActor* Interactor)
{
	if (!CanInteract(Interactor))
	{
		return;
	}

	const UUEGT2NPCDirector* Director = UUEGT2NPCDirector::Get(GetWorld());
	const float Hour = Director ? Director->GetHour() : 12.0f;
	const EUEGT2Weather Weather = Director ? Director->GetWeather() : EUEGT2Weather::Clear;

	// A different line each time you ask, but the same sequence every run.
	const uint32 Variation = (uint32)FMath::FloorToInt(GetWorld()->GetTimeSeconds() * 0.5f);
	const FText Line = GetSpeechLine(NPCRole, Species, Decision.Activity,
		EUEGT2SpeechMood::Reply, Weather, Hour, (uint32)Seed, Variation);
	Say(Line, 4.4f, 0.35f);

	// The HUD line is the honest one: it names what they are actually doing,
	// which is the part a playtester needs to be able to check.
	if (!IsAnimal())
	{
		const FText Message = FText::Format(LOCTEXT("NPCStatus", "{0}, {1} - {2}"),
			DisplayName.IsEmpty() ? GetRoleDisplayName(NPCRole) : DisplayName,
			GetRoleDisplayName(NPCRole),
			GetActivityDisplayName(Decision.Activity));

		const APawn* Pawn = Cast<APawn>(Interactor);
		const APlayerController* Controller = Pawn ? Cast<APlayerController>(Pawn->GetController()) : nullptr;
		if (AUEGT2HUD* Hud = Controller ? Cast<AUEGT2HUD>(Controller->GetHUD()) : nullptr)
		{
			Hud->ShowMessage(Message, 4.0f);
		}
	}

	if (USoundBase* Click = LoadObject<USoundBase>(nullptr, TEXT("/Game/Fairhaven/Audio/S_Interact")))
	{
		UGameplayStatics::PlaySoundAtLocation(this, Click, GetActorLocation(), 0.45f);
	}

	// Being spoken to is company, and a lonely villager who has just been
	// talked to should stop looking for someone to talk to.
	Needs.Company = FMath::Min(Needs.Company + 0.3f, 1.0f);
	FocusPoint = Interactor ? Interactor->GetActorLocation() : FocusPoint;
	FocusCountdown = 5.0f;

	UE_LOG(LogUEGT2NPC, Verbose, TEXT("%s spoken to while %s."),
		*DisplayName.ToString(), *GetActivityDisplayName(Decision.Activity).ToString());
}

void AUEGT2NPCActor::SetInteractionFocus(bool bInFocused)
{
	bFocused = bInFocused;
	if (Body)
	{
		Body->SetRenderCustomDepth(bInFocused);
	}
	if (bInFocused)
	{
		// Look back at whoever is looking at you.
		if (const APawn* Player = UGameplayStatics::GetPlayerPawn(GetWorld(), 0))
		{
			FocusPoint = Player->GetActorLocation();
			FocusCountdown = 4.0f;
		}
	}
}

FVector AUEGT2NPCActor::GetInteractionPoint() const
{
	return Body ? Body->Bounds.Origin : GetActorLocation();
}

// ---------------------------------------------------------------------------
// Life
// ---------------------------------------------------------------------------
FVector AUEGT2NPCActor::GetAnchorLocation(EUEGT2Anchor Type) const
{
	for (const FUEGT2NPCAnchorPoint& Anchor : Anchors)
	{
		if (Anchor.Type == Type)
		{
			return Anchor.Location;
		}
	}
	// Home is the universal fallback, and the spawn point is home's.
	if (Type != EUEGT2Anchor::Home)
	{
		for (const FUEGT2NPCAnchorPoint& Anchor : Anchors)
		{
			if (Anchor.Type == EUEGT2Anchor::Home)
			{
				return Anchor.Location;
			}
		}
	}
	return SpawnLocation;
}

FVector AUEGT2NPCActor::ResolveDestinationFor(const FUEGT2ActivityDecision& InDecision) const
{
	const uint32 SeedU = (uint32)Seed;

	if (InDecision.Activity == EUEGT2Activity::Flee)
	{
		// Straight away from the player, downhill of nothing in particular.
		const UUEGT2NPCDirector* Director = UUEGT2NPCDirector::Get(GetWorld());
		const FVector Player = Director ? Director->GetPlayerLocation() : GetActorLocation();
		FVector Away = GetActorLocation() - Player;
		Away.Z = 0.0f;
		if (Away.SizeSquared() < 1.0f)
		{
			Away = FVector(1.0f, 0.0f, 0.0f);
		}
		Away.Normalize();
		// A slight turn, so a scattering flock fans out instead of forming a line.
		const float Turn = (UEGT2HashUnit(SeedU, 0x5EEDu) - 0.5f) * 70.0f;
		Away = Away.RotateAngleAxis(Turn, FVector::UpVector);
		return GetActorLocation() + Away * (1400.0f + UEGT2HashUnit(SeedU, 0x6EEDu) * 1200.0f);
	}

	if (InDecision.Activity == EUEGT2Activity::Follow)
	{
		const UUEGT2NPCDirector* Director = UUEGT2NPCDirector::Get(GetWorld());
		const FVector Player = Director ? Director->GetPlayerLocation() : GetActorLocation();
		FVector Offset(220.0f, 0.0f, 0.0f);
		Offset = Offset.RotateAngleAxis(UEGT2HashUnit(SeedU, 0x7EEDu) * 360.0f, FVector::UpVector);
		return Player + Offset;
	}

	if (InDecision.Anchor == EUEGT2Anchor::Wander)
	{
		const FVector Base = AnchorCentre.IsNearlyZero() ? SpawnLocation : AnchorCentre;
		if (Routes)
		{
			const FVector Target = Routes->GetWanderTarget(Base, FMath::Max(WanderRadius * 3.0f, 2500.0f),
				UEGT2HashSeed(SeedU, (uint32)FMath::FloorToInt(GetWorld()->GetTimeSeconds() * 0.2f)));
			if (!Target.Equals(Base, 1.0f))
			{
				return Target;
			}
		}
		// No road here (a field, a beach): drift around the spawn point.
		const float Angle = UEGT2HashUnit(SeedU,
			(uint32)FMath::FloorToInt(GetWorld()->GetTimeSeconds() * 0.2f), 0x9EEDu) * 360.0f;
		const float Range = WanderRadius * (0.4f + UEGT2HashUnit(SeedU, 0xAEEDu) * 0.9f);
		return SpawnLocation + FVector(FMath::Cos(FMath::DegreesToRadians(Angle)) * Range,
			FMath::Sin(FMath::DegreesToRadians(Angle)) * Range, 0.0f);
	}

	// A stable offset around the anchor, so a crowd is a crowd rather than a
	// stack of NPCs sharing one coordinate.
	const FVector Base = GetAnchorLocation(InDecision.Anchor);
	const float Spread = UEGT2NPCActorLocal::AnchorSpread(InDecision.Anchor);
	const float Angle = UEGT2HashUnit(SeedU, (uint32)InDecision.Anchor, 0xBEEDu) * 360.0f;
	const float Radius = Spread * FMath::Sqrt(UEGT2HashUnit(SeedU, (uint32)InDecision.Anchor, 0xCEEDu));
	return Base + FVector(FMath::Cos(FMath::DegreesToRadians(Angle)) * Radius,
		FMath::Sin(FMath::DegreesToRadians(Angle)) * Radius, 0.0f);
}

void AUEGT2NPCActor::EvaluateSchedule(const FUEGT2NPCContext& Context, bool bForceRepath)
{
	FUEGT2NPCContext Local = Context;
	Local.Personality = Personality;
	Local.Needs = Needs;
	Local.Seed = Seed;
	Local.bExposed = !bIndoors;

	const FUEGT2ActivityDecision Previous = Decision;
	Decision = ResolveActivity(NPCRole, Species, Local);

	const bool bChanged = Previous.Activity != Decision.Activity
		|| Previous.Anchor != Decision.Anchor;
	bActivityChanged |= (Previous.Activity != Decision.Activity);

	if (bChanged || bForceRepath)
	{
		AnchorCentre = (Decision.Anchor == EUEGT2Anchor::Wander)
			? (AnchorCentre.IsNearlyZero() ? SpawnLocation : AnchorCentre)
			: GetAnchorLocation(Decision.Anchor);

		const FVector NewDestination = ResolveDestinationFor(Decision);
		ApplyIndoors(IsIndoorActivity(Decision.Activity));

		if (bIndoors)
		{
			// Straight to the doorstep and out of sight: there are no interiors,
			// so "inside" is the honest way to spend the night.
			const FVector Home = GetAnchorLocation(EUEGT2Anchor::Home);
			GroundZ = GroundZAt(Home);
			SetActorLocation(FVector(Home.X, Home.Y, GroundZ));
			PathPoints.Reset();
			Destination = Home;
			bArrived = true;
		}
		else if (LOD == EUEGT2NPCLOD::Dormant)
		{
			// Nobody is within four hundred metres of this one. Walking it
			// there would cost an A* search and a tick budget to animate a
			// journey no one can see, and skipping the move entirely would
			// leave the far side of the map frozen at whatever hour the player
			// last visited. So it simply arrives.
			Destination = FVector(NewDestination.X, NewDestination.Y, GroundZAt(NewDestination));
			PathPoints.Reset();
			PathIndex = 0;
			bArrived = true;
			SetActorLocation(Destination);
			GroundZ = (float)Destination.Z;
		}
		else
		{
			RepathTo(NewDestination);
		}

		StuckSeconds = 0.0f;
		LastStuckCheckLocation = GetActorLocation();
	}
}

bool AUEGT2NPCActor::ConsumeActivityChanged()
{
	const bool bWas = bActivityChanged;
	bActivityChanged = false;
	return bWas;
}

void AUEGT2NPCActor::SnapToSchedule(const FUEGT2NPCContext& Context)
{
	EvaluateSchedule(Context, true);
	if (!bIndoors)
	{
		// Settle onto the ground, rather than merely working out where it is.
		// A frozen capture never ticks, so this is the only chance these actors
		// get to stand on anything.
		const FVector Target = Destination;
		GroundZ = GroundZAt(Target);
		SetActorLocation(FVector(Target.X, Target.Y, GroundZ));
	}
	PathPoints.Reset();
	PathIndex = 0;
	bArrived = true;
	SpeedFraction = 0.0f;
	// Face somewhere plausible rather than all facing north.
	CurrentYaw = UEGT2HashUnit((uint32)Seed, 0xF00Du) * 360.0f;
	SetActorRotation(FRotator(0.0f, CurrentYaw, 0.0f));
}

void AUEGT2NPCActor::AdvanceNeeds(float WorldHours)
{
	Needs.Advance(WorldHours, Decision.Activity);
}

void AUEGT2NPCActor::RepathTo(const FVector& Goal)
{
	// Every destination is ground-sampled here, and only here.
	//
	// An anchor is one point; the crowd around it is spread over fifteen
	// metres, and the offsets that spread it are horizontal. Inheriting the
	// anchor's height meant that on any ground that is not flat, most of the
	// crowd stood in the air - 343 of 786 of them, at the last count, which is
	// what "floating" looked like.
	Destination = FVector(Goal.X, Goal.Y, GroundZAt(Goal));
	PathPoints.Reset();
	PathIndex = 0;
	bArrived = false;

	const float Straight = FVector::Dist2D(GetActorLocation(), Goal);
	// Short hops are not worth a search, and following a road for twelve metres
	// looks worse than walking across the grass.
	if (Routes && Straight > 1800.0f)
	{
		TArray<FVector> Points;
		if (Routes->FindPath(GetActorLocation(), Goal, Points) && Points.Num() > 0)
		{
			PathPoints = MoveTemp(Points);
			// Drop leading nodes that are behind us: the nearest node can be
			// the one just walked past, which produces a visible about-turn.
			while (PathPoints.Num() > 1
				&& FVector::Dist2D(PathPoints[0], GetActorLocation()) < 400.0f)
			{
				PathPoints.RemoveAt(0);
			}
		}
	}

	BeginSegment();
	DriftCountdown = 3.0f + UEGT2HashUnit((uint32)Seed, 0xD11Fu) * 7.0f;
}

void AUEGT2NPCActor::BeginSegment()
{
	const FVector Target = PathPoints.IsValidIndex(PathIndex) ? PathPoints[PathIndex] : Destination;
	SegmentStartZ = GroundZ;
	SegmentLength = FVector::Dist2D(GetActorLocation(), Target);
}

void AUEGT2NPCActor::PickArrivalTarget()
{
	if (!UEGT2NPCActorLocal::Drifts(Decision.Activity))
	{
		DriftCountdown = 6.0f;
		return;
	}
	// Wandering activities keep picking fresh points; settled ones shuffle
	// around the anchor they arrived at.
	const uint32 Tick = (uint32)FMath::FloorToInt(GetWorld()->GetTimeSeconds() * 0.25f);
	if (Decision.Anchor == EUEGT2Anchor::Wander)
	{
		RepathTo(ResolveDestinationFor(Decision));
		return;
	}

	// The anchor's own spread, and nothing else.
	//
	// This used to mix in WanderRadius, which is the radius for *roaming* - and
	// roaming radii are large. A dockhand with a 9 m roam drifted 4.5 m around
	// a pier 2.6 m wide, which put him on the sea. WanderRadius belongs to the
	// Wander anchor, which has its own branch above.
	const float Spread = UEGT2NPCActorLocal::AnchorSpread(Decision.Anchor) * 0.8f;
	const float Angle = UEGT2HashUnit((uint32)Seed, Tick, 0xE11Fu) * 360.0f;
	const float Radius = Spread * FMath::Sqrt(UEGT2HashUnit((uint32)Seed, Tick, 0xF11Fu));
	const FVector Base = AnchorCentre.IsNearlyZero() ? SpawnLocation : AnchorCentre;
	RepathTo(Base + FVector(FMath::Cos(FMath::DegreesToRadians(Angle)) * Radius,
		FMath::Sin(FMath::DegreesToRadians(Angle)) * Radius, 0.0f));
}

void AUEGT2NPCActor::ApplyIndoors(bool bNewIndoors)
{
	if (bNewIndoors == bIndoors)
	{
		return;
	}
	bIndoors = bNewIndoors;
	if (Body)
	{
		Body->SetVisibility(!bIndoors && !bSuppressed);
		Body->SetCollisionEnabled(bIndoors || bSuppressed
			? ECollisionEnabled::NoCollision : ECollisionEnabled::QueryOnly);
	}
}

void AUEGT2NPCActor::SetLOD(EUEGT2NPCLOD NewLOD)
{
	if (NewLOD == LOD)
	{
		return;
	}
	const bool bWasDormant = LOD == EUEGT2NPCLOD::Dormant;
	LOD = NewLOD;

	// Coming out of dormancy: the actor was teleported along its schedule while
	// nobody was looking, so it has no path. Give it one from where it is.
	if (bWasDormant && LOD != EUEGT2NPCLOD::Dormant && !bSuppressed && !bIndoors)
	{
		RepathTo(Destination);
	}

	const bool bShouldTick = LOD != EUEGT2NPCLOD::Dormant && !bSuppressed;
	SetActorTickEnabled(bShouldTick);
	if (bShouldTick)
	{
		SetActorTickInterval(UEGT2NPCActorLocal::TickIntervalFor(LOD));
	}
}

void AUEGT2NPCActor::SetSuppressed(bool bNewSuppressed)
{
	if (bNewSuppressed == bSuppressed)
	{
		return;
	}
	bSuppressed = bNewSuppressed;
	if (Body)
	{
		Body->SetVisibility(!bSuppressed && !bIndoors);
		Body->SetCollisionEnabled(bSuppressed || bIndoors
			? ECollisionEnabled::NoCollision : ECollisionEnabled::QueryOnly);
	}
	SetActorTickEnabled(!bSuppressed && LOD != EUEGT2NPCLOD::Dormant);
}

// ---------------------------------------------------------------------------
// Speech
// ---------------------------------------------------------------------------
void AUEGT2NPCActor::Say(const FText& Line, float HoldSeconds, float TypingSeconds)
{
	if (Line.IsEmpty() || !GetWorld())
	{
		return;
	}
	SpokenLine = Line;
	BubbleStartTime = GetWorld()->GetTimeSeconds();
	BubbleTypingSeconds = FMath::Max(TypingSeconds, 0.0f);
	BubbleHoldSeconds = FMath::Max(HoldSeconds, 0.5f);
	LastSpokeTime = BubbleStartTime;
}

bool AUEGT2NPCActor::HasBubble() const
{
	if (SpokenLine.IsEmpty() || !GetWorld())
	{
		return false;
	}
	const float Elapsed = GetWorld()->GetTimeSeconds() - BubbleStartTime;
	return Elapsed >= 0.0f && Elapsed < BubbleTypingSeconds + BubbleHoldSeconds;
}

bool AUEGT2NPCActor::IsTyping() const
{
	if (!GetWorld() || SpokenLine.IsEmpty())
	{
		return false;
	}
	return (GetWorld()->GetTimeSeconds() - BubbleStartTime) < BubbleTypingSeconds;
}

float AUEGT2NPCActor::GetBubbleAlpha() const
{
	if (!GetWorld())
	{
		return 0.0f;
	}
	const float Elapsed = GetWorld()->GetTimeSeconds() - BubbleStartTime;
	const float Total = BubbleTypingSeconds + BubbleHoldSeconds;
	if (Elapsed < 0.0f || Elapsed > Total)
	{
		return 0.0f;
	}
	const float FadeIn = FMath::Clamp(Elapsed / 0.18f, 0.0f, 1.0f);
	const float FadeOut = FMath::Clamp((Total - Elapsed) / 0.45f, 0.0f, 1.0f);
	return FMath::Min(FadeIn, FadeOut);
}

FVector AUEGT2NPCActor::GetSpeechAnchor() const
{
	// Just above the mesh, whatever the mesh is: a seagull's bubble should not
	// float where a person's head would be.
	const float Top = Body ? Body->Bounds.BoxExtent.Z * 2.0f : 180.0f;
	return GetActorLocation() + FVector(0.0f, 0.0f, Top + 34.0f);
}

float AUEGT2NPCActor::GetSecondsSinceSpoke() const
{
	return GetWorld() ? GetWorld()->GetTimeSeconds() - LastSpokeTime : 1000.0f;
}

FLinearColor AUEGT2NPCActor::GetBubbleTint() const
{
	if (IsAnimal())
	{
		return FLinearColor(0.36f, 0.58f, 0.44f, 1.0f);      // mossy green
	}
	switch (NPCRole)
	{
	case EUEGT2NPCRole::Child:    return FLinearColor(0.62f, 0.44f, 0.72f, 1.0f);
	case EUEGT2NPCRole::Officer:  return FLinearColor(0.30f, 0.45f, 0.68f, 1.0f);
	case EUEGT2NPCRole::Priest:   return FLinearColor(0.55f, 0.50f, 0.36f, 1.0f);
	default:                      return FLinearColor(0.20f, 0.34f, 0.42f, 1.0f);
	}
}

bool AUEGT2NPCActor::IsWalking() const
{
	return SpeedFraction > 0.08f;
}

// ---------------------------------------------------------------------------
// Tick
// ---------------------------------------------------------------------------
void AUEGT2NPCActor::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	if (bSuppressed || bIndoors)
	{
		SpeedFraction = 0.0f;
		return;
	}

	AdvanceMovement(DeltaSeconds);

	// Cosmetics are the whole reason an NPC looks alive, and also the only part
	// nobody can see from three hundred metres away.
	if (LOD == EUEGT2NPCLOD::Near || LOD == EUEGT2NPCLOD::Mid)
	{
		AdvanceCosmetics(DeltaSeconds);
	}

	// Mid tier corrects too, just less often. A hundred and sixty metres away is
	// still close enough to notice somebody standing a metre off the ground.
	if (LOD == EUEGT2NPCLOD::Near || LOD == EUEGT2NPCLOD::Mid)
	{
		GroundTraceCountdown -= DeltaSeconds;
		if (GroundTraceCountdown <= 0.0f)
		{
			GroundTraceCountdown = (LOD == EUEGT2NPCLOD::Near) ? 0.5f : 1.5f;
			UpdateGroundHeight();
		}
	}
}

void AUEGT2NPCActor::AdvanceMovement(float DeltaSeconds)
{
	const FVector Current = GetActorLocation();

	// Waypoint first, final destination last.
	const bool bOnPath = PathPoints.IsValidIndex(PathIndex);
	const FVector Target = bOnPath ? PathPoints[PathIndex] : Destination;
	const float Accept = bOnPath ? 240.0f : ArriveRadius;

	FVector ToTarget = Target - Current;
	ToTarget.Z = 0.0f;
	const float Distance = ToTarget.Size();

	if (Distance <= Accept)
	{
		if (bOnPath)
		{
			++PathIndex;
			BeginSegment();
			return;
		}
		if (!bArrived)
		{
			bArrived = true;
			DriftCountdown = 2.5f + UEGT2HashUnit((uint32)Seed, 0x2A2Au) * 6.0f;
		}
		SpeedFraction = FMath::FInterpTo(SpeedFraction, 0.0f, DeltaSeconds, 6.0f);

		DriftCountdown -= DeltaSeconds;
		if (DriftCountdown <= 0.0f)
		{
			PickArrivalTarget();
		}
		// Settle onto the ground even while standing still: the arrival point
		// came from a baked height that may be a few centimetres out.
		FVector Settled = Current;
		Settled.Z = FMath::FInterpTo(Current.Z, GroundZ, DeltaSeconds, 6.0f);
		SetActorLocation(Settled);
		return;
	}

	const float Pace = GetActivityPace(Decision.Activity);
	const float Speed = BaseSpeed * Pace * (0.82f + Personality.Energy * 0.36f);
	const FVector Direction = ToTarget / Distance;
	const float Step = FMath::Min(Speed * DeltaSeconds, Distance);

	// Height by progress along the leg, not by a time constant. Both ends of
	// the leg carry a real ground height - path nodes are baked from the
	// heightmap and destinations are traced - so walking the straight line
	// between them in 3D follows the ground exactly.
	const float Progress = (SegmentLength > 1.0f)
		? FMath::Clamp(1.0f - Distance / SegmentLength, 0.0f, 1.0f) : 1.0f;
	GroundZ = FMath::Lerp(SegmentStartZ, (float)Target.Z, Progress);

	FVector Next = Current + Direction * Step;
	Next.Z = GroundZ;
	SetActorLocation(Next);

	SpeedFraction = FMath::Clamp(Speed / FMath::Max(BaseSpeed * 1.6f, 1.0f), 0.0f, 1.4f);
	StridePhase += Step;

	// Facing: toward travel while moving.
	const float TargetYaw = FMath::RadiansToDegrees(FMath::Atan2(Direction.Y, Direction.X));
	const float TurnRate = 260.0f + Personality.Energy * 180.0f;
	const float Delta = FMath::UnwindDegrees(TargetYaw - CurrentYaw);
	CurrentYaw += FMath::Clamp(Delta, -TurnRate * DeltaSeconds, TurnRate * DeltaSeconds);
	SetActorRotation(FRotator(0.0f, CurrentYaw, 0.0f));

	// --- Stuck handling ---------------------------------------------------
	// Nothing here does obstacle avoidance: the route network keeps travel on
	// the streets, and the last leg is short. What is left is the case where
	// somebody's destination ended up inside a wall, and the fix for that is to
	// stop trying rather than to grind against it forever.
	StuckSeconds += DeltaSeconds;
	if (StuckSeconds > 1.5f)
	{
		const float Moved = FVector::Dist2D(Current, LastStuckCheckLocation);
		if (Moved < Speed * StuckSeconds * 0.25f)
		{
			// Sidestep once; if that does not help, accept the destination.
			const FVector Side = FVector::CrossProduct(Direction, FVector::UpVector)
				* (UEGT2HashUnit((uint32)Seed, (uint32)PathIndex) > 0.5f ? 320.0f : -320.0f);
			if (StuckSeconds > 6.0f)
			{
				SetActorLocation(FVector(Target.X, Target.Y, Target.Z));
				GroundZ = Target.Z;
				if (bOnPath) { ++PathIndex; } else { bArrived = true; }
			}
			else
			{
				RepathTo(Destination + Side);
			}
		}
		StuckSeconds = 0.0f;
		LastStuckCheckLocation = GetActorLocation();
	}
}

void AUEGT2NPCActor::AdvanceCosmetics(float DeltaSeconds)
{
	if (!Body)
	{
		return;
	}

	float BobHeight = 3.5f, StrideLength = 70.0f, RollAmount = 4.5f;
	UEGT2NPCActorLocal::GaitFor(Species, BobHeight, StrideLength, RollAmount);

	const float Phase = StridePhase / FMath::Max(StrideLength, 1.0f) * PI;
	const float Moving = FMath::Clamp(SpeedFraction, 0.0f, 1.0f);
	const float Time = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0f;

	// Two rises per stride (one per foot), plus a slow idle breath so a
	// standing figure is not perfectly, obviously still.
	const float Bob = FMath::Abs(FMath::Sin(Phase)) * BobHeight * Moving
		+ FMath::Sin(Time * 1.3f + Seed * 0.37f) * 0.9f * (1.0f - Moving);

	const float Roll = FMath::Sin(Phase) * RollAmount * Moving;
	const float Pitch = -3.2f * Moving;
	const float YawWobble = FMath::Sin(Phase * 0.5f) * 2.4f * Moving;

	Body->SetRelativeLocation(FVector(0.0f, 0.0f, Bob));
	Body->SetRelativeRotation(FRotator(Pitch, YawWobble, Roll));

	// Standing still: turn to look at whatever is interesting.
	if (Moving < 0.05f)
	{
		FocusCountdown -= DeltaSeconds;
		if (FocusCountdown <= 0.0f)
		{
			FocusCountdown = 2.5f + UEGT2HashUnit((uint32)Seed,
				(uint32)FMath::FloorToInt(Time * 0.2f)) * 5.0f;
			const float Angle = UEGT2HashUnit((uint32)Seed,
				(uint32)FMath::FloorToInt(Time * 0.2f), 0x1F1Fu) * 360.0f;
			FocusPoint = GetActorLocation() + FVector(
				FMath::Cos(FMath::DegreesToRadians(Angle)) * 600.0f,
				FMath::Sin(FMath::DegreesToRadians(Angle)) * 600.0f, 0.0f);
		}

		FVector ToFocus = FocusPoint - GetActorLocation();
		ToFocus.Z = 0.0f;
		if (ToFocus.SizeSquared() > 100.0f)
		{
			const float TargetYaw = FMath::RadiansToDegrees(FMath::Atan2(ToFocus.Y, ToFocus.X));
			const float Delta = FMath::UnwindDegrees(TargetYaw - CurrentYaw);
			CurrentYaw += FMath::Clamp(Delta, -110.0f * DeltaSeconds, 110.0f * DeltaSeconds);
			SetActorRotation(FRotator(0.0f, CurrentYaw, 0.0f));
		}
	}
}

float AUEGT2NPCActor::GroundZAt(const FVector& Point) const
{
	const UWorld* World = GetWorld();
	if (!World)
	{
		return (float)Point.Z;
	}

	// Object-type query rather than the Visibility channel: every NPC blocks
	// Visibility so the interaction probe can find them, and a channel trace
	// would happily land one NPC on another's head.
	FCollisionObjectQueryParams ObjectParams;
	ObjectParams.AddObjectTypesToQuery(ECC_WorldStatic);
	FCollisionQueryParams Params(SCENE_QUERY_STAT(UEGT2NPCGround), false, this);

	// Stage one: down from knee height. This finds the floor, the pier deck or
	// the doorstep, and cannot find a roof, because every roof in this world is
	// higher off its own floor than a knee is.
	FHitResult Hit;
	if (World->LineTraceSingleByObjectType(Hit,
		Point + FVector(0.0f, 0.0f, 90.0f),
		Point - FVector(0.0f, 0.0f, 2000.0f), ObjectParams, Params))
	{
		return (float)Hit.ImpactPoint.Z;
	}

	// Stage two only happens when stage one started underground, which means
	// Point is inside a hillside. Drop from overhead and take the lowest hit:
	// under an awning that is the ground, and under a building it is the ground
	// the building stands on.
	TArray<FHitResult> Hits;
	if (World->LineTraceMultiByObjectType(Hits,
		Point + FVector(0.0f, 0.0f, 4000.0f),
		Point - FVector(0.0f, 0.0f, 2000.0f), ObjectParams, Params))
	{
		double Lowest = Point.Z + 4000.0;
		for (const FHitResult& Found : Hits)
		{
			Lowest = FMath::Min(Lowest, Found.ImpactPoint.Z);
		}
		return (float)Lowest;
	}

	return (float)Point.Z;
}

void AUEGT2NPCActor::UpdateGroundHeight()
{
	GroundZ = GroundZAt(GetActorLocation());
}

#undef LOCTEXT_NAMESPACE
