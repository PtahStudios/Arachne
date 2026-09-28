#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Pawn.h"
#include "Engine/HitResult.h"
#include "Creature/ArachneRig.h"
#include "ArachnePawn.generated.h"

class USphereComponent;
class UPoseableMeshComponent;
class USkeletalMesh;
class UArachneSensesComponent;
class UArachneMemoryComponent;
class UArachneNavigatorComponent;
class UArachneBrainComponent;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_FourParams(FArachneFootPlantedSignature, int32, LegIndex, FVector, Location, FVector, Normal, float, Strength);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FArachneLandedSignature, float, ImpactSpeed);

/** A body pose the spider blends into and holds: a corner between walls and ceiling, a ceiling spot, the player's face. */
USTRUCT(BlueprintType)
struct FArachneAnchor
{
    GENERATED_BODY()

    /** Body centre + orientation. Z axis = body up (points away from the surfaces it clings to), X = facing. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Anchor") FTransform Body = FTransform::Identity;
    /** Seconds to glide from the current pose into the anchor. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Anchor", meta=(ClampMin="0.05")) float BlendTime = 1.f;
    /** Feet grab a virtual plane instead of geometry (the camera lens when the player is caught). */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Anchor") bool bVirtualSurface = false;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Anchor") FVector VirtualPoint = FVector::ZeroVector;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Anchor") FVector VirtualNormal = FVector::UpVector;
};

/** Rigid rotation of the body around an edge / inner corner line: exact circular arc, both position and orientation. */
struct FArachneTransition
{
    bool bActive = false;
    bool bConvex = false;
    FVector Pivot = FVector::ZeroVector;
    FVector Offset0 = FVector::ZeroVector;   // body centre - pivot at alpha 0
    FVector Up0 = FVector::UpVector, Up1 = FVector::UpVector;
    FVector Forward0 = FVector::ForwardVector;
    FVector Axis = FVector::RightVector;
    double Angle = 0.0, Radius = 1.0, Alpha = 0.0, Speed = 0.0;
    TWeakObjectPtr<UPrimitiveComponent> Support;
};

/**
 * ARACHNE - the tarantula body. AI driven (no input, no camera): the components below think, this class moves.
 *  - Adhesive locomotion on any collision surface (floor, walls, ceilings, slopes, moving platforms)
 *  - Arc transitions over convex edges and into concave corners, driven by speed so they never pop
 *  - Anchors: glides into a held pose (corners, ceilings) with every foot on the nearest surface
 *  - Procedural wave gait with 8 grounded IK chains (coxa yaw + planar FABRIK), spring driven secondary motion
 * Steering comes from UArachneNavigatorComponent through SetMoveDirection / SetFacingDirection / SetSprint.
 * It never jumps: when knocked off a surface it just falls and grabs whatever it hits.
 */
UCLASS(Blueprintable)
class ARACHNE_API AArachnePawn : public APawn
{
    GENERATED_BODY()
public:
    AArachnePawn();
    virtual void OnConstruction(const FTransform& Transform) override;
    virtual void BeginPlay() override;
    virtual void Tick(float DeltaSeconds) override;
    virtual FVector GetVelocity() const override { return TravelVelocity; }

    // ---------------------------------------------------------------- components
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Arachne") TObjectPtr<USphereComponent> Collision;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Arachne") TObjectPtr<UPoseableMeshComponent> SpiderMesh;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Arachne|AI") TObjectPtr<UArachneSensesComponent> Senses;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Arachne|AI") TObjectPtr<UArachneMemoryComponent> Memory;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Arachne|AI") TObjectPtr<UArachneNavigatorComponent> Navigator;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Arachne|AI") TObjectPtr<UArachneBrainComponent> Brain;
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Arachne") TObjectPtr<USkeletalMesh> SpiderAsset;

    // ---------------------------------------------------------------- movement
    /** Walking speed (cm/s). */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Arachne|Movement", meta=(ClampMin="1")) float MoveSpeed = 230.f;
    /** Speed multiplier while sprinting. The brain only sprints when it commits to an attack. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Arachne|Movement") float SprintMultiplier = 2.4f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Arachne|Movement") float Acceleration = 7.f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Arachne|Movement") float Deceleration = 9.f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Arachne|Movement", meta=(ClampMin="40")) float BodyHeight = 75.f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Arachne|Movement") float AdhesionReach = 65.f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Arachne|Movement") float HoverStiffness = 10.f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Arachne|Movement") float SurfaceTurnSpeed = 9.f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Arachne|Movement") float FacingTurnSpeed = 7.f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Arachne|Movement") float MaxStepUp = 50.f;
    /** Distance before a steep surface at which the inner-corner arc starts. Larger = rounder, earlier climb. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Arachne|Movement") float CornerLead = 45.f;
    /** Surfaces steeper than this (relative to the current one) are entered with an arc instead of normal smoothing. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Arachne|Movement") float TransitionMinAngle = 38.f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Arachne|Movement") float EdgeWrapReach = 90.f;

    // ---------------------------------------------------------------- falling (only when knocked off a surface)
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Arachne|Falling") float GravityScale = 1.4f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Arachne|Falling") float AirGrabReach = 220.f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Arachne|Falling") float CoyoteTime = .12f;

    // ---------------------------------------------------------------- legs / IK
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Arachne|Leg IK") float StepDistance = 38.f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Arachne|Leg IK") float StepHeight = 22.f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Arachne|Leg IK") float StepDuration = .22f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Arachne|Leg IK") float StepLead = .85f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Arachne|Leg IK") float FootProbeReach = 110.f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Arachne|Leg IK") int32 MaxSwingingLegs = 4;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Arachne|Leg IK") float IdleSettleDistance = 7.f;
    /** While anchored, a foot further than this from its anchor contact steps again. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Arachne|Leg IK") float AnchorFootTolerance = 6.f;

    // ---------------------------------------------------------------- secondary motion
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Arachne|Secondary") float BodySpringFrequency = 3.2f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Arachne|Secondary") float BodySpringDamping = .55f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Arachne|Secondary") float LeanAmount = 1.f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Arachne|Secondary") float AbdomenJiggle = 1.f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Arachne|Secondary") float PedipalpMotion = 1.f;

    // ---------------------------------------------------------------- senses geometry
    /** Eye position relative to the body centre: X = along facing, Z = along surface up. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Arachne|Senses") FVector EyeOffset = FVector(55.f, 0.f, 18.f);

    // ---------------------------------------------------------------- debug
    /** Draws leg chains, foot targets, surface normal and transition pivots. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Arachne|Debug") bool bDebugBody = false;

    // ---------------------------------------------------------------- state (read only)
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Arachne|State") FVector SurfaceUp = FVector::UpVector;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Arachne|State") bool bAttached = false;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Arachne|State") int32 SupportedFeet = 0;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Arachne|State") float MaxFootError = 0.f;

    UPROPERTY(BlueprintAssignable, Category="Arachne|Events") FArachneFootPlantedSignature OnFootPlanted;
    UPROPERTY(BlueprintAssignable, Category="Arachne|Events") FArachneLandedSignature OnLanded;

    // ---------------------------------------------------------------- steering
    /** World-space travel direction, length 0..1 = fraction of speed. Projected onto the current surface. */
    UFUNCTION(BlueprintCallable, Category="Arachne|Steering") void SetMoveDirection(FVector WorldDirection);
    /** Heading-relative input (X forward, Y right). The heading rolls with the surface, so "forward" climbs walls. */
    UFUNCTION(BlueprintCallable, Category="Arachne|Steering") void SetMovementInput(float Forward, float Right);
    /** Where the body faces. Zero = face the direction of travel. Anything else = crab walk / look around. */
    UFUNCTION(BlueprintCallable, Category="Arachne|Steering") void SetFacingDirection(FVector WorldDirection);
    UFUNCTION(BlueprintCallable, Category="Arachne|Steering") void SetSprint(bool bInSprint) { bSprint = bInSprint; }
    /** 0 = lively idle, 1 = frozen (breathing, pedipalps and fangs almost stop). */
    UFUNCTION(BlueprintCallable, Category="Arachne|Steering") void SetStillness(float InStillness) { Stillness = FMath::Clamp(InStillness, 0.f, 1.f); }
    UFUNCTION(BlueprintPure, Category="Arachne|Steering") bool IsSprinting() const { return bSprint; }

    // ---------------------------------------------------------------- anchors
    UFUNCTION(BlueprintCallable, Category="Arachne|Anchor") void BeginAnchor(const FArachneAnchor& InAnchor);
    UFUNCTION(BlueprintCallable, Category="Arachne|Anchor") void EndAnchor();
    UFUNCTION(BlueprintPure, Category="Arachne|Anchor") bool IsAnchored() const { return bAnchored; }
    /** True once the body reached the anchor and every foot that has a contact is planted on it. */
    UFUNCTION(BlueprintPure, Category="Arachne|Anchor") bool IsAnchorSettled() const;

    // ---------------------------------------------------------------- misc
    UFUNCTION(BlueprintCallable, Category="Arachne") void ResetCrawler(FVector Location, FRotator Rotation);
    /** Physics kick. Strong kicks away from the surface detach the spider. */
    UFUNCTION(BlueprintCallable, Category="Arachne") void AddImpulse(FVector Impulse);
    UFUNCTION(BlueprintPure, Category="Arachne") bool IsTransitioning() const { return Transition.bActive; }
    UFUNCTION(BlueprintPure, Category="Arachne") FVector GetSurfaceVelocity() const { return Velocity; }
    UFUNCTION(BlueprintPure, Category="Arachne") FVector GetFacing() const { return Facing; }
    UFUNCTION(BlueprintPure, Category="Arachne") FVector GetEyeLocation() const;

    const FArachneRig& GetRig() const { return Rig; }
    bool IsRigValid() const { return Rig.IsValid(); }
    bool IsWrappingEdge() const { return Transition.bActive; }

private:
    // locomotion
    void SimulateStep(float Dt);
    void StepAttached(float Dt);
    void StepTransition(float Dt);
    void StepAir(float Dt);
    void StepAnchored(float Dt);
    void FollowSupport();
    void SetBodySupport(UPrimitiveComponent* Component);
    bool Probe(const FVector& Start, const FVector& End, FHitResult& Hit, float Radius = 0.f) const;
    bool IsStepUp(const FHitResult& Wall) const;
    bool FindConvexEdge(const FVector& Direction, FHitResult& Face, FVector& Edge) const;
    void BeginTransition(const FVector& Pivot, const FVector& NewUp, const FVector& Forward, bool bConvex, UPrimitiveComponent* NewSupport);
    void Land(const FHitResult& Hit);
    void Detach();
    void RotateFrame(const FQuat& Delta, bool bRotateVelocity);
    void ApplyFrame();
    void TurnFacing(const FVector& Target, float Dt);
    FVector WishDirection() const;
    bool MoveBody(const FVector& Delta, FHitResult& Block);

    // legs + visuals
    void LoadSpiderAsset();
    FTransform BaseMeshTransform() const;
    bool FindFoot(const FArachneLeg& Leg, const FVector& Home, FHitResult& Hit) const;
    void ComputeAnchorFeet();
    void UpdateLegs(float Dt);
    void UpdateGroundedLegs(float Dt);
    void UpdateAnchoredLegs(float Dt);
    void UpdateAirLegs(float Dt);
    bool BeginSwing(int32 Index, const FHitResult& Hit, float SwingTime);
    void AdvanceSwing(int32 Index, float Dt);
    void UpdateBodyVisual(float Dt);
    void BuildPose(float Dt);
    void PlantLeg(int32 Index, bool bBroadcast);
    void DrawBodyDebug() const;

    FArachneRig Rig;

    // steering
    FVector2D MoveInput = FVector2D::ZeroVector;
    FVector WorldMove = FVector::ZeroVector;
    FVector FacingOverride = FVector::ZeroVector;
    bool bWorldMove = true;
    bool bSprint = false;
    float Stillness = 0.f;

    // body state
    FVector Velocity = FVector::ZeroVector;
    FVector TravelVelocity = FVector::ZeroVector;
    FVector PrevTravelVelocity = FVector::ZeroVector;
    FVector Facing = FVector::ForwardVector;
    FVector Heading = FVector::ForwardVector;   // rolls with the surface; reference for heading-relative input
    FVector SupportNormal = FVector::UpVector;
    FVector SupportPoint = FVector::ZeroVector;
    float UnsupportedTime = 0.f;
    float AttachCooldown = 0.f;
    float LandingBoost = 0.f;
    float IdleTime = 0.f;
    float AnimTime = 0.f;
    float YawRate = 0.f;
    float GaitPhase = 0.f;
    FArachneTransition Transition;
    TWeakObjectPtr<UPrimitiveComponent> BodySupport;
    FTransform LastSupportTransform = FTransform::Identity;

    // anchor
    FArachneAnchor Anchor;
    bool bAnchored = false;
    float AnchorAlpha = 0.f;
    FVector AnchorStartLocation = FVector::ZeroVector;
    FQuat AnchorStartRotation = FQuat::Identity;
    TArray<FHitResult> AnchorFeet;      // one per leg; bBlockingHit = false when that leg has nothing to grab
    float FootFitWeight = 1.f;          // how much the body tilts/lifts to fit the feet (0 while anchored)

    // visuals
    FArachneSpring BodyOffsetSpring;   // x = forward, y = right, z = up (cm)
    FArachneSpring BodyTiltSpring;     // x = roll, y = pitch (deg)
    FArachneSpring AbdomenSpring;      // x = roll, y = pitch, z = yaw (deg)
    FVector PrevFacing = FVector::ForwardVector;
    FVector SpawnLocation = FVector::ZeroVector;
    FRotator SpawnRotation = FRotator::ZeroRotator;
};
