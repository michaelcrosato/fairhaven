#include "NPC/UEGT2RouteNetwork.h"

#include "Engine/World.h"
#include "EngineUtils.h"
#include "NPC/UEGT2NPCTypes.h"
#include "UEGT2LogChannels.h"

AUEGT2RouteNetwork::AUEGT2RouteNetwork()
{
	PrimaryActorTick.bCanEverTick = false;
	RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	bIsEditorOnlyActor = false;
}

AUEGT2RouteNetwork* AUEGT2RouteNetwork::Get(const UWorld* World)
{
	if (!World)
	{
		return nullptr;
	}
	for (TActorIterator<AUEGT2RouteNetwork> It(const_cast<UWorld*>(World)); It; ++It)
	{
		return *It;
	}
	return nullptr;
}

void AUEGT2RouteNetwork::PostLoad()
{
	Super::PostLoad();
	bIndexBuilt = false;
}

void AUEGT2RouteNetwork::BeginPlay()
{
	Super::BeginPlay();
	BuildSpatialIndex();
	UE_LOG(LogUEGT2NPC, Log, TEXT("Route network: %d nodes, %d links."),
		NodeLocations.Num(), GetLinkCount());
}

int32 AUEGT2RouteNetwork::AddNode(const FVector& Location)
{
	const int32 Index = NodeLocations.Add(Location);
	NodeLinks.AddDefaulted();
	bIndexBuilt = false;
	return Index;
}

void AUEGT2RouteNetwork::LinkNodes(int32 A, int32 B)
{
	if (A == B || !NodeLinks.IsValidIndex(A) || !NodeLinks.IsValidIndex(B))
	{
		return;
	}
	NodeLinks[A].To.AddUnique(B);
	NodeLinks[B].To.AddUnique(A);
	// A prior query may have omitted these nodes while they were unlinked.
	bIndexBuilt = false;
}

void AUEGT2RouteNetwork::FinaliseNetwork()
{
	// Nodes that ended up with no links are sampling artefacts - a road that
	// contributed a single point before running off the map. They would only
	// ever be found as "nearest node" and then fail to path anywhere.
	// Keep their authored indices stable, but omit them from the lookup grid.
	int32 Orphans = 0;
	for (const FUEGT2RouteLinks& Links : NodeLinks)
	{
		if (Links.To.Num() == 0)
		{
			++Orphans;
		}
	}

	bIndexBuilt = false;
	BuildSpatialIndex();

	UE_LOG(LogUEGT2NPC, Log,
		TEXT("Route network baked: %d nodes, %d links, %d unlinked, %d grid cells."),
		NodeLocations.Num(), GetLinkCount(), Orphans, Cells.Num());
}

int32 AUEGT2RouteNetwork::GetLinkCount() const
{
	int32 Total = 0;
	for (const FUEGT2RouteLinks& Links : NodeLinks)
	{
		Total += Links.To.Num();
	}
	return Total / 2;                       // stored both ways
}

FVector AUEGT2RouteNetwork::GetNodeLocation(int32 Index) const
{
	return NodeLocations.IsValidIndex(Index) ? NodeLocations[Index] : FVector::ZeroVector;
}

FIntPoint AUEGT2RouteNetwork::CellOf(const FVector& Location) const
{
	return FIntPoint(FMath::FloorToInt(Location.X / CellSize),
		FMath::FloorToInt(Location.Y / CellSize));
}

void AUEGT2RouteNetwork::BuildSpatialIndex() const
{
	if (bIndexBuilt)
	{
		return;
	}
	Cells.Reset();
	Cells.Reserve(NodeLocations.Num());
	for (int32 Index = 0; Index < NodeLocations.Num(); ++Index)
	{
		if (!NodeLinks.IsValidIndex(Index) || NodeLinks[Index].To.Num() == 0)
		{
			continue;
		}
		Cells.FindOrAdd(CellOf(NodeLocations[Index])).Add(Index);
	}
	bIndexBuilt = true;
}

int32 AUEGT2RouteNetwork::FindNearestNode(const FVector& Location, float MaxDistance) const
{
	if (NodeLocations.Num() == 0)
	{
		return INDEX_NONE;
	}
	BuildSpatialIndex();

	const FIntPoint Centre = CellOf(Location);
	const int32 MaxRing = FMath::Max(1, FMath::CeilToInt(MaxDistance / CellSize));
	const float MaxSquared = MaxDistance * MaxDistance;

	int32 Best = INDEX_NONE;
	float BestSquared = MaxSquared;

	for (int32 Ring = 0; Ring <= MaxRing; ++Ring)
	{
		for (int32 DX = -Ring; DX <= Ring; ++DX)
		{
			for (int32 DY = -Ring; DY <= Ring; ++DY)
			{
				// Only the shell of the ring; the interior was done already.
				if (Ring > 0 && FMath::Abs(DX) != Ring && FMath::Abs(DY) != Ring)
				{
					continue;
				}
				const TArray<int32>* Bucket = Cells.Find(FIntPoint(Centre.X + DX, Centre.Y + DY));
				if (!Bucket)
				{
					continue;
				}
				for (int32 Index : *Bucket)
				{
					const float Squared = FVector::DistSquared2D(NodeLocations[Index], Location);
					if (Squared < BestSquared)
					{
						BestSquared = Squared;
						Best = Index;
					}
				}
			}
		}
		// A hit inside this ring can still be beaten by the next ring out only
		// if that ring can reach closer, which it cannot once the best distance
		// is within the ring's inner radius.
		if (Best != INDEX_NONE && BestSquared <= FMath::Square(Ring * CellSize))
		{
			break;
		}
	}
	return Best;
}

bool AUEGT2RouteNetwork::FindPath(const FVector& Start, const FVector& Goal,
	TArray<FVector>& OutPoints) const
{
	OutPoints.Reset();
	if (NodeLocations.Num() == 0)
	{
		return false;
	}

	// Generous snap radius: an NPC standing in a field or on a plaza is a long
	// way from the nearest kerb and still wants to use the road once it is on it.
	const int32 StartNode = FindNearestNode(Start, 9000.0f);
	const int32 GoalNode = FindNearestNode(Goal, 9000.0f);
	if (StartNode == INDEX_NONE || GoalNode == INDEX_NONE)
	{
		return false;
	}
	if (StartNode == GoalNode)
	{
		return true;                        // both ends share a node: walk straight
	}

	++SearchCount;

	// Costs are doubles because FVector is: UE5 world coordinates are double
	// precision, and taking a float here is a narrowing conversion the compiler
	// rejects outright inside a braced initialiser.
	struct FOpen
	{
		int32 Node = INDEX_NONE;
		double Estimate = 0.0;
		bool operator<(const FOpen& Other) const { return Estimate < Other.Estimate; }
	};

	TMap<int32, double> BestCost;
	TMap<int32, int32> CameFrom;
	TArray<FOpen> Open;

	BestCost.Add(StartNode, 0.0);
	Open.HeapPush(FOpen{ StartNode, FVector::Dist2D(NodeLocations[StartNode], NodeLocations[GoalNode]) });

	int32 Visited = 0;
	bool bFound = false;

	while (Open.Num() > 0 && Visited < MaxVisitedNodes)
	{
		FOpen Current;
		Open.HeapPop(Current);
		++Visited;

		if (Current.Node == GoalNode)
		{
			bFound = true;
			break;
		}

		// By value, and this is the whole bug this function used to have.
		//
		// It was a pointer into BestCost, and BestCost is added to further down
		// this very loop - which rehashes the map and frees the block the
		// pointer was aimed at. Every neighbour after the first reallocation
		// read freed memory as "the cost so far". Garbage there produces a
		// garbage Candidate, which breaks the one invariant the parent links
		// rely on - that a node's parent always costs less than the node - and
		// once that breaks, CameFrom can contain a cycle. The walk back at the
		// bottom of this function follows parents until it reaches the start,
		// so a cycle there is an infinite loop appending to an array: about
		// four gigabytes in eight seconds, and then the allocator asserts.
		//
		// From the outside that is "flying around in god mode freezes after a
		// few minutes", because flying is what makes hundreds of inhabitants
		// change tier at once and repath, which is what rolls this dice often
		// enough to hit it.
		const double* CostFound = BestCost.Find(Current.Node);
		if (!CostFound)
		{
			continue;
		}
		const double CostHere = *CostFound;
		for (int32 Next : NodeLinks[Current.Node].To)
		{
			if (!NodeLocations.IsValidIndex(Next))
			{
				continue;
			}
			const double Step = FVector::Dist(NodeLocations[Current.Node], NodeLocations[Next]);
			const double Candidate = CostHere + Step;
			const double* Known = BestCost.Find(Next);
			if (Known && *Known <= Candidate)
			{
				continue;
			}
			BestCost.Add(Next, Candidate);
			CameFrom.Add(Next, Current.Node);
			Open.HeapPush(FOpen{ Next,
				Candidate + FVector::Dist2D(NodeLocations[Next], NodeLocations[GoalNode]) });
		}
	}

	if (!bFound)
	{
		return false;
	}

	// Walk the parents back, then reverse. The start node is included: an NPC
	// standing off the road needs to be told to get onto it first.
	//
	// Bounded by the node count, because a path cannot visit more nodes than
	// exist. That is belt and braces on top of the fix above: this loop is the
	// place where a bad parent link stops being a wrong route and becomes a
	// frozen game, and the difference in cost between "give up and walk
	// straight there" and "allocate until the process dies" is not close.
	TArray<int32> Reversed;
	Reversed.Reserve(FMath::Min(NodeLocations.Num(), 256));
	for (int32 Node = GoalNode; Node != StartNode; )
	{
		if (Reversed.Num() > NodeLocations.Num())
		{
			++CycleCount;
			UE_LOG(LogUEGT2NPC, Error,
				TEXT("Route network: parent links cycle between nodes %d and %d. "
					 "Walking straight there instead."), StartNode, GoalNode);
			return false;
		}
		Reversed.Add(Node);
		const int32* Parent = CameFrom.Find(Node);
		if (!Parent)
		{
			return false;
		}
		Node = *Parent;
	}
	Reversed.Add(StartNode);

	OutPoints.Reserve(Reversed.Num());
	for (int32 Index = Reversed.Num() - 1; Index >= 0; --Index)
	{
		OutPoints.Add(NodeLocations[Reversed[Index]]);
	}
	return true;
}

FVector AUEGT2RouteNetwork::GetWanderTarget(const FVector& Location, float Radius, uint32 Seed) const
{
	if (Radius <= 0.0f)
	{
		return Location;
	}
	// The first node must obey the same bound as every subsequent hop. A
	// generous snap radius can otherwise pull an animal out of its field.
	const int32 Nearest = FindNearestNode(Location, Radius);
	if (Nearest == INDEX_NONE)
	{
		return Location;
	}

	// A walk along the network rather than a step off it. Three hops kept a
	// wanderer within about twenty-five metres of where it started, which meant
	// Newhaven's avenues stayed empty however many people were told to wander -
	// they were all circling their own doorway. The radius still bounds it, so
	// an animal stays in its field and a villager stays in their quarter.
	int32 Node = Nearest;
	for (int32 Hop = 0; Hop < 6; ++Hop)
	{
		const TArray<int32>& Links = NodeLinks[Node].To;
		if (Links.Num() == 0)
		{
			break;
		}
		const int32 Pick = (int32)(UEGT2HashSeed(Seed, (uint32)Hop, (uint32)Node) % (uint32)Links.Num());
		const int32 Candidate = Links[Pick];
		if (FVector::Dist2D(NodeLocations[Candidate], Location) > Radius)
		{
			break;
		}
		Node = Candidate;
	}
	return NodeLocations[Node];
}
