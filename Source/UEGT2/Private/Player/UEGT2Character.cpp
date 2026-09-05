#include "Player/UEGT2Character.h"

#include "Camera/CameraComponent.h"
#include "Components/CapsuleComponent.h"
#include "EnhancedInputComponent.h"
#include "Engine/World.h"
#include "Engine/Console.h"
#include "Engine/GameViewportClient.h"
#include "Engine/LocalPlayer.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PhysicsVolume.h"
#include "Interaction/UEGT2InteractionComponent.h"
#include "Kismet/GameplayStatics.h"
#include "Player/UEGT2InputConfig.h"
#include "Player/UEGT2NeedsComponent.h"
#include "Player/UEGT2PlayerController.h"
#include "Settings/UEGT2GameUserSettings.h"
#include "Sound/SoundBase.h"
#include "UEGT2LogChannels.h"

namespace UEGT2Character
{
	USoundBase* TryLoadSound(const TCHAR* Path)
	{
		return LoadObject<USoundBase>(nullptr, Path);
	}
}

AUEGT2Character::AUEGT2Character()
{
	PrimaryActorTick.bCanEverTick = true;

	UCapsuleComponent* Capsule = GetCapsuleComponent();
	Capsule->InitCapsuleSize(34.0f, 90.0f);

	// No skeletal mesh: this pawn is a camera on a capsule.
	if (USkeletalMeshComponent* MeshComponent = GetMesh())
	{
		MeshComponent->SetVisibility(false);
		MeshComponent->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	}

	Camera = CreateDefaultSubobject<UCameraComponent>(TEXT("Camera"));
	Camera->SetupAttachment(Capsule);
	Camera->SetRelativeLocation(FVector(0.0f, 0.0f, EyeHeight));
	Camera->bUsePawnControlRotation = true;
	Camera->SetFieldOfView(90.0f);

	Interaction = CreateDefaultSubobject<UUEGT2InteractionComponent>(TEXT("Interaction"));
	Life = CreateDefaultSubobject<UUEGT2NeedsComponent>(TEXT("Life"));

	bUseControllerRotationPitch = false;
	bUseControllerRotationYaw = true;
	bUseControllerRotationRoll = false;

	UCharacterMovementComponent* Movement = GetCharacterMovement();
	Movement->MaxWalkSpeed = WalkSpeed;
	Movement->MaxWalkSpeedCrouched = CrouchSpeed;
	Movement->MaxSwimSpeed = SwimSpeed;
	Movement->JumpZVelocity = 470.0f;
	Movement->AirControl = 0.28f;
	Movement->BrakingDecelerationWalking = 1800.0f;
	Movement->GroundFriction = 8.0f;
	Movement->MaxStepHeight = 45.0f;
	Movement->SetWalkableFloorAngle(50.0f);
	Movement->bCanWalkOffLedges = true;
	Movement->NavAgentProps.bCanCrouch = true;
	Movement->SetCrouchedHalfHeight(52.0f);
	Movement->bMaintainHorizontalGroundVelocity = true;

	DefaultJumpMaxCount = JumpMaxCount;
}

void AUEGT2Character::BeginPlay()
{
	Super::BeginPlay();

	BaseEyeZ = EyeHeight;
	FootstepSound = UEGT2Character::TryLoadSound(TEXT("/Game/Fairhaven/Audio/S_FootstepGround"));
	FootstepWaterSound = UEGT2Character::TryLoadSound(TEXT("/Game/Fairhaven/Audio/S_FootstepWater"));
	JumpSound = UEGT2Character::TryLoadSound(TEXT("/Game/Fairhaven/Audio/S_Jump"));

	RefreshFromSettings();
	UUEGT2GameUserSettings::OnSettingsApplied.AddUObject(this, &AUEGT2Character::RefreshFromSettings);

	UE_LOG(LogUEGT2Player, Log, TEXT("Explorer spawned at %s."),
		*GetActorLocation().ToCompactString());
}

void AUEGT2Character::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	CancelAutoWalk();
	UUEGT2GameUserSettings::OnSettingsApplied.RemoveAll(this);
	Super::EndPlay(EndPlayReason);
}

void AUEGT2Character::RefreshFromSettings()
{
	if (!IsAutoWalkEnabled()) { CancelAutoWalk(); }
	if (const UUEGT2GameUserSettings* Settings = UUEGT2GameUserSettings::Get())
	{
		CurrentFov = Settings->GetFieldOfView();
		if (Camera)
		{
			Camera->SetFieldOfView(CurrentFov);
		}
	}
}

bool AUEGT2Character::IsAutoWalkEnabled() const
{
	const UUEGT2GameUserSettings* Settings = UUEGT2GameUserSettings::Get();
	return IsAutoWalkAvailable() && Settings && Settings->GetAutoWalkEnabled();
}

bool AUEGT2Character::CanAutoWalk() const
{
	const UWorld* World = GetWorld();
	const AUEGT2PlayerController* PC = Cast<AUEGT2PlayerController>(Controller);
	const UCharacterMovementComponent* Movement = GetCharacterMovement();
	const ULocalPlayer* LocalPlayer = PC ? PC->GetLocalPlayer() : nullptr;
	UGameViewportClient* Viewport = LocalPlayer ? LocalPlayer->ViewportClient.Get() : nullptr;
	// The console flushes PlayerInput directly, bypassing the controller's
	// FlushPressedKeys override. UI ownership must also be checked here.
	if (Viewport && (Viewport->IgnoreInput() || (Viewport->ViewportConsole && Viewport->ViewportConsole->ConsoleActive()))) { return false; }
	return IsAutoWalkEnabled() && World && (World->WorldType == EWorldType::Game || World->WorldType == EWorldType::PIE) && HasActorBegunPlay()
		&& PC && PC->IsLocalController() && PC->GetPawn() == this
		&& !World->IsPaused() && !PC->IsMenuOpen() && !PC->IsDialogueOpen() && !IsMoveInputIgnored()
		&& Movement && Movement->IsMovingOnGround() && !Movement->bWantsToCrouch
		&& !bIsCrouched && !bFlying && !bNoclip && (!Life || !Life->IsOccupied())
		&& FMath::IsFinite(PC->GetControlRotation().Yaw);
}

bool AUEGT2Character::ToggleAutoWalk()
{
	if (bAutoWalking) { CancelAutoWalk(); return true; }
	if (!CanAutoWalk() || bManualActionPending || GetPendingMovementInputVector().SizeSquared() >= 0.2f * 0.2f) { return false; }
	// OnMove may already have queued resting-stick drift earlier in this input
	// tick. Starting must behave the same whichever action delegate ran first.
	ConsumeMovementInputVector();
	bSprinting = false;
	bAutoWalking = true;
	UE_LOG(LogUEGT2Player, Log, TEXT("Auto-walk started."));
	return true;
}

void AUEGT2Character::CancelAutoWalk()
{
	if (!bAutoWalking) { return; }
	bAutoWalking = false;
	ConsumeMovementInputVector();
	// Menus must not preserve a forward velocity to resume later. When ground
	// movement ends, retain the normal falling/swimming velocity and gravity.
	if (UCharacterMovementComponent* Movement = GetCharacterMovement(); Movement && Movement->IsMovingOnGround())
	{
		Movement->StopMovementImmediately();
	}
	UE_LOG(LogUEGT2Player, Log, TEXT("Auto-walk stopped."));
}

void AUEGT2Character::ApplyAutoWalkInput()
{
	const bool bManualAction = bManualActionPending;
	bManualActionPending = false;
	if (!bAutoWalking) { return; }
	if (bManualAction || !CanAutoWalk()) { CancelAutoWalk(); return; }
	// Another input producer takes priority too; do not erase its input while
	// retiring assistance. The normal OnMove path cancels before adding its own.
	if (GetPendingMovementInputVector().SizeSquared() >= 0.2f * 0.2f)
	{
		const FVector ManualInput = ConsumeMovementInputVector();
		CancelAutoWalk();
		AddMovementInput(ManualInput, 1.0f);
		return;
	}
	ConsumeMovementInputVector();
	const FRotator Yaw(0.0f, Controller->GetControlRotation().Yaw, 0.0f);
	AddMovementInput(Yaw.Vector(), 1.0f);
}

void AUEGT2Character::OnMovementModeChanged(EMovementMode PrevMovementMode, uint8 PreviousCustomMode)
{
	Super::OnMovementModeChanged(PrevMovementMode, PreviousCustomMode);
	if (!GetCharacterMovement()->IsMovingOnGround()) { CancelAutoWalk(); }
}

bool AUEGT2Character::TeleportTo(const FVector& DestLocation, const FRotator& DestRotation, bool bIsATest, bool bNoCheck)
{
	const bool bMoved = Super::TeleportTo(DestLocation, DestRotation, bIsATest, bNoCheck);
	if (bMoved && !bIsATest) { CancelAutoWalk(); }
	return bMoved;
}

FVector AUEGT2Character::GetPawnViewLocation() const
{
	return Camera ? Camera->GetComponentLocation() : Super::GetPawnViewLocation();
}

float AUEGT2Character::GetHorizontalSpeed() const
{
	return GetVelocity().Size2D();
}

float AUEGT2Character::DesiredMaxSpeed() const
{
	// Tiredness is in the legs. An NPC with no energy left walks off to sit
	// down and you can see them do it; the player's only tell is their own
	// pace, so an empty Energy need is worth about half a walking speed.
	const float Exertion = Life ? Life->GetExertionScale() : 1.0f;
	if (bIsCrouched)
	{
		return CrouchSpeed * Exertion;
	}
	const bool bCanSprint = !Life || Life->CanSprint();
	return (bSprinting && bCanSprint ? SprintSpeed : WalkSpeed) * Exertion;
}

void AUEGT2Character::BindInputActions(UEnhancedInputComponent* Input, UUEGT2InputConfig* Config)
{
	if (!Input || !Config)
	{
		return;
	}
	Input->BindAction(Config->MoveAction, ETriggerEvent::Triggered, this, &AUEGT2Character::OnMove);
	Input->BindAction(Config->LookAction, ETriggerEvent::Triggered, this, &AUEGT2Character::OnLook);
	Input->BindAction(Config->JumpAction, ETriggerEvent::Started, this, &AUEGT2Character::OnJumpStarted);
	Input->BindAction(Config->JumpAction, ETriggerEvent::Completed, this, &AUEGT2Character::OnJumpStopped);
	Input->BindAction(Config->SprintAction, ETriggerEvent::Started, this, &AUEGT2Character::OnSprintStarted);
	Input->BindAction(Config->SprintAction, ETriggerEvent::Completed, this, &AUEGT2Character::OnSprintStopped);
	Input->BindAction(Config->CrouchAction, ETriggerEvent::Started, this, &AUEGT2Character::OnCrouchToggle);
	// Held jump/crouch ascend and descend, but only while flying. Reusing the
	// existing actions keeps flight off the rebind list entirely.
	Input->BindAction(Config->JumpAction, ETriggerEvent::Triggered, this, &AUEGT2Character::OnFlyUp);
	Input->BindAction(Config->CrouchAction, ETriggerEvent::Triggered, this, &AUEGT2Character::OnFlyDown);
	Input->BindAction(Config->InteractAction, ETriggerEvent::Started, this, &AUEGT2Character::OnInteract);
}

void AUEGT2Character::OnMove(const FInputActionValue& Value)
{
	const FVector2D Axis = Value.Get<FVector2D>();
	if (Axis.IsNearlyZero() || !Controller)
	{
		return;
	}
	// A resting stick may still report small analog values. Only assistance
	// ignores that noise; ordinary manual movement keeps its existing response.
	if (bAutoWalking && Axis.SizeSquared() < 0.2f * 0.2f) { return; }
	if (Axis.SizeSquared() >= 0.2f * 0.2f) { bManualActionPending = true; }
	CancelAutoWalk();
	// Flying follows the full camera rotation, so looking up and holding
	// forward climbs. On foot it stays yaw-only or you would walk into the
	// ground every time you looked down.
	const FRotator Basis = bFlying
		? Controller->GetControlRotation()
		: FRotator(0.0f, Controller->GetControlRotation().Yaw, 0.0f);
	AddMovementInput(FRotationMatrix(Basis).GetUnitAxis(EAxis::X), Axis.Y);
	AddMovementInput(FRotationMatrix(Basis).GetUnitAxis(EAxis::Y), Axis.X);
}

void AUEGT2Character::OnLook(const FInputActionValue& Value)
{
	const FVector2D Axis = Value.Get<FVector2D>();
	const UUEGT2GameUserSettings* Settings = UUEGT2GameUserSettings::Get();
	const float Sensitivity = Settings ? Settings->GetMouseSensitivity() : 1.0f;
	const float PitchSign = (Settings && Settings->GetInvertLookY()) ? 1.0f : -1.0f;

	AddControllerYawInput(Axis.X * Sensitivity);
	AddControllerPitchInput(Axis.Y * Sensitivity * PitchSign);
}

void AUEGT2Character::OnJumpStarted()
{
	bManualActionPending = true;
	CancelAutoWalk();
	if (bFlying)
	{
		return;
	}
	if (GetCharacterMovement()->IsMovingOnGround() && JumpSound)
	{
		UGameplayStatics::PlaySoundAtLocation(this, JumpSound, GetActorLocation(), 0.5f);
	}
	Jump();
}

void AUEGT2Character::OnJumpStopped()
{
	StopJumping();
}

void AUEGT2Character::OnSprintStarted()
{
	bManualActionPending = true;
	CancelAutoWalk();
	const UUEGT2GameUserSettings* Settings = UUEGT2GameUserSettings::Get();
	if (Settings && Settings->GetToggleSprint())
	{
		bSprinting = !bSprinting;
	}
	else
	{
		bSprinting = true;
	}
	if (bSprinting && bIsCrouched)
	{
		UnCrouch();
	}
}

bool AUEGT2Character::IsSprinting() const
{
	// Holding the key with nothing left in the tank is not sprinting, and the
	// HUD and the field of view should not pretend otherwise.
	return bSprinting && (!Life || Life->CanSprint());
}

void AUEGT2Character::OnSprintStopped()
{
	const UUEGT2GameUserSettings* Settings = UUEGT2GameUserSettings::Get();
	if (!Settings || !Settings->GetToggleSprint())
	{
		bSprinting = false;
	}
}

void AUEGT2Character::OnCrouchToggle()
{
	bManualActionPending = true;
	CancelAutoWalk();
	if (bFlying)
	{
		return;
	}
	if (bIsCrouched)
	{
		UnCrouch();
	}
	else
	{
		bSprinting = false;
		Crouch();
	}
}

void AUEGT2Character::OnInteract()
{
	bManualActionPending = true;
	CancelAutoWalk();
	if (Interaction)
	{
		Interaction->TryInteract();
	}
}

// ---------------------------------------------------------------------------
// Dev mode
// ---------------------------------------------------------------------------
void AUEGT2Character::SetGodMode(bool bEnabled)
{
	bGodMode = bEnabled;
	SetCanBeDamaged(!bEnabled);
	// Fairhaven has no damage sources yet, so the observable half of god mode
	// is the jump count. SetCanBeDamaged is here so it keeps meaning something
	// the moment a hazard exists.
	JumpMaxCount = bEnabled ? 999 : DefaultJumpMaxCount;
	UE_LOG(LogUEGT2Player, Log, TEXT("God mode %s."), bEnabled ? TEXT("on") : TEXT("off"));
}

void AUEGT2Character::SetFlyEnabled(bool bEnabled)
{
	CancelAutoWalk();
	if (bFlying == bEnabled)
	{
		return;
	}
	bFlying = bEnabled;

	UCharacterMovementComponent* Movement = GetCharacterMovement();
	if (bEnabled)
	{
		if (bIsCrouched)
		{
			UnCrouch();
		}
		Movement->SetMovementMode(MOVE_Flying);
	}
	else
	{
		// Clearing flight clears noclip too: falling through the world with
		// collision off is not a state anyone wants to land in.
		SetNoclipEnabled(false);
		Movement->SetMovementMode(MOVE_Falling);
	}
	UE_LOG(LogUEGT2Player, Log, TEXT("Fly %s."), bEnabled ? TEXT("on") : TEXT("off"));
}

void AUEGT2Character::SetNoclipEnabled(bool bEnabled)
{
	CancelAutoWalk();
	if (bNoclip == bEnabled)
	{
		return;
	}
	bNoclip = bEnabled;

	if (bEnabled)
	{
		// Noclip is flight plus no collision; asking for it alone would leave
		// you standing on the ground inside the terrain.
		SetFlyEnabled(true);
	}
	SetActorEnableCollision(!bEnabled);
	UE_LOG(LogUEGT2Player, Log, TEXT("Noclip %s."), bEnabled ? TEXT("on") : TEXT("off"));
}

void AUEGT2Character::SetSpeedMultiplier(float Multiplier)
{
	SpeedMultiplier = FMath::Clamp(Multiplier, 1.0f, 50.0f);
}

void AUEGT2Character::ClearDevMovement()
{
	CancelAutoWalk();
	SetNoclipEnabled(false);
	SetFlyEnabled(false);
	SetGodMode(false);
	SetSpeedMultiplier(1.0f);
}

void AUEGT2Character::OnFlyUp()
{
	if (bFlying)
	{
		AddMovementInput(FVector::UpVector, 1.0f);
	}
}

void AUEGT2Character::OnFlyDown()
{
	if (bFlying)
	{
		AddMovementInput(FVector::UpVector, -1.0f);
	}
}

void AUEGT2Character::UpdateHeadBob(float DeltaSeconds)
{
	if (!Camera)
	{
		return;
	}
	const UUEGT2GameUserSettings* Settings = UUEGT2GameUserSettings::Get();
	const float UserScale = Settings ? Settings->GetHeadBobScale() : 1.0f;

	const bool bGrounded = GetCharacterMovement()->IsMovingOnGround();
	const float Speed = GetHorizontalSpeed();
	const float SpeedAlpha = FMath::Clamp(Speed / FMath::Max(WalkSpeed, 1.0f), 0.0f, 2.0f);
	const float TargetStrength = (bGrounded && Speed > 20.0f) ? SpeedAlpha : 0.0f;

	BobStrength = FMath::FInterpTo(BobStrength, TargetStrength, DeltaSeconds, 7.0f);
	BobPhase += DeltaSeconds * (5.0f + 2.6f * SpeedAlpha) * FMath::Max(TargetStrength, 0.0f);

	const float Amplitude = 2.6f * BobStrength * UserScale;
	const float Vertical = FMath::Sin(BobPhase * 2.0f) * Amplitude;
	const float Lateral = FMath::Cos(BobPhase) * Amplitude * 0.55f;

	const float CrouchOffset = bIsCrouched ? -34.0f : 0.0f;
	Camera->SetRelativeLocation(FVector(0.0f, Lateral, BaseEyeZ + CrouchOffset + Vertical));

	// One footstep per half bob cycle, only while actually moving on the ground.
	const int32 HalfCycle = FMath::FloorToInt(BobPhase * 2.0f / PI);
	if (HalfCycle != LastFootstepHalfCycle)
	{
		LastFootstepHalfCycle = HalfCycle;
		if (BobStrength > 0.25f)
		{
			PlayFootstep();
		}
	}
}

void AUEGT2Character::PlayFootstep()
{
	const bool bInWater = GetCharacterMovement()->IsSwimming()
		|| (GetPhysicsVolume() && GetPhysicsVolume()->bWaterVolume);
	USoundBase* Sound = bInWater ? FootstepWaterSound : FootstepSound;
	if (!Sound)
	{
		return;
	}
	const float Volume = IsSprinting() ? 0.55f : 0.38f;
	const float Pitch = FMath::FRandRange(0.92f, 1.08f);
	UGameplayStatics::PlaySoundAtLocation(this, Sound, GetActorLocation(), Volume, Pitch);
}

void AUEGT2Character::UpdateFieldOfView(float DeltaSeconds)
{
	if (!Camera)
	{
		return;
	}
	const UUEGT2GameUserSettings* Settings = UUEGT2GameUserSettings::Get();
	const float BaseFov = Settings ? Settings->GetFieldOfView() : 90.0f;
	const bool bFast = IsSprinting() && GetHorizontalSpeed() > WalkSpeed * 1.05f;
	const float Target = BaseFov + (bFast ? SprintFovBonus : 0.0f);

	CurrentFov = FMath::FInterpTo(CurrentFov, Target, DeltaSeconds, 6.0f);
	Camera->SetFieldOfView(CurrentFov);
}

void AUEGT2Character::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	UCharacterMovementComponent* Movement = GetCharacterMovement();
	Movement->MaxWalkSpeed = DesiredMaxSpeed() * SpeedMultiplier;
	Movement->MaxWalkSpeedCrouched = CrouchSpeed * SpeedMultiplier;
	Movement->MaxSwimSpeed = SwimSpeed * SpeedMultiplier;
	// Flight is a dev tool and does not care how tired the player is.
	Movement->MaxFlySpeed = FlySpeed * SpeedMultiplier * (bSprinting ? FlySprintScale : 1.0f);

	UpdateHeadBob(DeltaSeconds);
	UpdateFieldOfView(DeltaSeconds);
}
