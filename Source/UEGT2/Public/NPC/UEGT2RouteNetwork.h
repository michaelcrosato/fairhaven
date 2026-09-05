// Fairhaven (UEGT2) - the walkable road graph, baked by the content build.
//
// NPCs do not use a navmesh. Building one over a 4 km landscape would cost more
// to bake and to store than the entire rest of the map, and the thing NPCs
// actually need is much smaller: the streets are already polylines in
// world_features.json, and almost every journey in the town or the city is
// "get to a street, follow it, leave it". So the npc content stage samples
// those polylines into nodes, welds the junctions, and stores the graph here.
//
// The nodes carry their ground Z. That is the part worth knowing: because the
// height is baked, an NPC following a path needs no line traces at all, which
// is what makes hundreds of them affordable.
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "UEGT2RouteNetwork.generated.h"

/** The outgoing links of one node. A struct because TArray<TArray<>> is not a UPROPERTY. */
USTRUCT()
struct UEGT2_API FUEGT2RouteLinks
{
	GENERATED_BODY()

	UPROPERTY() TArray<int32> To;
};

UCLASS(ClassGroup = "UEGT2")
class UEGT2_API AUEGT2RouteNetwork : public AActor
{
	GENERATED_BODY()

public:
	AUEGT2RouteNetwork();

	virtual void BeginPlay() override;
	virtual void PostLoad() override;

	/** The first route network in the world, or null if the map has none. */
	static AUEGT2RouteNetwork* Get(const UWorld* World);

	// ---- Authoring (called from the content build) -------------------------
	/** Add a node at a world position. Returns its index. */
	UFUNCTION(BlueprintCallable, Category = "UEGT2|Routes")
	int32 AddNode(const FVector& Location);

	/** Link two nodes both ways. Ignores duplicates and out-of-range indices. */
	UFUNCTION(BlueprintCallable, Category = "UEGT2|Routes")
	void LinkNodes(int32 A, int32 B);

	/** Exclude unlinked stubs from the lookup grid, preserving node IDs. Call after baking. */
	UFUNCTION(BlueprintCallable, Category = "UEGT2|Routes")
	void FinaliseNetwork();

	UFUNCTION(BlueprintPure, Category = "UEGT2|Routes")
	int32 GetNodeCount() const { return NodeLocations.Num(); }

	UFUNCTION(BlueprintPure, Category = "UEGT2|Routes")
	int32 GetLinkCount() const;

	UFUNCTION(BlueprintPure, Category = "UEGT2|Routes")
	FVector GetNodeLocation(int32 Index) const;

	// ---- Queries -----------------------------------------------------------
	/** Nearest linked node within MaxDistance in XY, or INDEX_NONE. Ignores Z. */
	int32 FindNearestNode(const FVector& Location, float MaxDistance) const;

	/**
	 * A* from the node nearest Start to the node nearest Goal.
	 *
	 * OutPoints is the node positions along the route, without Start or Goal
	 * themselves - the caller owns the first and last legs, because it knows
	 * how close it wants to get. Returns false when either end is off the road
	 * network or the search hit its visit cap; the caller then walks straight,
	 * which is correct out in the fields where there are no streets anyway.
	 */
	bool FindPath(const FVector& Start, const FVector& Goal, TArray<FVector>& OutPoints) const;

	/** A linked node within Radius in XY, chosen by Seed; Location when none is in range. */
	FVector GetWanderTarget(const FVector& Location, float Radius, uint32 Seed) const;

	/** How many A* searches this network has run. Diagnostics only. */
	int32 GetSearchCount() const { return SearchCount; }
	/**
	 * How many searches have found a cycle in their own parent links.
	 *
	 * Must be zero. It is a counter rather than a check() because a cycle used
	 * to take the whole game down and the point of the guard is that it no
	 * longer does - but a test asserts it stays at zero, so a search that
	 * starts producing them cannot go quiet again.
	 */
	UFUNCTION(BlueprintPure, Category = "UEGT2|NPC")
	int32 GetCycleCount() const { return CycleCount; }

private:
	void BuildSpatialIndex() const;
	FIntPoint CellOf(const FVector& Location) const;

	UPROPERTY() TArray<FVector> NodeLocations;
	UPROPERTY() TArray<FUEGT2RouteLinks> NodeLinks;

	/**
	 * Uniform grid over the node set, built on demand. Mutable because the
	 * queries are const and the index is pure derived data - a network loaded
	 * from the map has never had BeginPlay called on it in the editor.
	 */
	mutable TMap<FIntPoint, TArray<int32>> Cells;
	mutable bool bIndexBuilt = false;
	mutable int32 SearchCount = 0;
	/** Parent-link cycles found and refused. Must stay zero; see GetCycleCount. */
	mutable int32 CycleCount = 0;

	/** 60 m cells: a couple of nodes each at the 25 m sampling the bake uses. */
	static constexpr float CellSize = 6000.0f;

	/** Stop a pathological query rather than stalling the frame. */
	static constexpr int32 MaxVisitedNodes = 6000;

};
