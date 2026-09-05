#include "Diagnostics/UEGT2HudSizeSmokeSubsystem.h"

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
#include "Interaction/UEGT2InteractionComponent.h"
#include "Interaction/UEGT2WorldInteractables.h"
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
#include "Settings/UEGT2GameUserSettings.h"
#include "Survey/UEGT2SurveySubsystem.h"
#include "UI/UEGT2HUD.h"
#include "UI/UEGT2HUDLayout.h"
#include "UEGT2LogChannels.h"
#include "UnrealClient.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Layout/SScrollBox.h"
#include "World/UEGT2SkyController.h"

namespace UEGT2HudSizeSmoke
{
	const FName Harbour(TEXT("fairhaven_harbour"));
	FText Speech() { return NSLOCTEXT("UEGT2HudSizeSmoke", "Speech", "The harbour is a pleasant walk from here. Follow the waterfront and you will find it."); }
	FUEGT2NPCNeeds Needs()
	{
		FUEGT2NPCNeeds Value;
		Value.Energy = 0.73f; Value.Fed = 0.42f; Value.Relief = 0.61f; Value.Company = 0.28f;
		return Value;
	}
	bool Equal(const FUEGT2NPCNeeds& A, const FUEGT2NPCNeeds& B)
	{
		return A.Energy == B.Energy && A.Fed == B.Fed && A.Relief == B.Relief && A.Company == B.Company;
	}
	struct FContext
	{
		AUEGT2PlayerController* PC = nullptr;
		AUEGT2Character* Player = nullptr;
		UUEGT2NeedsComponent* Life = nullptr;
		UUEGT2NPCDirector* Director = nullptr;
		AUEGT2SkyController* Sky = nullptr;
		UUEGT2SurveySubsystem* Survey = nullptr;
		AUEGT2HUD* Hud = nullptr;
		UUEGT2GameUserSettings* Settings = nullptr;
		explicit FContext(UWorld* World)
		{
			PC = World ? Cast<AUEGT2PlayerController>(World->GetFirstPlayerController()) : nullptr;
			Player = PC ? Cast<AUEGT2Character>(PC->GetPawn()) : nullptr;
			Life = Player ? Player->GetLife() : nullptr;
			Director = UUEGT2NPCDirector::Get(World); Sky = AUEGT2SkyController::Get(World);
			Survey = UUEGT2SurveySubsystem::Get(World); Hud = PC ? Cast<AUEGT2HUD>(PC->GetHUD()) : nullptr;
			Settings = UUEGT2GameUserSettings::Get();
		}
		bool IsValid() const { return PC && Player && Life && Life->HasBegunPlay() && Director && Sky && Survey && Hud && Settings; }
	};
	TSharedPtr<SWidget> FindText(const TSharedRef<SWidget>& Widget, const FText& Expected, int32& Budget, FString& Seen, int32 Depth = 0)
	{
		if (--Budget < 0 || Depth > 32 || !Widget->GetVisibility().IsVisible()) { return nullptr; }
		if (Widget->GetType() == TEXT("STextBlock"))
		{
			const FString Text = StaticCastSharedRef<STextBlock>(Widget)->GetText().ToString();
			if (Seen.Len() < 1600) { Seen += FString::Printf(TEXT("['%s'] "), *Text.Left(100)); }
			if (Text == Expected.ToString()) { return Widget; }
		}
		FChildren* Children = Widget->GetChildren();
		for (int32 Index = 0; Index < Children->Num() && Budget > 0; ++Index)
		{
			if (TSharedPtr<SWidget> Found = FindText(Children->GetChildAt(Index), Expected, Budget, Seen, Depth + 1)) { return Found; }
		}
		return nullptr;
	}
}

bool UUEGT2HudSizeSmokeSubsystem::ShouldCreateSubsystem(UObject* Outer) const
{
	return FParse::Param(FCommandLine::Get(), TEXT("UEGT2HudSizeSmoke")) && Super::ShouldCreateSubsystem(Outer);
}
bool UUEGT2HudSizeSmokeSubsystem::DoesSupportWorldType(EWorldType::Type Type) const { return Type == EWorldType::Game; }
TStatId UUEGT2HudSizeSmokeSubsystem::GetStatId() const { RETURN_QUICK_DECLARE_CYCLE_STAT(UUEGT2HudSizeSmokeSubsystem, STATGROUP_Tickables); }

void UUEGT2HudSizeSmokeSubsystem::OnWorldBeginPlay(UWorld& InWorld)
{
	Super::OnWorldBeginPlay(InWorld);
	bRequested = FParse::Param(FCommandLine::Get(), TEXT("UEGT2HudSizeSmoke"));
	StartedSeconds = FPlatformTime::Seconds();
	FString UserDirectory, OtherPhase;
	FParse::Value(FCommandLine::Get(), TEXT("UserDir="), UserDirectory); FPaths::NormalizeDirectoryName(UserDirectory);
	RunId = FPaths::GetCleanFilename(UserDirectory);
	FGuid Guid;
	FString Expected = FPaths::ConvertRelativePathToFull(FPaths::Combine(FPaths::ProjectDir(), TEXT("Saved/HudSizeSmoke"), RunId));
	FPaths::NormalizeDirectoryName(Expected);
	if (!Check(FGuid::ParseExact(RunId, EGuidFormats::Digits, Guid) && !FPaths::IsRelative(UserDirectory)
		&& UserDirectory.Equals(Expected, ESearchCase::IgnoreCase), TEXT("expected isolated packaged Saved/HudSizeSmoke/<guid> UserDir"))) { return; }
	if (!Check(!UUEGT2CaptureSubsystem::IsCaptureRequested() && !UUEGT2CaptureSubsystem::IsLifeCaptureRequested() && !UUEGT2CaptureSubsystem::IsWalkSmokeRequested()
		&& !UUEGT2CaptureSubsystem::IsFlySoakRequested() && !FParse::Param(FCommandLine::Get(), TEXT("UEGT2SurveySmoke"))
		&& !FParse::Param(FCommandLine::Get(), TEXT("UEGT2RestSmoke"))
		&& !FParse::Value(FCommandLine::Get(), TEXT("UEGT2ProgressSmoke="), OtherPhase)
		&& !FParse::Value(FCommandLine::Get(), TEXT("UEGT2AutosaveSmoke="), OtherPhase), TEXT("HUD size smoke cannot share another diagnostic"))) { return; }
	FParse::Value(FCommandLine::Get(), TEXT("UEGT2HudSizeCapture="), CaptureDirectory);
	FParse::Value(FCommandLine::Get(), TEXT("ResX="), ExpectedWidth); FParse::Value(FCommandLine::Get(), TEXT("ResY="), ExpectedHeight);
	if (!CaptureDirectory.IsEmpty())
	{
		if (!Check(!FPaths::IsRelative(CaptureDirectory) && FPaths::GetCleanFilename(CaptureDirectory) == RunId
			&& IFileManager::Get().MakeDirectory(*CaptureDirectory, true), TEXT("invalid or unwritable HUD size capture directory"))) { return; }
		ScreenshotHandle = UGameViewportClient::OnScreenshotCaptured().AddUObject(this, &UUEGT2HudSizeSmokeSubsystem::HandleScreenshot);
	}
	UE_LOG(LogUEGT2Diag, Log, TEXT("HUD size smoke starting: run=%s resolution=%dx%d"), *RunId, ExpectedWidth, ExpectedHeight);
}

void UUEGT2HudSizeSmokeSubsystem::Deinitialize()
{
	UGameViewportClient::OnScreenshotCaptured().Remove(ScreenshotHandle); RestorePreferences(); Super::Deinitialize();
}
void UUEGT2HudSizeSmokeSubsystem::RestorePreferences()
{
	if (!bPreferencesChanged) { return; }
	UEGT2HudSizeSmoke::FContext C(GetWorld());
	if (C.Settings)
	{
		C.Settings->SetSaveProgressEnabled(bOriginalSave); C.Settings->SetAutosaveEnabled(bOriginalAutosave);
		C.Settings->SetSurveyJournalEnabled(bOriginalSurvey); C.Settings->SetHudSizeLevel(OriginalSize);
		C.Settings->SetShowCrosshair(OriginalVisible[0]); C.Settings->SetShowInteractPrompts(OriginalVisible[1]);
		C.Settings->SetShowAlmanac(OriginalVisible[2]); C.Settings->SetShowNeeds(OriginalVisible[3]); C.Settings->SetShowSpeechBubbles(OriginalVisible[4]);
	}
	if (C.Hud) { C.Hud->bHudScalingEnabled = bOriginalHardGate; }
	if (C.Sky) { C.Sky->SetDayNightCycleEnabled(bOriginalClock); }
	if (C.PC) { C.PC->SetDiagnosticsVisible(bOriginalDiagnostics); C.PC->SetPause(false); }
	if (Speaker.IsValid()) { Speaker->SetActorTickEnabled(bOriginalSpeakerTick); }
	bPreferencesChanged = false;
}
bool UUEGT2HudSizeSmokeSubsystem::Check(bool bCondition, const TCHAR* Reason) { if (!bCondition) { Finish(false, Reason); } return bCondition; }
void UUEGT2HudSizeSmokeSubsystem::Finish(bool bSuccess, const TCHAR* Reason)
{
	if (bFinished) { return; }
	bFinished = true; RestorePreferences();
	if (bSuccess) { UE_LOG(LogUEGT2Diag, Log, TEXT("UEGT2_HUD_SIZE_SMOKE_COMPLETE run=%s %s"), *RunId, Reason); }
	else { UE_LOG(LogUEGT2Diag, Error, TEXT("UEGT2_HUD_SIZE_SMOKE_FAILED run=%s %s"), *RunId, Reason); }
	FPlatformMisc::RequestExitWithStatus(false, bSuccess ? 0 : 1);
}
void UUEGT2HudSizeSmokeSubsystem::SetStep(EStep Next) { Step = Next; StepStartedSeconds = FPlatformTime::Seconds(); }

void UUEGT2HudSizeSmokeSubsystem::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);
	if (bFinished) { return; }
	const double Now = FPlatformTime::Seconds();
	if (Now - StartedSeconds > 120.0) { Finish(false, TEXT("HUD size smoke exceeded 120 seconds")); return; }
	if (Step == EStep::Startup) { if (Now - StartedSeconds >= 8.0) { StartCheck(); } return; }
	if (Step == EStep::HudImage && !CheckFixture()) { return; }
	if (Step == EStep::HudImage || Step == EStep::SettingsImage)
	{
		if (bScreenshotComplete && Now - StepStartedSeconds >= 0.3) { Advance(); }
		else if (!bScreenshotRequested && Now - StepStartedSeconds >= 1.5)
		{
			bScreenshotRequested = true; PendingFile = CaptureFile; FScreenshotRequest::RequestScreenshot(true);
		}
		else if (Now - StepStartedSeconds > 30.0) { Finish(false, TEXT("HUD size screenshot callback timed out")); }
	}
	else if (Now - StepStartedSeconds >= 0.3) { Advance(); }
}

void UUEGT2HudSizeSmokeSubsystem::StartCheck()
{
	using namespace UEGT2HudSizeSmoke;
	FContext C(GetWorld());
	if (!Check(C.IsValid() && C.Player->GetInteraction() && FSlateApplication::IsInitialized()
		&& C.PC->GetMenuState() == EUEGT2MenuState::None && !GetWorld()->IsPaused(), TEXT("HUD fixture requires ordinary ready gameplay"))) { return; }
	bOriginalSave = C.Settings->GetSaveProgressEnabled(); bOriginalAutosave = C.Settings->GetAutosaveEnabled();
	bOriginalSurvey = C.Settings->GetSurveyJournalEnabled(); OriginalSize = C.Settings->GetHudSizeLevel();
	bOriginalHardGate = C.Hud->bHudScalingEnabled; bOriginalClock = C.Sky->IsDayNightCycleEnabled(); bOriginalDiagnostics = C.PC->IsDiagnosticsVisible();
	OriginalVisible[0] = C.Settings->GetShowCrosshair(); OriginalVisible[1] = C.Settings->GetShowInteractPrompts();
	OriginalVisible[2] = C.Settings->GetShowAlmanac(); OriginalVisible[3] = C.Settings->GetShowNeeds(); OriginalVisible[4] = C.Settings->GetShowSpeechBubbles();
	bPreferencesChanged = true;
	C.Settings->SetSaveProgressEnabled(false); C.Settings->SetAutosaveEnabled(false); C.Settings->SetSurveyJournalEnabled(true);
	C.Settings->SetHudSizeLevel(0); C.Hud->bHudScalingEnabled = true;
	C.Settings->SetShowCrosshair(true); C.Settings->SetShowInteractPrompts(true); C.Settings->SetShowAlmanac(true);
	C.Settings->SetShowNeeds(true); C.Settings->SetShowSpeechBubbles(true); C.PC->SetDiagnosticsVisible(false);
	C.Sky->SetDayNightCycleEnabled(false);
	if (!Check(!C.PC->IsProgressEnabled() && !C.PC->IsAutosaveEnabled() && C.Survey->IsEnabled()
		&& C.Director->RestoreCalendar(7, 13.25f, EUEGT2Weather::Clear)
		&& C.Life->RestoreProgress(Needs(), FUEGT2Purse(137.625f), EUEGT2NPCRole::Smith), TEXT("cannot seed HUD life/calendar or persistence gates"))) { return; }
	for (TActorIterator<AUEGT2Landmark> It(GetWorld()); It; ++It) { It->SetDiscovered(It->GetPersistentId() == Harbour); }
	if (!Check(C.Survey->TrackLandmark(Harbour) && AUEGT2Landmark::GetDiscoveredCount(GetWorld()) == 1, TEXT("HUD tracking landmark unavailable"))) { return; }
	SetStep(EStep::FindSpeaker);
}

bool UUEGT2HudSizeSmokeSubsystem::PositionAtSpeaker()
{
	UEGT2HudSizeSmoke::FContext C(GetWorld());
	if (!Check(C.IsValid() && Speaker.IsValid(), TEXT("HUD speaker disappeared"))) { return false; }
	const FVector Point = Speaker->GetInteractionPoint();
	for (int32 Attempt = 0; Attempt < 8; ++Attempt)
	{
		const float Angle = FMath::DegreesToRadians((NextApproach + Attempt) * 45.0f);
		const FVector Candidate(Point.X + FMath::Cos(Angle) * 175.0f, Point.Y + FMath::Sin(Angle) * 175.0f, Speaker->GetActorLocation().Z + 95.0f);
		FCollisionQueryParams Params(SCENE_QUERY_STAT(HudSizeApproach), false, C.Player);
		FHitResult Hit;
		if (GetWorld()->LineTraceSingleByChannel(Hit, Candidate + FVector(0, 0, 68), Point, ECC_Visibility, Params) && Hit.GetActor() != Speaker.Get()) { continue; }
		NextApproach = (NextApproach + Attempt + 1) % 8;
		if (!Check(C.Player->TeleportTo(Candidate, C.Player->GetActorRotation(), false, true), TEXT("cannot place HUD player beside speaker"))) { return false; }
		C.Player->GetCharacterMovement()->StopMovementImmediately();
		C.PC->SetControlRotation((Point - C.Player->GetPawnViewLocation()).Rotation());
		return true;
	}
	return Check(false, TEXT("all eight HUD speaker approaches were blocked"));
}

bool UUEGT2HudSizeSmokeSubsystem::CheckFixture()
{
	using namespace UEGT2HudSizeSmoke;
	FContext C(GetWorld());
	if (!Check(C.IsValid() && Speaker.IsValid(), TEXT("frozen HUD fixture disappeared"))) { return false; }
	FVector View; FRotator Rotation; C.PC->GetPlayerViewPoint(View, Rotation);
	FUEGT2SurveyDirection Direction;
	const bool bDirection = C.Survey->GetTrackedDirection(View, Rotation.Yaw, Direction);
	const FUEGT2NPCNeeds& CurrentNeeds = C.Life->GetNeeds();
	const int32 Discoveries = AUEGT2Landmark::GetDiscoveredCount(GetWorld());
	const FUEGT2HUDLayout Layout = UEGT2HUDLayout::Resolve(FVector2D(ExpectedWidth, ExpectedHeight), C.Settings->GetHudScale(), C.Hud->bHudScalingEnabled);
	const float ExpectedScale = ImageIndex == 0 || ImageIndex == 3 ? 1.0f : ImageIndex == 1 ? 1.25f : 1.5f;
	struct FField { const TCHAR* Name; bool bMatches; };
	const FField Fields[] = {
		{ TEXT("paused"), GetWorld()->IsPaused() },
		{ TEXT("menu"), C.PC->GetMenuState() == EUEGT2MenuState::None },
		{ TEXT("world_time"), GetWorld()->GetTimeSeconds() == SnapshotTime },
		{ TEXT("player_location"), C.Player->GetActorLocation().Equals(SnapshotPlayer, 0.01) },
		{ TEXT("speaker_location"), Speaker->GetActorLocation().Equals(SnapshotSpeaker, 0.01) },
		{ TEXT("view_location"), View.Equals(SnapshotView, 0.01) },
		{ TEXT("view_rotation"), Rotation.Equals(SnapshotRotation, 0.01) },
		{ TEXT("energy"), CurrentNeeds.Energy == SnapshotNeeds.Energy },
		{ TEXT("fed"), CurrentNeeds.Fed == SnapshotNeeds.Fed },
		{ TEXT("relief"), CurrentNeeds.Relief == SnapshotNeeds.Relief },
		{ TEXT("company"), CurrentNeeds.Company == SnapshotNeeds.Company },
		{ TEXT("purse"), C.Life->GetPurse().Coins == 137.625f },
		{ TEXT("trade"), C.Life->GetTrade() == EUEGT2NPCRole::Smith },
		{ TEXT("day"), C.Director->GetDayIndex() == 7 },
		{ TEXT("hour"), C.Director->GetHour() == 13.25f },
		{ TEXT("weather"), C.Director->GetWeather() == EUEGT2Weather::Clear },
		{ TEXT("prompt_actor"), C.Player->GetInteraction()->GetFocusedActor() == Speaker.Get() },
		{ TEXT("prompt_text"), C.Player->GetInteraction()->GetFocusedPrompt().ToString() == SnapshotPrompt },
		{ TEXT("bubble_present"), Speaker->HasBubble() },
		{ TEXT("bubble_typing"), !Speaker->IsTyping() },
		{ TEXT("bubble_alpha"), Speaker->GetBubbleAlpha() == 1.0f },
		{ TEXT("bubble_line"), Speaker->GetSpokenLine().EqualTo(Speech()) },
		{ TEXT("direction_valid"), bDirection },
		{ TEXT("tracked_id"), Direction.Id == Harbour },
		{ TEXT("direction_distance"), FMath::IsFinite(Direction.DistanceMetres) },
		{ TEXT("discoveries"), Discoveries == 1 },
		{ TEXT("progress_off"), !C.PC->IsProgressEnabled() },
		{ TEXT("autosave_off"), !C.PC->IsAutosaveEnabled() },
		{ TEXT("layout_scale"), Layout.Scale == ExpectedScale },
		{ TEXT("layout_enhanced"), Layout.bEnhanced == (ImageIndex == 1 || ImageIndex == 2) }
	};
	FString FailedFields;
	for (const FField& Field : Fields) { if (!Field.bMatches) { if (!FailedFields.IsEmpty()) { FailedFields += TEXT(", "); } FailedFields += Field.Name; } }
	if (FailedFields.IsEmpty()) { return true; }
	UE_LOG(LogUEGT2Diag, Log, TEXT("HUD fixture time/pose: time=%.17g expected=%.17g delta=%.17g paused=%d menu=%d player_delta=%s speaker_delta=%s view_delta=%s rotation=%s expected_rotation=%s"),
		GetWorld()->GetTimeSeconds(), SnapshotTime, GetWorld()->GetTimeSeconds() - SnapshotTime, GetWorld()->IsPaused(), static_cast<int32>(C.PC->GetMenuState()),
		*(C.Player->GetActorLocation() - SnapshotPlayer).ToString(), *(Speaker->GetActorLocation() - SnapshotSpeaker).ToString(), *(View - SnapshotView).ToString(),
		*Rotation.ToString(), *SnapshotRotation.ToString());
	UE_LOG(LogUEGT2Diag, Log, TEXT("HUD fixture life/calendar: needs(E,F,R,C)=%.9g,%.9g,%.9g,%.9g expected=%.9g,%.9g,%.9g,%.9g coins=%.9g expected=137.625 trade=%d expected=%d day=%d expected=7 hour=%.9g expected=13.25 weather=%d expected=%d"),
		CurrentNeeds.Energy, CurrentNeeds.Fed, CurrentNeeds.Relief, CurrentNeeds.Company, SnapshotNeeds.Energy, SnapshotNeeds.Fed, SnapshotNeeds.Relief, SnapshotNeeds.Company,
		C.Life->GetPurse().Coins, static_cast<int32>(C.Life->GetTrade()), static_cast<int32>(EUEGT2NPCRole::Smith), C.Director->GetDayIndex(), C.Director->GetHour(),
		static_cast<int32>(C.Director->GetWeather()), static_cast<int32>(EUEGT2Weather::Clear));
	UE_LOG(LogUEGT2Diag, Log, TEXT("HUD fixture prompt/speech: focused=%s expected=%s prompt='%s' expected='%s' present=%d typing=%d alpha=%.9g line='%s' expected='%s'"),
		*GetNameSafe(C.Player->GetInteraction()->GetFocusedActor()), *Speaker->GetName(), *C.Player->GetInteraction()->GetFocusedPrompt().ToString(), *SnapshotPrompt,
		Speaker->HasBubble(), Speaker->IsTyping(), Speaker->GetBubbleAlpha(), *Speaker->GetSpokenLine().ToString(), *Speech().ToString());
	UE_LOG(LogUEGT2Diag, Log, TEXT("HUD fixture survey/gates/layout: direction=%d id=%s expected=%s distance=%.9g discoveries=%d expected=1 progress=%d autosave=%d level=%d requested=%.9g hard=%d scale=%.9g expected=%.9g enhanced=%d"),
		bDirection, *Direction.Id.ToString(), *Harbour.ToString(), Direction.DistanceMetres, Discoveries, C.PC->IsProgressEnabled(), C.PC->IsAutosaveEnabled(),
		C.Settings->GetHudSizeLevel(), C.Settings->GetHudScale(), C.Hud->bHudScalingEnabled, Layout.Scale, ExpectedScale, Layout.bEnhanced);
	return Check(false, *FString::Printf(TEXT("HUD scene changed at image %d: %s"), ImageIndex, *FailedFields));
}

void UUEGT2HudSizeSmokeSubsystem::Advance()
{
	using namespace UEGT2HudSizeSmoke;
	FContext C(GetWorld());
	if (!Check(C.IsValid(), TEXT("HUD smoke context disappeared"))) { return; }
	switch (Step)
	{
	case EStep::FindSpeaker:
	{
		double Best = TNumericLimits<double>::Max();
		for (TActorIterator<AUEGT2NPCActor> It(GetWorld()); It; ++It)
		{
			if (It->IsAnimal() || !It->CanInteract(C.Player)) { continue; }
			const double Distance = FVector::DistSquared(C.Player->GetActorLocation(), It->GetActorLocation());
			if (Distance < Best) { Best = Distance; Speaker = *It; }
		}
		if (!Check(Speaker.IsValid(), TEXT("no visible human for HUD prompt and speech"))) { return; }
		bOriginalSpeakerTick = Speaker->IsActorTickEnabled(); Speaker->SetActorTickEnabled(false);
		if (PositionAtSpeaker()) { SetStep(EStep::Probe); }
		break;
	}
	case EStep::Probe:
		if (!C.Player->GetCharacterMovement()->IsMovingOnGround() || C.Player->GetInteraction()->GetFocusedActor() != Speaker.Get())
		{
			if (!Check(++ProbeAttempts < 8, TEXT("real HUD interaction probe did not settle on the speaker after eight attempts"))) { return; }
			if (C.Player->GetCharacterMovement()->IsMovingOnGround() && !PositionAtSpeaker()) { return; }
			SetStep(EStep::Probe); return;
		}
		Speaker->Say(Speech(), 120.0f, 0.0f);
		C.Hud->ShowMessage(NSLOCTEXT("UEGT2HudSizeSmoke", "Message", "A good day to explore Fairhaven."), 120.0f);
		SetStep(EStep::Speech);
		break;
	case EStep::Speech:
	{
		FVector View; FRotator Rotation; C.PC->GetPlayerViewPoint(View, Rotation);
		TArray<FUEGT2SpeechBubble> Bubbles; C.Director->GatherBubbles(View, Bubbles);
		bool bFound = false;
		for (const FUEGT2SpeechBubble& Bubble : Bubbles) { bFound |= Bubble.Line.EqualTo(Speech()) && Bubble.Alpha == 1.0f && !Bubble.bTyping; }
		if (!Check(bFound && !C.Player->GetInteraction()->GetFocusedPrompt().IsEmpty() && C.PC->SetPause(true), TEXT("real prompt/speech fixture did not settle before world pause"))) { return; }
		SetStep(EStep::Paused);
		break;
	}
	case EStep::Paused:
		SnapshotTime = GetWorld()->GetTimeSeconds(); SnapshotNeeds = C.Life->GetNeeds(); SnapshotPlayer = C.Player->GetActorLocation(); SnapshotSpeaker = Speaker->GetActorLocation();
		C.PC->GetPlayerViewPoint(SnapshotView, SnapshotRotation); SnapshotPrompt = C.Player->GetInteraction()->GetFocusedPrompt().ToString();
		if (!Check(Equal(SnapshotNeeds, Needs()), TEXT("frozen clock changed seeded HUD needs")) || !CheckFixture()) { return; }
		UE_LOG(LogUEGT2Diag, Log, TEXT("HUD fixture frozen: speaker=%s prompt='%s' position=%s view=%s speech='%s' day=7 hour=13.25 coins=137.625 surveys=1"),
			*Speaker->GetDisplayName().ToString(), *SnapshotPrompt, *SnapshotPlayer.ToCompactString(), *SnapshotRotation.ToCompactString(), *Speech().ToString());
		CaptureHud(0);
		break;
	case EStep::HudImage:
		if (ImageIndex < 3) { CaptureHud(ImageIndex + 1); }
		else { C.Hud->bHudScalingEnabled = true; C.PC->ShowPauseMenu(); C.PC->ShowSettingsPage(3); SetStep(EStep::SettingsReady); }
		break;
	case EStep::SettingsReady:
		if (CheckSettings()) { BeginCapture(EStep::SettingsImage, TEXT("05_HudSizeSetting.png")); }
		break;
	case EStep::SettingsImage:
		if (CheckSettings()) { Finish(true, TEXT("same frozen scene verified at Normal/Large/Larger/hard-off; real prompt, speech, life, tracking and settings captured")); }
		break;
	default: break;
	}
}

void UUEGT2HudSizeSmokeSubsystem::CaptureHud(int32 Index)
{
	UEGT2HudSizeSmoke::FContext C(GetWorld());
	ImageIndex = Index;
	C.Settings->SetHudSizeLevel(FMath::Min(Index, 2)); C.Hud->bHudScalingEnabled = Index != 3;
	const float PreferenceScale = Index == 0 ? 1.0f : Index == 1 ? 1.25f : 1.5f;
	const FUEGT2HUDLayout Layout = UEGT2HUDLayout::Resolve(FVector2D(ExpectedWidth, ExpectedHeight), C.Settings->GetHudScale(), C.Hud->bHudScalingEnabled);
	if (!Check(C.Settings->GetHudScale() == PreferenceScale && C.Settings->GetHudSizeLevel() == FMath::Min(Index, 2)
		&& Layout.Scale == (Index == 3 ? 1.0f : PreferenceScale), TEXT("HUD size preference/effective layout scale did not match requested level"))) { return; }
	const TCHAR* Names[] = { TEXT("01_Normal.png"), TEXT("02_Large.png"), TEXT("03_Larger.png"), TEXT("04_HardOff.png") };
	UE_LOG(LogUEGT2Diag, Log, TEXT("HUD size capture: %s preference_scale=%.2f effective_scale=%.2f hard_enabled=%d"), Names[Index], PreferenceScale, Layout.Scale, C.Hud->bHudScalingEnabled);
	BeginCapture(EStep::HudImage, Names[Index]);
}

bool UUEGT2HudSizeSmokeSubsystem::CheckSettings()
{
	UEGT2HudSizeSmoke::FContext C(GetWorld());
	TSharedPtr<SWidget> Root = FSlateApplication::Get().GetKeyboardFocusedWidget();
	for (int32 Depth = 0; Root.IsValid() && Root->GetType() != TEXT("SUEGT2Menu") && Depth < 32; ++Depth) { Root = Root->GetParentWidget(); }
	if (!Check(Root.IsValid() && Root->GetType() == TEXT("SUEGT2Menu"), TEXT("HUD settings focus has no actual menu ancestor"))) { return false; }
	int32 Budget = 512; FString Seen;
	const TSharedPtr<SWidget> Label = UEGT2HudSizeSmoke::FindText(Root.ToSharedRef(), NSLOCTEXT("UEGT2Menu", "HudSize", "HUD Size"), Budget, Seen);
	Budget = 512;
	const TSharedPtr<SWidget> Choice = UEGT2HudSizeSmoke::FindText(Root.ToSharedRef(), NSLOCTEXT("UEGT2Menu", "HudLarger", "Larger (150%)"), Budget, Seen);
	if (!Check(Label.IsValid() && Choice.IsValid() && C.Settings->GetHudSizeLevel() == 2 && C.Hud->bHudScalingEnabled,
		*FString::Printf(TEXT("actual HUD size setting missing: label=%d choice=%d root=%s texts=%s"), Label.IsValid(), Choice.IsValid(), *Root->GetTypeAsString(), *Seen))) { return false; }
	// This is a real scrolling settings page; reveal the existing row without
	// moving focus or changing any setting just to make the capture readable.
	TSharedPtr<SWidget> Scroll = Label;
	for (int32 Depth = 0; Scroll.IsValid() && Scroll->GetType() != TEXT("SScrollBox") && Depth < 32; ++Depth) { Scroll = Scroll->GetParentWidget(); }
	if (!Check(Scroll.IsValid() && Scroll->GetType() == TEXT("SScrollBox"), TEXT("HUD setting has no scroll ancestor"))) { return false; }
	if (Step == EStep::SettingsReady)
	{
		StaticCastSharedPtr<SScrollBox>(Scroll)->ScrollDescendantIntoView(Label, false, EDescendantScrollDestination::Center);
	}
	else
	{
		const FSlateRect Bounds = Scroll->GetCachedGeometry().GetLayoutBoundingRect();
		if (!Check(Bounds.ContainsRect(Label->GetCachedGeometry().GetLayoutBoundingRect())
			&& Bounds.ContainsRect(Choice->GetCachedGeometry().GetLayoutBoundingRect()), TEXT("HUD size row remained clipped after scrolling"))) { return false; }
	}
	return true;
}

void UUEGT2HudSizeSmokeSubsystem::BeginCapture(EStep Next, const TCHAR* Name)
{
	SetStep(Next); CaptureFile = FPaths::Combine(CaptureDirectory, Name);
	bScreenshotRequested = false; bScreenshotComplete = CaptureDirectory.IsEmpty();
}
void UUEGT2HudSizeSmokeSubsystem::HandleScreenshot(int32 Width, int32 Height, const TArray<FColor>& Bitmap)
{
	if (PendingFile.IsEmpty() || bFinished) { return; }
	if (!Check(Width == ExpectedWidth && Height == ExpectedHeight && Bitmap.Num() == Width * Height, TEXT("HUD screenshot resolution mismatch"))) { return; }
	TArray<FColor> Opaque = Bitmap; for (FColor& Pixel : Opaque) { Pixel.A = 255; }
	TArray64<uint8> Png;
	FImageUtils::PNGCompressImageArray(Width, Height, TArrayView64<const FColor>(Opaque.GetData(), Opaque.Num()), Png);
	if (!Check(Png.Num() > 0 && FFileHelper::SaveArrayToFile(Png, *PendingFile), TEXT("cannot write HUD screenshot"))) { return; }
	UE_LOG(LogUEGT2Diag, Log, TEXT("HUD size screenshot: %s"), *PendingFile); PendingFile.Reset(); bScreenshotComplete = true;
}
