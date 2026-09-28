#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Pawn.h"
#include "ArachnePawn.generated.h"

class USphereComponent;
class UPoseableMeshComponent;
class USpringArmComponent;
class UCameraComponent;
class USkeletalMesh;
class UInputAction;
class UInputMappingContext;
struct FInputActionValue;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_FourParams(FArachneFootPlantedSignature, int32, LegIndex, FVector, Location, FVector, Normal, float, Strength);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FArachneLandedSignature, float, ImpactSpeed);
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FArachneJumpedSignature);

/** Damped harmonic spring (semi-implicit Euler). Frequency in Hz, Damping = damping ratio (1 = critical). */
struct FArachneSpring
{
    FVector X = FVector::ZeroVector;
    FVector V = FVector::ZeroVector;
    void Step(const FVector& Target, double Frequency, double Damping, double Dt);
    void Reset(const FVector& Value = FVector::ZeroVector) { X = Value; V = FVector::ZeroVector; }
};

/** One procedural leg: 7 rigid segments (coxa..tarsus) + claw tip bone. */
struct FArachneLeg
{
    TArray<int32> Bones;        // coxa, trochanter, femur, patella, tibia, metatarsus, tarsus, foot
    TArray<FVector> Rest;       // component-space joint positions (8, last = claw tip)
    TArray<double> Lengths;     // 7 segment lengths
    double Reach = 0.0;         // femur joint -> claw tip, fully stretched
    int32 Pair = 0;             // 1..4 from the front
    int32 Side = 0;             // 0 = left, 1 = right

    FVector Foot = FVector::ZeroVector;          // current world position of the claw (what IK solves to)
    FVector Plant = FVector::ZeroVector;         // world contact while planted
    FVector PlantNormal = FVector::UpVector;
    FVector LocalPlant = FVector::ZeroVector;    // contact in support component space (moving platforms)
    TWeakObjectPtr<UPrimitiveComponent> Support;

    FVector Start = FVector::ZeroVector, StartNormal = FVector::UpVector;
    FVector Target = FVector::ZeroVector, TargetNormal = FVector::UpVector;
    FVector LocalTarget = FVector::ZeroVector;
    TWeakObjectPtr<UPrimitiveComponent> TargetSupport;

    FVector AirVelocity = FVector::ZeroVector;
    float Swing = 1.f;
    float SwingDuration = .2f;
    float SwingHeight = 20.f;
    float SwingLength = 0.f;
    float PlantedTime = 0.f;
    bool bPlanted = false;
    bool bSwinging = false;
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
 * Surface crawler for the Arachne tarantula.
 *  - Adhesive locomotion on any collision surface (floor, walls, ceilings, slopes, moving platforms)
 *  - Arc transitions over convex edges and into concave corners, driven by speed so they never pop
 *  - Ballistic physics when detached: gravity, jump, landing impact, external impulses
 *  - Procedural wave/tetrapod gait with 8 grounded IK chains (coxa yaw + planar FABRIK)
 *  - Spring driven body sway, abdomen jiggle and pedipalp motion
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
    virtual void SetupPlayerInputComponent(UInputComponent* Input) override;
    virtual void NotifyControllerChanged() override;
    virtual FVector GetVelocity() const override { return TravelVelocity; }

    // ---------------------------------------------------------------- components
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Arachne") TObjectPtr<USphereComponent> Collision;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Arachne") TObjectPtr<UPoseableMeshComponent> SpiderMesh;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Arachne") TObjectPtr<USpringArmComponent> CameraBoom;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Arachne") TObjectPtr<UCameraComponent> Camera;
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Arachne") TObjectPtr<USkeletalMesh> SpiderAsset;

    // ---------------------------------------------------------------- movement
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Arachne|Movement", meta=(ClampMin="1")) float MoveSpeed = 230.f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Arachne|Movement") float SprintMultiplier = 1.75f;
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

    // ---------------------------------------------------------------- physics
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Arachne|Physics") float GravityScale = 1.4f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Arachne|Physics") float JumpSpeed = 640.f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Arachne|Physics") float JumpForwardBoost = 180.f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Arachne|Physics") float AirControl = 1.2f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Arachne|Physics") float AirGrabReach = 220.f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Arachne|Physics") float CoyoteTime = .12f;

    // ---------------------------------------------------------------- legs / IK
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Arachne|Leg IK") float StepDistance = 38.f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Arachne|Leg IK") float StepHeight = 22.f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Arachne|Leg IK") float StepDuration = .22f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Arachne|Leg IK") float StepLead = .85f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Arachne|Leg IK") float FootProbeReach = 110.f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Arachne|Leg IK") int32 MaxSwingingLegs = 4;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Arachne|Leg IK") float IdleSettleDistance = 7.f;

    // ---------------------------------------------------------------- secondary motion
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Arachne|Secondary") float BodySpringFrequency = 3.2f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Arachne|Secondary") float BodySpringDamping = .55f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Arachne|Secondary") float LeanAmount = 1.f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Arachne|Secondary") float AbdomenJiggle = 1.f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Arachne|Secondary") float PedipalpMotion = 1.f;

    // ---------------------------------------------------------------- camera
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Arachne|Camera") float CameraUpFollowSpeed = 4.f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Arachne|Camera") float MouseSensitivity = 1.f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Arachne|Debug") bool bShowDebug = false;

    // ---------------------------------------------------------------- state (read only)
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Arachne|State") FVector SurfaceUp = FVector::UpVector;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Arachne|State") bool bAttached = false;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Arachne|State") int32 SupportedFeet = 0;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Arachne|State") float MaxFootError = 0.f;

    UPROPERTY(BlueprintAssignable, Category="Arachne|Events") FArachneFootPlantedSignature OnFootPlanted;
    UPROPERTY(BlueprintAssignable, Category="Arachne|Events") FArachneLandedSignature OnLanded;
    UPROPERTY(BlueprintAssignable, Category="Arachne|Events") FArachneJumpedSignature OnJumped;

    UFUNCTION(BlueprintCallable, Category="Arachne") void SetMovementInput(float Forward, float Right);
    UFUNCTION(BlueprintCallable, Category="Arachne") void ResetCrawler(FVector Location, FRotator Rotation);
    UFUNCTION(BlueprintCallable, Category="Arachne") void Jump();
    /** Physics kick. Strong kicks away from the surface detach the spider. */
    UFUNCTION(BlueprintCallable, Category="Arachne") void AddImpulse(FVector Impulse);
    UFUNCTION(BlueprintPure, Category="Arachne") bool IsTransitioning() const { return Transition.bActive; }
    UFUNCTION(BlueprintPure, Category="Arachne") FVector GetSurfaceVelocity() const { return Velocity; }

    bool IsRigValid() const { return Legs.Num() == 8; }
    bool IsWrappingEdge() const { return Transition.bActive; }

private:
    // input
    void EnsureInputAssets();
    void OnMove(const FInputActionValue& Value);
    void OnMoveStop(const FInputActionValue& Value);
    void OnLook(const FInputActionValue& Value);
    void OnZoom(const FInputActionValue& Value);
    void OnSprintOn(const FInputActionValue& Value);
    void OnSprintOff(const FInputActionValue& Value);
    void OnJump(const FInputActionValue& Value);
    void OnDebug(const FInputActionValue& Value);
    void OnReset(const FInputActionValue& Value);

    UPROPERTY(Transient) TObjectPtr<UInputMappingContext> InputContext;
    UPROPERTY(Transient) TObjectPtr<UInputAction> MoveAction;
    UPROPERTY(Transient) TObjectPtr<UInputAction> LookAction;
    UPROPERTY(Transient) TObjectPtr<UInputAction> ZoomAction;
    UPROPERTY(Transient) TObjectPtr<UInputAction> SprintAction;
    UPROPERTY(Transient) TObjectPtr<UInputAction> JumpAction;
    UPROPERTY(Transient) TObjectPtr<UInputAction> DebugAction;
    UPROPERTY(Transient) TObjectPtr<UInputAction> ResetAction;

    // locomotion
    void SimulateStep(float Dt);
    void StepAttached(float Dt);
    void StepTransition(float Dt);
    void StepAir(float Dt);
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
    FVector WishDirection() const;
    bool MoveBody(const FVector& Delta, FHitResult& Block);

    // animation
    void InitializeRig();
    FTransform BaseMeshTransform() const;
    bool FindFoot(const FArachneLeg& Leg, const FVector& Home, FHitResult& Hit) const;
    void UpdateLegs(float Dt);
    void UpdateBodyVisual(float Dt);
    void SolveLeg(const FArachneLeg& Leg, const FVector& FootWorld, TArray<FTransform>& Pose);
    void AnimateExtras(float Dt, TArray<FTransform>& Pose);
    void BuildPose(float Dt);
    void PlantLeg(int32 Index, bool bBroadcast);
    void UpdateCamera(float Dt);

    FVector2D MoveInput = FVector2D::ZeroVector;
    FVector Velocity = FVector::ZeroVector;
    FVector TravelVelocity = FVector::ZeroVector;
    FVector PrevTravelVelocity = FVector::ZeroVector;
    FVector Facing = FVector::ForwardVector;
    FVector ViewHeading = FVector::ForwardVector;
    FVector SupportNormal = FVector::UpVector;
    FVector SupportPoint = FVector::ZeroVector;
    FVector CameraUp = FVector::UpVector;
    float CameraPitch = -24.f;
    float DesiredArmLength = 520.f;
    float UnsupportedTime = 0.f;
    float AttachCooldown = 0.f;
    float LandingBoost = 0.f;
    float IdleTime = 0.f;
    float AnimTime = 0.f;
    float YawRate = 0.f;
    float GaitPhase = 0.f;
    bool bSprint = false;
    FArachneTransition Transition;
    TWeakObjectPtr<UPrimitiveComponent> BodySupport;
    FTransform LastSupportTransform = FTransform::Identity;

    // visuals
    FArachneSpring BodyOffsetSpring;   // x = forward, y = right, z = up (cm)
    FArachneSpring BodyTiltSpring;     // x = roll, y = pitch (deg)
    FArachneSpring AbdomenSpring;      // x = roll, y = pitch, z = yaw (deg)
    FVector PrevFacing = FVector::ForwardVector;

    // rig
    TArray<FArachneLeg> Legs;
    TArray<FTransform> ReferenceCS;
    TArray<TArray<int32>> Children;
    int32 AbdomenBone = INDEX_NONE;
    TArray<int32> AbdomenSubtree;
    int32 PalpBones[2][4];
    FVector PalpPivots[2][4];
    int32 ChelBones[2] = {INDEX_NONE, INDEX_NONE};
    int32 FangBones[2] = {INDEX_NONE, INDEX_NONE};
    FVector SpawnLocation = FVector::ZeroVector;
    FRotator SpawnRotation = FRotator::ZeroRotator;
};
