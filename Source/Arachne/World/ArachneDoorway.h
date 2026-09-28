#pragma once
#include "CoreMinimal.h"
#include "World/ArachneWaypoint.h"
#include "ArachneDoorway.generated.h"

class UBoxComponent;

/**
 * A door frame. Arachne never just walks "to" this point: the navigator turns it into a crossing manoeuvre -
 * approach on the floor to ApproachDistance in front, line up, walk straight through the middle (no climbing the
 * jambs, feet squeezed to the opening, smaller body collision), come out ApproachDistance on the other side.
 * Place it in the middle of the opening at body height (~75 cm above the floor) with the arrow pointing through it
 * (either way). Doors crossed by any route - graph, chase, trail - are detected automatically, so one per opening.
 */
UCLASS(Blueprintable)
class ARACHNE_API AArachneDoorway : public AArachneWaypoint
{
    GENERATED_BODY()
public:
    AArachneDoorway();
    virtual void OnConstruction(const FTransform& Transform) override;
    virtual void BeginPlay() override;

    /** Distance before / after the door plane where the crossing starts and ends (cm). */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Arachne|Doorway", meta=(ClampMin="60")) float ApproachDistance = 110.f;
    /** Measure the opening with casts to the jambs (editor and BeginPlay). Off = use OpeningHalfWidth as typed. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Arachne|Doorway") bool bAutoMeasure = true;
    /** Half the clear width between the jambs (cm), measured from the actor position. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Arachne|Doorway", meta=(ClampMin="20")) float OpeningHalfWidth = 50.f;

    FVector GetPassageCenter() const { return GetActorLocation(); }
    /** Horizontal axis through the opening (the arrow). Crossing works both ways. */
    FVector GetPassageDirection() const;
    float GetHalfWidth() const { return OpeningHalfWidth; }

    /** Editor helper: centres the actor between the jambs and re-measures. */
    UFUNCTION(CallInEditor, Category="Arachne|Doorway") void SnapToOpening();

private:
    /** Casts left / right to the jambs. Returns false when no jamb was found. */
    bool MeasureOpening(float& OutLeft, float& OutRight) const;
    void Remeasure();

#if WITH_EDITORONLY_DATA
    UPROPERTY(VisibleAnywhere, Category="Arachne") TObjectPtr<UBoxComponent> OpeningPreview;
#endif
};
