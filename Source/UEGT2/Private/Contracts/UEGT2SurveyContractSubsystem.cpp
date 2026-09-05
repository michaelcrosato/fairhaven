#include "Contracts/UEGT2SurveyContractSubsystem.h"

#include "Contracts/UEGT2SurveyContract.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Interaction/UEGT2WorldInteractables.h"
#include "Player/UEGT2Character.h"
#include "Player/UEGT2NeedsComponent.h"
#include "Player/UEGT2PlayerController.h"
#include "Settings/UEGT2GameUserSettings.h"
#include "UEGT2LogChannels.h"

#define LOCTEXT_NAMESPACE "UEGT2SurveyContract"

namespace UEGT2SurveyContract
{
	constexpr int32 RequiredCount = 3;
	bool IsFinite(const FVector& Point)
	{
		return FMath::IsFinite(Point.X) && FMath::IsFinite(Point.Y) && FMath::IsFinite(Point.Z);
	}
	const AUEGT2Character* GetPlayer(const UWorld* World, const AUEGT2PlayerController* Controller)
	{
		if (!IsValid(Controller) || Controller->IsActorBeingDestroyed() || Controller->GetWorld() != World
			|| !Controller->IsLocalController()) { return nullptr; }
		const AUEGT2Character* Player = Cast<AUEGT2Character>(Controller->GetPawn());
		return IsValid(Player) && !Player->IsActorBeingDestroyed() && Player->HasActorBegunPlay()
			&& Player->GetWorld() == World && Player->GetController() == Controller
			&& IsFinite(Player->GetActorLocation()) ? Player : nullptr;
	}
	FText FallbackName(int32 Index)
	{
		switch (Index)
		{
		case 0: return LOCTEXT("Harbour", "The Harbour");
		case 1: return LOCTEXT("Light", "Fairhaven Light");
		default: return LOCTEXT("Mill", "Mill Rise");
		}
	}
}

UUEGT2SurveyContractSubsystem* UUEGT2SurveyContractSubsystem::Get(const UWorld* World)
{
	return World ? World->GetSubsystem<UUEGT2SurveyContractSubsystem>() : nullptr;
}

bool UUEGT2SurveyContractSubsystem::DoesSupportWorldType(EWorldType::Type WorldType) const
{
	return WorldType == EWorldType::Game || WorldType == EWorldType::PIE;
}

TConstArrayView<FName> UUEGT2SurveyContractSubsystem::RequiredLandmarkIds()
{
	static const FName Ids[] = { TEXT("fairhaven_harbour"), TEXT("fairhaven_light"), TEXT("mill_rise") };
	return MakeArrayView(Ids);
}

float UUEGT2SurveyContractSubsystem::GetReward()
{
	return 2.0f * UEGT2WageFor(EUEGT2NPCRole::Courier, EUEGT2Activity::Errand);
}

bool UUEGT2SurveyContractSubsystem::IsAvailable() const { return bFeatureEnabled; }

bool UUEGT2SurveyContractSubsystem::IsEnabled() const
{
	const UUEGT2GameUserSettings* Settings = UUEGT2GameUserSettings::Get();
	return IsAvailable() && Settings && Settings->GetTownSurveyContractEnabled();
}

TArray<FUEGT2SurveyContractEntry> UUEGT2SurveyContractSubsystem::GetEntries(const AUEGT2PlayerController* Controller) const
{
	using namespace UEGT2SurveyContract;
	TArray<FUEGT2SurveyContractEntry> Entries;
	const AUEGT2Character* Player = GetPlayer(GetWorld(), Controller);
	if (!IsEnabled() || !Player) { return Entries; }
	const TConstArrayView<FName> Ids = RequiredLandmarkIds();
	AUEGT2Landmark* Landmarks[RequiredCount] = {};
	int32 Counts[RequiredCount] = {};
	// Explicit page/claim requests resolve the fixed roster once. No ticking,
	// discovery ledger or cached strong actor ownership is necessary.
	for (TActorIterator<AUEGT2Landmark> It(GetWorld()); It; ++It)
	{
		if (It->IsActorBeingDestroyed()) { continue; }
		for (int32 Index = 0; Index < RequiredCount; ++Index)
		{
			if (It->GetPersistentId() == Ids[Index]) { ++Counts[Index]; Landmarks[Index] = *It; }
		}
	}
	FVector Origin;
	FRotator View;
	Controller->GetPlayerViewPoint(Origin, View);
	Entries.SetNum(RequiredCount);
	for (int32 Index = 0; Index < RequiredCount; ++Index)
	{
		FUEGT2SurveyContractEntry& Entry = Entries[Index];
		Entry.Id = Ids[Index];
		Entry.Name = FallbackName(Index);
		AUEGT2Landmark* Landmark = Landmarks[Index];
		Entry.bAvailable = Counts[Index] == 1 && IsValid(Landmark) && IsFinite(Landmark->GetActorLocation());
		if (!Entry.bAvailable) { continue; }
		if (!Landmark->GetLandmarkName().IsEmptyOrWhitespace()) { Entry.Name = Landmark->GetLandmarkName(); }
		Entry.bDiscovered = Landmark->IsDiscovered();
		Entry.bHasDirection = UUEGT2SurveySubsystem::CalculateDirection(Origin, Landmark->GetActorLocation(), View.Yaw, Entry.Direction);
		Entry.Direction.Id = Entry.Id;
		Entry.Direction.Name = Entry.Name;
	}
	return Entries;
}

bool UUEGT2SurveyContractSubsystem::CanOpenAt(const AUEGT2PlayerController* Controller,
	const AUEGT2SurveyContract* Board, FText& Reason) const
{
	Reason = FText::GetEmpty();
	if (!IsEnabled()) { Reason = LOCTEXT("Disabled", "Town Survey Contract is turned off. Your surveys and any payment are preserved."); return false; }
	const AUEGT2Character* Player = UEGT2SurveyContract::GetPlayer(GetWorld(), Controller);
	const UUEGT2NeedsComponent* Life = Player ? Player->GetLife() : nullptr;
	if (!Player || !IsValid(Life) || !Life->HasBegunPlay() || Controller->GetMenuState() == EUEGT2MenuState::Main)
	{
		Reason = LOCTEXT("NoPlayer", "Start a visit and read this sign in person."); return false;
	}
	if (!IsValid(Board) || Board->IsActorBeingDestroyed() || !Board->HasActorBegunPlay() || Board->GetWorld() != GetWorld())
	{
		Reason = LOCTEXT("NoBoard", "This contract sign is no longer available."); return false;
	}
	const double DistanceSquared = FVector::DistSquared(Player->GetActorLocation(), Board->GetActorLocation());
	const float Range = Board->GetUseRange();
	if (!FMath::IsFinite(DistanceSquared) || !FMath::IsFinite(Range) || Range <= 0.0f
		|| DistanceSquared > FMath::Square(static_cast<double>(Range)))
	{
		Reason = LOCTEXT("TooFar", "Return to the town survey sign to collect payment."); return false;
	}
	if (!UUEGT2NeedsComponent::IsValidProgress(Life->GetNeeds(), Life->GetPurse(), Life->GetTrade()))
	{
		Reason = LOCTEXT("InvalidLife", "Your current purse cannot receive payment."); return false;
	}
	return true;
}

bool UUEGT2SurveyContractSubsystem::CanClaim(const AUEGT2PlayerController* Controller,
	const AUEGT2SurveyContract* Board, FText& Reason) const
{
	if (!CanOpenAt(Controller, Board, Reason)) { return false; }
	if (!GetWorld()->IsPaused() || !Controller->IsSurveyContractOpen() || Controller->GetSurveyContractBoard() != Board)
	{
		Reason = LOCTEXT("OpenPage", "Read this sign and claim payment from its contract page."); return false;
	}
	if (bPaid) { Reason = LOCTEXT("Paid", "This contract has already been paid."); return false; }
	const TArray<FUEGT2SurveyContractEntry> Entries = GetEntries(Controller);
	if (Entries.Num() != UEGT2SurveyContract::RequiredCount)
	{
		Reason = LOCTEXT("NotReady", "The contract's survey places are not ready."); return false;
	}
	for (const FUEGT2SurveyContractEntry& Entry : Entries)
	{
		if (!Entry.bAvailable) { Reason = LOCTEXT("UnavailablePlace", "A required survey place is missing or ambiguous. Payment is unavailable."); return false; }
		if (!Entry.bDiscovered) { Reason = LOCTEXT("SurveyFirst", "Survey all three places, then return here for payment."); return false; }
	}
	FUEGT2Purse Candidate = CastChecked<AUEGT2Character>(Controller->GetPawn())->GetLife()->GetPurse();
	if (!UEGT2TryCredit(GetReward(), Candidate))
	{
		Reason = LOCTEXT("PurseLimit", "Your purse cannot hold the full payment. Spend some coins and return."); return false;
	}
	return true;
}

bool UUEGT2SurveyContractSubsystem::TryClaim(AUEGT2PlayerController* Controller, AUEGT2SurveyContract* Board, FText& Reason)
{
	if (!CanClaim(Controller, Board, Reason))
	{
		UE_LOG(LogUEGT2Contracts, Log, TEXT("Survey payment refused: %s"), *Reason.ToString());
		return false;
	}
	UUEGT2NeedsComponent* Life = CastChecked<AUEGT2Character>(Controller->GetPawn())->GetLife();
	// TryCredit emits no delegate, changes no activity and performs no latent
	// work. No observer can see a credited purse with an unpaid contract.
	if (!Life->TryCredit(GetReward())) { Reason = LOCTEXT("CreditFailed", "Payment could not be added to your purse."); return false; }
	bPaid = true;
	UE_LOG(LogUEGT2Contracts, Log, TEXT("Town survey contract paid: %.3f coins; purse %.3f."), GetReward(), Life->GetPurse().Coins);
	return true;
}

#undef LOCTEXT_NAMESPACE
