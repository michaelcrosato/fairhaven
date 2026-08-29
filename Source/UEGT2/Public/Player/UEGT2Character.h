// Fairhaven (UEGT2) - the first-person explorer pawn.
//
// Deliberately animation-free: the player is a camera on a capsule with
// procedural head bob and FOV response. No skeletal mesh, no anim blueprint.
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Character.h"
#include "UEGT2Character.generated.h"

class UCameraComponent;
class UInputAction;
class UUEGT2InteractionComponent;
class UUEGT2NeedsComponent;
class USoundBase;
struct FInputActionValue;

UCLASS(ClassGroup = "UEGT2")
class UEGT2_API AUEGT2Character : public ACharacter
{
	GENERATED_BODY()

public:
	AUEGT2Character();

	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;
	virtual FVector GetPawnViewLocation() const override;

	/** Bind this pawn's actions onto an already-created Enhanced Input component. */
	void BindInputActions(class UEnhancedInputComponent* Input, class UUEGT2InputConfig* Config);

	UFUNCTION(BlueprintPure, Category = "UEGT2|Player")
	UUEGT2InteractionComponent* GetInteraction() const { return Interaction; }

	UFUNCTION(BlueprintPure, Category = "UEGT2|Player")
	UCameraComponent* GetCamera() const { return Camera; }

	/** The player's four needs, their trade and their purse. */
	UFUNCTION(BlueprintPure, Category = "UEGT2|Player")
	UUEGT2NeedsComponent* GetLife() const { return Life; }

	/** Holding sprint and actually able to: an exhausted player is not. */
	UFUNCTION(BlueprintPure, Category = "UEGT2|Player")
	bool IsSprinting() const;

	/** Horizontal speed in cm/s, for the diagnostics overlay. */
	UFUNCTION(BlueprintPure, Category = "UEGT2|Player")
	float GetHorizontalSpeed() const;

	/** Re-read anything that depends on player settings (FOV, bob scale). */
	void RefreshFromSettings();

	// ---- Dev mode ---------------------------------------------------------
	// Owned by UUEGT2DevModeSubsystem; the pawn only knows how to be in these
	// states, not when it should be.

	/** Invulnerable and unlimited air jumps. */
	void SetGodMode(bool bEnabled);
	bool IsGodMode() const { return bGodMode; }

	/** Free 3D flight along the camera direction. */
	void SetFlyEnabled(bool bEnabled);
	bool IsFlyEnabled() const { return bFlying; }

	/** Fly and pass through geometry. Implies flight. */
	void SetNoclipEnabled(bool bEnabled);
	bool IsNoclipEnabled() const { return bNoclip; }

	/** 1-50x on walk, sprint, crouch, swim and fly speed. */
	void SetSpeedMultiplier(float Multiplier);
	float GetSpeedMultiplier() const { return SpeedMultiplier; }

	/** Put movement, collision, jump count and speed back to normal play. */
	void ClearDevMovement();

	// ---- Tuning -----------------------------------------------------------
	UPROPERTY(EditDefaultsOnly, Category = "UEGT2|Movement") float WalkSpeed = 380.0f;
	UPROPERTY(EditDefaultsOnly, Category = "UEGT2|Movement") float SprintSpeed = 720.0f;
	UPROPERTY(EditDefaultsOnly, Category = "UEGT2|Movement") float CrouchSpeed = 190.0f;
	UPROPERTY(EditDefaultsOnly, Category = "UEGT2|Movement") float SwimSpeed = 260.0f;
	UPROPERTY(EditDefaultsOnly, Category = "UEGT2|Movement") float FlySpeed = 900.0f;
	/** Extra multiplier from holding sprint while flying. */
	UPROPERTY(EditDefaultsOnly, Category = "UEGT2|Movement") float FlySprintScale = 2.5f;
	UPROPERTY(EditDefaultsOnly, Category = "UEGT2|Camera") float SprintFovBonus = 8.0f;
	UPROPERTY(EditDefaultsOnly, Category = "UEGT2|Camera") float EyeHeight = 68.0f;

protected:
	void OnMove(const FInputActionValue& Value);
	void OnLook(const FInputActionValue& Value);
	void OnJumpStarted();
	void OnJumpStopped();
	void OnSprintStarted();
	void OnSprintStopped();
	void OnCrouchToggle();
	void OnInteract();
	/** Jump and crouch become ascend/descend while flying. */
	void OnFlyUp();
	void OnFlyDown();

private:
	void UpdateHeadBob(float DeltaSeconds);
	void UpdateFieldOfView(float DeltaSeconds);
	void PlayFootstep();
	float DesiredMaxSpeed() const;

	UPROPERTY(VisibleAnywhere, Category = "UEGT2") TObjectPtr<UCameraComponent> Camera;
	UPROPERTY(VisibleAnywhere, Category = "UEGT2") TObjectPtr<UUEGT2InteractionComponent> Interaction;
	UPROPERTY(VisibleAnywhere, Category = "UEGT2") TObjectPtr<UUEGT2NeedsComponent> Life;

	UPROPERTY(Transient) TObjectPtr<USoundBase> FootstepSound;
	UPROPERTY(Transient) TObjectPtr<USoundBase> FootstepWaterSound;
	UPROPERTY(Transient) TObjectPtr<USoundBase> JumpSound;

	bool bSprinting = false;
	bool bGodMode = false;
	bool bFlying = false;
	bool bNoclip = false;
	float SpeedMultiplier = 1.0f;
	/** JumpMaxCount before god mode raised it, so it can be put back. */
	int32 DefaultJumpMaxCount = 1;
	float BobPhase = 0.0f;
	float BobStrength = 0.0f;
	float CurrentFov = 90.0f;
	float BaseEyeZ = 68.0f;
	int32 LastFootstepHalfCycle = 0;
};
