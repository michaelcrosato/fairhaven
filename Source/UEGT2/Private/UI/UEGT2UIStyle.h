// Fairhaven (UEGT2) - the one place the UI's colours and boxes are defined.
//
// Solid tinted boxes and the stock font, so the whole interface needs no
// assets. It lives in a header rather than in SUEGT2Menu.cpp because the
// conversation panel has to look like it belongs to the same game as the menu,
// and two copies of a palette is one palette and one drift waiting to happen.
#pragma once

#include "CoreMinimal.h"
#include "Styling/CoreStyle.h"
#include "Styling/SlateBrush.h"
#include "Styling/SlateTypes.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Text/STextBlock.h"

namespace UEGT2UI
{
	static const FLinearColor Ink(0.94f, 0.95f, 0.97f, 1.0f);
	static const FLinearColor Muted(0.66f, 0.69f, 0.74f, 1.0f);
	static const FLinearColor Accent(0.42f, 0.78f, 0.80f, 1.0f);
	static const FLinearColor Scrim(0.02f, 0.03f, 0.05f, 0.52f);
	static const FLinearColor Panel(0.05f, 0.07f, 0.09f, 0.88f);
	/** The conversation panel is read, not glanced at, so it is more solid. */
	static const FLinearColor SolidPanel(0.04f, 0.06f, 0.08f, 0.96f);
	static const FLinearColor Well(0.0f, 0.0f, 0.0f, 0.22f);
	static const FLinearColor Divider(1.0f, 1.0f, 1.0f, 0.08f);

	/** Warm, for the person you are speaking to; cool Accent stays the player's. */
	static const FLinearColor Voice(0.96f, 0.82f, 0.55f, 1.0f);

	inline const FSlateBrush* Box()
	{
		return FCoreStyle::Get().GetBrush("GenericWhiteBox");
	}

	inline FSlateFontInfo Font(const ANSICHAR* Style, int32 Size)
	{
		return FCoreStyle::GetDefaultFontStyle(Style, Size);
	}

	inline FSlateBrush Tinted(const FLinearColor& Colour)
	{
		FSlateBrush Brush = *Box();
		Brush.TintColor = FSlateColor(Colour);
		return Brush;
	}

	inline const FButtonStyle& ButtonStyle()
	{
		static const FButtonStyle Style = []
		{
			FButtonStyle Result;
			Result.SetNormal(Tinted(FLinearColor(1.0f, 1.0f, 1.0f, 0.06f)));
			Result.SetHovered(Tinted(FLinearColor(Accent.R, Accent.G, Accent.B, 0.28f)));
			Result.SetPressed(Tinted(FLinearColor(Accent.R, Accent.G, Accent.B, 0.46f)));
			Result.SetNormalPadding(FMargin(16.0f, 9.0f));
			Result.SetPressedPadding(FMargin(16.0f, 9.0f));
			return Result;
		}();
		return Style;
	}

	inline TSharedRef<SWidget> Label(const FText& Text, int32 Size = 13,
		const FLinearColor& Colour = Ink, const ANSICHAR* Style = "Regular")
	{
		return SNew(STextBlock)
			.Text(Text)
			.Font(Font(Style, Size))
			.ColorAndOpacity(FSlateColor(Colour));
	}
}
