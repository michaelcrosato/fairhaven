#include "Diagnostics/UEGT2RestSmokeSubsystem.h"

#include "Diagnostics/UEGT2CaptureSubsystem.h"
#include "Engine/GameViewportClient.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Framework/Application/SlateApplication.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformMisc.h"
#include "HAL/PlatformTime.h"
#include "ImageUtils.h"
#include "Input/Events.h"
#include "Interaction/UEGT2Amenity.h"
#include "Interaction/UEGT2InteractionComponent.h"
#include "Layout/Children.h"
#include "Misc/CommandLine.h"
#include "Misc/FileHelper.h"
#include "Misc/Guid.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "NPC/UEGT2NPCActor.h"
#include "NPC/UEGT2NPCDirector.h"
#include "Player/UEGT2Character.h"
#include "Player/UEGT2NeedsComponent.h"
#include "Player/UEGT2PlayerController.h"
#include "Progress/UEGT2ProgressSubsystem.h"
#include "Rest/UEGT2RestSubsystem.h"
#include "Settings/UEGT2GameUserSettings.h"
#include "UEGT2LogChannels.h"
#include "UnrealClient.h"
#include "Widgets/Text/STextBlock.h"
#include "World/UEGT2Almanac.h"
#include "World/UEGT2SkyController.h"

namespace UEGT2RestSmoke
{
	FUEGT2NPCNeeds StartingNeeds()
	{
		FUEGT2NPCNeeds Needs;
		Needs.Energy = 0.22f; Needs.Fed = 0.87f; Needs.Relief = 0.71f; Needs.Company = 0.63f;
		return Needs;
	}

	bool Equal(const FUEGT2NPCNeeds& A, const FUEGT2NPCNeeds& B, float Tolerance = 0.0001f)
	{
		return FMath::IsNearlyEqual(A.Energy, B.Energy, Tolerance) && FMath::IsNearlyEqual(A.Fed, B.Fed, Tolerance)
			&& FMath::IsNearlyEqual(A.Relief, B.Relief, Tolerance) && FMath::IsNearlyEqual(A.Company, B.Company, Tolerance);
	}

	struct FContext
	{
		AUEGT2PlayerController* PC = nullptr;
		AUEGT2Character* Player = nullptr;
		UUEGT2NeedsComponent* Life = nullptr;
		UUEGT2NPCDirector* Director = nullptr;
		AUEGT2SkyController* Sky = nullptr;
		UUEGT2RestSubsystem* Rest = nullptr;
		UUEGT2GameUserSettings* Settings = nullptr;
		explicit FContext(UWorld* World)
		{
			PC = World ? Cast<AUEGT2PlayerController>(World->GetFirstPlayerController()) : nullptr;
			Player = PC ? Cast<AUEGT2Character>(PC->GetPawn()) : nullptr;
			Life = Player ? Player->GetLife() : nullptr;
			Director = UUEGT2NPCDirector::Get(World);
			Sky = AUEGT2SkyController::Get(World);
			Rest = UUEGT2RestSubsystem::Get(World);
			Settings = UUEGT2GameUserSettings::Get();
		}
		bool IsValid() const { return PC && Player && Life && Life->HasBegunPlay() && Director && Sky && Rest && Settings; }
	};

	FString StateDelta(const FContext& C, const FUEGT2NPCNeeds& Needs, const FUEGT2Purse& Purse, int32 Day, float Hour)
	{
		const FUEGT2NPCNeeds& Now = C.Life->GetNeeds();
		return FString::Printf(TEXT("day=%d->%d hour=%.9f->%.9f energy_delta=%.9f fed_delta=%.9f relief_delta=%.9f company_delta=%.9f coins_delta=%.9f panel=%d paused=%d menu=%d"),
			Day, C.Director->GetDayIndex(), Hour, C.Director->GetHour(), Now.Energy - Needs.Energy,
			Now.Fed - Needs.Fed, Now.Relief - Needs.Relief, Now.Company - Needs.Company, C.Life->GetPurse().Coins - Purse.Coins,
			C.PC->IsRestPanelOpen(), C.PC->GetWorld()->IsPaused(), static_cast<int32>(C.PC->GetMenuState()));
	}

	TSharedPtr<SWidget> FindButton(const TSharedRef<SWidget>& Widget, const FText& Caption, int32& Budget, FString& Seen, int32 Depth = 0)
	{
		if (--Budget < 0 || Depth > 32 || !Widget->GetVisibility().IsVisible()) { return nullptr; }
		FChildren* Children = Widget->GetChildren();
		if (Widget->GetType() == TEXT("SButton") && Children->Num() == 1)
		{
			const TSharedRef<SWidget> Label = Children->GetChildAt(0);
			if (Label->GetType() == TEXT("STextBlock"))
			{
				const FString Text = StaticCastSharedRef<STextBlock>(Label)->GetText().ToString();
				if (Seen.Len() < 1024) { Seen += FString::Printf(TEXT("['%s' enabled=%d] "), *Text.Left(80), Widget->IsEnabled()); }
				if (Text == Caption.ToString()) { return Widget; }
			}
		}
		for (int32 Index = 0; Index < Children->Num() && Budget > 0; ++Index)
		{
			if (TSharedPtr<SWidget> Found = FindButton(Children->GetChildAt(Index), Caption, Budget, Seen, Depth + 1)) { return Found; }
		}
		return nullptr;
	}

	bool HasText(const TSharedRef<SWidget>& Widget, const FString& Expected, int32& Budget, FString& Seen, int32 Depth = 0)
	{
		if (--Budget < 0 || Depth > 32 || !Widget->GetVisibility().IsVisible()) { return false; }
		if (Widget->GetType() == TEXT("STextBlock"))
		{
			const FString Text = StaticCastSharedRef<STextBlock>(Widget)->GetText().ToString();
			if (Seen.Len() < 1024) { Seen += FString::Printf(TEXT("['%s'] "), *Text.Left(120)); }
			if (Text == Expected) { return true; }
		}
		FChildren* Children = Widget->GetChildren();
		for (int32 Index = 0; Index < Children->Num() && Budget > 0; ++Index)
		{
			if (HasText(Children->GetChildAt(Index), Expected, Budget, Seen, Depth + 1)) { return true; }
		}
		return false;
	}
}

bool UUEGT2RestSmokeSubsystem::ShouldCreateSubsystem(UObject* Outer) const
{
	return FParse::Param(FCommandLine::Get(), TEXT("UEGT2RestSmoke")) && Super::ShouldCreateSubsystem(Outer);
}

bool UUEGT2RestSmokeSubsystem::DoesSupportWorldType(EWorldType::Type WorldType) const { return WorldType == EWorldType::Game; }

TStatId UUEGT2RestSmokeSubsystem::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(UUEGT2RestSmokeSubsystem, STATGROUP_Tickables);
}

void UUEGT2RestSmokeSubsystem::OnWorldBeginPlay(UWorld& InWorld)
{
	Super::OnWorldBeginPlay(InWorld);
	bRequested = FParse::Param(FCommandLine::Get(), TEXT("UEGT2RestSmoke"));
	StartedSeconds = FPlatformTime::Seconds();
	FString UserDirectory, OtherPhase;
	FParse::Value(FCommandLine::Get(), TEXT("UserDir="), UserDirectory);
	FPaths::NormalizeDirectoryName(UserDirectory);
	RunId = FPaths::GetCleanFilename(UserDirectory);
	FGuid Guid;
	FString Expected = FPaths::ConvertRelativePathToFull(FPaths::Combine(FPaths::ProjectDir(), TEXT("Saved/RestSmoke"), RunId));
	FPaths::NormalizeDirectoryName(Expected);
	if (!Check(FGuid::ParseExact(RunId, EGuidFormats::Digits, Guid) && !FPaths::IsRelative(UserDirectory)
		&& UserDirectory.Equals(Expected, ESearchCase::IgnoreCase), TEXT("expected isolated packaged Saved/RestSmoke/<guid> UserDir"))) { return; }
	if (!Check(!UUEGT2CaptureSubsystem::IsCaptureRequested() && !UUEGT2CaptureSubsystem::IsWalkSmokeRequested()
		&& !UUEGT2CaptureSubsystem::IsFlySoakRequested() && !FParse::Param(FCommandLine::Get(), TEXT("UEGT2SurveySmoke"))
		&& !FParse::Value(FCommandLine::Get(), TEXT("UEGT2ProgressSmoke="), OtherPhase), TEXT("rest smoke cannot share another diagnostic run"))) { return; }
	FParse::Value(FCommandLine::Get(), TEXT("UEGT2RestCapture="), CaptureDirectory);
	FParse::Value(FCommandLine::Get(), TEXT("ResX="), ExpectedWidth);
	FParse::Value(FCommandLine::Get(), TEXT("ResY="), ExpectedHeight);
	if (!CaptureDirectory.IsEmpty())
	{
		if (!Check(!FPaths::IsRelative(CaptureDirectory) && FPaths::GetCleanFilename(CaptureDirectory) == RunId
			&& IFileManager::Get().MakeDirectory(*CaptureDirectory, true), TEXT("invalid or unwritable rest capture directory"))) { return; }
		ScreenshotHandle = UGameViewportClient::OnScreenshotCaptured().AddUObject(this, &UUEGT2RestSmokeSubsystem::HandleScreenshot);
	}
	UE_LOG(LogUEGT2Diag, Log, TEXT("Rest smoke starting: run=%s resolution=%dx%d"), *RunId, ExpectedWidth, ExpectedHeight);
}

void UUEGT2RestSmokeSubsystem::Deinitialize()
{
	UGameViewportClient::OnScreenshotCaptured().Remove(ScreenshotHandle);
	RestorePreferences();
	Super::Deinitialize();
}

void UUEGT2RestSmokeSubsystem::RestorePreferences()
{
	if (!bPreferencesChanged) { return; }
	UEGT2RestSmoke::FContext C(GetWorld());
	if (C.Settings)
	{
		C.Settings->SetSaveProgressEnabled(bOriginalSavePreference);
		C.Settings->SetSleepUntilEnabled(bOriginalRestPreference);
	}
	if (C.Rest) { C.Rest->bFeatureEnabled = bOriginalFeatureEnabled; }
	if (C.Sky) { C.Sky->SetDayLengthMinutes(OriginalDayLength); C.Sky->SetDayNightCycleEnabled(bOriginalClockEnabled); }
	if (C.Director) { C.Director->SetCrowdDensity(OriginalDensity); }
	bPreferencesChanged = false;
}

bool UUEGT2RestSmokeSubsystem::Check(bool bCondition, const TCHAR* Reason)
{
	if (!bCondition) { Finish(false, Reason); }
	return bCondition;
}

void UUEGT2RestSmokeSubsystem::Finish(bool bSuccess, const TCHAR* Reason)
{
	if (bFinished) { return; }
	bFinished = true;
	RestorePreferences();
	if (bSuccess) { UE_LOG(LogUEGT2Diag, Log, TEXT("UEGT2_REST_SMOKE_COMPLETE run=%s skip_ms=%.3f population=%d %s"), *RunId, SkipMilliseconds, WakePopulation.Num(), Reason); }
	else { UE_LOG(LogUEGT2Diag, Error, TEXT("UEGT2_REST_SMOKE_FAILED run=%s %s"), *RunId, Reason); }
	FPlatformMisc::RequestExitWithStatus(false, bSuccess ? 0 : 1);
}

void UUEGT2RestSmokeSubsystem::SetStep(EStep NextStep) { Step = NextStep; StepStartedSeconds = FPlatformTime::Seconds(); }

void UUEGT2RestSmokeSubsystem::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);
	if (bFinished) { return; }
	const double Now = FPlatformTime::Seconds();
	if (Now - StartedSeconds > 150.0) { Finish(false, TEXT("rest smoke exceeded 150 seconds")); return; }
	if (Step == EStep::Startup) { if (Now - StartedSeconds >= 8.0) { StartCheck(); } return; }
	// Inspect the first live frame as well as the later population slices.
	if ((Step == EStep::AwakeImage || Step == EStep::DebtCheck) && !CheckWakeSnapshot()) { return; }
	if (Step == EStep::PanelImage || Step == EStep::AwakeImage)
	{
		if (bScreenshotComplete) { Advance(); }
		else if (!bScreenshotRequested && Now - StepStartedSeconds >= 1.5)
		{
			bScreenshotRequested = true; PendingFile = CaptureFile;
			FScreenshotRequest::RequestScreenshot(true);
		}
		else if (Now - StepStartedSeconds > 30.0) { Finish(false, TEXT("rest screenshot callback timed out")); }
	}
	else if (Step == EStep::DebtCheck) { if (Now - WakeStartedSeconds >= 3.0) { Advance(); } }
	else if (Step == EStep::LiveClock)
	{
		LiveElapsed += DeltaTime; LargestLiveFrame = FMath::Max(LargestLiveFrame, DeltaTime);
		if (LiveElapsed >= 3.0f) { Advance(); }
	}
	else if (Now - StepStartedSeconds >= 0.3) { Advance(); }
}

void UUEGT2RestSmokeSubsystem::StartCheck()
{
	using namespace UEGT2RestSmoke;
	FContext C(GetWorld());
	if (!Check(C.IsValid() && C.Player->GetInteraction() && FSlateApplication::IsInitialized(), TEXT("rest smoke world context unavailable"))) { return; }
	if (!Check(C.PC->GetMenuState() == EUEGT2MenuState::None && !C.Director->IsFrozen() && !C.Director->AreSchedulesPaused(),
		TEXT("rest smoke needs ordinary gameplay and live schedules"))) { return; }
	bOriginalSavePreference = C.Settings->GetSaveProgressEnabled();
	bOriginalRestPreference = C.Settings->GetSleepUntilEnabled();
	bOriginalFeatureEnabled = C.Rest->bFeatureEnabled;
	OriginalDensity = C.Director->GetCrowdDensity();
	OriginalDayLength = C.Sky->GetDayLengthMinutes();
	bOriginalClockEnabled = C.Sky->IsDayNightCycleEnabled();
	bPreferencesChanged = true;
	C.Settings->SetSaveProgressEnabled(false);
	C.Settings->SetSleepUntilEnabled(true);
	C.Director->SetCrowdDensity(1.0f);
	C.Sky->SetDayLengthMinutes(20.0f);
	C.Sky->SetDayNightCycleEnabled(true);
	if (!Check(C.Rest->IsEnabled() && !C.PC->IsProgressEnabled(), TEXT("rest or progress gate does not match smoke preferences"))) { return; }
	double BestDistance = TNumericLimits<double>::Max();
	for (TActorIterator<AUEGT2Amenity> It(GetWorld()); It; ++It)
	{
		const double Distance = FVector::DistSquared(C.Player->GetActorLocation(), It->GetActorLocation());
		if (It->GetKind() == EUEGT2AmenityKind::Bed && (Distance < BestDistance
			|| (Distance == BestDistance && Bed.IsValid() && It->GetName() < Bed->GetName()))) { Bed = *It; BestDistance = Distance; }
	}
	if (!Check(Bed.IsValid(), TEXT("no generated Bed amenity found")) || !PositionAtBed()) { return; }
	SetStep(EStep::Approach);
}

bool UUEGT2RestSmokeSubsystem::PositionAtBed()
{
	UEGT2RestSmoke::FContext C(GetWorld());
	if (!Check(C.IsValid() && Bed.IsValid(), TEXT("bed or player disappeared"))) { return false; }
	const FVector Point = Bed->GetInteractionPoint();
	const FVector Ground = Bed->GetActorLocation();
	for (int32 Attempt = 0; Attempt < 8; ++Attempt)
	{
		const float Angle = FMath::DegreesToRadians(((NextApproach + Attempt) % 8) * 45.0f);
		const FVector Candidate(Point.X + FMath::Cos(Angle) * 175.0f, Point.Y + FMath::Sin(Angle) * 175.0f, Ground.Z + 95.0f);
		FCollisionQueryParams Params(SCENE_QUERY_STAT(UEGT2RestSmoke), false, C.Player);
		FHitResult Hit;
		if (GetWorld()->LineTraceSingleByChannel(Hit, Candidate + FVector(0.0f, 0.0f, 68.0f), Point, ECC_Visibility, Params)
			&& Hit.GetActor() != Bed.Get()) { continue; }
		NextApproach = (NextApproach + Attempt + 1) % 8;
		if (!Check(C.Player->TeleportTo(Candidate, C.Player->GetActorRotation(), false, true), TEXT("cannot position player beside bed"))) { return false; }
		C.Player->GetCharacterMovement()->StopMovementImmediately();
		C.PC->SetControlRotation((Point - C.Player->GetPawnViewLocation()).Rotation());
		return true;
	}
	return Check(false, TEXT("all eight bed approaches are blocked"));
}

bool UUEGT2RestSmokeSubsystem::UseBed()
{
	UEGT2RestSmoke::FContext C(GetWorld());
	if (!Check(C.IsValid() && Bed.IsValid() && C.Player->GetInteraction(), TEXT("interaction context disappeared"))) { return false; }
	UUEGT2InteractionComponent* Probe = C.Player->GetInteraction();
	if (C.Rest->IsEnabled() && !C.Player->GetCharacterMovement()->IsMovingOnGround())
	{
		if (++ProbeAttempts >= 8) { return Check(false, TEXT("player did not settle on the ground beside bed")); }
		StepStartedSeconds = FPlatformTime::Seconds();
		return false;
	}
	if (Probe->GetFocusedActor() != Bed.Get())
	{
		if (++ProbeAttempts >= 8) { return Check(false, TEXT("real interaction probe could not focus bed after eight approaches")); }
		if (PositionAtBed()) { StepStartedSeconds = FPlatformTime::Seconds(); }
		return false;
	}
	ProbeAttempts = 0;
	return Check(Probe->TryInteract(), TEXT("real interaction probe refused the bed"));
}

bool UUEGT2RestSmokeSubsystem::CheckFocusedButton(const FText& Caption)
{
	const TSharedPtr<SWidget> Root = PanelRoot.Pin();
	const TSharedPtr<SWidget> FocusedBefore = FSlateApplication::Get().GetKeyboardFocusedWidget();
	const FString RootType = Root.IsValid() ? Root->GetTypeAsString() : TEXT("None");
	const FString FocusedType = FocusedBefore.IsValid() ? FocusedBefore->GetTypeAsString() : TEXT("None");
	if (!Check(Root.IsValid(), *FString::Printf(TEXT("rest button '%s': root=%s focused=%s"), *Caption.ToString(), *RootType, *FocusedType))) { return false; }
	int32 Budget = 512;
	FString Seen;
	const TSharedPtr<SWidget> Button = UEGT2RestSmoke::FindButton(Root.ToSharedRef(), Caption, Budget, Seen);
	UEGT2RestSmoke::FContext C(GetWorld());
	FText Reason;
	const bool bCanSleep = C.Rest && C.Rest->CanSleepAt(C.PC, Bed.Get(), Reason);
	const FString Detail = FString::Printf(TEXT("caption='%s' root=%s focused=%s visited=%d found=%d enabled=%d can_sleep=%d reason='%s' buttons=%s"),
		*Caption.ToString(), *RootType, *FocusedType, 512 - Budget, Button.IsValid(), Button.IsValid() && Button->IsEnabled(),
		bCanSleep, *Reason.ToString(), *Seen);
	if (!Check(Button.IsValid() && Button->IsEnabled(), *FString::Printf(TEXT("rest button lookup failed: %s"), *Detail))) { return false; }
	if (!Check(FSlateApplication::Get().GetKeyboardFocusedWidget() == Button,
		*FString::Printf(TEXT("rest button focus failed: %s"), *Detail))) { return false; }
	UE_LOG(LogUEGT2Diag, Log, TEXT("Rest smoke natural focus: %s"), *Detail);
	return true;
}

bool UUEGT2RestSmokeSubsystem::CheckVisibleHour(int32 Hour)
{
	const TSharedPtr<SWidget> Root = PanelRoot.Pin();
	const FString Expected = UEGT2FormatClock(static_cast<float>(Hour)).ToString();
	int32 Budget = 512;
	FString Seen;
	const bool bFound = Root.IsValid() && UEGT2RestSmoke::HasText(Root.ToSharedRef(), Expected, Budget, Seen);
	return Check(bFound,
		*FString::Printf(TEXT("rest hour did not change to %s through gamepad A: visible text=%s"), *Expected, *Seen));
}

bool UUEGT2RestSmokeSubsystem::SendGamepadKey(FKey Key)
{
	const FKeyEvent Event(Key, FModifierKeysState(), 0, false, 0, 0);
	const bool bDown = FSlateApplication::Get().ProcessKeyDownEvent(Event);
	const bool bUp = FSlateApplication::Get().ProcessKeyUpEvent(Event);
	UE_LOG(LogUEGT2Diag, Log, TEXT("Rest smoke gamepad: key=%s down_handled=%d up_handled=%d"), *Key.ToString(), bDown, bUp);
	// Navigation may be handled after the ordinary key reply; its next-step
	// focused-caption assertion is the proof that a D-pad event moved focus.
	return Key != EKeys::Gamepad_FaceButton_Bottom || Check(bDown && bUp,
		*FString::Printf(TEXT("rest button did not accept gamepad A: down=%d up=%d"), bDown, bUp));
}

bool UUEGT2RestSmokeSubsystem::CapturePopulation(TArray<FUEGT2RestSmokeNPCState>& Out)
{
	Out.Reset();
	UEGT2RestSmoke::FContext C(GetWorld());
	for (TActorIterator<AUEGT2NPCActor> It(GetWorld()); It; ++It)
	{
		if (!Check(!It->IsSuppressed() && UUEGT2NeedsComponent::IsValidProgress(It->GetNeeds(), It->GetPurse(), It->GetNPCRole()),
			TEXT("full population contains suppressed NPC or invalid needs/purse"))) { return false; }
		Out.Add({ *It, It->GetNeeds(), It->GetPurse() });
	}
	return Check(C.Director && Out.Num() >= 700 && Out.Num() == C.Director->GetPopulation()
		&& C.Director->GetPeopleCount() + C.Director->GetAnimalCount() == Out.Num(), TEXT("smoke did not include the full generated population"));
}

bool UUEGT2RestSmokeSubsystem::CheckWakeSnapshot()
{
	using namespace UEGT2RestSmoke;
	FContext C(GetWorld());
	if (!Check(C.IsValid() && Equal(C.Life->GetNeeds(), SnapshotNeeds) && FMath::IsNearlyEqual(C.Life->GetPurse().Coins, SnapshotPurse.Coins, 0.0001f)
		&& C.Director->GetDayIndex() == 8 && FMath::IsNearlyEqual(C.Director->GetHour(), 8.0f, 0.0001f),
		TEXT("player or calendar changed after waking with clock frozen"))) { return false; }
	for (const FUEGT2RestSmokeNPCState& State : WakePopulation)
	{
		const AUEGT2NPCActor* NPC = State.Actor.Get();
		if (!Check(NPC && Equal(NPC->GetNeeds(), State.Needs) && FMath::IsNearlyEqual(NPC->GetPurse().Coins, State.Purse.Coins, 0.0001f),
			TEXT("NPC received a duplicate or deferred life charge after waking"))) { return false; }
	}
	return true;
}

void UUEGT2RestSmokeSubsystem::Advance()
{
	using namespace UEGT2RestSmoke;
	FContext C(GetWorld());
	if (!Check(C.IsValid() && Bed.IsValid(), TEXT("rest smoke context disappeared"))) { return; }
	switch (Step)
	{
	case EStep::Approach:
		if (!UseBed()) { return; }
		if (!Check(C.PC->IsRestPanelOpen() && GetWorld()->IsPaused(), TEXT("bed interaction did not open a paused rest panel"))) { return; }
		SnapshotNeeds = C.Life->GetNeeds(); SnapshotPurse = C.Life->GetPurse();
		SnapshotDay = C.Director->GetDayIndex(); SnapshotHour = C.Director->GetHour();
		SetStep(EStep::CancelReady);
		break;
	case EStep::CancelReady:
		if (!Check(C.PC->IsRestPanelOpen() && GetWorld()->IsPaused(), TEXT("rest panel did not remain paused while its opening frame settled"))) { return; }
		// The director may follow this subsystem in the opening frame. Baseline
		// after that frame finishes, then require another full paused interval.
		UE_LOG(LogUEGT2Diag, Log, TEXT("Rest smoke opening frame settled: %s"),
			*StateDelta(C, SnapshotNeeds, SnapshotPurse, SnapshotDay, SnapshotHour));
		SnapshotNeeds = C.Life->GetNeeds(); SnapshotPurse = C.Life->GetPurse();
		SnapshotDay = C.Director->GetDayIndex(); SnapshotHour = C.Director->GetHour();
		SetStep(EStep::Cancel);
		break;
	case EStep::Cancel:
	{
		auto CheckCancelState = [&](bool bOpen, const TCHAR* Stage)
		{
			return Check(C.PC->IsRestPanelOpen() == bOpen && GetWorld()->IsPaused() == bOpen
				&& C.PC->GetMenuState() == (bOpen ? EUEGT2MenuState::Pause : EUEGT2MenuState::None)
				&& Equal(C.Life->GetNeeds(), SnapshotNeeds) && C.Life->GetPurse().Coins == SnapshotPurse.Coins
				&& C.Director->GetDayIndex() == SnapshotDay && C.Director->GetHour() == SnapshotHour,
				*FString::Printf(TEXT("%s: %s"), Stage, *StateDelta(C, SnapshotNeeds, SnapshotPurse, SnapshotDay, SnapshotHour)));
		};
		if (!CheckCancelState(true, TEXT("paused interval changed state before cancel"))) { return; }
		const FKeyEvent Escape(EKeys::Escape, FModifierKeysState(), 0, false, 0, 0);
		FSlateApplication::Get().ProcessKeyDownEvent(Escape);
		FSlateApplication::Get().ProcessKeyUpEvent(Escape);
		if (!CheckCancelState(false, TEXT("cancel changed state or left the panel paused"))) { return; }
		SetStep(EStep::Reopen);
		break;
	}
	case EStep::Reopen:
	{
		if (!UseBed()) { return; }
		if (!Check(C.PC->IsRestPanelOpen() && GetWorld()->IsPaused(), TEXT("second bed interaction did not reopen rest panel"))) { return; }
		if (!Check(C.Life->RestoreProgress(StartingNeeds(), FUEGT2Purse(137.625f), EUEGT2NPCRole::Smith)
			&& C.Director->RestoreCalendar(7, 8.0f, EUEGT2Weather::Clear), TEXT("cannot prepare paused 24-hour rest fixture"))) { return; }
		FUEGT2RestPreview Preview;
		FText Reason;
		if (!Check(C.Rest->GetPreview(C.PC, Bed.Get(), 8, Preview, Reason) && Preview.StartDayIndex == 7
			&& Preview.WakeDayIndex == 8 && Preview.WakeHour == 8 && FMath::IsNearlyEqual(Preview.DurationHours, 24.0f), TEXT("same chosen hour did not preview a full day"))) { return; }
		ExpectedNeeds = C.Life->GetNeeds(); ExpectedPurse = C.Life->GetPurse();
		UEGT2AdvanceLife(24.0f, EUEGT2Activity::Sleep, C.Life->GetTrade(), ExpectedNeeds, ExpectedPurse);
		if (!CapturePopulation(BeforePopulation)) { return; }
		SetStep(EStep::PreparePanel);
		break;
	}
	case EStep::PreparePanel:
	{
		// SetInputMode queues a LocalPlayer Slate reply. Read focus after that
		// reply has run; caching it in Reopen retains the old game viewport.
		const TSharedPtr<SWidget> Focused = FSlateApplication::Get().GetKeyboardFocusedWidget();
		TSharedPtr<SWidget> Root = Focused;
		for (int32 Depth = 0; Root.IsValid() && Root->GetType() != TEXT("SUEGT2Menu") && Depth < 32; ++Depth) { Root = Root->GetParentWidget(); }
		if (!Check(Root.IsValid() && Root->GetType() == TEXT("SUEGT2Menu"),
			*FString::Printf(TEXT("rest panel focus did not settle within SUEGT2Menu: actual=%s panel=%d paused=%d"),
				Focused.IsValid() ? *Focused->GetTypeAsString() : TEXT("None"), C.PC->IsRestPanelOpen(), GetWorld()->IsPaused()))) { return; }
		PanelRoot = Root;
		if (!CheckFocusedButton(NSLOCTEXT("UEGT2RestPanel", "Cancel", "Cancel"))) { return; }
		SendGamepadKey(EKeys::Gamepad_DPad_Up);
		SetStep(EStep::EarlierFocus);
		break;
	}
	case EStep::EarlierFocus:
		if (!CheckFocusedButton(NSLOCTEXT("UEGT2RestPanel", "PreviousHour", "< Earlier"))) { return; }
		SendGamepadKey(EKeys::Gamepad_DPad_Right);
		SetStep(EStep::LaterFocus);
		break;
	case EStep::LaterFocus:
		if (!CheckFocusedButton(NSLOCTEXT("UEGT2RestPanel", "NextHour", "Later >")) || !SendGamepadKey(EKeys::Gamepad_FaceButton_Bottom)) { return; }
		SetStep(EStep::FirstHour);
		break;
	case EStep::FirstHour:
		if (!CheckVisibleHour(7) || !CheckFocusedButton(NSLOCTEXT("UEGT2RestPanel", "NextHour", "Later >"))
			|| !SendGamepadKey(EKeys::Gamepad_FaceButton_Bottom)) { return; }
		SetStep(EStep::SecondHour);
		break;
	case EStep::SecondHour:
		if (!CheckVisibleHour(8)) { return; }
		SendGamepadKey(EKeys::Gamepad_DPad_Down);
		SetStep(EStep::SleepFocus);
		break;
	case EStep::SleepFocus:
		if (!CheckFocusedButton(NSLOCTEXT("UEGT2RestPanel", "Sleep", "Sleep"))) { return; }
		BeginCapture(EStep::PanelImage, TEXT("01_RestPanel.png"));
		break;
	case EStep::PanelImage:
	{
		if (!CheckFocusedButton(NSLOCTEXT("UEGT2RestPanel", "Sleep", "Sleep"))) { return; }
		const double Before = FPlatformTime::Seconds();
		if (!SendGamepadKey(EKeys::Gamepad_FaceButton_Bottom)) { return; }
		SkipMilliseconds = (FPlatformTime::Seconds() - Before) * 1000.0;
		// Stop only clock advancement, after the real operation returns. NPC and
		// player ticks remain active, exposing stale per-actor elapsed-time debt.
		C.Sky->SetDayNightCycleEnabled(false);
		WakeStartedSeconds = FPlatformTime::Seconds();
		if (!Check(!C.PC->IsRestPanelOpen() && !GetWorld()->IsPaused() && C.PC->GetMenuState() == EUEGT2MenuState::None
			&& !C.Life->IsUsing(Bed.Get()) && C.Life->GetActivity() != EUEGT2Activity::Sleep
			&& Equal(C.Life->GetNeeds(), ExpectedNeeds) && FMath::IsNearlyEqual(C.Life->GetPurse().Coins, ExpectedPurse.Coins, 0.0001f)
			&& C.Director->GetDayIndex() == 8 && FMath::IsNearlyEqual(C.Director->GetHour(), 8.0f, 0.0001f)
			&& FMath::IsNearlyEqual(C.Sky->GetTimeOfDay(), 8.0f, 0.0001f), TEXT("24-hour rest did not restore gameplay with the shared sleep ledger and calendar"))) { return; }
		if (!CapturePopulation(WakePopulation) || !Check(WakePopulation.Num() == BeforePopulation.Num(), TEXT("rest changed population size"))) { return; }
		int32 Changed = 0;
		for (const FUEGT2RestSmokeNPCState& State : BeforePopulation)
		{
			const AUEGT2NPCActor* NPC = State.Actor.Get();
			if (NPC && (!Equal(State.Needs, NPC->GetNeeds()) || !FMath::IsNearlyEqual(State.Purse.Coins, NPC->GetPurse().Coins))) { ++Changed; }
		}
		if (!Check(Changed > 0, TEXT("whole day passed without advancing any NPC ledger"))) { return; }
		SnapshotNeeds = C.Life->GetNeeds(); SnapshotPurse = C.Life->GetPurse();
		UE_LOG(LogUEGT2Diag, Log, TEXT("Rest smoke: 24 hours advanced in %.3f ms; population=%d changed=%d."), SkipMilliseconds, WakePopulation.Num(), Changed);
		BeginCapture(EStep::AwakeImage, TEXT("02_Awake.png"));
		break;
	}
	case EStep::AwakeImage:
		SetStep(EStep::DebtCheck);
		break;
	case EStep::DebtCheck:
		SnapshotHour = C.Director->GetHour(); SnapshotDay = C.Director->GetDayIndex();
		SnapshotNeeds = C.Life->GetNeeds(); SnapshotPurse = C.Life->GetPurse();
		C.Sky->SetDayLengthMinutes(4.0f); C.Sky->SetDayNightCycleEnabled(true);
		LiveElapsed = LargestLiveFrame = 0.0f;
		SetStep(EStep::LiveClock);
		break;
	case EStep::LiveClock:
	{
		const float Hours = (C.Director->GetDayIndex() - SnapshotDay) * 24.0f + C.Director->GetHour() - SnapshotHour;
		FUEGT2NPCNeeds LiveNeeds = SnapshotNeeds;
		FUEGT2Purse LivePurse = SnapshotPurse;
		UEGT2AdvanceLife(Hours, EUEGT2Activity::Idle, C.Life->GetTrade(), LiveNeeds, LivePurse);
		const float Tolerance = FMath::Max(0.003f, 0.03f * (LargestLiveFrame + 0.15f));
		if (!Check(Hours > 0.05f && Hours < 1.0f && C.Life->GetNeeds().Energy < SnapshotNeeds.Energy
			&& Equal(C.Life->GetNeeds(), LiveNeeds, Tolerance) && FMath::IsNearlyEqual(C.Life->GetPurse().Coins, LivePurse.Coins, 0.001f),
			TEXT("woken player no longer follows the live world clock"))) { return; }
		C.Settings->SetSleepUntilEnabled(false);
		C.Life->RestoreProgress(StartingNeeds(), FUEGT2Purse(137.625f), EUEGT2NPCRole::Smith);
		if (!PositionAtBed()) { return; }
		SetStep(EStep::PlayerOff);
		break;
	}
	case EStep::PlayerOff:
	case EStep::HardOff:
	{
		const bool bHard = Step == EStep::HardOff;
		if (!UseBed()) { return; }
		FUEGT2RestPreview Preview;
		FText Reason;
		if (!Check(!C.Rest->IsEnabled() && C.Rest->IsAvailable() != bHard
			&& !C.Rest->GetPreview(C.PC, Bed.Get(), 8, Preview, Reason) && !C.PC->IsRestPanelOpen() && !GetWorld()->IsPaused()
			&& C.Life->IsUsing(Bed.Get()) && C.Life->GetActivity() == EUEGT2Activity::Sleep,
			TEXT("disabled chosen-hour feature did not restore ordinary bed Sleep"))) { return; }
		SetStep(bHard ? EStep::HardSleeping : EStep::PlayerSleeping);
		break;
	}
	case EStep::PlayerSleeping:
	case EStep::HardSleeping:
	{
		const bool bHard = Step == EStep::HardSleeping;
		if (!Check(C.Life->IsUsing(Bed.Get()) && C.Life->GetActivity() == EUEGT2Activity::Sleep,
			TEXT("ordinary disabled-path Sleep did not survive a live tick"))) { return; }
		if (!UseBed()) { return; }
		if (!Check(!C.Life->IsUsing(Bed.Get()) && !C.PC->IsRestPanelOpen() && !GetWorld()->IsPaused(), TEXT("second bed use did not get up normally"))) { return; }
		if (bHard)
		{
			if (!Check(!C.PC->IsProgressEnabled(), TEXT("rest smoke enabled checkpoint saving"))) { return; }
			Finish(true, TEXT("real bed probe, cancel, 24-hour ledger, live wake and both ordinary-sleep fallbacks verified"));
		}
		else
		{
			C.Settings->SetSleepUntilEnabled(true); C.Rest->bFeatureEnabled = false;
			C.Life->RestoreProgress(StartingNeeds(), FUEGT2Purse(137.625f), EUEGT2NPCRole::Smith);
			if (PositionAtBed()) { SetStep(EStep::HardOff); }
		}
		break;
	}
	default: break;
	}
}

void UUEGT2RestSmokeSubsystem::BeginCapture(EStep NextStep, const TCHAR* FileName)
{
	SetStep(NextStep); CaptureFile = FPaths::Combine(CaptureDirectory, FileName);
	bScreenshotRequested = false; bScreenshotComplete = CaptureDirectory.IsEmpty();
}

void UUEGT2RestSmokeSubsystem::HandleScreenshot(int32 Width, int32 Height, const TArray<FColor>& Bitmap)
{
	if (PendingFile.IsEmpty() || bFinished) { return; }
	if (!Check(Width == ExpectedWidth && Height == ExpectedHeight && Bitmap.Num() == Width * Height, TEXT("rest screenshot resolution differs from request"))) { return; }
	TArray<FColor> Opaque = Bitmap;
	for (FColor& Pixel : Opaque) { Pixel.A = 255; }
	TArray64<uint8> Png;
	FImageUtils::PNGCompressImageArray(Width, Height, TArrayView64<const FColor>(Opaque.GetData(), Opaque.Num()), Png);
	if (!Check(Png.Num() > 0 && FFileHelper::SaveArrayToFile(Png, *PendingFile), TEXT("cannot write rest screenshot"))) { return; }
	UE_LOG(LogUEGT2Diag, Log, TEXT("Rest screenshot: %s"), *PendingFile);
	PendingFile.Reset(); bScreenshotComplete = true;
}
