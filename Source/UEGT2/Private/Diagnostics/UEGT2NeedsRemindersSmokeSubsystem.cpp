#include "Diagnostics/UEGT2NeedsRemindersSmokeSubsystem.h"

#include "Engine/GameViewportClient.h"
#include "Engine/World.h"
#include "Framework/Application/SlateApplication.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformMisc.h"
#include "HAL/PlatformTime.h"
#include "ImageUtils.h"
#include "Layout/Children.h"
#include "Misc/CommandLine.h"
#include "Misc/FileHelper.h"
#include "Misc/Guid.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "NPC/UEGT2NPCDirector.h"
#include "Player/UEGT2Character.h"
#include "Player/UEGT2NeedsComponent.h"
#include "Player/UEGT2PlayerController.h"
#include "Settings/UEGT2GameUserSettings.h"
#include "UI/UEGT2HUD.h"
#include "UEGT2LogChannels.h"
#include "UnrealClient.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/Text/STextBlock.h"
#include "World/UEGT2SkyController.h"

namespace UEGT2NeedsRemindersSmoke
{
	int32 OptionCount(const TCHAR* Name, FString* Value = nullptr, bool* bBare = nullptr)
	{
		const TCHAR* Cursor = FCommandLine::Get(); FString Token; int32 Count = 0;
		while (FParse::Token(Cursor, Token, false))
		{
			if (!Token.RemoveFromStart(TEXT("-"))) { continue; }
			FString Key, Suffix; const bool bValue = Token.Split(TEXT("="), &Key, &Suffix);
			if (!(bValue ? Key : Token).Equals(Name, ESearchCase::IgnoreCase)) { continue; }
			if (Suffix.Len() >= 2 && Suffix[0] == TEXT('"') && Suffix[Suffix.Len()-1] == TEXT('"')) { Suffix = Suffix.Mid(1, Suffix.Len()-2); }
			++Count; if (Value) { *Value = Suffix; } if (bBare) { *bBare = !bValue; }
		}
		return Count;
	}
	bool NoOtherDiagnostic()
	{
		const TCHAR* Cursor = FCommandLine::Get(); FString Token;
		while (FParse::Token(Cursor, Token, false))
		{
			FString Key, Value; if (!Token.Split(TEXT("="), &Key, &Value)) { Key = Token; }
			if (Key.StartsWith(TEXT("-UEGT2"), ESearchCase::IgnoreCase)
				&& !Key.Equals(TEXT("-UEGT2NeedsRemindersSmoke"), ESearchCase::IgnoreCase)
				&& !Key.Equals(TEXT("-UEGT2NeedsRemindersCapture"), ESearchCase::IgnoreCase)
				&& !Key.Equals(TEXT("-UEGT2SkipMenu"), ESearchCase::IgnoreCase)) { return false; }
		}
		return true;
	}
	struct FContext
	{
		AUEGT2PlayerController* PC = nullptr;
		UUEGT2NeedsComponent* Life = nullptr;
		AUEGT2HUD* Hud = nullptr;
		UUEGT2GameUserSettings* Settings = UUEGT2GameUserSettings::Get();
		UUEGT2NPCDirector* Director = nullptr;
		AUEGT2SkyController* Sky = nullptr;
		explicit FContext(UWorld* World)
		{
			PC = World ? Cast<AUEGT2PlayerController>(World->GetFirstPlayerController()) : nullptr;
			const AUEGT2Character* Player = PC ? Cast<AUEGT2Character>(PC->GetPawn()) : nullptr;
			Life = Player ? Player->GetLife() : nullptr;
			Hud = PC ? Cast<AUEGT2HUD>(PC->GetHUD()) : nullptr;
			Director = UUEGT2NPCDirector::Get(World); Sky = AUEGT2SkyController::Get(World);
		}
		bool Ready() const { return PC && Life && Life->HasBegunPlay() && Hud && Settings && Director && Sky; }
	};
	bool Same(const FUEGT2NPCNeeds& A, const FUEGT2NPCNeeds& B)
	{
		return A.Energy == B.Energy && A.Fed == B.Fed && A.Relief == B.Relief && A.Company == B.Company;
	}
	FText Ordinary() { return NSLOCTEXT("UEGT2NeedsRemindersSmoke", "Ordinary", "Your interaction message stays visible while needs are low."); }
	TSharedPtr<SWidget> FindText(const TSharedRef<SWidget>& Node, const FString& Text, int32& Budget, int32 Depth = 0)
	{
		if (--Budget < 0 || Depth > 32 || !Node->GetVisibility().IsVisible()) { return nullptr; }
		if (Node->GetType() == TEXT("STextBlock") && StaticCastSharedRef<STextBlock>(Node)->GetText().ToString() == Text) { return Node; }
		FChildren* Children = Node->GetChildren();
		for (int32 I = 0; I < Children->Num() && Budget > 0; ++I)
		{
			if (TSharedPtr<SWidget> Found = FindText(Children->GetChildAt(I), Text, Budget, Depth + 1)) { return Found; }
		}
		return nullptr;
	}
}

bool UUEGT2NeedsRemindersSmokeSubsystem::IsRequested() { return UEGT2NeedsRemindersSmoke::OptionCount(TEXT("UEGT2NeedsRemindersSmoke")) > 0; }
bool UUEGT2NeedsRemindersSmokeSubsystem::ShouldCreateSubsystem(UObject* Outer) const { return IsRequested() && Super::ShouldCreateSubsystem(Outer); }
bool UUEGT2NeedsRemindersSmokeSubsystem::DoesSupportWorldType(EWorldType::Type Type) const { return Type == EWorldType::Game; }
TStatId UUEGT2NeedsRemindersSmokeSubsystem::GetStatId() const { RETURN_QUICK_DECLARE_CYCLE_STAT(UUEGT2NeedsRemindersSmokeSubsystem, STATGROUP_Tickables); }

void UUEGT2NeedsRemindersSmokeSubsystem::OnWorldBeginPlay(UWorld& World)
{
	Super::OnWorldBeginPlay(World); bRequested = true; Started = FPlatformTime::Seconds();
	using namespace UEGT2NeedsRemindersSmoke;
	FString UserDirectory; bool bBare = false, bSkipBare = false;
	const int32 CaptureCount = OptionCount(TEXT("UEGT2NeedsRemindersCapture"), &CaptureDirectory);
	if (!Check(OptionCount(TEXT("UEGT2NeedsRemindersSmoke"), nullptr, &bBare) == 1 && bBare
		&& OptionCount(TEXT("UEGT2SkipMenu"), nullptr, &bSkipBare) == 1 && bSkipBare && NoOtherDiagnostic()
		&& OptionCount(TEXT("UserDir"), &UserDirectory) == 1 && CaptureCount <= 1
		&& (CaptureCount == 0 || !CaptureDirectory.IsEmpty()), TEXT("expected one plain smoke flag, SkipMenu and isolated UserDir; no other diagnostics"))) { return; }
	FPaths::NormalizeDirectoryName(UserDirectory); RunId = FPaths::GetCleanFilename(UserDirectory);
	FGuid Guid;
	FString Expected = FPaths::ConvertRelativePathToFull(FPaths::Combine(FPaths::ProjectDir(), TEXT("Saved/NeedsRemindersSmoke"), RunId));
	FPaths::NormalizeDirectoryName(Expected);
	if (!Check(FGuid::ParseExact(RunId, EGuidFormats::Digits, Guid) && RunId == Guid.ToString(EGuidFormats::Digits).ToLower()
		&& !FPaths::IsRelative(UserDirectory) && UserDirectory.Equals(Expected, ESearchCase::IgnoreCase), TEXT("expected packaged Saved/NeedsRemindersSmoke/<guid> UserDir"))) { return; }
	FParse::Value(FCommandLine::Get(), TEXT("ResX="), ExpectedWidth); FParse::Value(FCommandLine::Get(), TEXT("ResY="), ExpectedHeight);
	if (!CaptureDirectory.IsEmpty())
	{
		FPaths::NormalizeDirectoryName(CaptureDirectory);
		if (!Check(!FPaths::IsRelative(CaptureDirectory) && FPaths::GetCleanFilename(CaptureDirectory) == RunId
			&& IFileManager::Get().MakeDirectory(*CaptureDirectory, true), TEXT("invalid capture directory"))) { return; }
		ScreenshotHandle = UGameViewportClient::OnScreenshotCaptured().AddUObject(this, &UUEGT2NeedsRemindersSmokeSubsystem::HandleScreenshot);
	}
	UE_LOG(LogUEGT2Diag, Log, TEXT("Needs reminders smoke starting: run=%s %dx%d"), *RunId, ExpectedWidth, ExpectedHeight);
}

void UUEGT2NeedsRemindersSmokeSubsystem::Deinitialize()
{
	UGameViewportClient::OnScreenshotCaptured().Remove(ScreenshotHandle); RestorePreferences(); Super::Deinitialize();
}
void UUEGT2NeedsRemindersSmokeSubsystem::RestorePreferences()
{
	if (!bChanged) { return; }
	UEGT2NeedsRemindersSmoke::FContext C(GetWorld());
	if (C.Settings)
	{
		C.Settings->SetNeedsRemindersEnabled(bOriginalPlayerGate); C.Settings->SetShowNeeds(bOriginalNeeds);
		C.Settings->SetShowSpeechBubbles(bOriginalSpeech); C.Settings->SetHudSizeLevel(OriginalHudSize);
		C.Settings->SetNearbyServicesEnabled(bOriginalServices);
	}
	if (C.Hud) { C.Hud->bNeedsRemindersEnabled = bOriginalHardGate; C.Hud->bHudScalingEnabled = bOriginalHudScale; }
	GetMutableDefault<AUEGT2HUD>()->bNeedsRemindersEnabled = bOriginalCdoGate;
	if (C.Sky) { C.Sky->SetDayNightCycleEnabled(bOriginalClock); }
	if (C.PC) { C.PC->SetPause(false); }
	bChanged = false;
}
bool UUEGT2NeedsRemindersSmokeSubsystem::Check(bool bCondition, const TCHAR* Reason) { if (!bCondition) { Finish(false, Reason); } return bCondition; }
void UUEGT2NeedsRemindersSmokeSubsystem::Finish(bool bSuccess, const TCHAR* Reason)
{
	if (bFinished) { return; }
	bFinished = true; RestorePreferences();
	if (bSuccess) { UE_LOG(LogUEGT2Diag, Log, TEXT("UEGT2_NEEDS_REMINDERS_SMOKE_COMPLETE run=%s %s"), *RunId, Reason); }
	else { UE_LOG(LogUEGT2Diag, Error, TEXT("UEGT2_NEEDS_REMINDERS_SMOKE_FAILED run=%s %s"), *RunId, Reason); }
	FPlatformMisc::RequestExitWithStatus(false, bSuccess ? 0 : 1);
}
bool UUEGT2NeedsRemindersSmokeSubsystem::CheckLife()
{
	UEGT2NeedsRemindersSmoke::FContext C(GetWorld());
	return Check(C.Ready() && UEGT2NeedsRemindersSmoke::Same(C.Life->GetNeeds(), ExpectedNeeds)
		&& C.Life->GetPurse().Coins == 137.625f && C.Life->GetTrade() == EUEGT2NPCRole::Smith
		&& C.Life->GetActivity() == EUEGT2Activity::Idle && !C.Life->IsOccupied()
		&& C.Director->GetDayIndex() == 7 && C.Director->GetHour() == 13.25f
		&& C.Director->GetWeather() == EUEGT2Weather::Clear && !C.PC->IsProgressEnabled() && !C.PC->IsAutosaveEnabled(),
		TEXT("reminders or gates changed the frozen life/calendar fixture or enabled persistence"));
}
void UUEGT2NeedsRemindersSmokeSubsystem::StartCheck()
{
	using namespace UEGT2NeedsRemindersSmoke;
	FContext C(GetWorld());
	if (!Check(C.Ready() && !C.PC->IsMenuOpen() && !GetWorld()->IsPaused() && FSlateApplication::IsInitialized(), TEXT("ordinary gameplay context not ready"))) { return; }
	bOriginalPlayerGate = C.Settings->GetNeedsRemindersEnabled(); bOriginalHardGate = C.Hud->bNeedsRemindersEnabled;
	bOriginalCdoGate = GetDefault<AUEGT2HUD>()->bNeedsRemindersEnabled;
	bOriginalNeeds = C.Settings->GetShowNeeds(); bOriginalSpeech = C.Settings->GetShowSpeechBubbles();
	bOriginalClock = C.Sky->IsDayNightCycleEnabled(); bOriginalHudScale = C.Hud->bHudScalingEnabled;
	bOriginalServices = C.Settings->GetNearbyServicesEnabled(); OriginalHudSize = C.Settings->GetHudSizeLevel(); bChanged = true;
	C.Settings->SetNeedsRemindersEnabled(true); C.Settings->SetShowNeeds(true); C.Settings->SetShowSpeechBubbles(false);
	C.Settings->SetNearbyServicesEnabled(true); C.Settings->SetHudSizeLevel(2);
	C.Hud->bNeedsRemindersEnabled = true; C.Hud->bHudScalingEnabled = true;
	GetMutableDefault<AUEGT2HUD>()->bNeedsRemindersEnabled = true;
	C.Sky->SetDayNightCycleEnabled(false);
	ExpectedNeeds.Energy = ExpectedNeeds.Fed = ExpectedNeeds.Relief = ExpectedNeeds.Company = 0.40f;
	if (!Check(C.Director->RestoreCalendar(7, 13.25f, EUEGT2Weather::Clear)
		&& C.Life->RestoreProgress(ExpectedNeeds, FUEGT2Purse(137.625f), EUEGT2NPCRole::Smith), TEXT("cannot establish reminder fixture"))) { return; }
	// Cross the warning thresholds through the real shared ledger. A frozen sun
	// then keeps the same values while the ordinary HUD world timer keeps running.
	FUEGT2Purse Reference(137.625f);
	UEGT2AdvanceLife(2.0f, EUEGT2Activity::Idle, EUEGT2NPCRole::Smith, ExpectedNeeds, Reference);
	C.Life->AdvanceLife(2.0f);
	if (!CheckLife() || !Check(ExpectedNeeds.Energy < 0.34f && ExpectedNeeds.Fed < 0.34f
		&& ExpectedNeeds.Relief < 0.34f && ExpectedNeeds.Company < 0.34f, TEXT("ledger fixture did not cross all four thresholds"))) { return; }
	C.Hud->ShowMessage(Ordinary(), 12.0f); SetStep(EStep::Busy);
}
void UUEGT2NeedsRemindersSmokeSubsystem::SetStep(EStep Next)
{
	Step = Next; StepStarted = FPlatformTime::Seconds(); PhaseWorldTime = GetWorld()->GetTimeSeconds();
	UEGT2NeedsRemindersSmoke::FContext C(GetWorld());
	if (Next == EStep::PlayerOff) { C.Settings->SetNeedsRemindersEnabled(false); }
	if (Next == EStep::Reenabled) { C.Settings->SetNeedsRemindersEnabled(true); }
	if (Next == EStep::Interrupted) { C.Hud->ShowMessage(UEGT2NeedsRemindersSmoke::Ordinary(), 1.0f); }
	if (Next == EStep::Restored)
	{
		if (!Check(C.Life->RestoreProgress(ExpectedNeeds, FUEGT2Purse(137.625f), EUEGT2NPCRole::Smith), TEXT("valid low-state restore failed"))) { return; }
	}
	if (Next == EStep::HardOff)
	{
		C.Hud->bNeedsRemindersEnabled = false; GetMutableDefault<AUEGT2HUD>()->bNeedsRemindersEnabled = false;
	}
	if (Next == EStep::Settings) { C.PC->ShowPauseMenu(); C.PC->ShowSettingsPage(3); }
	if (Next == EStep::Done) { Finish(true, TEXT("real timer, four ledger crossings, ordinary priority, interruption latch, restore grace, both off gates and native setting verified")); }
}
void UUEGT2NeedsRemindersSmokeSubsystem::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime); if (bFinished) { return; }
	const double Now = FPlatformTime::Seconds();
	if (Now - Started > 180.0) { Finish(false, TEXT("reminder smoke exceeded 180 seconds")); return; }
	if (Step == EStep::Startup) { if (Now - Started >= 8.0) { StartCheck(); } return; }
	if (!CheckLife()) { return; }
	if (bImagePending)
	{
		if (bImageComplete && Now - ImageStarted >= 0.3) { bImagePending = false; SetStep(AfterImage); }
		else if (!bImageRequested && Now - ImageStarted >= 0.6)
		{
			if (!CheckImageState()) { return; }
			bImageRequested = true; FScreenshotRequest::RequestScreenshot(true);
		}
		else if (Now - ImageStarted > 20.0) { Finish(false, TEXT("reminder screenshot callback timed out")); }
		return;
	}
	if (Now - StepStarted >= 0.15) { Advance(); }
}
void UUEGT2NeedsRemindersSmokeSubsystem::Advance()
{
	using namespace UEGT2NeedsRemindersSmoke;
	FContext C(GetWorld()); const double Elapsed = GetWorld()->GetTimeSeconds() - PhaseWorldTime;
	const uint8 Mask = C.Hud->GetNeedsReminderMask(); const FText Text = C.Hud->GetNeedsReminderText();
	switch (Step)
	{
	case EStep::Busy:
		if (!Check(Mask == 0 && Text.IsEmpty() && C.Hud->GetOrdinaryMessageText().EqualTo(Ordinary()), TEXT("reminder displaced an ordinary message"))) { return; }
		if (Elapsed >= 6.0) { Capture(TEXT("02_OrdinaryMessage.png"), EStep::FirstReminder); } break;
	case EStep::FirstReminder:
		if (Mask != 0)
		{
			if (!Check(Mask == 15 && !Text.IsEmpty() && C.Hud->GetOrdinaryMessageText().IsEmpty(), TEXT("four pending needs did not coalesce after ordinary expiry"))) { return; }
			UE_LOG(LogUEGT2Diag, Log, TEXT("Reminder admitted through real timer: %s"), *Text.ToString());
			Capture(TEXT("01_Reminder.png"), EStep::PlayerOff);
		}
		else { Check(Elapsed < 15.0, TEXT("real HUD timer never delivered pending reminder")); } break;
	case EStep::PlayerOff:
		if (!Check(Mask == 0 && Text.IsEmpty() && !C.Settings->GetNeedsRemindersEnabled(), TEXT("player off retained reminder"))) { return; }
		if (Elapsed >= 6.0) { Capture(TEXT("03_PlayerOff.png"), EStep::Reenabled); } break;
	case EStep::Reenabled:
	case EStep::Restored:
		if (Elapsed < 4.5) { Check(Mask == 0 && Text.IsEmpty(), TEXT("re-enable or restore skipped its grace interval")); }
		else if (Mask != 0)
		{
			if (!Check(Mask == 15 && !Text.IsEmpty(), TEXT("low-state restart did not coalesce all needs"))) { return; }
			SetStep(Step == EStep::Reenabled ? EStep::Interrupted : EStep::HardOff);
		}
		else { Check(Elapsed < 9.0, TEXT("low-state restart did not produce a fresh reminder")); } break;
	case EStep::Interrupted:
		if (!Check(Mask == 0 && Text.IsEmpty(), TEXT("ordinary interruption replayed a latched need"))) { return; }
		if (Elapsed < 0.8 && !Check(C.Hud->GetOrdinaryMessageText().EqualTo(Ordinary()), TEXT("ordinary interruption message missing"))) { return; }
		if (Elapsed >= 31.0) { SetStep(EStep::Restored); } break;
	case EStep::HardOff:
		if (!Check(Mask == 0 && Text.IsEmpty() && C.Settings->GetNeedsRemindersEnabled(), TEXT("hard off retained reminder or changed player preference"))) { return; }
		if (Elapsed >= 6.0) { Capture(TEXT("04_HardOff.png"), EStep::Settings); } break;
	case EStep::Settings:
		if (CheckSettings(true)) { SetStep(EStep::SettingsImage); } break;
	case EStep::SettingsImage:
		if (CheckSettings(false)) { Capture(TEXT("05_ReminderSetting.png"), EStep::Done); } break;
	default: break;
	}
}
bool UUEGT2NeedsRemindersSmokeSubsystem::CheckSettings(bool bScroll)
{
	TSharedPtr<SWidget> Root = FSlateApplication::Get().GetKeyboardFocusedWidget();
	for (int32 Depth = 0; Root.IsValid() && Root->GetType() != TEXT("SUEGT2Menu") && Depth < 32; ++Depth) { Root = Root->GetParentWidget(); }
	if (!Check(Root.IsValid() && Root->GetType() == TEXT("SUEGT2Menu"), TEXT("settings focus has no real menu ancestor"))) { return false; }
	int32 Budget = 1024;
	TSharedPtr<SWidget> Label = UEGT2NeedsRemindersSmoke::FindText(Root.ToSharedRef(), TEXT("Needs Reminders"), Budget);
	if (!Check(Label.IsValid(), TEXT("real Gameplay reminder setting missing"))) { return false; }
	TSharedPtr<SWidget> Scroll = Label; bool bDisabledAncestor = false;
	for (int32 Depth = 0; Scroll.IsValid() && Scroll->GetType() != TEXT("SScrollBox") && Depth < 32; ++Depth)
	{
		bDisabledAncestor |= !Scroll->IsEnabled(); Scroll = Scroll->GetParentWidget();
	}
	if (!Check(Scroll.IsValid() && Scroll->GetType() == TEXT("SScrollBox") && bDisabledAncestor, TEXT("hard-off setting row is not disabled in real scroll page"))) { return false; }
	if (bScroll) { StaticCastSharedPtr<SScrollBox>(Scroll)->ScrollDescendantIntoView(Label, false, EDescendantScrollDestination::Center); }
	else if (!Check(Scroll->GetCachedGeometry().GetLayoutBoundingRect().ContainsRect(Label->GetCachedGeometry().GetLayoutBoundingRect()), TEXT("reminder setting remains clipped after scrolling"))) { return false; }
	return true;
}
void UUEGT2NeedsRemindersSmokeSubsystem::Capture(const TCHAR* Name, EStep Next)
{
	AfterImage = Next; ImageStarted = FPlatformTime::Seconds(); bImagePending = true;
	bImageRequested = false; bImageComplete = CaptureDirectory.IsEmpty();
	PendingImage = CaptureDirectory.IsEmpty() ? FString() : FPaths::Combine(CaptureDirectory, Name);
}
bool UUEGT2NeedsRemindersSmokeSubsystem::CheckImageState()
{
	using namespace UEGT2NeedsRemindersSmoke;
	FContext C(GetWorld());
	if (!Check(C.Ready(), TEXT("screenshot context disappeared"))) { return false; }
	const uint8 Mask = C.Hud->GetNeedsReminderMask();
	switch (Step)
	{
	case EStep::FirstReminder:
		return Check(Mask == 15 && !C.Hud->GetNeedsReminderText().IsEmpty() && C.Hud->GetOrdinaryMessageText().IsEmpty(), TEXT("reminder expired or changed before its screenshot"));
	case EStep::Busy:
		return Check(Mask == 0 && C.Hud->GetOrdinaryMessageText().EqualTo(Ordinary()), TEXT("ordinary message changed before its screenshot"));
	case EStep::PlayerOff:
		return Check(Mask == 0 && !C.Settings->GetNeedsRemindersEnabled(), TEXT("player-off screenshot lost its gate"));
	case EStep::HardOff:
		return Check(Mask == 0 && C.Settings->GetNeedsRemindersEnabled() && !C.Hud->bNeedsRemindersEnabled, TEXT("hard-off screenshot lost its gate"));
	case EStep::SettingsImage: return CheckSettings(false);
	default: return Check(false, TEXT("screenshot requested outside its expected stage"));
	}
}
void UUEGT2NeedsRemindersSmokeSubsystem::HandleScreenshot(int32 Width, int32 Height, const TArray<FColor>& Pixels)
{
	if (PendingImage.IsEmpty() || bFinished || !bImageRequested) { return; }
	if (!CheckLife() || !CheckImageState()) { return; }
	if (!Check(Width == ExpectedWidth && Height == ExpectedHeight && Pixels.Num() == Width * Height, TEXT("reminder screenshot dimensions mismatch"))) { return; }
	TArray<FColor> Opaque = Pixels; for (FColor& Pixel : Opaque) { Pixel.A = 255; }
	TArray64<uint8> Png; FImageUtils::PNGCompressImageArray(Width, Height, TArrayView64<const FColor>(Opaque.GetData(), Opaque.Num()), Png);
	if (!Check(!Png.IsEmpty() && FFileHelper::SaveArrayToFile(Png, *PendingImage), TEXT("could not save reminder screenshot"))) { return; }
	UE_LOG(LogUEGT2Diag, Log, TEXT("Reminder screenshot: %s"), *PendingImage); PendingImage.Reset(); bImageComplete = true;
}
