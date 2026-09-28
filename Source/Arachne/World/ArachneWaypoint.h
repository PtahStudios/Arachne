#pragma once
#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "ArachneWaypoint.generated.h"

class UBillboardComponent;
class UArrowComponent;

/**
 * Patrol waypoint. Arachne walks between these (no navmesh): waypoints that see each other within AutoLinkDistance
 * are linked automatically at runtime by line-of-sight casts; ManualLinks add links the casts cannot find.
 * Place them at body height (~75 cm) above floors, in doorways and at corridor ends.
 */
UCLASS(Blueprintable)
class ARACHNE_API AArachneWaypoint : public AActor
{
    GENERATED_BODY()
public:
    AArachneWaypoint();
    virtual void BeginPlay() override;
    virtual void EndPlay(const EEndPlayReason::Type Reason) override;

    /** Seconds Arachne lingers here before moving on. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Arachne|Waypoint", meta=(ClampMin="0")) float WaitTimeMin = .5f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Arachne|Waypoint", meta=(ClampMin="0")) float WaitTimeMax = 3.f;
    /** How close the body has to get to count as arrived (cm). */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Arachne|Waypoint", meta=(ClampMin="20")) float AcceptRadius = 110.f;
    /** Link automatically to every waypoint in clear line of sight within AutoLinkDistance. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Arachne|Links") bool bAutoLink = true;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Arachne|Links", meta=(ClampMin="100")) float AutoLinkDistance = 1800.f;
    /** Extra two-way links (e.g. around a corner the line-of-sight cast cannot see through). */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Arachne|Links") TArray<TObjectPtr<AArachneWaypoint>> ManualLinks;

    /** Where the navigator steers to. Camp points override this with their body pose. */
    virtual FVector GetArrivalLocation() const { return GetActorLocation(); }
    virtual bool IsCampPoint() const { return false; }

    /** Editor helper: draws the links this waypoint gets at runtime for 10 seconds. */
    UFUNCTION(CallInEditor, Category="Arachne|Links") void PreviewLinks();

protected:
    UPROPERTY(VisibleAnywhere, Category="Arachne") TObjectPtr<USceneComponent> Root;
#if WITH_EDITORONLY_DATA
    UPROPERTY(VisibleAnywhere, Category="Arachne") TObjectPtr<UBillboardComponent> Sprite;
    UPROPERTY(VisibleAnywhere, Category="Arachne") TObjectPtr<UArrowComponent> Arrow;
#endif
};
