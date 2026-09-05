// Fairhaven (UEGT2) - Enhanced Input built entirely in code.
//
// Input actions and the mapping context are constructed at runtime from the
// table in UEGT2InputConfig.cpp rather than stored as binary assets. That keeps
// every binding readable in source, diffable, and rebindable at runtime: a
// rebind is just a key override in UUEGT2GameUserSettings plus a rebuild.
#pragma once

#include "CoreMinimal.h"
#include "InputCoreTypes.h"
#include "UObject/Object.h"
#include "UEGT2InputConfig.generated.h"

class UInputAction;
class UInputMappingContext;

/** Every rebindable control in the game. */
UENUM(BlueprintType)
enum class EUEGT2InputSlot : uint8
{
	MoveForward,
	MoveBack,
	MoveLeft,
	MoveRight,
	Jump,
	Sprint,
	Crouch,
	Interact,
	Menu,
	Diagnostics,
	Journal,
	Count UMETA(Hidden)
};

/** Static description of one control slot. */
struct FUEGT2InputSlotDef
{
	EUEGT2InputSlot Slot = EUEGT2InputSlot::Count;
	FName Name;
	FText DisplayName;
	FKey DefaultKey;
	FKey DefaultGamepadKey;
	/** Movement slots feed the Move axis; the rest are their own button action. */
	bool bIsMovement = false;
};

UCLASS()
class UEGT2_API UUEGT2InputConfig : public UObject
{
	GENERATED_BODY()

public:
	/** Create the input actions. Safe to call once per owning controller. */
	void Initialize();

	/**
	 * (Re)build the mapping context, applying any key overrides the player has
	 * saved. Call again after a rebind and re-add the context to the subsystem.
	 */
	UInputMappingContext* BuildMappingContext();

	/** Ordered table of every control slot, for the settings screen. */
	static const TArray<FUEGT2InputSlotDef>& GetSlotDefs();
	static const FUEGT2InputSlotDef* FindSlot(EUEGT2InputSlot Slot);

	/** Currently bound keyboard/mouse key for a slot, honouring player overrides. */
	static FKey GetEffectiveKey(EUEGT2InputSlot Slot);

	UPROPERTY(Transient) TObjectPtr<UInputAction> MoveAction = nullptr;
	UPROPERTY(Transient) TObjectPtr<UInputAction> LookAction = nullptr;
	UPROPERTY(Transient) TObjectPtr<UInputAction> JumpAction = nullptr;
	UPROPERTY(Transient) TObjectPtr<UInputAction> SprintAction = nullptr;
	UPROPERTY(Transient) TObjectPtr<UInputAction> CrouchAction = nullptr;
	UPROPERTY(Transient) TObjectPtr<UInputAction> InteractAction = nullptr;
	UPROPERTY(Transient) TObjectPtr<UInputAction> MenuAction = nullptr;
	UPROPERTY(Transient) TObjectPtr<UInputAction> DiagnosticsAction = nullptr;
	UPROPERTY(Transient) TObjectPtr<UInputAction> JournalAction = nullptr;
	UPROPERTY(Transient) TObjectPtr<UInputMappingContext> MappingContext = nullptr;

private:
	UInputAction* MakeAction(FName Name, uint8 ValueType);
};
