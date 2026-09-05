#include "Player/UEGT2InputConfig.h"

#include "EnhancedActionKeyMapping.h"
#include "InputAction.h"
#include "InputMappingContext.h"
#include "InputModifiers.h"
#include "Settings/UEGT2GameUserSettings.h"
#include "UEGT2LogChannels.h"

#define LOCTEXT_NAMESPACE "UEGT2Input"

const TArray<FUEGT2InputSlotDef>& UUEGT2InputConfig::GetSlotDefs()
{
	static const TArray<FUEGT2InputSlotDef> Defs = []
	{
		TArray<FUEGT2InputSlotDef> Result;
		auto Add = [&Result](EUEGT2InputSlot Slot, const TCHAR* Name, FText Display,
			FKey Key, FKey Pad, bool bMovement)
		{
			FUEGT2InputSlotDef Def;
			Def.Slot = Slot;
			Def.Name = FName(Name);
			Def.DisplayName = MoveTemp(Display);
			Def.DefaultKey = Key;
			Def.DefaultGamepadKey = Pad;
			Def.bIsMovement = bMovement;
			Result.Add(MoveTemp(Def));
		};

		Add(EUEGT2InputSlot::MoveForward, TEXT("MoveForward"), LOCTEXT("MoveForward", "Move Forward"), EKeys::W, FKey(), true);
		Add(EUEGT2InputSlot::MoveBack,    TEXT("MoveBack"),    LOCTEXT("MoveBack", "Move Back"),       EKeys::S, FKey(), true);
		Add(EUEGT2InputSlot::MoveLeft,    TEXT("MoveLeft"),    LOCTEXT("MoveLeft", "Move Left"),       EKeys::A, FKey(), true);
		Add(EUEGT2InputSlot::MoveRight,   TEXT("MoveRight"),   LOCTEXT("MoveRight", "Move Right"),     EKeys::D, FKey(), true);
		Add(EUEGT2InputSlot::Jump,        TEXT("Jump"),        LOCTEXT("Jump", "Jump"),                EKeys::SpaceBar, EKeys::Gamepad_FaceButton_Bottom, false);
		Add(EUEGT2InputSlot::Sprint,      TEXT("Sprint"),      LOCTEXT("Sprint", "Sprint"),            EKeys::LeftShift, EKeys::Gamepad_LeftThumbstick, false);
		Add(EUEGT2InputSlot::Crouch,      TEXT("Crouch"),      LOCTEXT("Crouch", "Crouch"),            EKeys::LeftControl, EKeys::Gamepad_FaceButton_Right, false);
		Add(EUEGT2InputSlot::Interact,    TEXT("Interact"),    LOCTEXT("Interact", "Interact"),        EKeys::E, EKeys::Gamepad_FaceButton_Left, false);
		Add(EUEGT2InputSlot::Menu,        TEXT("Menu"),        LOCTEXT("Menu", "Menu"),                EKeys::Escape, EKeys::Gamepad_Special_Right, false);
		Add(EUEGT2InputSlot::Diagnostics, TEXT("Diagnostics"), LOCTEXT("Diagnostics", "Diagnostics Overlay"), EKeys::F3, FKey(), false);
		Add(EUEGT2InputSlot::Journal,     TEXT("Journal"),     LOCTEXT("Journal", "Survey Journal"), EKeys::J, EKeys::Gamepad_Special_Left, false);
		Add(EUEGT2InputSlot::ToggleAutoWalk, TEXT("ToggleAutoWalk"), LOCTEXT("ToggleAutoWalk", "Toggle Auto-walk"), EKeys::V, EKeys::Gamepad_RightThumbstick, false);
		return Result;
	}();
	return Defs;
}

const FUEGT2InputSlotDef* UUEGT2InputConfig::FindSlot(EUEGT2InputSlot Slot)
{
	return GetSlotDefs().FindByPredicate(
		[Slot](const FUEGT2InputSlotDef& Def) { return Def.Slot == Slot; });
}

FKey UUEGT2InputConfig::GetEffectiveKey(EUEGT2InputSlot Slot)
{
	const FUEGT2InputSlotDef* Def = FindSlot(Slot);
	if (!Def)
	{
		return FKey();
	}
	if (const UUEGT2GameUserSettings* Settings = UUEGT2GameUserSettings::Get())
	{
		const FKey Override = Settings->GetKeyOverride(Def->Name);
		if (Override.IsValid())
		{
			return Override;
		}
	}
	return Def->DefaultKey;
}

UInputAction* UUEGT2InputConfig::MakeAction(FName Name, uint8 ValueType)
{
	UInputAction* Action = NewObject<UInputAction>(this, Name);
	Action->ValueType = static_cast<EInputActionValueType>(ValueType);
	return Action;
}

void UUEGT2InputConfig::Initialize()
{
	const uint8 Axis2D = static_cast<uint8>(EInputActionValueType::Axis2D);
	const uint8 Boolean = static_cast<uint8>(EInputActionValueType::Boolean);

	MoveAction = MakeAction(TEXT("IA_Move"), Axis2D);
	LookAction = MakeAction(TEXT("IA_Look"), Axis2D);
	JumpAction = MakeAction(TEXT("IA_Jump"), Boolean);
	SprintAction = MakeAction(TEXT("IA_Sprint"), Boolean);
	CrouchAction = MakeAction(TEXT("IA_Crouch"), Boolean);
	InteractAction = MakeAction(TEXT("IA_Interact"), Boolean);
	MenuAction = MakeAction(TEXT("IA_Menu"), Boolean);
	DiagnosticsAction = MakeAction(TEXT("IA_Diagnostics"), Boolean);
	JournalAction = MakeAction(TEXT("IA_Journal"), Boolean);
	AutoWalkAction = MakeAction(TEXT("IA_AutoWalk"), Boolean);

	// The menu must still respond while the world is paused.
	MenuAction->bTriggerWhenPaused = true;
	JournalAction->bTriggerWhenPaused = true;
}

UInputMappingContext* UUEGT2InputConfig::BuildMappingContext()
{
	if (!MoveAction)
	{
		Initialize();
	}

	MappingContext = NewObject<UInputMappingContext>(this, TEXT("IMC_Fairhaven"));

	auto AddModifier = [this](FEnhancedActionKeyMapping& Mapping, UInputModifier* Modifier)
	{
		if (Modifier)
		{
			Mapping.Modifiers.Add(Modifier);
		}
	};

	auto MakeSwizzle = [this]()
	{
		UInputModifierSwizzleAxis* Swizzle = NewObject<UInputModifierSwizzleAxis>(this);
		Swizzle->Order = EInputAxisSwizzle::YXZ;
		return Swizzle;
	};

	auto MakeNegate = [this](bool bX, bool bY, bool bZ)
	{
		UInputModifierNegate* Negate = NewObject<UInputModifierNegate>(this);
		Negate->bX = bX;
		Negate->bY = bY;
		Negate->bZ = bZ;
		return Negate;
	};

	// --- Movement: four keys folded into one Axis2D -------------------------
	// X is strafe, Y is forward (see AUEGT2Character::OnMove). A key press
	// arrives as (1, 0, 0), so forward and back need a swizzle to move it onto
	// Y - and the negate that makes "back" backwards has to follow it there.
	// Modifier order matters: swizzle first, then negate the swizzled axis.
	struct FMoveMapping { EUEGT2InputSlot Slot; bool bSwizzle; bool bNegate; };
	static const FMoveMapping MoveMappings[] = {
		{ EUEGT2InputSlot::MoveForward, true,  false },
		{ EUEGT2InputSlot::MoveBack,    true,  true  },
		{ EUEGT2InputSlot::MoveRight,   false, false },
		{ EUEGT2InputSlot::MoveLeft,    false, true  },
	};

	for (const FMoveMapping& Move : MoveMappings)
	{
		const FKey Key = GetEffectiveKey(Move.Slot);
		if (!Key.IsValid())
		{
			continue;
		}
		FEnhancedActionKeyMapping& Mapping = MappingContext->MapKey(MoveAction, Key);
		if (Move.bSwizzle)
		{
			AddModifier(Mapping, MakeSwizzle());
		}
		if (Move.bNegate)
		{
			// Negate the axis the value is actually on, which for forward and
			// back is Y *because* the swizzle above just moved it there. Always
			// negating X meant S negated a zero and came out identical to W:
			// the key worked, the modifier ran, and the player walked forward.
			AddModifier(Mapping, Move.bSwizzle ? MakeNegate(false, true, false)
											   : MakeNegate(true, false, false));
		}
	}
	MappingContext->MapKey(MoveAction, EKeys::Gamepad_Left2D);

	// --- Look ---------------------------------------------------------------
	MappingContext->MapKey(LookAction, EKeys::Mouse2D);
	MappingContext->MapKey(LookAction, EKeys::Gamepad_Right2D);

	// --- Buttons ------------------------------------------------------------
	struct FButtonMapping { EUEGT2InputSlot Slot; TObjectPtr<UInputAction> UUEGT2InputConfig::* Member; };
	static const FButtonMapping ButtonMappings[] = {
		{ EUEGT2InputSlot::Jump,        &UUEGT2InputConfig::JumpAction },
		{ EUEGT2InputSlot::Sprint,      &UUEGT2InputConfig::SprintAction },
		{ EUEGT2InputSlot::Crouch,      &UUEGT2InputConfig::CrouchAction },
		{ EUEGT2InputSlot::Interact,    &UUEGT2InputConfig::InteractAction },
		{ EUEGT2InputSlot::Menu,        &UUEGT2InputConfig::MenuAction },
		{ EUEGT2InputSlot::Diagnostics, &UUEGT2InputConfig::DiagnosticsAction },
		{ EUEGT2InputSlot::Journal,     &UUEGT2InputConfig::JournalAction },
		{ EUEGT2InputSlot::ToggleAutoWalk, &UUEGT2InputConfig::AutoWalkAction },
	};

	for (const FButtonMapping& Button : ButtonMappings)
	{
		UInputAction* Action = this->*(Button.Member);
		const FUEGT2InputSlotDef* Def = FindSlot(Button.Slot);
		if (!Action || !Def)
		{
			continue;
		}
		const FKey Key = GetEffectiveKey(Button.Slot);
		if (Key.IsValid())
		{
			MappingContext->MapKey(Action, Key);
		}
		if (Def->DefaultGamepadKey.IsValid())
		{
			MappingContext->MapKey(Action, Def->DefaultGamepadKey);
		}
	}

	UE_LOG(LogUEGT2Player, Log, TEXT("Input mapping context built with %d slots."),
		GetSlotDefs().Num());
	return MappingContext;
}

#undef LOCTEXT_NAMESPACE
