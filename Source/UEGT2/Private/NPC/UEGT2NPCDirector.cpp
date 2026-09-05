#include "NPC/UEGT2NPCDirector.h"

#include "Diagnostics/UEGT2CaptureSubsystem.h"
#include "Engine/EngineTypes.h"
#include "DrawDebugHelpers.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "HAL/IConsoleManager.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "NPC/UEGT2NPCActor.h"
#include "NPC/UEGT2NPCSpeech.h"
#include "Settings/UEGT2GameUserSettings.h"
#include "UEGT2LogChannels.h"
#include "World/UEGT2SkyController.h"

#define LOCTEXT_NAMESPACE "UEGT2NPC"

// Named rather than anonymous: see the note in UEGT2NPCTypes.cpp.
namespace UEGT2Director
{
	/** How long a bubble stays up, by length. Long lines need longer to read. */
	float HoldSecondsFor(const FText& Line)
	{
		const int32 Length = Line.ToString().Len();
		return FMath::Clamp(2.1f + Length * 0.055f, 2.4f, 6.2f);
	}

	/** The pause before the words appear, which is what sells "message". */
	float TypingSecondsFor(uint32 Seed)
	{
		return 0.45f + UEGT2HashUnit(Seed, 0x7A17u) * 0.7f;
	}
}

// ---------------------------------------------------------------------------
// Subsystem
// ---------------------------------------------------------------------------
UUEGT2NPCDirector* UUEGT2NPCDirector::Get(const UWorld* World)
{
	return World ? World->GetSubsystem<UUEGT2NPCDirector>() : nullptr;
}

bool UUEGT2NPCDirector::DoesSupportWorldType(const EWorldType::Type WorldType) const
{
	// Game and PIE only. An editor world would tick the whole population while
	// somebody is trying to edit the map.
	return WorldType == EWorldType::Game || WorldType == EWorldType::PIE;
}

TStatId UUEGT2NPCDirector::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(UUEGT2NPCDirector, STATGROUP_Tickables);
}

void UUEGT2NPCDirector::OnWorldBeginPlay(UWorld& InWorld)
{
	Super::OnWorldBeginPlay(InWorld);

	Sky = AUEGT2SkyController::Get(&InWorld);
	Hour = Sky ? Sky->GetTimeOfDay() : 10.5f;
	PreviousHour = Hour;
	Weather = Sky ? Sky->GetWeather() : EUEGT2Weather::Clear;
	PreviousWeather = Weather;

	// A screenshot tour that produced a different image every run would be
	// useless, and a town full of people walking about is exactly that. So a
	// capture places everyone where the clock says they should be and then
	// stops them dead. The day/night cycle is frozen for the same reason; see
	// AUEGT2SkyController.
	//
	// -UEGT2LiveNPCs opts out, for the one job the frozen version cannot do:
	// looking at the speech bubbles. Such a capture is not reproducible, which
	// is exactly why it is not the default.
	const bool bCapturing = UUEGT2CaptureSubsystem::IsCaptureRequested()
		|| UUEGT2CaptureSubsystem::IsWalkSmokeRequested();
	const bool bForceLive = FParse::Param(FCommandLine::Get(), TEXT("UEGT2LiveNPCs"));
	bFrozen = bCapturing && !bForceLive;
	bLogSpeech = bForceLive;
	if (bForceLive)
	{
		UE_LOG(LogUEGT2NPC, Log,
			TEXT("Live inhabitants forced: not reproducible, and every line is logged."));
	}

	RefreshFromSettings();
	UUEGT2GameUserSettings::OnSettingsApplied.AddUObject(this, &UUEGT2NPCDirector::RefreshFromSettings);

	RegisterConsoleCommands();

	UE_LOG(LogUEGT2NPC, Log, TEXT("NPC director ready: %d registered, %s, %.2f h, density %.0f%%."),
		Population.Num(), bFrozen ? TEXT("frozen for capture") : TEXT("live"),
		Hour, CrowdDensity * 100.0f);
}

void UUEGT2NPCDirector::Deinitialize()
{
	UUEGT2GameUserSettings::OnSettingsApplied.RemoveAll(this);
	Population.Reset();
	LastNeedsHours.Reset();
	Conversations.Reset();
	Super::Deinitialize();
}

void UUEGT2NPCDirector::RegisterNPC(AUEGT2NPCActor* NPC)
{
	if (NPC && !Population.Contains(NPC))
	{
		Population.Add(NPC);
		LastNeedsHours.Add(NPC, SimulatedHours);
	}
}

void UUEGT2NPCDirector::UnregisterNPC(AUEGT2NPCActor* NPC)
{
	const int32 Index = Population.IndexOfByKey(NPC);
	if (Index != INDEX_NONE)
	{
		AdvanceNeedsToNow(NPC);
		Population.RemoveAt(Index);
		LastNeedsHours.Remove(NPC);
		// Removing an earlier entry shifts the next inhabitant left too.
		if (Index < SliceCursor) { --SliceCursor; }
		if (SliceCursor >= Population.Num()) { SliceCursor = 0; }
	}
	Conversations.RemoveAll([NPC](const FUEGT2Conversation& Chat)
	{
		return Chat.Opener.Get() == NPC || Chat.Responder.Get() == NPC;
	});
}

// ---------------------------------------------------------------------------
// Tick
// ---------------------------------------------------------------------------
void UUEGT2NPCDirector::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

	UWorld* World = GetWorld();
	if (!World || Population.Num() == 0)
	{
		return;
	}

	RefreshWorldFacts();

	// The first tick is the only place everyone is guaranteed to have begun
	// play, which is when it is safe to put the whole town where the clock says
	// it should be. Loading at half past ten should show a market, not two
	// hundred people standing on their own doorsteps.
	if (!bSnapped)
	{
		bSnapped = true;
		SnapEveryone();
		ApplyCrowdDensity();
		ReportCountdown = 12.0f;
		if (bFrozen)
		{
			// Nothing moves and nothing speaks: the capture is reproducible.
			for (AUEGT2NPCActor* NPC : Population)
			{
				if (NPC) { NPC->SetLOD(EUEGT2NPCLOD::Dormant); }
			}
			return;
		}
	}

	// The report is deliberately after the freeze check as well as before it:
	// a frozen capture still wants to know whether anybody is standing there.
	if (ReportCountdown > 0.0f)
	{
		// Frame rate, measured over the second half of the window only. The
		// first six seconds after a level loads are shader compilation and
		// streaming hitches; averaging those in reports a frame rate the game
		// never actually runs at. This is the one cheap performance number the
		// project can collect without a human watching a counter.
		ReportCountdown -= DeltaTime;
		if (ReportCountdown <= 6.0f)
		{
			++ReportFrames;
			ReportSeconds += DeltaTime;
		}
		if (ReportCountdown <= 0.0f)
		{
			ReportCountdown = -1.0f;
			LogPopulationReport();
		}
	}

	if (bFrozen)
	{
		return;
	}
	// Integrate each frame at its own clock rate. Multiplying the latest slice
	// interval by six overcharged a hitch's slice and undercharged the others;
	// small populations also take fewer than six passes to visit everybody.
	SimulatedHours += static_cast<double>(DeltaTime) * GetWorldHoursPerSecond();

	LodCountdown -= DeltaTime;
	if (LodCountdown <= 0.0f)
	{
		LodCountdown = LodPeriod;
		UpdateLODs();
	}

	ScheduleCountdown -= DeltaTime;
	if (ScheduleCountdown <= 0.0f)
	{
		ScheduleCountdown = SchedulePeriod;
		RunScheduleSlice();
	}

	SpeechCountdown -= DeltaTime;
	if (SpeechCountdown <= 0.0f)
	{
		SpeechCountdown = SpeechPeriod;
		UpdateSpeech();
	}

	UpdateConversations();

	if (bDebugOverlay)
	{
		DrawDebug();
	}
}

float UUEGT2NPCDirector::GetWorldHoursPerSecond() const
{
	const float DayLengthMinutes = Sky && Sky->IsDayNightCycleEnabled()
		? Sky->GetDayLengthMinutes() : 0.0f;
	return DayLengthMinutes > KINDA_SMALL_NUMBER ? 24.0f / (DayLengthMinutes * 60.0f) : 0.0f;
}

void UUEGT2NPCDirector::RefreshWorldFacts()
{
	if (!Sky)
	{
		Sky = AUEGT2SkyController::Get(GetWorld());
	}

	PreviousHour = Hour;
	if (Sky)
	{
		Hour = Sky->GetTimeOfDay();
		Weather = Sky->GetWeather();
	}

	// Midnight rollover. Compared against a large backward jump rather than
	// "hour < previous" so scrubbing the dev time slider a few minutes back
	// does not invent a new day.
	if (Hour + 12.0f < PreviousHour)
	{
		++DayIndex;
		UE_LOG(LogUEGT2NPC, Log, TEXT("Day %d begins%s."), DayIndex,
			IsMarketDay(DayIndex) ? TEXT(" - market day")
			: IsRestDay(DayIndex) ? TEXT(" - rest day") : TEXT(""));
	}

	// The view point, not the pawn.
	//
	// In play these are the same thing to within eye height. They differ during
	// a screenshot tour, which parks the pawn at the player start and flies a
	// separate camera around - and an NPC that reacts to a pawn standing in a
	// field two hundred metres away, while the camera is in the middle of the
	// market, is reacting to nothing anyone can see.
	if (APlayerController* PC = UGameplayStatics::GetPlayerController(GetWorld(), 0))
	{
		FRotator ViewRotation = FRotator::ZeroRotator;
		PC->GetPlayerViewPoint(PlayerLocation, ViewRotation);
		bHasPlayer = true;
	}
	else if (const APawn* Player = UGameplayStatics::GetPlayerPawn(GetWorld(), 0))
	{
		PlayerLocation = Player->GetActorLocation();
		bHasPlayer = true;
	}
	else
	{
		bHasPlayer = false;
	}
}

FUEGT2NPCContext UUEGT2NPCDirector::MakeContext() const
{
	FUEGT2NPCContext Context;
	Context.Hour = Hour;
	Context.DayIndex = DayIndex;
	Context.Weather = Weather;
	Context.PlayerLocation = PlayerLocation;
	Context.PlayerDistance = bHasPlayer ? 0.0f : 1.0e9f;   // filled in per NPC
	return Context;
}

void UUEGT2NPCDirector::UpdateLODs()
{
	NearCount = 0;
	const float NearSq = NearRadius * NearRadius;
	const float MidSq = MidRadius * MidRadius;
	const float FarSq = FarRadius * FarRadius;

	for (AUEGT2NPCActor* NPC : Population)
	{
		if (!NPC || NPC->IsSuppressed())
		{
			continue;
		}
		if (!bHasPlayer)
		{
			NPC->SetLOD(EUEGT2NPCLOD::Dormant);
			continue;
		}

		const float DistanceSq = FVector::DistSquared(NPC->GetActorLocation(), PlayerLocation);
		EUEGT2NPCLOD Tier = EUEGT2NPCLOD::Dormant;
		if (DistanceSq < NearSq)      { Tier = EUEGT2NPCLOD::Near; ++NearCount; }
		else if (DistanceSq < MidSq)  { Tier = EUEGT2NPCLOD::Mid; }
		else if (DistanceSq < FarSq)  { Tier = EUEGT2NPCLOD::Far; }
		NPC->SetLOD(Tier);
	}
}

void UUEGT2NPCDirector::AdvanceNeedsToNow(AUEGT2NPCActor* NPC)
{
	if (double* LastHours = LastNeedsHours.Find(NPC))
	{
		const float Elapsed = static_cast<float>(SimulatedHours - *LastHours);
		*LastHours = SimulatedHours;
		if (!NPC->IsSuppressed())
		{
			NPC->AdvanceNeeds(Elapsed);
		}
	}
}

void UUEGT2NPCDirector::RunScheduleSlice()
{
	if (Population.Num() == 0)
	{
		return;
	}

	const int32 SliceSize = FMath::Max(1, FMath::DivideAndRoundUp(Population.Num(), ScheduleSlices));
	const int32 Start = SliceCursor;
	const int32 End = FMath::Min(Start + SliceSize, Population.Num());

	FUEGT2NPCContext Context = MakeContext();
	for (int32 Index = Start; Index < End; ++Index)
	{
		AUEGT2NPCActor* NPC = Population[Index];
		if (!NPC)
		{
			continue;
		}

		AdvanceNeedsToNow(NPC);

		if (NPC->IsSuppressed() || bSchedulesPaused)
		{
			continue;
		}

		Context.PlayerDistance = bHasPlayer
			? FVector::Dist(NPC->GetActorLocation(), PlayerLocation) : 1.0e9f;
		NPC->EvaluateSchedule(Context, false);
	}

	SliceCursor = (End >= Population.Num()) ? 0 : End;
}

// ---------------------------------------------------------------------------
// Speech
// ---------------------------------------------------------------------------
bool UUEGT2NPCDirector::CanSpeakNow(const AUEGT2NPCActor* NPC, float MinCooldown) const
{
	if (!NPC || NPC->IsSuppressed() || NPC->IsIndoors() || NPC->HasBubble())
	{
		return false;
	}
	// Personal cooldown, spread by seed so the same handful of NPCs are not
	// always the ones who happen to be off cooldown first.
	const float Personal = MinCooldown + UEGT2HashUnit((uint32)NPC->GetSeed(), 0x3C3Cu) * 24.0f;
	return NPC->GetSecondsSinceSpoke() > Personal;
}

void UUEGT2NPCDirector::SpeakAnnounce(AUEGT2NPCActor* NPC, EUEGT2SpeechMood Mood, uint32 Variation)
{
	if (!NPC)
	{
		return;
	}
	const FText Line = GetSpeechLine(NPC->GetNPCRole(), NPC->GetSpecies(), NPC->GetActivity(),
		Mood, Weather, Hour, (uint32)NPC->GetSeed(), Variation);
	if (Line.IsEmpty())
	{
		return;
	}
	NPC->Say(Line, UEGT2Director::HoldSecondsFor(Line),
		UEGT2Director::TypingSecondsFor((uint32)NPC->GetSeed() + Variation));
	LastSpeechTime = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0f;

	if (bLogSpeech)
	{
		UE_LOG(LogUEGT2NPC, Log, TEXT("%s (%.0f m): \"%s\""),
			*NPC->GetDisplayName().ToString(),
			FVector::Dist(NPC->GetActorLocation(), PlayerLocation) / 100.0f,
			*Line.ToString());
	}
}

void UUEGT2NPCDirector::UpdateSpeech()
{
	UWorld* World = GetWorld();
	if (!World || !bHasPlayer)
	{
		return;
	}
	const UUEGT2GameUserSettings* Settings = UUEGT2GameUserSettings::Get();
	if (Settings && !Settings->GetShowSpeechBubbles())
	{
		return;
	}

	const float Now = World->GetTimeSeconds();
	if (Now - LastSpeechTime < GlobalSpeechGap)
	{
		return;
	}
	if (GetSpeakingCount() >= MaxConcurrentBubbles)
	{
		return;
	}

	// Only NPCs close enough to read are candidates. Everything else in this
	// function is about picking the most interesting one of those.
	TArray<AUEGT2NPCActor*> Candidates;
	Candidates.Reserve(24);
	const float RadiusSq = BubbleRadius * BubbleRadius;
	for (AUEGT2NPCActor* NPC : Population)
	{
		if (!NPC || NPC->IsSuppressed() || NPC->IsIndoors())
		{
			continue;
		}
		if (FVector::DistSquared(NPC->GetActorLocation(), PlayerLocation) < RadiusSq)
		{
			Candidates.Add(NPC);
		}
	}
	if (Candidates.Num() == 0)
	{
		return;
	}

	// 1. The weather just turned. Somebody remarks on it. This is the cheapest
	//    possible way to make a storm feel like it happened to the town rather
	//    than to the renderer.
	if (Weather != PreviousWeather)
	{
		PreviousWeather = Weather;
		for (AUEGT2NPCActor* NPC : Candidates)
		{
			if (!NPC->IsAnimal() && CanSpeakNow(NPC, 6.0f)
				&& NPC->GetPersonality().Sociability > 0.35f)
			{
				SpeakAnnounce(NPC, EUEGT2SpeechMood::Comment);
				return;
			}
		}
	}

	// 2. The player is right there and nobody has said hello.
	AUEGT2NPCActor* Nearest = nullptr;
	float NearestSq = 900.0f * 900.0f;
	for (AUEGT2NPCActor* NPC : Candidates)
	{
		const float DistanceSq = FVector::DistSquared(NPC->GetActorLocation(), PlayerLocation);
		if (DistanceSq < NearestSq && !NPC->IsAnimal()
			&& NPC->GetPersonality().Sociability > 0.5f && CanSpeakNow(NPC, 34.0f))
		{
			Nearest = NPC;
			NearestSq = DistanceSq;
		}
	}
	if (Nearest)
	{
		SpeakAnnounce(Nearest, EUEGT2SpeechMood::Greet);
		return;
	}

	// 3. Somebody just changed what they are doing and is willing to say so.
	//    This is the main event: the announcement lands as they set off, so the
	//    player watches them do the thing they just described.
	for (AUEGT2NPCActor* NPC : Candidates)
	{
		if (!NPC->ConsumeActivityChanged())
		{
			continue;
		}
		const float Talkativeness = NPC->IsAnimal() ? 0.28f : NPC->GetPersonality().Sociability;
		if (UEGT2HashUnit((uint32)NPC->GetSeed(), (uint32)NPC->GetActivity(), 0x4B4Bu) > Talkativeness)
		{
			continue;
		}
		if (!CanSpeakNow(NPC, 18.0f))
		{
			continue;
		}
		SpeakAnnounce(NPC, EUEGT2SpeechMood::Announce);
		return;
	}

	// 4. Two people standing near each other start talking to each other.
	if (Conversations.Num() < 2)
	{
		for (int32 A = 0; A < Candidates.Num(); ++A)
		{
			AUEGT2NPCActor* First = Candidates[A];
			if (First->IsAnimal() || First->IsWalking()
				|| First->GetPersonality().Sociability < 0.4f || !CanSpeakNow(First, 20.0f))
			{
				continue;
			}
			for (int32 B = A + 1; B < Candidates.Num(); ++B)
			{
				AUEGT2NPCActor* Second = Candidates[B];
				if (Second->IsAnimal() || Second->IsWalking()
					|| Second->GetPersonality().Sociability < 0.4f || !CanSpeakNow(Second, 20.0f))
				{
					continue;
				}
				if (FVector::DistSquared(First->GetActorLocation(), Second->GetActorLocation())
					> 520.0f * 520.0f)
				{
					continue;
				}

				FUEGT2Conversation Chat;
				Chat.Opener = First;
				Chat.Responder = Second;
				Chat.StartTime = Now;
				Conversations.Add(Chat);
				SpeakAnnounce(First, EUEGT2SpeechMood::Announce, 7u);
				return;
			}
		}
	}

	// 5. Filler: an animal noise, or somebody muttering. Rare on purpose - the
	//    bubbles are worth reading precisely because they are not constant.
	const uint32 Slot = (uint32)FMath::FloorToInt(Now * 0.25f);
	AUEGT2NPCActor* Filler = Candidates[UEGT2HashSeed(Slot, 0x9F9Fu) % (uint32)Candidates.Num()];
	if (CanSpeakNow(Filler, Filler->IsAnimal() ? 26.0f : 55.0f))
	{
		if (Filler->IsAnimal())
		{
			// Animals are louder when something is happening to them.
			const bool bReacting = Filler->GetActivity() == EUEGT2Activity::Flee
				|| Filler->GetActivity() == EUEGT2Activity::Follow
				|| Filler->GetActivity() == EUEGT2Activity::Play;
			if (bReacting || UEGT2HashUnit(Slot, (uint32)Filler->GetSeed()) < 0.35f)
			{
				SpeakAnnounce(Filler, EUEGT2SpeechMood::Idle, Slot);
			}
		}
		else if (UEGT2HashUnit(Slot, (uint32)Filler->GetSeed(), 0xAFAFu) < 0.4f)
		{
			SpeakAnnounce(Filler, EUEGT2SpeechMood::Idle, Slot);
		}
	}
}

void UUEGT2NPCDirector::UpdateConversations()
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}
	const float Now = World->GetTimeSeconds();

	for (int32 Index = Conversations.Num() - 1; Index >= 0; --Index)
	{
		FUEGT2Conversation& Chat = Conversations[Index];
		AUEGT2NPCActor* Opener = Chat.Opener.Get();
		AUEGT2NPCActor* Responder = Chat.Responder.Get();

		if (!Opener || !Responder || Now - Chat.StartTime > 12.0f)
		{
			Conversations.RemoveAtSwap(Index);
			continue;
		}
		// The reply lands after the opener's bubble has been up long enough to
		// read, which is what makes it read as a reply rather than a chorus.
		if (!Chat.bReplied && Now - Chat.StartTime > 2.6f)
		{
			Chat.bReplied = true;
			if (!Responder->IsIndoors() && !Responder->IsSuppressed())
			{
				const FText Line = GetSpeechLine(Responder->GetNPCRole(), Responder->GetSpecies(),
					Responder->GetActivity(), EUEGT2SpeechMood::Reply, Weather, Hour,
					(uint32)Responder->GetSeed(), 3u);
				Responder->Say(Line, UEGT2Director::HoldSecondsFor(Line), 0.5f);
				LastSpeechTime = Now;
			}
		}
	}
}

void UUEGT2NPCDirector::GatherBubbles(const FVector& ViewLocation,
	TArray<FUEGT2SpeechBubble>& OutBubbles) const
{
	OutBubbles.Reset();
	const float RadiusSq = (BubbleRadius * 1.3f) * (BubbleRadius * 1.3f);

	for (const AUEGT2NPCActor* NPC : Population)
	{
		if (!NPC || !NPC->HasBubble() || NPC->IsSuppressed() || NPC->IsIndoors())
		{
			continue;
		}
		const float DistanceSq = FVector::DistSquared(NPC->GetActorLocation(), ViewLocation);
		if (DistanceSq > RadiusSq)
		{
			continue;
		}

		FUEGT2SpeechBubble Bubble;
		Bubble.WorldLocation = NPC->GetSpeechAnchor();
		Bubble.Speaker = NPC->IsAnimal()
			? GetSpeciesDisplayName(NPC->GetSpecies()) : NPC->GetDisplayName();
		Bubble.Line = NPC->GetSpokenLine();
		Bubble.Tint = NPC->GetBubbleTint();
		Bubble.Alpha = NPC->GetBubbleAlpha();
		Bubble.Distance = FMath::Sqrt(DistanceSq);
		Bubble.bTyping = NPC->IsTyping();
		Bubble.bAnimal = NPC->IsAnimal();
		OutBubbles.Add(Bubble);
	}

	// Farthest first, so the nearest bubble paints last and wins any overlap.
	OutBubbles.Sort([](const FUEGT2SpeechBubble& A, const FUEGT2SpeechBubble& B)
	{
		return A.Distance > B.Distance;
	});
}

int32 UUEGT2NPCDirector::TriggerChatter()
{
	int32 Spoke = 0;
	const float RadiusSq = (BubbleRadius * 1.2f) * (BubbleRadius * 1.2f);
	for (AUEGT2NPCActor* NPC : Population)
	{
		if (!NPC || NPC->IsSuppressed() || NPC->IsIndoors())
		{
			continue;
		}
		if (FVector::DistSquared(NPC->GetActorLocation(), PlayerLocation) > RadiusSq)
		{
			continue;
		}
		SpeakAnnounce(NPC, EUEGT2SpeechMood::Announce, (uint32)Spoke);
		++Spoke;
	}
	UE_LOG(LogUEGT2NPC, Log, TEXT("Chatter: %d NPCs announced their plans."), Spoke);
	return Spoke;
}

// ---------------------------------------------------------------------------
// Controls and counts
// ---------------------------------------------------------------------------
void UUEGT2NPCDirector::SetSchedulesPaused(bool bPaused)
{
	bSchedulesPaused = bPaused;
	UE_LOG(LogUEGT2NPC, Log, TEXT("NPC schedules %s."), bPaused ? TEXT("paused") : TEXT("running"));
}

void UUEGT2NPCDirector::SetDebugOverlay(bool bEnabled)
{
	bDebugOverlay = bEnabled;
}

void UUEGT2NPCDirector::SetCrowdDensity(float Density)
{
	CrowdDensity = FMath::Clamp(Density, 0.1f, 1.0f);
	ApplyCrowdDensity();
}

void UUEGT2NPCDirector::ApplyCrowdDensity()
{
	int32 Hidden = 0;
	for (AUEGT2NPCActor* NPC : Population)
	{
		if (!NPC)
		{
			continue;
		}
		// Deterministic: the same people are missing at the same density every
		// run, so turning it down and back up does not reshuffle the town.
		const bool bSuppress = UEGT2HashUnit((uint32)NPC->GetSeed(), 0x5171u) > CrowdDensity;
		if (bSuppress != NPC->IsSuppressed())
		{
			// Finish the active interval before hiding, and discard the hidden
			// interval before showing. Neither depends on when its slice ran.
			AdvanceNeedsToNow(NPC);
		}
		NPC->SetSuppressed(bSuppress);
		Hidden += bSuppress ? 1 : 0;
	}
	if (Hidden > 0)
	{
		UE_LOG(LogUEGT2NPC, Log, TEXT("Crowd density %.0f%%: %d of %d hidden."),
			CrowdDensity * 100.0f, Hidden, Population.Num());
	}
}

void UUEGT2NPCDirector::RefreshFromSettings()
{
	if (const UUEGT2GameUserSettings* Settings = UUEGT2GameUserSettings::Get())
	{
		SetCrowdDensity(Settings->GetCrowdDensity());
	}
}

void UUEGT2NPCDirector::SnapEveryone()
{
	FUEGT2NPCContext Context = MakeContext();
	for (AUEGT2NPCActor* NPC : Population)
	{
		if (!NPC)
		{
			continue;
		}
		Context.PlayerDistance = bHasPlayer
			? FVector::Dist(NPC->GetActorLocation(), PlayerLocation) : 1.0e9f;
		NPC->SnapToSchedule(Context);
		NPC->ConsumeActivityChanged();          // the initial placement is not news
	}
	UE_LOG(LogUEGT2NPC, Log, TEXT("Placed %d inhabitants for %.2f h (day %d%s)."),
		Population.Num(), Hour, DayIndex,
		IsMarketDay(DayIndex) ? TEXT(", market day")
		: IsRestDay(DayIndex) ? TEXT(", rest day") : TEXT(""));
}

void UUEGT2NPCDirector::LogPopulationReport()
{
	// Counts alone are not enough. The failure worth catching is "769 placed,
	// none of them anywhere near the player", which is what a movement bug
	// looks like from the outside - and which a headless capture would
	// otherwise report as a perfectly clean run over an empty town.
	int32 Visible = 0, Walking = 0, Indoors = 0;
	int32 Within100m = 0;
	float Nearest = FLT_MAX;
	const AUEGT2NPCActor* Closest = nullptr;

	for (const AUEGT2NPCActor* NPC : Population)
	{
		if (!NPC)
		{
			continue;
		}
		Indoors += NPC->IsIndoors() ? 1 : 0;
		Visible += (!NPC->IsIndoors() && !NPC->IsSuppressed()) ? 1 : 0;
		Walking += NPC->IsWalking() ? 1 : 0;
		if (!bHasPlayer)
		{
			continue;
		}
		const float Distance = FVector::Dist(NPC->GetActorLocation(), PlayerLocation);
		Within100m += (Distance < 10000.0f) ? 1 : 0;
		if (Distance < Nearest)
		{
			Nearest = Distance;
			Closest = NPC;
		}
	}

	// Two different ways to be in the wrong place, and they need telling apart.
	//
	// A short underfoot trace flags feet away from a surface. It also misses
	// when feet are below terrain, so a miss does not prove terrain is absent.
	//
	// *Raised* is more than two metres above the deepest surface below - which
	// catches a villager perched on a market awning, and also catches a
	// perfectly correct dockhand standing on a pier over a sloping seabed. So
	// it is counted per destination: "24 at the Dock" is the piers doing their
	// job, and "24 at the Market" is a bug.
	int32 Airborne = 0;
	int32 Raised = 0;
	TMap<FString, int32> AirborneBy;
	TArray<FString> GroundCheckExamples;
	float WorstPerch = 0.0f;
	const AUEGT2NPCActor* HighestPerched = nullptr;
	TMap<EUEGT2Anchor, int32> RaisedByAnchor;

	if (UWorld* World = GetWorld())
	{
		FCollisionObjectQueryParams ObjectParams;
		ObjectParams.AddObjectTypesToQuery(ECC_WorldStatic);

		for (const AUEGT2NPCActor* NPC : Population)
		{
			if (!NPC || NPC->IsIndoors() || NPC->IsSuppressed())
			{
				continue;
			}
			const FVector Location = NPC->GetActorLocation();
			FCollisionQueryParams Params(SCENE_QUERY_STAT(UEGT2NPCPerch), false, NPC);

			FHitResult Underfoot;
			if (!World->LineTraceSingleByObjectType(Underfoot, Location + FVector(0.0f, 0.0f, 30.0f),
				Location - FVector(0.0f, 0.0f, 60.0f), ObjectParams, Params))
			{
				++Airborne;
				AirborneBy.FindOrAdd(FString::Printf(TEXT("%s/%s"),
					*GetSpeciesDisplayName(NPC->GetSpecies()).ToString(),
					GetAnchorName(NPC->GetTargetAnchor()))) += 1;
				if (GroundCheckExamples.Num() < 5)
				{
					const TCHAR* Tier = NPC->GetLOD() == EUEGT2NPCLOD::Near ? TEXT("Near")
						: NPC->GetLOD() == EUEGT2NPCLOD::Mid ? TEXT("Mid")
						: NPC->GetLOD() == EUEGT2NPCLOD::Far ? TEXT("Far") : TEXT("Dormant");
					GroundCheckExamples.Add(FString::Printf(TEXT("%s at %s, %s, %s, target %s"),
						*NPC->GetDisplayName().ToString(), *Location.ToString(), Tier,
						*GetActivityDisplayName(NPC->GetActivity()).ToString(), GetAnchorName(NPC->GetTargetAnchor())));
				}
			}

			TArray<FHitResult> Hits;
			if (!World->LineTraceMultiByObjectType(Hits, Location + FVector(0.0f, 0.0f, 200.0f),
				Location - FVector(0.0f, 0.0f, 3000.0f), ObjectParams, Params))
			{
				continue;              // over water, or off the landscape
			}

			double Deepest = Location.Z;
			for (const FHitResult& Found : Hits)
			{
				Deepest = FMath::Min(Deepest, Found.ImpactPoint.Z);
			}
			const float Height = (float)(Location.Z - Deepest);
			if (Height > 200.0f)
			{
				++Raised;
				RaisedByAnchor.FindOrAdd(NPC->GetTargetAnchor()) += 1;
				if (Height > WorstPerch)
				{
					WorstPerch = Height;
					HighestPerched = NPC;
				}
			}
		}
	}

	const float MeanFps = (ReportSeconds > KINDA_SMALL_NUMBER)
		? ReportFrames / ReportSeconds : 0.0f;

	UE_LOG(LogUEGT2NPC, Log,
		TEXT("Population report at %.2f h: %d total, %d out, %d indoors, %d walking, "
			"%d within 100 m, %d talking. %.1f fps over the last %.1f s."),
		Hour, Population.Num(), Visible, Indoors, Walking, Within100m, GetSpeakingCount(),
		MeanFps, ReportSeconds);
	if (Closest)
	{
		UE_LOG(LogUEGT2NPC, Log, TEXT("Nearest is %s at %.0f m, %s, heading for the %s."),
			*Closest->GetDisplayName().ToString(), Nearest / 100.0f,
			*GetActivityDisplayName(Closest->GetActivity()).ToString(),
			GetAnchorName(Closest->GetTargetAnchor()));
	}

	if (Airborne > 0)
	{
		TArray<FString> Parts;
		for (const TPair<FString, int32>& Pair : AirborneBy)
		{
			Parts.Add(FString::Printf(TEXT("%s %d"), *Pair.Key, Pair.Value));
		}
		UE_LOG(LogUEGT2NPC, Warning, TEXT("%d inhabitant(s) failed the short underfoot ground check: %s"),
			Airborne, *FString::Join(Parts, TEXT(", ")));
		UE_LOG(LogUEGT2NPC, Warning, TEXT("Ground check examples (%d of %d): %s"),
			GroundCheckExamples.Num(), Airborne, *FString::Join(GroundCheckExamples, TEXT("; ")));
	}

	// How busy is each of the places we actually photograph?
	//
	// "The city feels empty" is a real report and an unmeasurable one until it
	// is a number per location. The tour viewpoints are exactly the spots a
	// player stands and looks around, so counting the inhabitants within sixty
	// metres of each one says which parts of the world are alive and which are
	// a diagram of a place.
	{
		TArray<FString> Busy;
		for (const FUEGT2Viewpoint& Point : UUEGT2CaptureSubsystem::GetTour())
		{
			int32 Here = 0;
			for (const AUEGT2NPCActor* NPC : Population)
			{
				if (!NPC || NPC->IsSuppressed())
				{
					continue;
				}
				const FVector Location = NPC->GetActorLocation();
				if (FVector2D::DistSquared(FVector2D(Location.X, Location.Y), Point.Location)
					< 6000.0f * 6000.0f)
				{
					++Here;
				}
			}
			Busy.Add(FString::Printf(TEXT("%s %d"), *Point.Name.ToString(), Here));
		}
		UE_LOG(LogUEGT2NPC, Log, TEXT("Within 60 m of each viewpoint: %s"),
			*FString::Join(Busy, TEXT(", ")));
	}

	// And what is the crowd near the player actually doing? A hundred and fifty
	// people all heading for the Market is the difference between a town and a
	// queue.
	if (bHasPlayer)
	{
		TMap<EUEGT2Anchor, int32> NearbyByAnchor;
		for (const AUEGT2NPCActor* NPC : Population)
		{
			if (!NPC || NPC->IsSuppressed() || NPC->IsIndoors())
			{
				continue;
			}
			if (FVector::Dist(NPC->GetActorLocation(), PlayerLocation) < 10000.0f)
			{
				NearbyByAnchor.FindOrAdd(NPC->GetTargetAnchor()) += 1;
			}
		}
		NearbyByAnchor.ValueSort([](int32 A, int32 B) { return A > B; });

		TArray<FString> Parts;
		for (const TPair<EUEGT2Anchor, int32>& Pair : NearbyByAnchor)
		{
			Parts.Add(FString::Printf(TEXT("%s %d"), GetAnchorName(Pair.Key), Pair.Value));
		}
		UE_LOG(LogUEGT2NPC, Log, TEXT("Within 100 m of the player, heading for: %s"),
			*FString::Join(Parts, TEXT(", ")));
	}

	if (Raised > 0)
	{
		TArray<FString> Parts;
		for (const TPair<EUEGT2Anchor, int32>& Pair : RaisedByAnchor)
		{
			Parts.Add(FString::Printf(TEXT("%s %d"), GetAnchorName(Pair.Key), Pair.Value));
		}
		UE_LOG(LogUEGT2NPC, Log,
			TEXT("%d standing over 2 m up (%s). Highest: %s at %.1f m, %s, heading for the %s."),
			Raised, *FString::Join(Parts, TEXT(", ")),
			HighestPerched ? *HighestPerched->GetDisplayName().ToString() : TEXT("?"),
			WorstPerch / 100.0f,
			HighestPerched ? *GetActivityDisplayName(HighestPerched->GetActivity()).ToString() : TEXT("?"),
			HighestPerched ? GetAnchorName(HighestPerched->GetTargetAnchor()) : TEXT("?"));
	}
	else if (Airborne == 0)
	{
		UE_LOG(LogUEGT2NPC, Log, TEXT("Everybody has their feet on the ground."));
	}
}

int32 UUEGT2NPCDirector::GetPeopleCount() const
{
	int32 Count = 0;
	for (const AUEGT2NPCActor* NPC : Population)
	{
		Count += (NPC && !NPC->IsAnimal()) ? 1 : 0;
	}
	return Count;
}

int32 UUEGT2NPCDirector::GetAnimalCount() const
{
	int32 Count = 0;
	for (const AUEGT2NPCActor* NPC : Population)
	{
		Count += (NPC && NPC->IsAnimal()) ? 1 : 0;
	}
	return Count;
}

int32 UUEGT2NPCDirector::GetActiveCount() const
{
	int32 Count = 0;
	for (const AUEGT2NPCActor* NPC : Population)
	{
		Count += (NPC && !NPC->IsIndoors() && !NPC->IsSuppressed()) ? 1 : 0;
	}
	return Count;
}

int32 UUEGT2NPCDirector::GetSpeakingCount() const
{
	int32 Count = 0;
	for (const AUEGT2NPCActor* NPC : Population)
	{
		Count += (NPC && NPC->HasBubble()) ? 1 : 0;
	}
	return Count;
}

FText UUEGT2NPCDirector::GetDayLabel() const
{
	if (IsMarketDay(DayIndex)) { return LOCTEXT("MarketDay", "market day"); }
	if (IsRestDay(DayIndex))   { return LOCTEXT("RestDay", "rest day"); }
	return FText::GetEmpty();
}

void UUEGT2NPCDirector::DrawDebug() const
{
	const UWorld* World = GetWorld();
	if (!World || !bHasPlayer)
	{
		return;
	}
	const float RadiusSq = 9000.0f * 9000.0f;

	for (const AUEGT2NPCActor* NPC : Population)
	{
		if (!NPC || NPC->IsSuppressed())
		{
			continue;
		}
		if (FVector::DistSquared(NPC->GetActorLocation(), PlayerLocation) > RadiusSq)
		{
			continue;
		}

		static const FColor ReasonColours[] = {
			FColor(190, 190, 190),   // Schedule
			FColor(110, 170, 255),   // Weather
			FColor(255, 200, 90),    // Player
			FColor(255, 130, 130),   // Need
			FColor(160, 255, 160),   // DayOfWeek
			FColor(220, 150, 255),   // Detour
		};
		const int32 ReasonIndex = FMath::Clamp((int32)NPC->GetActivityReason(), 0,
			(int32)UE_ARRAY_COUNT(ReasonColours) - 1);
		const FColor Colour = ReasonColours[ReasonIndex];

		DrawDebugString(const_cast<UWorld*>(World),
			NPC->GetActorLocation() + FVector(0, 0, 210.0f),
			FString::Printf(TEXT("%s\n%s -> %s"),
				*NPC->GetDisplayName().ToString(),
				*GetActivityDisplayName(NPC->GetActivity()).ToString(),
				GetAnchorName(NPC->GetTargetAnchor())),
			nullptr, Colour, 0.0f, true, 0.9f);

		DrawDebugLine(const_cast<UWorld*>(World), NPC->GetActorLocation(),
			NPC->GetDestination(), Colour, false, 0.0f, 0, 1.5f);
	}
}

// ---------------------------------------------------------------------------
void UUEGT2NPCDirector::RegisterConsoleCommands()
{
	static bool bRegistered = false;
	if (bRegistered)
	{
		return;
	}
	bRegistered = true;

	IConsoleManager& Console = IConsoleManager::Get();

	Console.RegisterConsoleCommand(TEXT("uegt2.NPC.Stats"),
		TEXT("Print the population, how many are out, and how many are talking."),
		FConsoleCommandWithWorldDelegate::CreateLambda([](UWorld* World)
		{
			if (const UUEGT2NPCDirector* Director = UUEGT2NPCDirector::Get(World))
			{
				UE_LOG(LogUEGT2NPC, Display,
					TEXT("Population %d (%d people, %d animals). Out %d, near %d, talking %d. %.2f h, day %d."),
					Director->GetPopulation(), Director->GetPeopleCount(), Director->GetAnimalCount(),
					Director->GetActiveCount(), Director->GetNearCount(), Director->GetSpeakingCount(),
					Director->GetHour(), Director->GetDayIndex());
			}
		}), ECVF_Default);

	Console.RegisterConsoleCommand(TEXT("uegt2.NPC.Chatter"),
		TEXT("Make everyone nearby announce what they are about to do."),
		FConsoleCommandWithWorldDelegate::CreateLambda([](UWorld* World)
		{
			if (UUEGT2NPCDirector* Director = UUEGT2NPCDirector::Get(World))
			{
				Director->TriggerChatter();
			}
		}), ECVF_Default);

	Console.RegisterConsoleCommand(TEXT("uegt2.NPC.Debug"),
		TEXT("uegt2.NPC.Debug 0|1 - draw each nearby NPC's plan in the world."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda(
			[](const TArray<FString>& Args, UWorld* World)
		{
			if (UUEGT2NPCDirector* Director = UUEGT2NPCDirector::Get(World))
			{
				const bool bEnabled = Args.Num() == 0 || FCString::Atoi(*Args[0]) != 0;
				Director->SetDebugOverlay(bEnabled);
			}
		}), ECVF_Cheat);

	Console.RegisterConsoleCommand(TEXT("uegt2.NPC.Density"),
		TEXT("uegt2.NPC.Density 0.1-1 - how much of the population is present."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda(
			[](const TArray<FString>& Args, UWorld* World)
		{
			UUEGT2NPCDirector* Director = UUEGT2NPCDirector::Get(World);
			if (Director && Args.Num() > 0)
			{
				Director->SetCrowdDensity(FCString::Atof(*Args[0]));
			}
		}), ECVF_Default);
}

#undef LOCTEXT_NAMESPACE
