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

	UFUNCTION(BlueprintPure, Category = "UEGT2|Player")
	bool IsSprinting() const { return bSprinting; }

	/** Horizontal speed in cm/s, for the diagnostics overlay. */
	UFUNCTION(BlueprintPure, Category = "UEGT2|Player")
	float GetHorizontalSpeed() const;

	/** Re-read anything that depends on player settings (FOV, bob scale). */
	void RefreshFromSettings();

	// ---- Tuning -----------------------------------------------------------
	UPROPERTY(EditDefaultsOnly, Category = "UEGT2|Movement") float WalkSpeed = 380.0f;
	UPROPERTY(EditDefaultsOnly, Category = "UEGT2|Movement") float SprintSpeed = 720.0f;
	UPROPERTY(EditDefaultsOnly, Category = "UEGT2|Movement") float CrouchSpeed = 190.0f;
	UPROPERTY(EditDefaultsOnly, Category = "UEGT2|Movement") float SwimSpeed = 260.0f;
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

private:
	void UpdateHeadBob(float DeltaSeconds);
	void UpdateFieldOfView(float DeltaSeconds);
	void PlayFootstep();
	float DesiredMaxSpeed() const;

	UPROPERTY(VisibleAnywhere, Category = "UEGT2") TObjectPtr<UCameraComponent> Camera;
	UPROPERTY(VisibleAnywhere, Category = "UEGT2") TObjectPtr<UUEGT2InteractionComponent> Interaction;

	UPROPERTY(Transient) TObjectPtr<USoundBase> FootstepSound;
	UPROPERTY(Transient) TObjectPtr<USoundBase> FootstepWaterSound;
	UPROPERTY(Transient) TObjectPtr<USoundBase> JumpSound;

	bool bSprinting = false;
	float BobPhase = 0.0f;
	float BobStrength = 0.0f;
	float CurrentFov = 90.0f;
	float BaseEyeZ = 68.0f;
	int32 LastFootstepHalfCycle = 0;
};
