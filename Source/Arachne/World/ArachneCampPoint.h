#pragma once
#include "CoreMinimal.h"
#include "World/ArachneWaypoint.h"
#include "Creature/ArachneRig.h"
#include "Creature/ArachnePawn.h"
#include "ArachneCampPoint.generated.h"

class UPoseableMeshComponent;
class USkeletalMesh;

/**
 * A spot where Arachne likes to hide and freeze: a corner between two walls and the ceiling, a ceiling above a door...
 * Place it roughly where the body should be and aim the arrow where she should face. The surfaces around it
 * (found by casts along the world axes) define the pose: body up = average of their normals, body centre = the point
 * SurfaceClearance away from each of them. The preview mesh shows exactly that pose with the runtime leg IK.
 */
UCLASS(Blueprintable)
class ARACHNE_API AArachneCampPoint : public AArachneWaypoint
{
    GENERATED_BODY()
public:
    AArachneCampPoint();
    virtual void OnConstruction(const FTransform& Transform) override;
    virtual void BeginPlay() override;
    virtual FVector GetArrivalLocation() const override;
    virtual bool IsCampPoint() const override { return true; }

    /** Seconds she stays frozen here before giving up. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Arachne|Camp", meta=(ClampMin="0")) float CampTimeMin = 10.f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Arachne|Camp", meta=(ClampMin="0")) float CampTimeMax = 30.f;
    /** Prey sensed inside this radius while she is camping = instant attack. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Arachne|Camp", meta=(ClampMin="0")) float AmbushRadius = 400.f;
    /** Seconds to glide from the approach into the pose. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Arachne|Camp", meta=(ClampMin="0.1")) float EnterBlendTime = 1.2f;

    /** How far from the actor to look for walls / floor / ceiling. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Arachne|Pose", meta=(ClampMin="20")) float SurfaceSearchDistance = 160.f;
    /** Body centre distance from each surface. <= 0 uses the pawn's BodyHeight. Smaller = tucked deeper into the corner. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Arachne|Pose") float SurfaceClearance = -1.f;
    /** Use the actor's up axis as the body up instead of the average surface normal (e.g. flat on the ceiling in a corner). */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Arachne|Pose") bool bUseActorUpAsBodyUp = false;

    /** Pawn class whose body size and mesh the preview uses. Set it to BP_Arachne. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Arachne|Preview") TSubclassOf<AArachnePawn> PreviewPawnClass;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Arachne|Preview") bool bShowPreview = true;
    /** Show the preview pose in PIE too (debugging). */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Arachne|Preview") bool bShowPreviewInGame = false;

    /** Computes the held pose from the surrounding surfaces. False when no surface was found (pose = actor transform). */
    bool ComputeAnchor(FArachneAnchor& OutAnchor, TArray<FHitResult>* OutSurfaces = nullptr) const;
    /** The held pose; computed once at BeginPlay in game (the level geometry does not move). */
    FArachneAnchor GetAnchor() const;

    /** Editor helper: moves the actor onto the computed body position. */
    UFUNCTION(CallInEditor, Category="Arachne|Pose") void SnapToPose();
    /** Editor helper: rebuilds the preview after moving geometry around. */
    UFUNCTION(CallInEditor, Category="Arachne|Preview") void RefreshPreview();

protected:
    UPROPERTY(VisibleAnywhere, Category="Arachne") TObjectPtr<UPoseableMeshComponent> Preview;

private:
    const AArachnePawn* GetPawnDefaults() const;
    void UpdatePreview();

    FArachneAnchor CachedAnchor;
    bool bAnchorCached = false;
    FArachneRig PreviewRig;
    TWeakObjectPtr<const USkeletalMesh> PreviewRigMesh;
};
