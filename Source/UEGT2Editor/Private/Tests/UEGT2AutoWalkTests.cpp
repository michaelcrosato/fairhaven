#include "Misc/AutomationTest.h"

#if WITH_AUTOMATION_TESTS

#include "Components/StaticMeshComponent.h"
#include "Camera/PlayerCameraManager.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/GameViewportClient.h"
#include "Engine/LocalPlayer.h"
#include "Engine/StaticMesh.h"
#include "Engine/StaticMeshActor.h"
#include "Engine/World.h"
#include "EnhancedInputComponent.h"
#include "EnhancedInputSubsystems.h"
#include "EnhancedPlayerInput.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerState.h"
#include "GameFramework/InputSettings.h"
#include "GameFramework/WorldSettings.h"
#include "HAL/PlatformTime.h"
#include "InputAction.h"
#include "InputKeyEventArgs.h"
#include "InputMappingContext.h"
#include "Interaction/UEGT2Amenity.h"
#include "Misc/CommandLine.h"
#include "Misc/Guid.h"
#include "NPC/UEGT2NPCActor.h"
#include "Player/UEGT2Character.h"
#include "Player/UEGT2InputConfig.h"
#include "Player/UEGT2NeedsComponent.h"
#include "Player/UEGT2PlayerController.h"
#include "Settings/UEGT2GameUserSettings.h"
#include "UObject/StrongObjectPtr.h"

namespace UEGT2AutoWalkTests
{
	/** A real local player's Enhanced Input stack, without a game viewport or boot menu. */
	struct FFixture
	{
		FString OriginalCommandLine = FCommandLine::Get();
		UUEGT2GameUserSettings* Settings = UUEGT2GameUserSettings::Get();
		bool bOriginalEnabled = Settings && Settings->GetAutoWalkEnabled();
		bool bOriginalSprint = Settings && Settings->GetToggleSprint();
		FKey OriginalKey = Settings ? Settings->GetKeyOverride(TEXT("ToggleAutoWalk")) : FKey();
		TStrongObjectPtr<UGameInstance> Instance;
		TStrongObjectPtr<ULocalPlayer> LocalPlayer;
		UWorld* World = nullptr;
		AUEGT2PlayerController* Controller = nullptr;
		AUEGT2Character* Player = nullptr;
		UEnhancedPlayerInput* Input = nullptr;
		UEnhancedInputLocalPlayerSubsystem* Subsystem = nullptr;
		APlayerState* Pauser = nullptr;
		bool bReady = false;
		static constexpr float StepSeconds = 1.0f / 60.0f;

		FFixture()
		{
			if (!Settings || !GEngine) { return; }
			// These tests neither read nor write checkpoints or player settings.
			FCommandLine::Set(TEXT("-UEGT2SmokeWalk"));
			Settings->SetAutoWalkEnabled(true);
			Settings->SetToggleSprint(false);
			Settings->SetKeyOverride(TEXT("ToggleAutoWalk"), EKeys::K);
			Instance.Reset(NewObject<UGameInstance>(GEngine));
			Instance->InitializeStandalone(FName(*(TEXT("AutoWalkFixture_") + FGuid::NewGuid().ToString(EGuidFormats::Digits))));
			World = Instance->GetWorld();
			if (!World) { return; }
			AStaticMeshActor* Floor = World->SpawnActor<AStaticMeshActor>(FVector(0, 0, -50), FRotator::ZeroRotator);
			UStaticMesh* Cube = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube"));
			if (!Floor || !Cube) { return; }
			Floor->GetStaticMeshComponent()->SetStaticMesh(Cube);
			Floor->SetActorScale3D(FVector(100, 100, 1));
			Floor->GetStaticMeshComponent()->SetCollisionProfileName(TEXT("BlockAll"));
			Controller = World->SpawnActor<AUEGT2PlayerController>();
			Player = World->SpawnActor<AUEGT2Character>(FVector(0, 0, 92), FRotator::ZeroRotator);
			Pauser = World->SpawnActor<APlayerState>();
			if (!Controller || !Player || !Pauser) { return; }
			World->AddController(Controller);
			// ULocalPlayer is Within=Engine. SetPlayer establishes its controller
			// association; it is not owned by the game instance UObject.
			LocalPlayer.Reset(NewObject<ULocalPlayer>(GEngine));
			LocalPlayer->PlayerAdded(nullptr, 0);
			Controller->SetPlayer(LocalPlayer.Get());
			Controller->Possess(Player);
			Controller->AcknowledgedPawn = Player;
			Controller->ChangeState(NAME_Playing);
			Controller->PlayerCameraManager = World->SpawnActor<APlayerCameraManager>();
			if (!Controller->PlayerCameraManager) { return; }
			Controller->PlayerCameraManager->InitializeFor(Controller);
			Player->DispatchBeginPlay();
			Player->bAutoWalkFeatureEnabled = true;
			Player->GetCharacterMovement()->SetMovementMode(MOVE_Walking);
			Controller->SetControlRotation(FRotator::ZeroRotator);
			Input = Cast<UEnhancedPlayerInput>(Controller->PlayerInput);
			Subsystem = LocalPlayer->GetSubsystem<UEnhancedInputLocalPlayerSubsystem>();
			if (!Input || !Subsystem || !Controller->GetInputConfig()) { return; }
			Rebuild();
			bReady = Controller->IsLocalController() && LocalPlayer->PlayerController == Controller
				&& World->GetFirstPlayerController() == Controller
				&& Player->HasActorBegunPlay() && Player->IsAutoWalkEnabled() && Input->GetEnhancedActionMappingsView().Num() > 0;
		}
		~FFixture()
		{
			if (Controller) { Controller->UnPossess(); }
			if (LocalPlayer.IsValid()) { LocalPlayer->PlayerRemoved(); }
			if (World) { World->DestroyWorld(false); }
			if (Instance.IsValid()) { Instance->Shutdown(); }
			if (World) { GEngine->DestroyWorldContext(World); }
			if (Settings)
			{
				Settings->SetAutoWalkEnabled(bOriginalEnabled);
				Settings->SetToggleSprint(bOriginalSprint);
				Settings->SetKeyOverride(TEXT("ToggleAutoWalk"), OriginalKey);
			}
			FCommandLine::Set(*OriginalCommandLine);
		}
		void Key(FKey Key, EInputEvent Event)
		{
			Controller->InputKey(FInputKeyEventArgs(nullptr, FInputDeviceId::CreateFromInternalId(0), Key, Event, FPlatformTime::Cycles64()));
		}
		void TickInput()
		{
			// The production controller processes the engine action delegates and
			// applies assistance after the same tick's view rotation.
			Controller->PlayerTick(World->IsPaused() ? 0.0f : StepSeconds);
			// TickActor normally clears this after PlayerTick. This fixture drives
			// only the input phase so it must not replay the previous frame's look.
			Controller->RotationInput = FRotator::ZeroRotator;
		}
		void Frame(bool bMove = false)
		{
			TickInput();
			if (bMove)
			{
				Player->GetCharacterMovement()->TickComponent(StepSeconds, LEVELTICK_All, nullptr);
				Player->Tick(StepSeconds);
			}
			else { Player->ConsumeMovementInputVector(); }
		}
		void Press(FKey KeyCode) { Key(KeyCode, IE_Pressed); Frame(); }
		void Release(FKey KeyCode) { Key(KeyCode, IE_Released); Frame(); }
		void Rebuild()
		{
			Controller->RebuildInputMappings();
			FModifyContextOptions Options;
			Options.bForceImmediately = true;
			Subsystem->RequestRebuildControlMappings(Options);
		}
		void Pause()
		{
			Controller->ShowPauseMenu();
			World->GetWorldSettings()->SetPauserPlayerState(Pauser);
		}
		void Resume()
		{
			World->GetWorldSettings()->SetPauserPlayerState(nullptr);
			Controller->CloseMenu();
		}
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUEGT2AutoWalkInputTest, "UEGT2.Player.AutoWalk.Input",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FUEGT2AutoWalkInputTest::RunTest(const FString& Parameters)
{
	using namespace UEGT2AutoWalkTests;
	FFixture Sim;
	if (!TestTrue(TEXT("real local input fixture"), Sim.bReady)) { return false; }
	const UUEGT2InputConfig* Config = Sim.Controller->GetInputConfig();
	TestEqual(TEXT("auto action is Boolean"), Config->AutoWalkAction->ValueType, EInputActionValueType::Boolean);
	TestEqual(TEXT("effective rebound key"), UUEGT2InputConfig::GetEffectiveKey(EUEGT2InputSlot::ToggleAutoWalk), EKeys::K);
	Sim.Press(EKeys::V);
	TestFalse(TEXT("old binding cannot start"), Sim.Player->IsAutoWalking());
	Sim.Release(EKeys::V);
	Sim.Controller->InputKey(FInputKeyEventArgs::CreateSimulated(EKeys::K, IE_Pressed, 1.0f));
	Sim.Frame();
	TestFalse(TEXT("synthetic press cannot authorize assistance"), Sim.Player->IsAutoWalking());
	Sim.Controller->InputKey(FInputKeyEventArgs::CreateSimulated(EKeys::K, IE_Released, 0.0f));
	Sim.Frame();
	Sim.Press(EKeys::K);
	TestTrue(TEXT("actual rebound press starts"), Sim.Player->IsAutoWalking());
	for (int32 Index = 0; Index < 4; ++Index) { Sim.Key(EKeys::K, IE_Repeat); Sim.Frame(); }
	TestTrue(TEXT("held key toggles only once"), Sim.Player->IsAutoWalking());
	Sim.Release(EKeys::K);
	TestTrue(TEXT("release leaves assistance active"), Sim.Player->IsAutoWalking());
	Sim.Press(EKeys::K);
	TestFalse(TEXT("second press stops"), Sim.Player->IsAutoWalking());
	Sim.Release(EKeys::K);
	Sim.Press(EKeys::Gamepad_RightThumbstick);
	TestTrue(TEXT("actual gamepad click starts"), Sim.Player->IsAutoWalking());
	Sim.Release(EKeys::Gamepad_RightThumbstick);
	Sim.Input->InjectInputForAction(Config->MoveAction, FInputActionValue(FVector2D(0.08, 0.0)));
	Sim.TickInput();
	TestTrue(TEXT("small stick drift retains assistance"), Sim.Player->IsAutoWalking());
	TestTrue(TEXT("drift does not add lateral movement"), Sim.Player->ConsumeMovementInputVector().Equals(FVector::ForwardVector, 0.001));
	Sim.Input->InjectInputForAction(Config->MoveAction, FInputActionValue(FVector2D(-0.75, 0.0)));
	Sim.TickInput();
	TestFalse(TEXT("deliberate stick movement cancels"), Sim.Player->IsAutoWalking());
	TestTrue(TEXT("manual input survives cancellation"), Sim.Player->ConsumeMovementInputVector().Equals(FVector(0, -0.75, 0), 0.001));
	// The real mapping processes both actions in a single input stack. Test both
	// arrival orders; no callback order may leave auto-walk latched after W.
	for (bool bToggleFirst : { false, true })
	{
		const FKey Forward = UUEGT2InputConfig::GetEffectiveKey(EUEGT2InputSlot::MoveForward);
		Sim.Key(bToggleFirst ? EKeys::K : Forward, IE_Pressed);
		Sim.Key(bToggleFirst ? Forward : EKeys::K, IE_Pressed);
		Sim.TickInput();
		TestFalse(TEXT("simultaneous manual movement wins"), Sim.Player->IsAutoWalking());
		TestTrue(TEXT("simultaneous W still moves forward"), Sim.Player->ConsumeMovementInputVector().X > 0.99);
		Sim.Release(EKeys::K); Sim.Release(Forward);
	}
	for (bool bToggleFirst : { false, true })
	{
		if (bToggleFirst) { TestTrue(TEXT("toggle before drift starts"), Sim.Player->ToggleAutoWalk()); }
		Sim.Input->InjectInputForAction(Config->MoveAction, FInputActionValue(FVector2D(0.08, 0.0)));
		Sim.TickInput();
		if (!bToggleFirst) { TestTrue(TEXT("drift already queued still permits start"), Sim.Player->ToggleAutoWalk()); }
		TestTrue(TEXT("resting drift permits either input order"), Sim.Player->IsAutoWalking());
		Sim.Player->CancelAutoWalk();
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUEGT2AutoWalkOwnershipTest, "UEGT2.Player.AutoWalk.Ownership",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FUEGT2AutoWalkOwnershipTest::RunTest(const FString& Parameters)
{
	UEGT2AutoWalkTests::FFixture Sim;
	if (!TestTrue(TEXT("real local input fixture"), Sim.bReady)) { return false; }
	Sim.Press(EKeys::K);
	Sim.Player->GetCharacterMovement()->Velocity = FVector(380, 0, 0);
	Sim.Pause();
	TestFalse(TEXT("pause immediately cancels active walk"), Sim.Player->IsAutoWalking());
	TestTrue(TEXT("pause clears forward velocity"), Sim.Player->GetVelocity().IsNearlyZero());
	Sim.Frame(); Sim.Resume(); Sim.Key(EKeys::K, IE_Repeat); Sim.Frame();
	TestFalse(TEXT("held toggle cannot restart on close"), Sim.Player->IsAutoWalking());
	Sim.Release(EKeys::K); Sim.Press(EKeys::K);
	TestTrue(TEXT("release and fresh press rearm"), Sim.Player->IsAutoWalking());
	Sim.Controller->FlushPressedKeys();
	Sim.Key(EKeys::K, IE_Repeat); Sim.Frame();
	TestFalse(TEXT("focus flush does not restart on repeat"), Sim.Player->IsAutoWalking());
	// The engine specifically handles a fast release/repress between input ticks.
	Sim.Key(EKeys::K, IE_Released); Sim.Key(EKeys::K, IE_Pressed); Sim.Frame();
	TestTrue(TEXT("fast release/repress after flush is not swallowed"), Sim.Player->IsAutoWalking());
	Sim.Release(EKeys::K); Sim.Player->CancelAutoWalk();
	Sim.Pause(); Sim.Press(EKeys::K);
	TestFalse(TEXT("pressing in a paused menu cannot start"), Sim.Player->IsAutoWalking());
	Sim.Resume(); Sim.Key(EKeys::K, IE_Repeat); Sim.Frame();
	TestFalse(TEXT("key first held in menu cannot start on close"), Sim.Player->IsAutoWalking());
	Sim.Release(EKeys::K); Sim.Press(EKeys::K);
	Sim.Rebuild(); Sim.Key(EKeys::K, IE_Repeat); Sim.Frame();
	TestFalse(TEXT("mapping rebuild cancels and suppresses held key"), Sim.Player->IsAutoWalking());
	Sim.Release(EKeys::K); Sim.Press(EKeys::K); Sim.Release(EKeys::K);
	AUEGT2NPCActor* Speaker = Sim.World->SpawnActor<AUEGT2NPCActor>(FVector(1000, 0, 0), FRotator::ZeroRotator);
	if (!TestNotNull(TEXT("dialogue actor"), Speaker)) { return false; }
	Sim.Controller->OpenDialogue(Speaker);
	TestFalse(TEXT("unpaused dialogue cancels"), Sim.Player->IsAutoWalking());
	Sim.Controller->CloseDialogue(); Sim.Frame();
	TestFalse(TEXT("dialogue close never resumes"), Sim.Player->IsAutoWalking());
	// UIOnly may discard the entire original press before the controller sees
	// it. The first event after control returns can be an OS repeat.
	Sim.Controller->FlushPressedKeys(); Sim.Frame();
	Sim.Key(EKeys::K, IE_Repeat); Sim.Frame();
	TestFalse(TEXT("repeat without a controller press never starts"), Sim.Player->IsAutoWalking());
	Sim.Release(EKeys::K);
	Sim.Key(EKeys::K, IE_Pressed); Sim.Key(EKeys::K, IE_Released); Sim.Frame();
	TestTrue(TEXT("genuine quick tap before input tick still starts"), Sim.Player->IsAutoWalking());
	Sim.Player->CancelAutoWalk(); Sim.Frame();
	Sim.Press(EKeys::K); Sim.Release(EKeys::K);
	Sim.Controller->UnPossess();
	TestFalse(TEXT("unpossess clears old pawn latch"), Sim.Player->IsAutoWalking());
	Sim.Controller->Possess(Sim.Player); Sim.Frame();
	TestFalse(TEXT("repossessing never resumes"), Sim.Player->IsAutoWalking());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUEGT2AutoWalkMovementTest, "UEGT2.Player.AutoWalk.Movement",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FUEGT2AutoWalkMovementTest::RunTest(const FString& Parameters)
{
	UEGT2AutoWalkTests::FFixture Sim;
	if (!TestTrue(TEXT("real local input fixture"), Sim.bReady)) { return false; }
	Sim.Controller->SetControlRotation(FRotator(70, 90, 0));
	Sim.Press(EKeys::K); Sim.Release(EKeys::K); Sim.TickInput();
	TestTrue(TEXT("looking up still walks level along yaw"), Sim.Player->ConsumeMovementInputVector().Equals(FVector::RightVector, 0.001));
	Sim.Controller->SetControlRotation(FRotator(-60, 180, 0)); Sim.TickInput();
	TestTrue(TEXT("new look yaw steers this input tick"), Sim.Player->ConsumeMovementInputVector().Equals(-FVector::ForwardVector, 0.001));
	Sim.Controller->SetControlRotation(FRotator::ZeroRotator);
	PRAGMA_DISABLE_DEPRECATION_WARNINGS
	const float LegacyYaw = GetDefault<UInputSettings>()->bEnableLegacyInputScales ? Sim.Controller->GetDeprecatedInputYawScale() : 1.0f;
	PRAGMA_ENABLE_DEPRECATION_WARNINGS
	if (!TestTrue(TEXT("yaw input scale is usable"), !FMath::IsNearlyZero(LegacyYaw))) { return false; }
	Sim.Controller->AddYawInput(90.0f / LegacyYaw); Sim.TickInput();
	TestTrue(TEXT("controller processes this frame's look"), FMath::IsNearlyEqual(Sim.Controller->GetControlRotation().Yaw, 90.0f));
	TestTrue(TEXT("auto movement follows updated look in same tick"), Sim.Player->ConsumeMovementInputVector().Equals(FVector::RightVector, 0.001));
	Sim.Controller->SetControlRotation(FRotator::ZeroRotator);
	const FVector Start = Sim.Player->GetActorLocation();
	for (int32 Index = 0; Index < 40; ++Index) { Sim.Frame(true); }
	TestTrue(TEXT("real movement advances across physical floor"), Sim.Player->GetActorLocation().X > Start.X + 80.0);
	TestTrue(TEXT("ordinary floor keeps walking grounded"), Sim.Player->GetCharacterMovement()->IsMovingOnGround());
	TestFalse(TEXT("auto-walk never sprints"), Sim.Player->IsSprinting());
	FUEGT2NPCNeeds Tired;
	Tired.Energy = 0.0f;
	Sim.Player->GetLife()->RestoreProgress(Tired, FUEGT2Purse(100.0f), EUEGT2NPCRole::Villager);
	Sim.Player->Tick(UEGT2AutoWalkTests::FFixture::StepSeconds);
	TestTrue(TEXT("shared fatigue reduces ordinary walking speed"), Sim.Player->GetCharacterMovement()->MaxWalkSpeed < Sim.Player->WalkSpeed * 0.75f);
	for (EMovementMode Mode : { MOVE_Falling, MOVE_Swimming, MOVE_Flying, MOVE_None })
	{
		Sim.Player->CancelAutoWalk();
		Sim.Player->GetCharacterMovement()->SetMovementMode(MOVE_Walking);
		TestTrue(TEXT("ground allows explicit restart"), Sim.Player->ToggleAutoWalk());
		Sim.Player->GetCharacterMovement()->Velocity = FVector(120, 0, -35);
		Sim.Player->GetCharacterMovement()->SetMovementMode(Mode);
		TestFalse(TEXT("leaving ground cancels"), Sim.Player->IsAutoWalking());
		if (Mode == MOVE_Falling) { TestEqual(TEXT("falling cancellation preserves vertical velocity"), Sim.Player->GetVelocity().Z, -35.0); }
		TestFalse(TEXT("non-ground restart is rejected"), Sim.Player->ToggleAutoWalk());
		Sim.Player->GetCharacterMovement()->SetMovementMode(MOVE_Walking);
		TestFalse(TEXT("landing never resumes"), Sim.Player->IsAutoWalking());
	}
	Sim.Player->GetCharacterMovement()->StopMovementImmediately();
	TestTrue(TEXT("start before teleport"), Sim.Player->ToggleAutoWalk());
	TestTrue(TEXT("placement-only teleport probe succeeds"), Sim.Player->TeleportTo(Sim.Player->GetActorLocation(), FRotator::ZeroRotator, true, true));
	TestTrue(TEXT("placement-only probe preserves latch"), Sim.Player->IsAutoWalking());
	TestTrue(TEXT("real teleport succeeds"), Sim.Player->TeleportTo(FVector(0, 200, 92), FRotator::ZeroRotator, false, true));
	TestFalse(TEXT("real teleport cancels"), Sim.Player->IsAutoWalking());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUEGT2AutoWalkDisabledTest, "UEGT2.Player.AutoWalk.DisabledAndActions",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FUEGT2AutoWalkDisabledTest::RunTest(const FString& Parameters)
{
	UEGT2AutoWalkTests::FFixture Sim;
	if (!TestTrue(TEXT("real local input fixture"), Sim.bReady)) { return false; }
	Sim.Press(EKeys::K); Sim.Release(EKeys::K);
	Sim.Settings->SetAutoWalkEnabled(false); Sim.Player->RefreshFromSettings();
	TestFalse(TEXT("applied opt-out stops immediately"), Sim.Player->IsAutoWalking());
	TestFalse(TEXT("opt-out rejects explicit start"), Sim.Player->ToggleAutoWalk());
	Sim.Settings->SetAutoWalkEnabled(true); Sim.Frame();
	TestFalse(TEXT("opting back in does not resume"), Sim.Player->IsAutoWalking());
	Sim.Press(EKeys::K); Sim.Release(EKeys::K);
	Sim.Player->bAutoWalkFeatureEnabled = false; Sim.TickInput();
	TestFalse(TEXT("hard-off cancels before movement"), Sim.Player->IsAutoWalking());
	TestTrue(TEXT("hard-off leaves no queued input"), Sim.Player->ConsumeMovementInputVector().IsNearlyZero());
	Sim.Press(EKeys::K); Sim.Release(EKeys::K);
	TestFalse(TEXT("actual key cannot start while hard-off"), Sim.Player->IsAutoWalking());
	Sim.Player->AddMovementInput(FVector::RightVector, 0.7f);
	Sim.Player->GetCharacterMovement()->Velocity = FVector(12, 34, 0);
	Sim.Player->CancelAutoWalk(); Sim.Player->ApplyAutoWalkInput();
	TestTrue(TEXT("inactive cancellation preserves manual input"), Sim.Player->ConsumeMovementInputVector().Equals(FVector(0, 0.7, 0), 0.001));
	TestTrue(TEXT("inactive cancellation preserves manual velocity"), Sim.Player->GetVelocity().Equals(FVector(12, 34, 0)));
	Sim.Player->bAutoWalkFeatureEnabled = true;
	Sim.Player->GetCharacterMovement()->StopMovementImmediately();
	Sim.Settings->SetToggleSprint(true);
	const FKey SprintKey = UUEGT2InputConfig::GetEffectiveKey(EUEGT2InputSlot::Sprint);
	Sim.Press(SprintKey); Sim.Release(SprintKey);
	TestTrue(TEXT("fixture has a latched sprint"), Sim.Player->IsSprinting());
	Sim.Press(EKeys::K); Sim.Release(EKeys::K);
	TestFalse(TEXT("auto-walk clears latched sprint"), Sim.Player->IsSprinting());
	Sim.Player->CancelAutoWalk(); Sim.Settings->SetToggleSprint(false);
	for (EUEGT2InputSlot Slot : { EUEGT2InputSlot::Sprint, EUEGT2InputSlot::Crouch, EUEGT2InputSlot::Interact, EUEGT2InputSlot::Jump })
	{
		Sim.Player->GetCharacterMovement()->SetMovementMode(MOVE_Walking);
		Sim.Player->GetCharacterMovement()->bWantsToCrouch = false;
		Sim.Player->UnCrouch();
		Sim.Press(EKeys::K); Sim.Release(EKeys::K);
		TestTrue(TEXT("explicit start before manual action"), Sim.Player->IsAutoWalking());
		const FKey Key = UUEGT2InputConfig::GetEffectiveKey(Slot);
		Sim.Press(Key);
		TestFalse(TEXT("manual action cancels assistance"), Sim.Player->IsAutoWalking());
		if (Slot == EUEGT2InputSlot::Jump) { TestTrue(TEXT("normal jump request is retained"), Sim.Player->bPressedJump); }
		Sim.Release(Key);
	}
	Sim.Player->GetCharacterMovement()->bWantsToCrouch = true;
	TestFalse(TEXT("pending crouch rejects start"), Sim.Player->ToggleAutoWalk());
	Sim.Player->GetCharacterMovement()->bWantsToCrouch = false;
	AUEGT2Amenity* Bench = Sim.World->SpawnActor<AUEGT2Amenity>();
	if (!TestNotNull(TEXT("real amenity"), Bench)) { return false; }
	Bench->ConfigureAmenity(EUEGT2AmenityKind::Seat, TEXT("test bench"), EUEGT2NPCRole::Villager);
	TestTrue(TEXT("actual venue activity begins"), Sim.Player->GetLife()->BeginActivity(EUEGT2Activity::Rest, Bench,
		FText::FromString(TEXT("test bench")), EUEGT2NPCRole::Villager, 340.0f));
	TestFalse(TEXT("occupied venue rejects start"), Sim.Player->ToggleAutoWalk());
	Sim.Player->GetLife()->StopActivity(FText::GetEmpty());
	TestTrue(TEXT("venue exit permits deliberate start"), Sim.Player->ToggleAutoWalk());
	TStrongObjectPtr<UGameViewportClient> Viewport(NewObject<UGameViewportClient>(GEngine));
	Sim.LocalPlayer->ViewportClient = Viewport.Get();
	Viewport->SetIgnoreInput(true);
	Sim.Player->ApplyAutoWalkInput();
	TestFalse(TEXT("viewport input ownership cancels assistance"), Sim.Player->IsAutoWalking());
	TestFalse(TEXT("viewport input ownership denies start"), Sim.Player->ToggleAutoWalk());
	Viewport->SetIgnoreInput(false);
	Sim.LocalPlayer->ViewportClient = nullptr;
	TestFalse(TEXT("restored viewport control does not resume"), Sim.Player->IsAutoWalking());
	TestTrue(TEXT("fresh start after viewport ownership"), Sim.Player->ToggleAutoWalk());
	Sim.Player->SetFlyEnabled(false);
	TestFalse(TEXT("dev cleanup cancels even with already-normal mode"), Sim.Player->IsAutoWalking());
	return true;
}

#endif
