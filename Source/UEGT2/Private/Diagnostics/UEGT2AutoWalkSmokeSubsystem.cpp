#include "Diagnostics/UEGT2AutoWalkSmokeSubsystem.h"

#include "Components/CapsuleComponent.h"
#include "Diagnostics/UEGT2CaptureSubsystem.h"
#include "Engine/Console.h"
#include "Engine/GameViewportClient.h"
#include "Engine/World.h"
#include "Framework/Application/SlateApplication.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/InputSettings.h"
#include "GenericPlatform/GenericPlatformInputDeviceMapper.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformMisc.h"
#include "HAL/PlatformTime.h"
#include "ImageUtils.h"
#include "InputKeyEventArgs.h"
#include "Layout/Children.h"
#include "Misc/CommandLine.h"
#include "Misc/FileHelper.h"
#include "Misc/Guid.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "Player/UEGT2Character.h"
#include "Player/UEGT2InputConfig.h"
#include "Player/UEGT2PlayerController.h"
#include "Settings/UEGT2GameUserSettings.h"
#include "UI/UEGT2HUD.h"
#include "UI/UEGT2HUDLayout.h"
#include "UEGT2LogChannels.h"
#include "UnrealClient.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/Text/STextBlock.h"
#include "World/UEGT2SkyController.h"

namespace UEGT2AutoWalkSmoke
{
	const FName ActionName(TEXT("ToggleAutoWalk"));
	struct FContext
	{
		AUEGT2PlayerController* PC = nullptr;
		AUEGT2Character* Player = nullptr;
		UUEGT2GameUserSettings* Settings = nullptr;
		AUEGT2SkyController* Sky = nullptr;
		AUEGT2HUD* Hud = nullptr;
		UConsole* Console = nullptr;
		explicit FContext(UWorld* World)
		{
			PC = World ? Cast<AUEGT2PlayerController>(World->GetFirstPlayerController()) : nullptr;
			Player = PC ? Cast<AUEGT2Character>(PC->GetPawn()) : nullptr;
			Settings = UUEGT2GameUserSettings::Get(); Sky = AUEGT2SkyController::Get(World);
			Hud = PC ? Cast<AUEGT2HUD>(PC->GetHUD()) : nullptr;
			Console = World && World->GetGameViewport() ? World->GetGameViewport()->ViewportConsole.Get() : nullptr;
		}
		bool IsValid() const { return PC && Player && Settings && Sky && Hud; }
	};
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

bool UUEGT2AutoWalkSmokeSubsystem::ShouldCreateSubsystem(UObject* Outer) const
{
	return FParse::Param(FCommandLine::Get(), TEXT("UEGT2AutoWalkSmoke")) && Super::ShouldCreateSubsystem(Outer);
}
bool UUEGT2AutoWalkSmokeSubsystem::DoesSupportWorldType(EWorldType::Type Type) const { return Type == EWorldType::Game; }
TStatId UUEGT2AutoWalkSmokeSubsystem::GetStatId() const { RETURN_QUICK_DECLARE_CYCLE_STAT(UUEGT2AutoWalkSmokeSubsystem, STATGROUP_Tickables); }
void UUEGT2AutoWalkSmokeSubsystem::OnWorldBeginPlay(UWorld& InWorld)
{
	Super::OnWorldBeginPlay(InWorld); bRequested = true; StartedSeconds = FPlatformTime::Seconds();
	FString UserDirectory, Other;
	FParse::Value(FCommandLine::Get(), TEXT("UserDir="), UserDirectory); FPaths::NormalizeDirectoryName(UserDirectory);
	RunId = FPaths::GetCleanFilename(UserDirectory); FGuid Guid;
	FString Expected = FPaths::ConvertRelativePathToFull(FPaths::Combine(FPaths::ProjectDir(), TEXT("Saved/AutoWalkSmoke"), RunId));
	FPaths::NormalizeDirectoryName(Expected);
	if (!Check(FGuid::ParseExact(RunId, EGuidFormats::Digits, Guid) && !FPaths::IsRelative(UserDirectory)
		&& UserDirectory.Equals(Expected, ESearchCase::IgnoreCase), TEXT("expected isolated packaged Saved/AutoWalkSmoke/<guid> UserDir"))) { return; }
	if (!Check(!UUEGT2CaptureSubsystem::IsCaptureRequested() && !UUEGT2CaptureSubsystem::IsLifeCaptureRequested()
		&& !UUEGT2CaptureSubsystem::IsWalkSmokeRequested() && !UUEGT2CaptureSubsystem::IsFlySoakRequested()
		&& !FParse::Param(FCommandLine::Get(), TEXT("UEGT2SurveySmoke")) && !FParse::Param(FCommandLine::Get(), TEXT("UEGT2RestSmoke"))
		&& !FParse::Param(FCommandLine::Get(), TEXT("UEGT2HudSizeSmoke"))
		&& !FParse::Value(FCommandLine::Get(), TEXT("UEGT2ProgressSmoke="), Other)
		&& !FParse::Value(FCommandLine::Get(), TEXT("UEGT2AutosaveSmoke="), Other), TEXT("auto-walk smoke cannot share another diagnostic"))) { return; }
	FParse::Value(FCommandLine::Get(), TEXT("UEGT2AutoWalkCapture="), CaptureDirectory);
	FParse::Value(FCommandLine::Get(), TEXT("ResX="), ExpectedWidth); FParse::Value(FCommandLine::Get(), TEXT("ResY="), ExpectedHeight);
	if (!CaptureDirectory.IsEmpty())
	{
		if (!Check(!FPaths::IsRelative(CaptureDirectory) && FPaths::GetCleanFilename(CaptureDirectory) == RunId
			&& IFileManager::Get().MakeDirectory(*CaptureDirectory, true), TEXT("invalid auto-walk capture directory"))) { return; }
		ScreenshotHandle = UGameViewportClient::OnScreenshotCaptured().AddUObject(this, &UUEGT2AutoWalkSmokeSubsystem::HandleScreenshot);
	}
	UE_LOG(LogUEGT2Diag, Log, TEXT("Auto-walk smoke starting: run=%s resolution=%dx%d"), *RunId, ExpectedWidth, ExpectedHeight);
}
void UUEGT2AutoWalkSmokeSubsystem::Deinitialize()
{
	UGameViewportClient::OnScreenshotCaptured().Remove(ScreenshotHandle); RestorePreferences(); Super::Deinitialize();
}
bool UUEGT2AutoWalkSmokeSubsystem::Check(bool bCondition, const TCHAR* Reason) { if (!bCondition) { Finish(false, Reason); } return bCondition; }
void UUEGT2AutoWalkSmokeSubsystem::Finish(bool bSuccess, const TCHAR* Reason)
{
	if (bFinished) { return; }
	bFinished = true; RestorePreferences();
	if (bSuccess) { UE_LOG(LogUEGT2Diag, Log, TEXT("UEGT2_AUTO_WALK_SMOKE_COMPLETE run=%s %s"), *RunId, Reason); }
	else { UE_LOG(LogUEGT2Diag, Error, TEXT("UEGT2_AUTO_WALK_SMOKE_FAILED run=%s step=%d %s"), *RunId, static_cast<int32>(Step), Reason); }
	FPlatformMisc::RequestExitWithStatus(false, bSuccess ? 0 : 1);
}
void UUEGT2AutoWalkSmokeSubsystem::RestorePreferences()
{
	if (!bPreferencesChanged) { return; }
	UEGT2AutoWalkSmoke::FContext C(GetWorld());
	if (C.Player) { C.Player->CancelAutoWalk(); C.Player->bAutoWalkFeatureEnabled = bOriginalHardGate; }
	if (C.PC) { for (FKey Button : { EKeys::K, EKeys::V, EKeys::W, EKeys::Gamepad_RightThumbstick }) { Key(Button, IE_Released); } }
	if (C.Console && C.Console->ConsoleActive()) { ConsoleKey(EKeys::Escape); }
	if (C.Settings)
	{
		C.Settings->SetAutoWalkEnabled(bOriginalPreference); C.Settings->SetSaveProgressEnabled(bOriginalSave); C.Settings->SetAutosaveEnabled(bOriginalAutosave);
		C.Settings->SetShowNeeds(bOriginalNeeds); C.Settings->SetHudSizeLevel(OriginalHudSize); C.Settings->SetKeyOverride(UEGT2AutoWalkSmoke::ActionName, OriginalKey);
	}
	if (C.Hud) { C.Hud->bHudScalingEnabled = bOriginalHudGate; }
	if (C.Sky) { C.Sky->SetDayNightCycleEnabled(bOriginalClock); }
	if (C.PC) { C.PC->RebuildInputMappings(); }
	bPreferencesChanged = false;
}
void UUEGT2AutoWalkSmokeSubsystem::Key(FKey Button, EInputEvent Event)
{
	UEGT2AutoWalkSmoke::FContext C(GetWorld());
	if (!C.PC) { return; }
	const FInputDeviceId Device = IPlatformInputDeviceMapper::Get().GetPrimaryInputDeviceForUser(C.PC->GetPlatformUserId());
	C.PC->InputKey(FInputKeyEventArgs(nullptr, Device, Button, Event, FPlatformTime::Cycles64()));
	UE_LOG(LogUEGT2Diag, Log, TEXT("Auto-walk physical key: %s event=%d"), *Button.ToString(), static_cast<int32>(Event));
}
bool UUEGT2AutoWalkSmokeSubsystem::ConsoleKey(FKey Button)
{
	UEGT2AutoWalkSmoke::FContext C(GetWorld());
	if (!C.Console) { return false; }
	const FInputDeviceId Device = IPlatformInputDeviceMapper::Get().GetDefaultInputDevice();
	C.Console->InputKey(Device, Button, IE_Pressed); C.Console->InputKey(Device, Button, IE_Released);
	return true;
}
void UUEGT2AutoWalkSmokeSubsystem::SetStep(EStep Next)
{
	Step = Next; StepStartedSeconds = FPlatformTime::Seconds();
	UEGT2AutoWalkSmoke::FContext C(GetWorld()); if (C.Player) { StepLocation = C.Player->GetActorLocation(); }
}
void UUEGT2AutoWalkSmokeSubsystem::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime); if (bFinished) { return; }
	const double Now = FPlatformTime::Seconds();
	if (Now - StartedSeconds > 120.0) { Finish(false, TEXT("auto-walk smoke exceeded 120 seconds")); return; }
	if (Step == EStep::Startup) { if (Now - StartedSeconds >= 8.0) { StartCheck(); } return; }
	UEGT2AutoWalkSmoke::FContext C(GetWorld());
	if (!Check(C.IsValid() && FVector::Dist2D(C.Player->GetActorLocation(), RouteStart) < 2300.0,
		TEXT("auto-walk context lost or bounded route exceeded"))) { return; }
	if (Step == EStep::NormalImage || Step == EStep::LargerImage || Step == EStep::SettingsImage)
	{
		if (Step != EStep::SettingsImage && !CheckIndicator()) { return; }
		if (bScreenshotComplete && Now - StepStartedSeconds >= 0.3) { Advance(); }
		else if (!bScreenshotRequested && Now - StepStartedSeconds >= 1.0)
		{
			bScreenshotRequested = true; PendingFile = CaptureFile; FScreenshotRequest::RequestScreenshot(true);
		}
		else if (Now - StepStartedSeconds > 30.0) { Finish(false, TEXT("auto-walk screenshot timed out")); }
		return;
	}
	const double Hold = Step == EStep::Forward || Step == EStep::Steering || Step == EStep::Manual
		|| Step == EStep::PreferenceManual || Step == EStep::HardManual ? 0.5 : 0.3;
	if (Now - StepStartedSeconds >= Hold) { Advance(); }
}

bool UUEGT2AutoWalkSmokeSubsystem::FindClearRoute()
{
	UEGT2AutoWalkSmoke::FContext C(GetWorld());
	const FVector Origin = C.Player->GetActorLocation();
	const UCapsuleComponent* Capsule = C.Player->GetCapsuleComponent();
	const float Radius = Capsule->GetScaledCapsuleRadius(), Half = Capsule->GetScaledCapsuleHalfHeight();
	FCollisionQueryParams Params(SCENE_QUERY_STAT(AutoWalkRoute), false, C.Player);
	auto Ground = [&](const FVector& XY, FVector& Out) -> bool
	{
		FHitResult Hit;
		if (!GetWorld()->LineTraceSingleByObjectType(Hit, FVector(XY.X, XY.Y, Origin.Z + 3000.0),
			FVector(XY.X, XY.Y, Origin.Z - 3000.0), FCollisionObjectQueryParams(ECC_WorldStatic), Params) || Hit.ImpactNormal.Z < 0.85f) { return false; }
		Out = Hit.ImpactPoint + FVector(0, 0, Half + 3.0f);
		return !GetWorld()->OverlapBlockingTestByProfile(Out, FQuat::Identity, TEXT("Pawn"), FCollisionShape::MakeCapsule(Radius, Half), Params);
	};
	for (int32 Offset = 0; Offset < 3; ++Offset)
	{
		for (int32 Heading = 0; Heading < 8; ++Heading)
		{
			const FRotator Rotation(0, Heading * 45.0, 0);
			const FVector Forward = Rotation.Vector(), Right = FRotationMatrix(Rotation).GetUnitAxis(EAxis::Y);
			FVector Start;
			if (!Ground(Origin + Right * (Offset * 600.0), Start)) { continue; }
			bool bClear = true;
			for (int32 Arm = 0; Arm < 2 && bClear; ++Arm)
			{
				FVector Previous = Start;
				const FVector Base = Arm == 0 ? Start : Start + Forward * 200.0;
				for (int32 Sample = 0; Sample <= (Arm == 0 ? 12 : 5); ++Sample)
				{
					FVector Point;
					if (!Ground(Base + (Arm == 0 ? Forward : Right) * (Sample * 200.0), Point) || FMath::Abs(Point.Z - Previous.Z) > 80.0)
					{ bClear = false; break; }
					FHitResult Hit;
					if (GetWorld()->SweepSingleByProfile(Hit, Previous, Point, FQuat::Identity, TEXT("Pawn"), FCollisionShape::MakeCapsule(Radius, Half), Params))
					{ bClear = false; break; }
					Previous = Point;
				}
			}
			if (bClear) { RouteStart = Start; RouteRotation = Rotation; UE_LOG(LogUEGT2Diag, Log, TEXT("Auto-walk clear route: start=%s yaw=%.0f"), *Start.ToString(), Rotation.Yaw); return true; }
		}
	}
	return Check(false, TEXT("no bounded clear auto-walk corridor found near player start"));
}
bool UUEGT2AutoWalkSmokeSubsystem::ResetPosition()
{
	UEGT2AutoWalkSmoke::FContext C(GetWorld());
	if (!Check(C.Player->TeleportTo(RouteStart, RouteRotation, false, true), TEXT("cannot reset to clear auto-walk start"))) { return false; }
	C.Player->GetCharacterMovement()->StopMovementImmediately(); C.Player->GetCharacterMovement()->SetMovementMode(MOVE_Falling);
	C.PC->SetControlRotation(RouteRotation);
	return Check(!C.Player->IsAutoWalking(), TEXT("ordinary teleport did not cancel auto-walk"));
}
void UUEGT2AutoWalkSmokeSubsystem::StartCheck()
{
	using namespace UEGT2AutoWalkSmoke;
	FContext C(GetWorld());
	if (!Check(C.IsValid() && C.PC->GetInputConfig() && C.PC->GetInputConfig()->AutoWalkAction
		&& C.PC->GetMenuState() == EUEGT2MenuState::None && !GetWorld()->IsPaused(), TEXT("auto-walk ordinary input context unavailable"))) { return; }
	bOriginalPreference = C.Settings->GetAutoWalkEnabled(); bOriginalHardGate = C.Player->bAutoWalkFeatureEnabled;
	bOriginalSave = C.Settings->GetSaveProgressEnabled(); bOriginalAutosave = C.Settings->GetAutosaveEnabled();
	bOriginalNeeds = C.Settings->GetShowNeeds(); OriginalHudSize = C.Settings->GetHudSizeLevel();
	bOriginalClock = C.Sky->IsDayNightCycleEnabled(); bOriginalHudGate = C.Hud->bHudScalingEnabled; OriginalKey = C.Settings->GetKeyOverride(ActionName);
	bPreferencesChanged = true;
	C.Settings->SetSaveProgressEnabled(false); C.Settings->SetAutosaveEnabled(false); C.Settings->SetAutoWalkEnabled(true);
	C.Settings->SetShowNeeds(false); C.Settings->SetHudSizeLevel(0); C.Hud->bHudScalingEnabled = true; C.Sky->SetDayNightCycleEnabled(false);
	C.Player->bAutoWalkFeatureEnabled = true; C.Settings->SetKeyOverride(ActionName, EKeys::K); C.PC->RebuildInputMappings();
	if (!Check(C.Player->IsAutoWalkEnabled() && !C.PC->IsProgressEnabled() && !C.PC->IsAutosaveEnabled()
		&& UUEGT2InputConfig::GetEffectiveKey(EUEGT2InputSlot::ToggleAutoWalk) == EKeys::K, TEXT("auto-walk gates or rebound key invalid"))) { return; }
	if (FindClearRoute() && ResetPosition()) { SetStep(EStep::Ready); }
}
bool UUEGT2AutoWalkSmokeSubsystem::CheckWalking(bool bExpected, const TCHAR* Reason)
{
	UEGT2AutoWalkSmoke::FContext C(GetWorld());
	return Check(C.Player->IsAutoWalking() == bExpected && (!bExpected || (!C.Player->IsSprinting() && C.Player->GetCharacterMovement()->IsMovingOnGround())),
		*FString::Printf(TEXT("%s: active=%d enabled=%d grounded=%d sprint=%d speed=%.3f pending=%s menu=%d paused=%d"), Reason,
		C.Player->IsAutoWalking(), C.Player->IsAutoWalkEnabled(), C.Player->GetCharacterMovement()->IsMovingOnGround(), C.Player->IsSprinting(), C.Player->GetHorizontalSpeed(),
		*C.Player->GetPendingMovementInputVector().ToString(), static_cast<int32>(C.PC->GetMenuState()), GetWorld()->IsPaused()));
}
bool UUEGT2AutoWalkSmokeSubsystem::CheckDisplacement(const FVector& Direction, float Minimum, const TCHAR* Reason)
{
	UEGT2AutoWalkSmoke::FContext C(GetWorld());
	const FVector Delta = C.Player->GetActorLocation() - StepLocation;
	const double Along = FVector::DotProduct(FVector(Delta.X, Delta.Y, 0), Direction);
	UE_LOG(LogUEGT2Diag, Log, TEXT("Auto-walk motion: %s delta=%s along=%.3f"), Reason, *Delta.ToString(), Along);
	return Check(Along >= Minimum && FMath::Abs(Delta.Z) < 100.0, Reason);
}

void UUEGT2AutoWalkSmokeSubsystem::Advance()
{
	using namespace UEGT2AutoWalkSmoke;
	FContext C(GetWorld()); if (!Check(C.IsValid(), TEXT("auto-walk context disappeared"))) { return; }
	const FVector Forward = RouteRotation.Vector(), Right = FRotationMatrix(RouteRotation).GetUnitAxis(EAxis::Y);
	switch (Step)
	{
	case EStep::Ready:
		if (!Check(C.Player->GetCharacterMovement()->IsMovingOnGround(), TEXT("auto-walk start did not settle on ground"))) { return; }
		Key(EKeys::V, IE_Pressed); SetStep(EStep::OldKey); break;
	case EStep::OldKey:
		if (!CheckWalking(false, TEXT("old keyboard binding still started auto-walk"))) { return; }
		Key(EKeys::V, IE_Released); Key(EKeys::K, IE_Pressed); SetStep(EStep::Forward); break;
	case EStep::Forward:
	{
		if (!CheckWalking(true, TEXT("held rebound key did not stay active")) || !CheckDisplacement(Forward, 60.0f, TEXT("rebound key did not move forward"))) { return; }
		Key(EKeys::K, IE_Released);
		// AddYawInput still honors the engine's legacy per-controller scale.
		// Measure one unit through that public path so the intended turn is 90
		// degrees under either input setting, without overriding control rotation.
		const double BeforeYaw = C.PC->RotationInput.Yaw;
		C.PC->AddYawInput(1.0f);
		const double UnitYaw = C.PC->RotationInput.Yaw - BeforeYaw;
		if (!Check(FMath::IsFinite(UnitYaw) && FMath::Abs(UnitYaw) > 0.0001, TEXT("look input did not accept yaw"))) { return; }
		C.PC->AddYawInput(static_cast<float>((90.0 - UnitYaw) / UnitYaw)); SetStep(EStep::Steering); break;
	}
	case EStep::Steering:
		if (!Check(FMath::Abs(FMath::FindDeltaAngleDegrees(RouteRotation.Yaw, C.PC->GetControlRotation().Yaw) - 90.0) < 0.01,
			*FString::Printf(TEXT("look input yaw mismatch: start=%.9g current=%.9g expected_delta=90"), RouteRotation.Yaw, C.PC->GetControlRotation().Yaw))
			|| !CheckWalking(true, TEXT("release/look cancelled auto-walk")) || !CheckDisplacement(Right, 60.0f, TEXT("look input did not steer auto-walk"))) { return; }
		Key(EKeys::W, IE_Pressed); SetStep(EStep::Manual); break;
	case EStep::Manual:
		if (!CheckWalking(false, TEXT("meaningful manual movement did not cancel")) || !CheckDisplacement(Right, 50.0f, TEXT("manual movement was swallowed by cancellation"))) { return; }
		Key(EKeys::W, IE_Released); if (ResetPosition()) { SetStep(EStep::NormalReady); } break;
	case EStep::NormalReady:
		Key(EKeys::Gamepad_RightThumbstick, IE_Pressed); SetStep(EStep::NormalStart); break;
	case EStep::NormalStart:
		if (!CheckWalking(true, TEXT("right-stick click did not start auto-walk"))) { return; }
		Key(EKeys::Gamepad_RightThumbstick, IE_Released); BeginCapture(EStep::NormalImage, TEXT("01_ActiveNormal.png")); break;
	case EStep::NormalImage:
		Key(EKeys::Gamepad_RightThumbstick, IE_Pressed); SetStep(EStep::GamepadStop); break;
	case EStep::GamepadStop:
		if (!CheckWalking(false, TEXT("second right-stick click did not stop"))) { return; }
		Key(EKeys::Gamepad_RightThumbstick, IE_Released); if (ResetPosition()) { SetStep(EStep::MenuReady); } break;
	case EStep::MenuReady:
		Key(EKeys::K, IE_Pressed); SetStep(EStep::MenuStart); break;
	case EStep::MenuStart:
		if (!CheckWalking(true, TEXT("menu cancellation fixture did not start"))) { return; }
		C.PC->ShowPauseMenu(); SetStep(EStep::MenuPaused); break;
	case EStep::MenuPaused:
		if (!CheckWalking(false, TEXT("pause menu did not cancel")) || !Check(C.Player->GetHorizontalSpeed() < 1.0f && C.Player->GetPendingMovementInputVector().IsNearlyZero(), TEXT("menu cancellation retained velocity/input"))) { return; }
		C.PC->CloseMenu(); SetStep(EStep::MenuClosed); break;
	case EStep::MenuClosed:
		if (!CheckWalking(false, TEXT("closing menu restarted held toggle"))) { return; }
		Key(EKeys::K, IE_Repeat); SetStep(EStep::MenuHeld); break;
	case EStep::MenuHeld:
		if (!CheckWalking(false, TEXT("held toggle repeat restarted after menu")) || !Check(FVector::Dist2D(StepLocation, C.Player->GetActorLocation()) < 2.0 && C.Player->GetPendingMovementInputVector().IsNearlyZero(), TEXT("cancelled menu retained movement"))) { return; }
		Key(EKeys::K, IE_Released); SetStep(EStep::FocusReady); break;
	case EStep::FocusReady:
		Key(EKeys::K, IE_Pressed); SetStep(EStep::FocusStart); break;
	case EStep::FocusStart:
		if (!CheckWalking(true, TEXT("release/repress did not rearm after menu"))) { return; }
		C.PC->FlushPressedKeys(); SetStep(EStep::FocusFlushed); break;
	case EStep::FocusFlushed:
		if (!CheckWalking(false, TEXT("focus flush did not cancel")) || !Check(C.Player->GetHorizontalSpeed() < 1.0f && C.Player->GetPendingMovementInputVector().IsNearlyZero(), TEXT("focus flush retained velocity/input"))) { return; }
		Key(EKeys::K, IE_Repeat); SetStep(EStep::FocusHeld); break;
	case EStep::FocusHeld:
		if (!CheckWalking(false, TEXT("focus flush rearmed a held toggle")) || !Check(FVector::Dist2D(StepLocation, C.Player->GetActorLocation()) < 2.0 && C.Player->GetPendingMovementInputVector().IsNearlyZero(), TEXT("focus flush retained movement"))) { return; }
		Key(EKeys::K, IE_Released); SetStep(EStep::ConsoleReady); break;
	case EStep::ConsoleReady:
		Key(EKeys::K, IE_Pressed); SetStep(EStep::ConsoleStart); break;
	case EStep::ConsoleStart:
		if (!CheckWalking(true, TEXT("release/repress did not rearm after focus"))) { return; }
		if (!Check(GetDefault<UInputSettings>()->ConsoleKeys.Num() > 0 && ConsoleKey(GetDefault<UInputSettings>()->ConsoleKeys[0]), TEXT("real console unavailable"))) { return; }
		SetStep(EStep::ConsoleOpen); break;
	case EStep::ConsoleOpen:
		if (!Check(C.Console && C.Console->ConsoleActive(), TEXT("console key did not open the console")) || !CheckWalking(false, TEXT("console input did not cancel"))
			|| !Check(C.Player->GetHorizontalSpeed() < 1.0f && C.Player->GetPendingMovementInputVector().IsNearlyZero(), TEXT("console cancellation retained velocity/input"))) { return; }
		ConsoleKey(EKeys::Escape); SetStep(EStep::ConsoleClosed); break;
	case EStep::ConsoleClosed:
		if (!Check(C.Console && !C.Console->ConsoleActive(), TEXT("Escape did not close console")) || !CheckWalking(false, TEXT("closing console restarted auto-walk"))) { return; }
		Key(EKeys::K, IE_Repeat); SetStep(EStep::ConsoleHeld); break;
	case EStep::ConsoleHeld:
		if (!CheckWalking(false, TEXT("console flush rearmed a held toggle"))) { return; }
		Key(EKeys::K, IE_Released); if (ResetPosition()) { SetStep(EStep::LargerReady); } break;
	case EStep::LargerReady:
		C.Settings->SetHudSizeLevel(2); Key(EKeys::Gamepad_RightThumbstick, IE_Pressed); SetStep(EStep::LargerStart); break;
	case EStep::LargerStart:
		if (!CheckWalking(true, TEXT("larger indicator fixture did not start"))) { return; }
		Key(EKeys::Gamepad_RightThumbstick, IE_Released); BeginCapture(EStep::LargerImage, TEXT("02_ActiveLarger.png")); break;
	case EStep::LargerImage:
		C.Settings->SetAutoWalkEnabled(false); SetStep(EStep::PreferenceStopped); break;
	case EStep::PreferenceStopped:
		if (!CheckWalking(false, TEXT("player preference off did not cancel")) || !Check(!C.Player->IsAutoWalkEnabled(), TEXT("preference off still enabled"))) { return; }
		if (!ResetPosition()) { return; } Key(EKeys::K, IE_Pressed); Key(EKeys::W, IE_Pressed); SetStep(EStep::PreferenceManual); break;
	case EStep::PreferenceManual:
		if (!CheckWalking(false, TEXT("preference off accepted key")) || !CheckDisplacement(Forward, 50.0f, TEXT("preference off broke ordinary walking"))) { return; }
		Key(EKeys::K, IE_Released); Key(EKeys::W, IE_Released); C.Settings->SetAutoWalkEnabled(true); SetStep(EStep::PreferenceOn); break;
	case EStep::PreferenceOn:
		if (!CheckWalking(false, TEXT("reenabling preference restarted movement"))) { return; }
		Key(EKeys::K, IE_Pressed); SetStep(EStep::HardStart); break;
	case EStep::HardStart:
		if (!CheckWalking(true, TEXT("hard gate cancellation fixture did not start"))) { return; }
		C.Player->bAutoWalkFeatureEnabled = false; SetStep(EStep::HardStopped); break;
	case EStep::HardStopped:
		if (!CheckWalking(false, TEXT("hard gate off did not cancel")) || !Check(!C.Player->IsAutoWalkAvailable(), TEXT("hard off still available"))) { return; }
		Key(EKeys::K, IE_Repeat); Key(EKeys::W, IE_Pressed); SetStep(EStep::HardManual); break;
	case EStep::HardManual:
		if (!CheckWalking(false, TEXT("hard off accepted held key")) || !CheckDisplacement(Forward, 50.0f, TEXT("hard off broke ordinary walking"))) { return; }
		Key(EKeys::K, IE_Released); Key(EKeys::W, IE_Released); C.Player->bAutoWalkFeatureEnabled = true; SetStep(EStep::HardOn); break;
	case EStep::HardOn:
		if (!CheckWalking(false, TEXT("hard gate reenable restarted movement"))) { return; }
		C.PC->ShowPauseMenu(); C.PC->ShowSettingsPage(3); SetStep(EStep::SettingsReady); break;
	case EStep::SettingsReady:
		if (CheckSettings()) { BeginCapture(EStep::SettingsImage, TEXT("03_AutoWalkSetting.png")); } break;
	case EStep::SettingsImage:
		if (CheckSettings() && CheckWalking(false, TEXT("settings restarted movement"))) { Finish(true, TEXT("rebound keyboard/gamepad, steering, manual/menu/focus/console cancellation, off gates and HUD independent of Needs verified")); } break;
	default: break;
	}
}

bool UUEGT2AutoWalkSmokeSubsystem::CheckIndicator()
{
	UEGT2AutoWalkSmoke::FContext C(GetWorld());
	const float Expected = Step == EStep::NormalImage ? 1.0f : 1.5f;
	const FUEGT2HUDLayout Layout = UEGT2HUDLayout::Resolve(FVector2D(ExpectedWidth, ExpectedHeight), C.Settings->GetHudScale(), C.Hud->bHudScalingEnabled);
	return CheckWalking(true, TEXT("active indicator disappeared during live capture"))
		&& Check(!GetWorld()->IsPaused() && C.PC->GetMenuState() == EUEGT2MenuState::None && !C.Settings->GetShowNeeds()
			&& Layout.Scale == Expected && UUEGT2InputConfig::GetEffectiveKey(EUEGT2InputSlot::ToggleAutoWalk) == EKeys::K,
			TEXT("indicator capture needs active gameplay, hidden Needs and requested HUD size"));
}
bool UUEGT2AutoWalkSmokeSubsystem::CheckSettings()
{
	TSharedPtr<SWidget> Root = FSlateApplication::Get().GetKeyboardFocusedWidget();
	for (int32 Depth = 0; Root.IsValid() && Root->GetType() != TEXT("SUEGT2Menu") && Depth < 32; ++Depth) { Root = Root->GetParentWidget(); }
	if (!Check(Root.IsValid() && Root->GetType() == TEXT("SUEGT2Menu"), TEXT("auto-walk settings has no actual menu root"))) { return false; }
	int32 Budget = 768;
	const TSharedPtr<SWidget> Label = UEGT2AutoWalkSmoke::FindText(Root.ToSharedRef(), NSLOCTEXT("UEGT2Menu", "AutoWalkSetting", "Auto-walk Control"), Budget);
	if (!Check(Label.IsValid(), TEXT("actual Auto-walk Control setting is missing"))) { return false; }
	TSharedPtr<SWidget> Scroll = Label;
	for (int32 Depth = 0; Scroll.IsValid() && Scroll->GetType() != TEXT("SScrollBox") && Depth < 32; ++Depth) { Scroll = Scroll->GetParentWidget(); }
	if (!Check(Scroll.IsValid() && Scroll->GetType() == TEXT("SScrollBox"), TEXT("auto-walk setting has no scroll ancestor"))) { return false; }
	if (Step == EStep::SettingsReady) { StaticCastSharedPtr<SScrollBox>(Scroll)->ScrollDescendantIntoView(Label, false, EDescendantScrollDestination::Center); }
	else if (!Check(Scroll->GetCachedGeometry().GetLayoutBoundingRect().ContainsRect(Label->GetCachedGeometry().GetLayoutBoundingRect()), TEXT("auto-walk setting is clipped after scrolling"))) { return false; }
	return true;
}
void UUEGT2AutoWalkSmokeSubsystem::BeginCapture(EStep Next, const TCHAR* Name)
{
	SetStep(Next); CaptureFile = FPaths::Combine(CaptureDirectory, Name); bScreenshotRequested = false; bScreenshotComplete = CaptureDirectory.IsEmpty();
}
void UUEGT2AutoWalkSmokeSubsystem::HandleScreenshot(int32 Width, int32 Height, const TArray<FColor>& Bitmap)
{
	if (PendingFile.IsEmpty() || bFinished) { return; }
	if (Step != EStep::SettingsImage && !CheckIndicator()) { return; }
	if (!Check(Width == ExpectedWidth && Height == ExpectedHeight && Bitmap.Num() == Width * Height, TEXT("auto-walk screenshot dimensions incorrect"))) { return; }
	TArray<FColor> Opaque = Bitmap; for (FColor& Pixel : Opaque) { Pixel.A = 255; }
	TArray64<uint8> Png;
	FImageUtils::PNGCompressImageArray(Width, Height, TArrayView64<const FColor>(Opaque.GetData(), Opaque.Num()), Png);
	if (!Check(Png.Num() > 0 && FFileHelper::SaveArrayToFile(Png, *PendingFile), TEXT("cannot save auto-walk screenshot"))) { return; }
	UE_LOG(LogUEGT2Diag, Log, TEXT("Auto-walk screenshot: %s"), *PendingFile); PendingFile.Reset(); bScreenshotComplete = true;
}
