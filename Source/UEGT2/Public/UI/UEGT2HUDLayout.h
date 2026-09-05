#pragma once

#include "CoreMinimal.h"

/** Canvas layout only: physical viewport/rectangles, logical content sizes. */
struct FUEGT2HUDLayout
{
	FVector2D Viewport = FVector2D(1280.0, 720.0);
	float Scale = 1.0f;
	bool bEnhanced = false;
	FVector2D LogicalViewport() const { return Viewport / Scale; }
	FVector2D ToScreen(const FVector2D& Point) const { return Point * Scale; }
	FVector2D ToLogical(const FVector2D& Point) const { return Point / Scale; }
};

enum class EUEGT2HUDAnchor : uint8 { TopLeft, BottomLeft, BottomRight };

namespace UEGT2HUDLayout
{
	/** Normal/hard-off is exactly 1.0. Invalid requests fall back to Normal. */
	UEGT2_API FUEGT2HUDLayout Resolve(const FVector2D& Viewport, float RequestedScale, bool bGateEnabled);
	/** Physical insets stay fixed; content size is in unscaled layout units. */
	UEGT2_API FBox2D AnchorPanel(const FUEGT2HUDLayout& Layout, const FVector2D& Size,
		EUEGT2HUDAnchor Anchor, const FVector2D& Inset);
	/** Width budget for life when a survey panel shares the bottom row. Logical units. */
	UEGT2_API float BottomLeftMaxWidth(const FUEGT2HUDLayout& Layout, float RightWidth, bool bRightVisible);
	/** Body includes its padding. Normal preserves the original height-140 text baseline. */
	UEGT2_API FBox2D PlaceMessage(const FUEGT2HUDLayout& Layout, const FVector2D& Size,
		const TArray<FBox2D>& BottomPanels);
	/** Occupancy includes body and tail. Returns false if no clear bounded placement exists. */
	UEGT2_API bool PlaceBubble(const FUEGT2HUDLayout& Layout, const FVector2D& ScreenAnchor,
		const FVector2D& Size, const TArray<FBox2D>& Occupied, FBox2D& OutBounds);
}
