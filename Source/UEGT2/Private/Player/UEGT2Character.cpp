#include "Player/UEGT2Character.h"

#include "Camera/CameraComponent.h"
#include "Components/CapsuleComponent.h"
#include "EnhancedInputComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PhysicsVolume.h"
#include "Interaction/UEGT2InteractionComponent.h"
#include "Kismet/GameplayStatics.h"
#include "Player/UEGT2InputConfig.h"
#include "Settings/UEGT2GameUserSettings.h"
#include "Sound/SoundBase.h"
#include "UEGT2LogChannels.h"

namespace
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
	Movement->CrouchedHalfHeight = 52.0f;
	Movement->bMaintainHorizontalGroundVelocity = true;

	DefaultJumpMaxCount = JumpMaxCount;
}

void AUEGT2Character::BeginPlay()
{
	Super::BeginPlay();

	BaseEyeZ = EyeHeight;
	FootstepSound = TryLoadSound(TEXT("/Game/Fairhaven/Audio/S_FootstepGround"));
	FootstepWaterSound = TryLoadSound(TEXT("/Game/Fairhaven/Audio/S_FootstepWater"));
	JumpSound = TryLoadSound(TEXT("/Game/Fairhaven/Audio/S_Jump"));

	RefreshFromSettings();
	UUEGT2GameUserSettings::OnSettingsApplied.AddUObject(this, &AUEGT2Character::RefreshFromSettings);

	UE_LOG(LogUEGT2Player, Log, TEXT("Explorer spawned at %s."),
		*GetActorLocation().ToCompactString());
}

void AUEGT2Character::RefreshFromSettings()
{
	if (const UUEGT2GameUserSettings* Settings = UUEGT2GameUserSettings::Get())
	{
		CurrentFov = Settings->GetFieldOfView();
		if (Camera)
		{
			Camera->SetFieldOfView(CurrentFov);
		}
	}
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
	if (bIsCrouched)
	{
		return CrouchSpeed;
	}
	return bSprinting ? SprintSpeed : WalkSpeed;
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
	const float Volume = bSprinting ? 0.55f : 0.38f;
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
	const bool bFast = bSprinting && GetHorizontalSpeed() > WalkSpeed * 1.05f;
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
	Movement->MaxFlySpeed = FlySpeed * SpeedMultiplier * (bSprinting ? FlySprintScale : 1.0f);

	UpdateHeadBob(DeltaSeconds);
	UpdateFieldOfView(DeltaSeconds);
}
