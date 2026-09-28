#pragma once
#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "ArachneWaypointSubsystem.generated.h"

class AArachneWaypoint;
class AArachneCampPoint;
class AArachneDoorway;

/**
 * Arachne's mental map of the house: the waypoint graph. Links come from line-of-sight casts between waypoints
 * (plus manual links), paths from Dijkstra over that graph. No navmesh anywhere.
 */
UCLASS()
class ARACHNE_API UArachneWaypointSubsystem : public UWorldSubsystem
{
    GENERATED_BODY()
public:
    void Register(AArachneWaypoint* Waypoint);
    void Unregister(AArachneWaypoint* Waypoint);

    /** Clear line between two points for a body, geometry only (pawns never block). */
    static bool HasClearPath(const UWorld* World, const FVector& From, const FVector& To, float Radius = 15.f);
    /**
     * A line the spider can walk along the floor: clear for a body of Radius, not steeper than a staircase, and with
     * ground close underneath all the way (no links across stair wells, galleries or through floors).
     */
    static bool HasWalkableLine(const UWorld* World, const FVector& From, const FVector& To, float Radius = 35.f);
    /** The automatic link rule: both allow it, in range, clear path. */
    static bool CanAutoLink(const AArachneWaypoint* A, const AArachneWaypoint* B);

    /** Nearest waypoint with a clear path from Location. */
    AArachneWaypoint* FindNearestReachable(const FVector& Location, bool bAllowCampPoints = true) const;
    /** Waypoints on the way from From to Goal (first = entry point near From, last = Goal). False when unreachable. */
    bool FindPath(const FVector& From, const AArachneWaypoint* Goal, TArray<AArachneWaypoint*>& OutPath) const;
    void GetNeighbours(const AArachneWaypoint* Waypoint, TArray<AArachneWaypoint*>& Out) const;
    /** Patrol destinations only (no camp points, no door / stair helpers). */
    void GetPatrolPoints(TArray<AArachneWaypoint*>& Out) const;
    void GetCampPoints(TArray<AArachneCampPoint*>& Out) const;
    void GetDoorways(TArray<AArachneDoorway*>& Out) const;

    void DrawDebug() const;

private:
    void RebuildIfDirty() const;
    int32 IndexOf(const AArachneWaypoint* Waypoint) const;

    TArray<TWeakObjectPtr<AArachneWaypoint>> Waypoints;
    mutable TArray<TArray<int32>> Links;   // adjacency, parallel to Waypoints
    mutable bool bDirty = true;
};
