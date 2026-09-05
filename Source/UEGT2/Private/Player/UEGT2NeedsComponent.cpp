#include "Player/UEGT2NeedsComponent.h"

#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "NPC/UEGT2NPCDirector.h"
#include "UEGT2LogChannels.h"
#include "UI/UEGT2HUD.h"

#define LOCTEXT_NAMESPACE "UEGT2Needs"

UUEGT2NeedsComponent::UUEGT2NeedsComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	// Needs move over minutes, not frames. Ten a second is already generous and
	// keeps this off the critical path entirely.
	PrimaryComponentTick.TickInterval = 0.1f;
}

void UUEGT2NeedsComponent::BeginPlay()
{
	Super::BeginPlay();

	// The player arrives with the same day's pay in their pocket as anybody
	// else of no particular trade, and the same full-ish needs.
	Purse.Coins = UEGT2StartingCoins(Trade);
	UE_LOG(LogUEGT2Player, Log, TEXT("Explorer starts with %d coins."), Purse.Whole());
}

// ---------------------------------------------------------------------------
EUEGT2Activity UUEGT2NeedsComponent::IdleActivity() const
{
	if (bConversing)
	{
		return EUEGT2Activity::Socialise;
	}
	const AActor* Owner = GetOwner();
	const float Speed = Owner ? Owner->GetVelocity().Size2D() : 0.0f;
	if (Speed <= MovingSpeed)
	{
		return EUEGT2Activity::Idle;
	}
	// Running about costs what a shift costs, because it is one. Commute is
	// the activity the needs table already charges at the working rate, and
	// reusing it keeps the numbers in one place.
	return Speed > 500.0f ? EUEGT2Activity::Commute : EUEGT2Activity::Stroll;
}

const float* UUEGT2NeedsComponent::NeedFor(EUEGT2Activity InActivity) const
{
	switch (InActivity)
	{
	case EUEGT2Activity::Eat:
	case EUEGT2Activity::Breakfast:
	case EUEGT2Activity::Lunch:
	case EUEGT2Activity::Dinner:    return &Needs.Fed;
	case EUEGT2Activity::Washroom:  return &Needs.Relief;
	case EUEGT2Activity::Rest:
	case EUEGT2Activity::Sleep:     return &Needs.Energy;
	case EUEGT2Activity::Socialise:
	case EUEGT2Activity::Worship:   return &Needs.Company;
	// Work, Tavern and Market answer no single need, so nothing stops them.
	default:                        return nullptr;
	}
}

void UUEGT2NeedsComponent::Announce(const FText& Message) const
{
	if (Message.IsEmpty())
	{
		return;
	}
	const APawn* Pawn = Cast<APawn>(GetOwner());
	const APlayerController* PC = Pawn ? Cast<APlayerController>(Pawn->GetController()) : nullptr;
	if (AUEGT2HUD* Hud = PC ? Cast<AUEGT2HUD>(PC->GetHUD()) : nullptr)
	{
		Hud->ShowMessage(Message, 3.5f);
	}
	else
	{
		UE_LOG(LogUEGT2Player, Log, TEXT("%s"), *Message.ToString());
	}
}

void UUEGT2NeedsComponent::SetActivity(EUEGT2Activity NewActivity, const FText& Note)
{
	if (Activity == NewActivity)
	{
		return;
	}
	Activity = NewActivity;
	OnActivityChanged.Broadcast(Activity, Note);
}

// ---------------------------------------------------------------------------
bool UUEGT2NeedsComponent::BeginActivity(EUEGT2Activity InActivity, AActor* InVenue,
	const FText& InVenueName, EUEGT2NPCRole JobRole, float InUseRange)
{
	// Pressing use on the place you are already using is how you leave it.
	if (IsUsing(InVenue) && VenueActivity == InActivity)
	{
		StopActivity(FText::GetEmpty());
		return true;
	}

	// A quarter hour of it up front, so "I cannot afford this" is answered at
	// the counter rather than a second later with a silent nothing.
	const float Deposit = UEGT2PriceFor(JobRole, InActivity) * 0.25f;
	if (!Purse.CanAfford(Deposit))
	{
		Announce(FText::Format(
			LOCTEXT("CannotAfford", "You cannot pay for that. {0} coins left."),
			FText::AsNumber(Purse.Whole())));
		return false;
	}

	Venue = InVenue;
	VenueName = InVenueName;
	VenueActivity = InActivity;
	UseRange = FMath::Max(120.0f, InUseRange);
	bWarnedBroke = false;

	// Taking a job changes your trade for good. It is what the wage is paid
	// at, and it is what the HUD calls you from then on.
	if (InActivity == EUEGT2Activity::Work || InActivity == EUEGT2Activity::Market)
	{
		if (Trade != JobRole)
		{
			Trade = JobRole;
			Announce(FText::Format(LOCTEXT("TookWork", "Taken on as a {0}."),
				GetRoleDisplayName(JobRole)));
		}
	}

	SetActivity(InActivity, FText::GetEmpty());
	UE_LOG(LogUEGT2Player, Log, TEXT("Player begins %s at %s."),
		*GetActivityDisplayName(InActivity).ToString(), *VenueName.ToString());
	return true;
}

void UUEGT2NeedsComponent::StopActivity(const FText& Note)
{
	if (!Venue.IsValid() && VenueActivity == EUEGT2Activity::Idle)
	{
		return;
	}
	Venue = nullptr;
	VenueName = FText::GetEmpty();
	VenueActivity = EUEGT2Activity::Idle;
	Announce(Note);
	SetActivity(IdleActivity(), Note);
}

void UUEGT2NeedsComponent::SetConversing(bool bTalking)
{
	bConversing = bTalking;
}

void UUEGT2NeedsComponent::SetCoins(float Amount)
{
	Purse.Coins = FMath::Max(0.0f, Amount);
}

void UUEGT2NeedsComponent::SetNeedsSatisfied(bool bFull)
{
	const float Value = bFull ? 1.0f : 0.1f;
	Needs.Energy = Value;
	Needs.Fed = Value;
	Needs.Relief = Value;
	Needs.Company = Value;
}

// ---------------------------------------------------------------------------
void UUEGT2NeedsComponent::CheckVenueStillValid()
{
	if (!Venue.IsValid())
	{
		// The actor went away underneath us - a content rebuild, or a door
		// that was destroyed. Fall back to walking about rather than staying
		// stuck in an activity with nowhere to do it.
		if (VenueActivity != EUEGT2Activity::Idle)
		{
			StopActivity(FText::GetEmpty());
		}
		return;
	}

	const AActor* Owner = GetOwner();
	if (Owner && FVector::Dist(Owner->GetActorLocation(), Venue->GetActorLocation()) > UseRange)
	{
		// A bench and a privy have no name worth printing, and "You leave ."
		// is what naming them anyway looks like.
		StopActivity(VenueName.IsEmpty()
			? LOCTEXT("WalkedOffAnon", "You move on.")
			: FText::Format(LOCTEXT("WalkedOff", "You leave {0}."), VenueName));
		return;
	}

	// Enough is enough: standing at a counter after the need it answers is
	// full only spends money.
	if (const float* Need = NeedFor(VenueActivity))
	{
		if (*Need >= SatisfiedAt)
		{
			FText Line;
			switch (VenueActivity)
			{
			case EUEGT2Activity::Eat:
			case EUEGT2Activity::Breakfast:
			case EUEGT2Activity::Lunch:
			case EUEGT2Activity::Dinner:   Line = LOCTEXT("DoneEat", "You have had enough to eat."); break;
			case EUEGT2Activity::Washroom: Line = LOCTEXT("DoneWash", "That is better."); break;
			case EUEGT2Activity::Rest:     Line = LOCTEXT("DoneRest", "You feel rested."); break;
			case EUEGT2Activity::Sleep:    Line = LOCTEXT("DoneSleep", "You wake up rested."); break;
			case EUEGT2Activity::Worship:  Line = LOCTEXT("DoneWorship", "You have sat long enough."); break;
			default: break;
			}
			StopActivity(Line);
		}
	}
}

void UUEGT2NeedsComponent::TickComponent(float DeltaTime, ELevelTick TickType,
	FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	const UUEGT2NPCDirector* Director = UUEGT2NPCDirector::Get(GetWorld());
	if (!Director)
	{
		return;
	}

	CheckVenueStillValid();
	SetActivity(Venue.IsValid() ? VenueActivity : IdleActivity(), FText::GetEmpty());

	// Frozen for a screenshot tour or the packaged walk smoke, for the same
	// reason the population and the sun are: a capture has to be repeatable,
	// and a player whose needs drain during one is not.
	if (Director->IsFrozen())
	{
		return;
	}

	// Integrate elapsed time at the shared rate. Scrubbing the clock changes
	// its position, not this delta; capping a hitch would lose hours of life.
	AdvanceLife(DeltaTime * Director->GetWorldHoursPerSecond());
}

void UUEGT2NeedsComponent::AdvanceLife(float WorldHours)
{
	if (WorldHours <= 0.0f)
	{
		return;
	}

	// The same call every inhabitant makes, with the same table behind it.
	if (!UEGT2AdvanceLife(WorldHours, Activity, Trade, Needs, Purse) && !bWarnedBroke)
	{
		bWarnedBroke = true;
		StopActivity(FText::Format(
			LOCTEXT("RanOut", "You have run out of coin. {0} will not serve you."),
			VenueName.IsEmpty() ? LOCTEXT("Here", "They") : VenueName));
	}
}

float UUEGT2NeedsComponent::GetExertionScale() const
{
	if (Needs.Energy >= TiredEnergy)
	{
		return 1.0f;
	}
	return FMath::Lerp(WornOutScale, 1.0f,
		FMath::Clamp(Needs.Energy / FMath::Max(TiredEnergy, KINDA_SMALL_NUMBER), 0.0f, 1.0f));
}

FText UUEGT2NeedsComponent::GetActivityText() const
{
	const FText Verb = GetActivityDisplayName(Activity);
	if (VenueName.IsEmpty())
	{
		return Verb;
	}
	// Three of the activity names already name a place - "at the tavern", "at
	// the market", "at the church" - and "at the tavern at The Fairhaven Inn"
	// is not English. Where the name has a place in it, the specific one wins.
	if (Verb.ToString().StartsWith(TEXT("at ")))
	{
		return FText::Format(LOCTEXT("AtVenue", "at {0}"), VenueName);
	}
	return FText::Format(LOCTEXT("DoingAt", "{0} at {1}"), Verb, VenueName);
}

#undef LOCTEXT_NAMESPACE
