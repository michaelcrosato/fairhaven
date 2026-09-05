#include "Diagnostics/UEGT2ServicesSmokeSubsystem.h"

#include "Diagnostics/UEGT2CaptureSubsystem.h"
#include "Engine/GameViewportClient.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Framework/Application/SlateApplication.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GenericPlatform/GenericPlatformInputDeviceMapper.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformMisc.h"
#include "HAL/PlatformTime.h"
#include "ImageUtils.h"
#include "Input/Events.h"
#include "InputKeyEventArgs.h"
#include "Interaction/UEGT2Amenity.h"
#include "Interaction/UEGT2InteractionComponent.h"
#include "Interaction/UEGT2WorldInteractables.h"
#include "Layout/Children.h"
#include "Misc/CommandLine.h"
#include "Misc/FileHelper.h"
#include "Misc/Guid.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "NPC/UEGT2NPCDirector.h"
#include "Player/UEGT2Character.h"
#include "Player/UEGT2InputConfig.h"
#include "Player/UEGT2NeedsComponent.h"
#include "Player/UEGT2PlayerController.h"
#include "Progress/UEGT2ProgressSubsystem.h"
#include "Services/UEGT2ServicesSubsystem.h"
#include "Settings/UEGT2GameUserSettings.h"
#include "Survey/UEGT2SurveySubsystem.h"
#include "UI/UEGT2HUD.h"
#include "UI/UEGT2HUDLayout.h"
#include "UEGT2LogChannels.h"
#include "UnrealClient.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/Text/STextBlock.h"
#include "World/UEGT2SkyController.h"

namespace UEGT2ServicesSmoke
{
	struct FContext
	{
		AUEGT2PlayerController* PC = nullptr;
		AUEGT2Character* Player = nullptr;
		UUEGT2NeedsComponent* Life = nullptr;
		UUEGT2ServicesSubsystem* Services = nullptr;
		UUEGT2SurveySubsystem* Survey = nullptr;
		UUEGT2NPCDirector* Director = nullptr;
		UUEGT2GameUserSettings* Settings = nullptr;
		AUEGT2SkyController* Sky = nullptr;
		AUEGT2HUD* Hud = nullptr;
		explicit FContext(UWorld* World)
		{
			PC = World ? Cast<AUEGT2PlayerController>(World->GetFirstPlayerController()) : nullptr;
			Player = PC ? Cast<AUEGT2Character>(PC->GetPawn()) : nullptr;
			Life = Player ? Player->GetLife() : nullptr;
			Services = UUEGT2ServicesSubsystem::Get(World); Survey = UUEGT2SurveySubsystem::Get(World);
			Director = UUEGT2NPCDirector::Get(World); Settings = UUEGT2GameUserSettings::Get();
			Sky = AUEGT2SkyController::Get(World); Hud = PC ? Cast<AUEGT2HUD>(PC->GetHUD()) : nullptr;
		}
		bool IsValid() const { return PC && Player && Life && Life->HasBegunPlay() && Services && Survey && Director && Settings && Sky && Hud; }
	};
	bool Equal(const FUEGT2NPCNeeds& A, const FUEGT2NPCNeeds& B)
	{
		return FMath::IsNearlyEqual(A.Energy, B.Energy, 0.0001f) && FMath::IsNearlyEqual(A.Fed, B.Fed, 0.0001f)
			&& FMath::IsNearlyEqual(A.Relief, B.Relief, 0.0001f) && FMath::IsNearlyEqual(A.Company, B.Company, 0.0001f);
	}
	bool Matches(EUEGT2ServiceCategory Category, const AUEGT2Amenity* Amenity)
	{
		const EUEGT2AmenityKind Kind = Amenity->GetKind();
		switch (Category)
		{
		case EUEGT2ServiceCategory::Food: return Kind == EUEGT2AmenityKind::Food || Kind == EUEGT2AmenityKind::Tavern;
		case EUEGT2ServiceCategory::Washroom: return Kind == EUEGT2AmenityKind::Washroom;
		case EUEGT2ServiceCategory::Rest: return Kind == EUEGT2AmenityKind::Seat;
		case EUEGT2ServiceCategory::PaidWork: return (Kind == EUEGT2AmenityKind::Work || Kind == EUEGT2AmenityKind::Market)
			&& UEGT2WageFor(Amenity->GetJobRole(), Amenity->GetActivity()) > 0.0f;
		case EUEGT2ServiceCategory::FoodAtHome: return Kind == EUEGT2AmenityKind::Larder;
		case EUEGT2ServiceCategory::Sleep: return Kind == EUEGT2AmenityKind::Bed;
		default: return false;
		}
	}
	TSharedPtr<SWidget> FindText(const TSharedRef<SWidget>& Widget, const FText& Caption, int32& Budget, int32 Depth = 0)
	{
		if (--Budget < 0 || Depth > 32 || !Widget->GetVisibility().IsVisible()) { return nullptr; }
		if (Widget->GetType() == TEXT("STextBlock") && StaticCastSharedRef<STextBlock>(Widget)->GetText().ToString() == Caption.ToString()) { return Widget; }
		FChildren* Children = Widget->GetChildren();
		for (int32 Index = 0; Index < Children->Num() && Budget > 0; ++Index)
		{
			if (TSharedPtr<SWidget> Found = FindText(Children->GetChildAt(Index), Caption, Budget, Depth + 1)) { return Found; }
		}
		return nullptr;
	}
}

bool UUEGT2ServicesSmokeSubsystem::ShouldCreateSubsystem(UObject* Outer) const
{
	return FParse::Param(FCommandLine::Get(), TEXT("UEGT2ServicesSmoke")) && Super::ShouldCreateSubsystem(Outer);
}
bool UUEGT2ServicesSmokeSubsystem::DoesSupportWorldType(EWorldType::Type Type) const { return Type == EWorldType::Game; }
TStatId UUEGT2ServicesSmokeSubsystem::GetStatId() const { RETURN_QUICK_DECLARE_CYCLE_STAT(UUEGT2ServicesSmokeSubsystem, STATGROUP_Tickables); }
void UUEGT2ServicesSmokeSubsystem::OnWorldBeginPlay(UWorld& InWorld)
{
	Super::OnWorldBeginPlay(InWorld); bRequested = true; StartedSeconds = FPlatformTime::Seconds();
	FString UserDirectory, Other;
	FParse::Value(FCommandLine::Get(), TEXT("UserDir="), UserDirectory); FPaths::NormalizeDirectoryName(UserDirectory);
	RunId = FPaths::GetCleanFilename(UserDirectory); FGuid Guid;
	FString Expected = FPaths::ConvertRelativePathToFull(FPaths::Combine(FPaths::ProjectDir(), TEXT("Saved/ServicesSmoke"), RunId));
	FPaths::NormalizeDirectoryName(Expected);
	if (!Check(FGuid::ParseExact(RunId, EGuidFormats::Digits, Guid) && !FPaths::IsRelative(UserDirectory)
		&& UserDirectory.Equals(Expected, ESearchCase::IgnoreCase), TEXT("expected isolated packaged Saved/ServicesSmoke/<guid> UserDir"))) { return; }
	if (!Check(!UUEGT2CaptureSubsystem::IsCaptureRequested() && !UUEGT2CaptureSubsystem::IsLifeCaptureRequested()
		&& !UUEGT2CaptureSubsystem::IsWalkSmokeRequested() && !UUEGT2CaptureSubsystem::IsFlySoakRequested()
		&& !FParse::Param(FCommandLine::Get(), TEXT("UEGT2SurveySmoke")) && !FParse::Param(FCommandLine::Get(), TEXT("UEGT2RestSmoke"))
		&& !FParse::Param(FCommandLine::Get(), TEXT("UEGT2HudSizeSmoke")) && !FParse::Param(FCommandLine::Get(), TEXT("UEGT2AutoWalkSmoke"))
		&& !FParse::Value(FCommandLine::Get(), TEXT("UEGT2ProgressSmoke="), Other)
		&& !FParse::Value(FCommandLine::Get(), TEXT("UEGT2AutosaveSmoke="), Other), TEXT("services smoke cannot share another diagnostic"))) { return; }
	FParse::Value(FCommandLine::Get(), TEXT("UEGT2ServicesCapture="), CaptureDirectory);
	FParse::Value(FCommandLine::Get(), TEXT("ResX="), ExpectedWidth); FParse::Value(FCommandLine::Get(), TEXT("ResY="), ExpectedHeight);
	if (!CaptureDirectory.IsEmpty())
	{
		if (!Check(!FPaths::IsRelative(CaptureDirectory) && FPaths::GetCleanFilename(CaptureDirectory) == RunId
			&& IFileManager::Get().MakeDirectory(*CaptureDirectory, true), TEXT("invalid services capture directory"))) { return; }
		ScreenshotHandle = UGameViewportClient::OnScreenshotCaptured().AddUObject(this, &UUEGT2ServicesSmokeSubsystem::HandleScreenshot);
	}
	UE_LOG(LogUEGT2Diag, Log, TEXT("Services smoke starting: run=%s resolution=%dx%d"), *RunId, ExpectedWidth, ExpectedHeight);
}
void UUEGT2ServicesSmokeSubsystem::Deinitialize()
{
	UGameViewportClient::OnScreenshotCaptured().Remove(ScreenshotHandle); Restore(); Super::Deinitialize();
}
bool UUEGT2ServicesSmokeSubsystem::Check(bool Condition, const TCHAR* Reason) { if (!Condition) { Finish(false, Reason); } return Condition; }
void UUEGT2ServicesSmokeSubsystem::Finish(bool Success, const TCHAR* Reason)
{
	if (bFinished) { return; }
	bFinished = true; Restore();
	if (Success) { UE_LOG(LogUEGT2Diag, Log, TEXT("UEGT2_SERVICES_SMOKE_COMPLETE run=%s %s"), *RunId, Reason); }
	else { UE_LOG(LogUEGT2Diag, Error, TEXT("UEGT2_SERVICES_SMOKE_FAILED run=%s step=%d %s"), *RunId, static_cast<int32>(Step), Reason); }
	FPlatformMisc::RequestExitWithStatus(false, Success ? 0 : 1);
}
void UUEGT2ServicesSmokeSubsystem::Restore()
{
	if (!bChanged) { return; }
	UEGT2ServicesSmoke::FContext C(GetWorld());
	if (C.Services) { C.Services->ClearTracking(); C.Services->bFeatureEnabled = bOriginalServiceGate; }
	if (C.Survey) { C.Survey->ClearTracking(); C.Survey->bFeatureEnabled = bOriginalSurveyGate; }
	if (Landmark.IsValid()) { Landmark->SetDiscovered(bOriginalDiscovered); }
	if (C.Player) { C.Player->CancelAutoWalk(); C.Player->bAutoWalkFeatureEnabled = bOriginalAutoWalkGate; }
	if (C.Life) { C.Life->RestoreProgress(OriginalNeeds, OriginalPurse, OriginalTrade); }
	if (C.Settings)
	{
		C.Settings->SetNearbyServicesEnabled(bOriginalServices); C.Settings->SetSaveProgressEnabled(bOriginalSave); C.Settings->SetAutosaveEnabled(bOriginalAutosave);
		C.Settings->SetSurveyJournalEnabled(bOriginalSurvey); C.Settings->SetAutoWalkEnabled(bOriginalAutoWalk);
		C.Settings->SetShowNeeds(bOriginalNeeds); C.Settings->SetShowInteractPrompts(bOriginalPrompts); C.Settings->SetHudSizeLevel(OriginalHudSize);
	}
	if (C.Hud) { C.Hud->bHudScalingEnabled = bOriginalHudGate; }
	if (C.Sky) { C.Sky->SetDayNightCycleEnabled(bOriginalClock); }
	if (C.PC) { Key(UUEGT2InputConfig::GetEffectiveKey(EUEGT2InputSlot::Menu), IE_Released); }
	bChanged = false;
}
void UUEGT2ServicesSmokeSubsystem::Key(FKey Button, EInputEvent Event)
{
	UEGT2ServicesSmoke::FContext C(GetWorld());
	if (!C.PC) { return; }
	const FInputDeviceId Device = IPlatformInputDeviceMapper::Get().GetPrimaryInputDeviceForUser(C.PC->GetPlatformUserId());
	C.PC->InputKey(FInputKeyEventArgs(nullptr, Device, Button, Event, FPlatformTime::Cycles64()));
}
bool UUEGT2ServicesSmokeSubsystem::SlateKey(FKey Button)
{
	const FKeyEvent Event(Button, FModifierKeysState(), 0, false, 0, 0);
	const bool Down = FSlateApplication::Get().ProcessKeyDownEvent(Event);
	const bool Up = FSlateApplication::Get().ProcessKeyUpEvent(Event);
	const TSharedPtr<SWidget> Focus = FSlateApplication::Get().GetKeyboardFocusedWidget();
	UE_LOG(LogUEGT2Diag, Log, TEXT("Services smoke Slate: key=%s down=%d up=%d focus=%s"), *Button.ToString(), Down, Up, Focus.IsValid() ? *Focus->GetTypeAsString() : TEXT("None"));
	return Button != EKeys::Gamepad_FaceButton_Bottom || Check(Down && Up, TEXT("services button did not accept gamepad A"));
}
TSharedPtr<SWidget> UUEGT2ServicesSmokeSubsystem::GetMenuRoot() const
{
	TSharedPtr<SWidget> Root = FSlateApplication::Get().GetKeyboardFocusedWidget();
	for (int32 Depth = 0; Root.IsValid() && Root->GetType() != TEXT("SUEGT2Menu") && Depth < 32; ++Depth) { Root = Root->GetParentWidget(); }
	return Root.IsValid() && Root->GetType() == TEXT("SUEGT2Menu") ? Root : nullptr;
}
bool UUEGT2ServicesSmokeSubsystem::FocusHasCaption(const FText& Caption) const
{
	const TSharedPtr<SWidget> Focus = FSlateApplication::Get().GetKeyboardFocusedWidget();
	int32 Budget = 32;
	return Focus.IsValid() && Focus->GetType() == TEXT("SButton") && Focus->IsEnabled()
		&& UEGT2ServicesSmoke::FindText(Focus.ToSharedRef(), Caption, Budget).IsValid();
}
bool UUEGT2ServicesSmokeSubsystem::FocusIsHomeTrack() const
{
	if (!FocusHasCaption(NSLOCTEXT("UEGT2ServicesGuide", "Track", "Track"))) { return false; }
	TSharedPtr<SWidget> Parent = FSlateApplication::Get().GetKeyboardFocusedWidget();
	// The nearest border is the actual row. Never match the whole guide, which
	// contains every category and six identical Track labels.
	for (int32 Depth = 0; Parent.IsValid() && Parent->GetType() != TEXT("SUEGT2ServicesGuide") && Depth < 12; ++Depth)
	{
		if (Parent->GetType() == TEXT("SBorder"))
		{
			int32 Budget = 80;
			return UEGT2ServicesSmoke::FindText(Parent.ToSharedRef(), HomeCategory, Budget).IsValid();
		}
		Parent = Parent->GetParentWidget();
	}
	return false;
}
void UUEGT2ServicesSmokeSubsystem::SetStep(EStep Next)
{
	Step = Next; StepStartedSeconds = FPlatformTime::Seconds();
}
void UUEGT2ServicesSmokeSubsystem::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime); if (bFinished) { return; }
	const double Now = FPlatformTime::Seconds();
	if (Now - StartedSeconds > 150.0) { Finish(false, TEXT("services smoke exceeded 150 seconds")); return; }
	if (Step == EStep::Startup) { if (Now - StartedSeconds >= 8.0) { StartCheck(); } return; }
	if (Step == EStep::GuideImage || Step == EStep::NormalImage || Step == EStep::LargerImage || Step == EStep::SettingsImage)
	{
		if ((Step == EStep::NormalImage || Step == EStep::LargerImage) && !CheckDirection()) { return; }
		if (bScreenshotComplete && Now - StepStartedSeconds >= 0.3) { Advance(); }
		else if (!bScreenshotRequested && Now - StepStartedSeconds >= 1.0)
		{
			bScreenshotRequested = true; PendingFile = CaptureFile; FScreenshotRequest::RequestScreenshot(true);
		}
		else if (Now - StepStartedSeconds > 30.0) { Finish(false, TEXT("services screenshot callback timed out")); }
		return;
	}
	if (Now - StepStartedSeconds >= 0.3) { Advance(); }
}
void UUEGT2ServicesSmokeSubsystem::StartCheck()
{
	UEGT2ServicesSmoke::FContext C(GetWorld());
	if (!Check(C.IsValid() && C.Player->GetInteraction() && FSlateApplication::IsInitialized(), TEXT("services world context unavailable"))) { return; }
	if (!Check(!C.PC->IsMenuOpen() && !C.Life->IsOccupied() && C.Player->GetCharacterMovement()->IsMovingOnGround(), TEXT("services smoke requires ordinary grounded gameplay"))) { return; }
	bOriginalServices = C.Settings->GetNearbyServicesEnabled(); bOriginalSave = C.Settings->GetSaveProgressEnabled(); bOriginalAutosave = C.Settings->GetAutosaveEnabled();
	bOriginalSurvey = C.Settings->GetSurveyJournalEnabled(); bOriginalAutoWalk = C.Settings->GetAutoWalkEnabled();
	bOriginalNeeds = C.Settings->GetShowNeeds(); bOriginalPrompts = C.Settings->GetShowInteractPrompts(); OriginalHudSize = C.Settings->GetHudSizeLevel();
	bOriginalServiceGate = C.Services->bFeatureEnabled; bOriginalSurveyGate = C.Survey->bFeatureEnabled;
	bOriginalAutoWalkGate = C.Player->bAutoWalkFeatureEnabled; bOriginalHudGate = C.Hud->bHudScalingEnabled; bOriginalClock = C.Sky->IsDayNightCycleEnabled();
	OriginalNeeds = C.Life->GetNeeds(); OriginalPurse = C.Life->GetPurse(); OriginalTrade = C.Life->GetTrade(); bChanged = true;
	C.Settings->SetNearbyServicesEnabled(true); C.Settings->SetSaveProgressEnabled(false); C.Settings->SetAutosaveEnabled(false);
	C.Settings->SetSurveyJournalEnabled(true); C.Settings->SetAutoWalkEnabled(true); C.Settings->SetShowNeeds(true); C.Settings->SetShowInteractPrompts(true); C.Settings->SetHudSizeLevel(0);
	C.Services->bFeatureEnabled = true; C.Survey->bFeatureEnabled = true; C.Player->bAutoWalkFeatureEnabled = true; C.Hud->bHudScalingEnabled = true;
	C.Services->ClearTracking(); C.Survey->ClearTracking(); C.Sky->SetDayNightCycleEnabled(false);
	FUEGT2NPCNeeds Needs; Needs.Energy = 0.77f; Needs.Fed = 0.44f; Needs.Relief = 0.62f; Needs.Company = 0.55f;
	if (!Check(C.Life->RestoreProgress(Needs, FUEGT2Purse(36.625f), EUEGT2NPCRole::Villager), TEXT("could not seed finite services HUD fixture"))) { return; }
	for (TActorIterator<AUEGT2Landmark> It(GetWorld()); It; ++It)
	{
		if (It->GetPersistentId() == FName(TEXT("fairhaven_harbour"))) { Landmark = *It; break; }
	}
	if (!Check(Landmark.IsValid(), TEXT("generated harbour landmark missing"))) { return; }
	bOriginalDiscovered = Landmark->IsDiscovered(); Landmark->SetDiscovered(true); DiscoveryCount = AUEGT2Landmark::GetDiscoveredCount(GetWorld());
	const UUEGT2ProgressSubsystem* Progress = UUEGT2ProgressSubsystem::Get(GetWorld());
	if (!Check(Progress && !Progress->IsEnabled(), TEXT("services diagnostic did not exclude progress IO"))) { return; }
	SetStep(EStep::Settled);
}
bool UUEGT2ServicesSmokeSubsystem::CheckLedger() const
{
	UEGT2ServicesSmoke::FContext C(GetWorld());
	if (!C.IsValid()) { return false; }
	const bool Unchanged = UEGT2ServicesSmoke::Equal(C.Life->GetNeeds(), SnapshotNeeds)
		&& FMath::IsNearlyEqual(C.Life->GetPurse().Coins, SnapshotPurse.Coins, 0.0001f) && C.Life->GetTrade() == SnapshotTrade
		&& C.Director->GetDayIndex() == SnapshotDay && C.Director->GetHour() == SnapshotHour
		&& AUEGT2Landmark::GetDiscoveredCount(GetWorld()) == DiscoveryCount;
	if (!Unchanged)
	{
		const FUEGT2NPCNeeds& Needs = C.Life->GetNeeds();
		UE_LOG(LogUEGT2Diag, Error, TEXT("Services ledger delta: day=%d/%d hour=%.9f/%.9f energy=%.9f fed=%.9f relief=%.9f company=%.9f coins=%.9f trade=%d/%d discoveries=%d/%d clock=%d"),
			C.Director->GetDayIndex(), SnapshotDay, C.Director->GetHour(), SnapshotHour, Needs.Energy - SnapshotNeeds.Energy,
			Needs.Fed - SnapshotNeeds.Fed, Needs.Relief - SnapshotNeeds.Relief, Needs.Company - SnapshotNeeds.Company,
			C.Life->GetPurse().Coins - SnapshotPurse.Coins, static_cast<int32>(C.Life->GetTrade()), static_cast<int32>(SnapshotTrade),
			AUEGT2Landmark::GetDiscoveredCount(GetWorld()), DiscoveryCount, C.Sky->IsDayNightCycleEnabled());
	}
	return Unchanged;
}
bool UUEGT2ServicesSmokeSubsystem::ValidateEntries()
{
	using namespace UEGT2ServicesSmoke;
	FContext C(GetWorld()); const TArray<FUEGT2ServiceEntry> Entries = C.Services->GetEntries(C.PC);
	if (!Check(Entries.Num() == static_cast<int32>(EUEGT2ServiceCategory::Count), TEXT("guide did not return six fixed service rows"))) { return false; }
	for (int32 Index = 0; Index < Entries.Num(); ++Index)
	{
		const FUEGT2ServiceEntry& Entry = Entries[Index]; AUEGT2Amenity* Amenity = Entry.Amenity.Get();
		if (!Check(Amenity && Entry.Category == static_cast<EUEGT2ServiceCategory>(Index) && Matches(Entry.Category, Amenity)
			&& !Entry.Name.IsEmpty() && !Entry.CategoryName.IsEmpty(), TEXT("generated service category missing, mislabeled or misclassified"))) { return false; }
		double Nearest = TNumericLimits<double>::Max();
		for (TActorIterator<AUEGT2Amenity> It(GetWorld()); It; ++It)
		{
			if (Matches(Entry.Category, *It)) { Nearest = FMath::Min(Nearest, FVector::Dist2D(C.Player->GetActorLocation(), It->GetActorLocation()) / 100.0); }
		}
		const float Wage = UEGT2WageFor(Amenity->GetJobRole(), Amenity->GetActivity());
		const float Cost = UEGT2PriceFor(Amenity->GetJobRole(), Amenity->GetActivity());
		if (!Check(FMath::IsNearlyEqual(static_cast<double>(Entry.DistanceMetres), Nearest, 0.01)
			&& Entry.Activity == Amenity->GetActivity() && Entry.JobRole == Amenity->GetJobRole()
			&& Entry.WagePerHour == Wage && Entry.CostPerHour == Cost,
			*FString::Printf(TEXT("service row does not match nearest actor/ledger: category=%d name='%s' distance=%.3f minimum=%.3f cost=%.3f/%.3f wage=%.3f/%.3f"),
				Index, *Entry.Name.ToString(), Entry.DistanceMetres, Nearest, Entry.CostPerHour, Cost, Entry.WagePerHour, Wage))) { return false; }
		UE_LOG(LogUEGT2Diag, Log, TEXT("Services row: category=%s kind=%s venue='%s' distance_m=%.2f cost_per_hour=%.2f wage_per_hour=%.2f role=%d"),
			*Entry.CategoryName.ToString(), UEGT2AmenityKindName(Amenity->GetKind()), *Entry.Name.ToString(), Entry.DistanceMetres, Cost, Wage, static_cast<int32>(Entry.JobRole));
		if (Entry.Category == EUEGT2ServiceCategory::FoodAtHome)
		{
			Home = Amenity;
			HomeCategory = FText::Format(NSLOCTEXT("UEGT2ServicesGuide", "Place", "{0} — {1}"), Entry.CategoryName, Entry.Name);
		}
		if (Entry.Category == EUEGT2ServiceCategory::PaidWork) { Work = Amenity; }
	}
	return Check(Home.IsValid() && Work.IsValid() && Home != Work && UEGT2PriceFor(Home->GetJobRole(), Home->GetActivity()) == 0.0f
		&& UEGT2WageFor(Work->GetJobRole(), Work->GetActivity()) > 0.0f && CheckLedger(), TEXT("free home/paid work separation changed player state"));
}
bool UUEGT2ServicesSmokeSubsystem::CheckDirection()
{
	UEGT2ServicesSmoke::FContext C(GetWorld()); FUEGT2SurveyDirection Direction;
	const float Scale = Step == EStep::LargerImage ? 1.5f : 1.0f;
	return Check(C.IsValid() && Home.IsValid() && !GetWorld()->IsPaused() && !C.PC->IsMenuOpen() && !C.Player->IsAutoWalking()
		&& C.Settings->GetShowNeeds() && C.Services->GetTrackedAmenity() == Home.Get() && C.Survey->GetTrackedLandmarkId().IsNone()
		&& C.Services->GetTrackedDirection(C.Player->GetActorLocation(), C.PC->GetControlRotation().Yaw, Direction)
		&& !Direction.Name.IsEmpty() && Direction.DistanceMetres > 10.0f
		&& UEGT2HUDLayout::Resolve(FVector2D(ExpectedWidth, ExpectedHeight), C.Settings->GetHudScale(), C.Hud->bHudScalingEnabled).Scale == Scale
		&& FVector::Dist(C.Player->GetActorLocation(), HudLocation) < 2.0 && C.PC->GetControlRotation().Equals(HudView, 0.01f) && CheckLedger(),
		TEXT("services HUD lost selected direction, requested scale, fixed player state or Needs"));
}
bool UUEGT2ServicesSmokeSubsystem::PositionAt(AUEGT2Amenity* Amenity)
{
	UEGT2ServicesSmoke::FContext C(GetWorld());
	if (!Check(C.IsValid() && IsValid(Amenity), TEXT("amenity approach context missing"))) { return false; }
	const FVector Point = Amenity->GetInteractionPoint();
	for (int32 Attempt = 0; Attempt < 8; ++Attempt)
	{
		const float Angle = FMath::DegreesToRadians(((ApproachIndex + Attempt) % 8) * 45.0f);
		const FVector Candidate(Point.X + FMath::Cos(Angle) * 175.0f, Point.Y + FMath::Sin(Angle) * 175.0f, Amenity->GetActorLocation().Z + 95.0f);
		FCollisionQueryParams Params(SCENE_QUERY_STAT(ServicesSmokeApproach), false, C.Player); FHitResult Hit;
		if (GetWorld()->LineTraceSingleByChannel(Hit, Candidate + FVector(0, 0, 68), Point, ECC_Visibility, Params) && Hit.GetActor() != Amenity) { continue; }
		ApproachIndex = (ApproachIndex + Attempt + 1) % 8;
		if (!Check(C.Player->TeleportTo(Candidate, C.Player->GetActorRotation(), false, true), TEXT("amenity approach teleport failed"))) { return false; }
		C.Player->GetCharacterMovement()->StopMovementImmediately(); C.PC->SetControlRotation((Point - C.Player->GetPawnViewLocation()).Rotation());
		return true;
	}
	return Check(false, *FString::Printf(TEXT("all eight approaches blocked for %s '%s'"), UEGT2AmenityKindName(Amenity->GetKind()), *Amenity->GetVenueName().ToString()));
}
bool UUEGT2ServicesSmokeSubsystem::Use(AUEGT2Amenity* Amenity)
{
	UEGT2ServicesSmoke::FContext C(GetWorld()); UUEGT2InteractionComponent* Probe = C.Player->GetInteraction();
	if (Probe->GetFocusedActor() != Amenity)
	{
		if (++ProbeAttempts >= 8) { return Check(false, *FString::Printf(TEXT("probe failed to focus %s after eight approaches; actual=%s"), *GetNameSafe(Amenity), *GetNameSafe(Probe->GetFocusedActor()))); }
		if (PositionAt(Amenity)) { StepStartedSeconds = FPlatformTime::Seconds(); }
		return false;
	}
	ProbeAttempts = 0;
	if (!Check(Probe->TryInteract() && C.Life->IsUsing(Amenity) && C.Life->GetActivity() == Amenity->GetActivity(), TEXT("real probe did not begin the listed activity"))) { return false; }
	if (!Check(Amenity != Work.Get() || C.Life->GetTrade() == Amenity->GetJobRole(), TEXT("actual paid work did not adopt the offered trade"))) { return false; }
	UE_LOG(LogUEGT2Diag, Log, TEXT("Services probe used %s venue='%s' activity=%d trade=%d coins=%.3f enabled=%d"),
		UEGT2AmenityKindName(Amenity->GetKind()), *Amenity->GetVenueName().ToString(), static_cast<int32>(C.Life->GetActivity()), static_cast<int32>(C.Life->GetTrade()), C.Life->GetPurse().Coins, C.Services->IsEnabled());
	return Check(Probe->TryInteract() && !C.Life->IsOccupied(), TEXT("real probe did not stop the amenity activity"));
}

void UUEGT2ServicesSmokeSubsystem::Advance()
{
	using namespace UEGT2ServicesSmoke;
	FContext C(GetWorld());
	if (!Check(C.IsValid(), TEXT("services context disappeared"))) { return; }
	FUEGT2SurveyDirection Direction;
	switch (Step)
	{
	case EStep::Settled:
		// Let the final live opening frame synchronize the director before taking
		// a frozen baseline, just as the rest diagnostic waits for its pause frame.
		SnapshotNeeds = C.Life->GetNeeds(); SnapshotPurse = C.Life->GetPurse(); SnapshotTrade = C.Life->GetTrade();
		SnapshotDay = C.Director->GetDayIndex(); SnapshotHour = C.Director->GetHour();
		if (!ValidateEntries() || !Check(C.Survey->TrackLandmark(Landmark->GetPersistentId()), TEXT("initial survey target could not be selected"))) { return; }
		if (!Check(C.Player->ToggleAutoWalk(), TEXT("auto-walk could not start before services pause"))) { return; }
		C.Player->ApplyAutoWalkInput();
		if (!Check(C.Player->IsAutoWalking() && !C.Player->GetPendingMovementInputVector().IsNearlyZero(), TEXT("active auto-walk did not queue movement"))) { return; }
		Key(UUEGT2InputConfig::GetEffectiveKey(EUEGT2InputSlot::Menu), IE_Pressed); SetStep(EStep::PauseReady); break;
	case EStep::PauseReady:
		Key(UUEGT2InputConfig::GetEffectiveKey(EUEGT2InputSlot::Menu), IE_Released);
		if (!Check(C.PC->GetMenuState() == EUEGT2MenuState::Pause && GetWorld()->IsPaused() && !C.Player->IsAutoWalking()
			&& C.Player->GetPendingMovementInputVector().IsNearlyZero() && C.Player->GetVelocity().IsNearlyZero() && CheckLedger(),
			TEXT("real Pause input did not stop active auto-walk or changed frozen life"))) { return; }
		NavigationSteps = 0; SetStep(EStep::FindGuide); break;
	case EStep::FindGuide:
	{
		if (FocusHasCaption(NSLOCTEXT("UEGT2Menu", "NearbyServices", "Nearby Services")))
		{
			if (SlateKey(EKeys::Gamepad_FaceButton_Bottom)) { SetStep(EStep::GuideReady); } break;
		}
		const TSharedPtr<SWidget> Focus = FSlateApplication::Get().GetKeyboardFocusedWidget();
		if (!Check(++NavigationSteps <= 14, *FString::Printf(TEXT("natural pause navigation did not reach Nearby Services; focus=%s root=%s"),
			Focus.IsValid() ? *Focus->GetTypeAsString() : TEXT("None"), GetMenuRoot().IsValid() ? TEXT("SUEGT2Menu") : TEXT("None")))) { return; }
		SlateKey(Focus.IsValid() && Focus->GetType() == TEXT("SButton") ? EKeys::Gamepad_DPad_Down : EKeys::Tab);
		SetStep(EStep::FindGuide); break;
	}
	case EStep::GuideReady:
		if (!Check(C.PC->IsServicesGuideOpen() && GetWorld()->IsPaused() && FocusHasCaption(NSLOCTEXT("UEGT2ServicesGuide", "Resume", "Resume"))
			&& CheckLedger(), TEXT("natural Nearby Services activation did not open paused guide with Resume focused"))) { return; }
		NavigationSteps = 0; SlateKey(EKeys::Gamepad_DPad_Up); SetStep(EStep::FindHome); break;
	case EStep::FindHome:
		if (FocusIsHomeTrack())
		{
			if (SlateKey(EKeys::Gamepad_FaceButton_Bottom)) { SetStep(EStep::Tracked); } break;
		}
		if (!Check(++NavigationSteps <= 6, TEXT("D-pad Up did not reach the Food at home Track row"))) { return; }
		SlateKey(EKeys::Gamepad_DPad_Up); SetStep(EStep::FindHome); break;
	case EStep::Tracked:
		if (!Check(C.PC->IsServicesGuideOpen() && GetWorld()->IsPaused() && C.Services->GetTrackedAmenity() == Home.Get()
			&& C.Survey->GetTrackedLandmarkId().IsNone() && FocusHasCaption(NSLOCTEXT("UEGT2ServicesGuide", "Tracking", "Tracking")) && CheckLedger(),
			TEXT("actual home Track did not retain paused selection, retire survey guidance or preserve life"))) { return; }
		BeginCapture(EStep::GuideImage, TEXT("01_Guide.png")); break;
	case EStep::GuideImage:
		SlateKey(EKeys::Gamepad_DPad_Left); SetStep(EStep::FindResume); break;
	case EStep::FindResume:
		if (!Check(FocusHasCaption(NSLOCTEXT("UEGT2ServicesGuide", "Resume", "Resume")), TEXT("D-pad Left did not reach real Resume"))) { return; }
		if (SlateKey(EKeys::Gamepad_FaceButton_Bottom)) { SetStep(EStep::Resumed); } break;
	case EStep::Resumed:
		HudLocation = C.Player->GetActorLocation(); HudView = C.PC->GetControlRotation();
		BeginCapture(EStep::NormalImage, TEXT("02_TrackingNormal.png")); break;
	case EStep::NormalImage:
		C.Settings->SetHudSizeLevel(2); BeginCapture(EStep::LargerImage, TEXT("03_TrackingLarger.png")); break;
	case EStep::LargerImage:
		if (!Check(C.Survey->TrackLandmark(Landmark->GetPersistentId()) && !C.Services->GetTrackedAmenity()
			&& !C.Services->TrackAmenity(nullptr) && C.Survey->GetTrackedLandmarkId() == Landmark->GetPersistentId()
			&& C.Services->TrackAmenity(Home.Get()) && C.Survey->GetTrackedLandmarkId().IsNone(), TEXT("successful/invalid tracking handoffs broke exclusive ownership"))) { return; }
		C.Settings->SetSurveyJournalEnabled(false); C.Settings->ApplyNonResolutionSettings();
		if (!Check(C.Services->GetTrackedAmenity() == Home.Get() && C.Services->GetTrackedDirection(C.Player->GetActorLocation(), 0.0f, Direction)
			&& !C.Survey->TrackLandmark(Landmark->GetPersistentId()) && CheckLedger(), TEXT("services depended on Survey preference or changed discoveries/life"))) { return; }
		C.Settings->SetSurveyJournalEnabled(true);
		if (PositionAt(Home.Get())) { SetStep(EStep::UseHome); } break;
	case EStep::UseHome:
		if (!Use(Home.Get())) { return; }
		if (!Check(CheckLedger(), TEXT("free home activity changed frozen needs, purse or trade"))) { return; }
		if (PositionAt(Work.Get())) { SetStep(EStep::UseWork); } break;
	case EStep::UseWork:
		if (!Use(Work.Get())) { return; }
		if (!Check(C.Life->GetTrade() == Work->GetJobRole() && C.Life->GetPurse().Coins == SnapshotPurse.Coins,
			TEXT("paid work changed coins without elapsed time or failed offered trade"))) { return; }
		if (!Check(C.Life->RestoreProgress(SnapshotNeeds, SnapshotPurse, SnapshotTrade), TEXT("cannot reset life after real paid work"))) { return; }
		C.Settings->SetNearbyServicesEnabled(false); C.Settings->ApplyNonResolutionSettings(); SetStep(EStep::PlayerOff); break;
	case EStep::PlayerOff:
		if (!Check(!C.Services->IsEnabled() && C.Services->IsAvailable() && C.Services->GetEntries(C.PC).IsEmpty()
			&& !C.Services->GetTrackedAmenity() && !C.Services->TrackAmenity(Home.Get()) && !C.PC->OpenServicesGuide()
			&& !C.Services->GetTrackedDirection(C.Player->GetActorLocation(), 0.0f, Direction) && CheckLedger(),
			TEXT("player off did not retire/refuse guide and guidance while preserving life"))) { return; }
		if (!Check(C.Survey->TrackLandmark(Landmark->GetPersistentId()), TEXT("Services off blocked ordinary Survey tracking"))) { return; }
		if (PositionAt(Home.Get())) { SetStep(EStep::PlayerOffUse); } break;
	case EStep::PlayerOffUse:
		if (!Use(Home.Get())) { return; }
		if (!Check(CheckLedger() && C.Survey->GetTrackedLandmarkId() == Landmark->GetPersistentId(), TEXT("player off amenity use changed life or other tracking"))) { return; }
		C.Settings->SetNearbyServicesEnabled(true);
		if (!Check(C.Services->GetTrackedAmenity() == nullptr && C.Services->TrackAmenity(Home.Get()), TEXT("services reenable resurrected a target or refused explicit selection"))) { return; }
		C.Services->bFeatureEnabled = false; C.Settings->ApplyNonResolutionSettings(); SetStep(EStep::HardOff); break;
	case EStep::HardOff:
		if (!Check(!C.Services->IsAvailable() && !C.Services->IsEnabled() && C.Settings->GetNearbyServicesEnabled()
			&& C.Services->GetEntries(C.PC).IsEmpty() && !C.Services->GetTrackedAmenity() && !C.Services->TrackAmenity(Home.Get())
			&& !C.PC->OpenServicesGuide() && !C.Services->GetTrackedDirection(C.Player->GetActorLocation(), 0.0f, Direction) && CheckLedger(),
			TEXT("hard off did not preserve preference/life while blocking services"))) { return; }
		if (PositionAt(Home.Get())) { SetStep(EStep::HardOffUse); } break;
	case EStep::HardOffUse:
		if (!Use(Home.Get())) { return; }
		if (!Check(CheckLedger(), TEXT("hard-off ordinary larder interaction changed frozen life"))) { return; }
		C.Services->bFeatureEnabled = true;
		if (!Check(!C.Services->GetTrackedAmenity(), TEXT("hard gate reenable resurrected guidance"))) { return; }
		C.PC->ShowPauseMenu(); C.PC->ShowSettingsPage(3); SetStep(EStep::SettingsReady); break;
	case EStep::SettingsReady:
	case EStep::SettingsImage:
	{
		const TSharedPtr<SWidget> Root = GetMenuRoot(); int32 Budget = 768;
		const TSharedPtr<SWidget> Label = Root.IsValid() ? FindText(Root.ToSharedRef(), NSLOCTEXT("UEGT2Menu", "NearbyServicesSetting", "Nearby Services"), Budget) : nullptr;
		if (!Check(Label.IsValid(), TEXT("Nearby Services Gameplay setting is missing"))) { return; }
		TSharedPtr<SWidget> Scroll = Label;
		for (int32 Depth = 0; Scroll.IsValid() && Scroll->GetType() != TEXT("SScrollBox") && Depth < 32; ++Depth) { Scroll = Scroll->GetParentWidget(); }
		if (!Check(Scroll.IsValid() && Scroll->GetType() == TEXT("SScrollBox"), TEXT("Services setting has no scroll ancestor"))) { return; }
		if (Step == EStep::SettingsReady)
		{
			StaticCastSharedPtr<SScrollBox>(Scroll)->ScrollDescendantIntoView(Label, false, EDescendantScrollDestination::Center);
			BeginCapture(EStep::SettingsImage, TEXT("04_Settings.png"));
		}
		else if (Check(Scroll->GetCachedGeometry().GetLayoutBoundingRect().ContainsRect(Label->GetCachedGeometry().GetLayoutBoundingRect())
			&& CheckLedger() && !C.Player->IsAutoWalking() && !C.PC->IsProgressEnabled() && !C.PC->IsAutosaveEnabled(),
			TEXT("Services setting clipped or diagnostic changed life/movement/persistence")))
		{
			Finish(true, TEXT("six nearest rows/rates, natural Track/Resume, Survey handoff, free home and paid work probes, both off gates and Normal/Larger HUD verified"));
		}
		break;
	}
	default: break;
	}
}
void UUEGT2ServicesSmokeSubsystem::BeginCapture(EStep Next, const TCHAR* Name)
{
	SetStep(Next); CaptureFile = FPaths::Combine(CaptureDirectory, Name); bScreenshotRequested = false; bScreenshotComplete = CaptureDirectory.IsEmpty();
}
void UUEGT2ServicesSmokeSubsystem::HandleScreenshot(int32 Width, int32 Height, const TArray<FColor>& Bitmap)
{
	if (PendingFile.IsEmpty() || bFinished) { return; }
	if ((Step == EStep::NormalImage || Step == EStep::LargerImage) && !CheckDirection()) { return; }
	if (!Check(Width == ExpectedWidth && Height == ExpectedHeight && Bitmap.Num() == Width * Height, TEXT("services screenshot dimensions incorrect"))) { return; }
	TArray<FColor> Opaque = Bitmap; for (FColor& Pixel : Opaque) { Pixel.A = 255; }
	TArray64<uint8> Png;
	FImageUtils::PNGCompressImageArray(Width, Height, TArrayView64<const FColor>(Opaque.GetData(), Opaque.Num()), Png);
	if (!Check(Png.Num() > 0 && FFileHelper::SaveArrayToFile(Png, *PendingFile), TEXT("cannot save services screenshot"))) { return; }
	UE_LOG(LogUEGT2Diag, Log, TEXT("Services screenshot: %s"), *PendingFile); PendingFile.Reset(); bScreenshotComplete = true;
}
