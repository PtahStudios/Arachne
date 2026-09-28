#pragma once
#include "CoreMinimal.h"

class USkeletalMesh;
class UPoseableMeshComponent;
class UPrimitiveComponent;
class UWorld;
class AActor;
struct FHitResult;

/** Damped harmonic spring (semi-implicit Euler). Frequency in Hz, Damping = damping ratio (1 = critical). */
struct FArachneSpring
{
    FVector X = FVector::ZeroVector;
    FVector V = FVector::ZeroVector;
    void Step(const FVector& Target, double Frequency, double Damping, double Dt);
    void Reset(const FVector& Value = FVector::ZeroVector) { X = Value; V = FVector::ZeroVector; }
};

/** One procedural leg: 7 rigid segments (coxa..tarsus) + claw tip bone. Static rig data + runtime gait state. */
struct FArachneLeg
{
    // ---- rig (filled by FArachneRig::Initialize)
    TArray<int32> Bones;        // coxa, trochanter, femur, patella, tibia, metatarsus, tarsus, foot
    TArray<FVector> Rest;       // component-space joint positions (8, last = claw tip)
    TArray<double> Lengths;     // 7 segment lengths
    double Reach = 0.0;         // femur joint -> claw tip, fully stretched
    int32 Pair = 0;             // 1..4 from the front
    int32 Side = 0;             // 0 = left, 1 = right

    // ---- gait state (owned by AArachnePawn)
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
    bool bVirtualPlant = false;   // planted on a virtual surface (no component), e.g. the camera lens
};

/** Everything the secondary animation needs from the body simulation. */
struct FArachneExtrasInput
{
    FVector AbdomenEuler = FVector::ZeroVector;   // deg: roll, pitch, yaw
    double SpeedRatio = 0.0;                       // 0..1.5
    double GaitPhase = 0.0;
    double AnimTime = 0.0;
    float PedipalpMotion = 1.f;
    float Stillness = 0.f;                         // 0 = lively, 1 = frozen (camping)
};

/**
 * The tarantula skeleton: bone lookup, planar FABRIK leg IK, secondary animation and pose upload.
 * Plain C++ so the same solver drives the live pawn and the editor preview of camp poses.
 */
struct ARACHNE_API FArachneRig
{
    TArray<FArachneLeg> Legs;
    TArray<FTransform> ReferenceCS;
    int32 AbdomenBone = INDEX_NONE;
    TArray<int32> AbdomenSubtree;
    int32 PalpBones[2][4];
    FVector PalpPivots[2][4];
    int32 ChelBones[2] = {INDEX_NONE, INDEX_NONE};
    int32 FangBones[2] = {INDEX_NONE, INDEX_NONE};

    FArachneRig();

    /** Finds the leg chains and helper bones. Returns true when all 8 legs were found. */
    bool Initialize(const USkeletalMesh* Mesh);
    bool IsValid() const { return Legs.Num() == 8; }

    /**
     * Solves one leg towards FootWorld and writes its component-space bone transforms into Pose.
     * MeshTransform = the poseable mesh component's world transform. Returns the remaining foot error in cm.
     */
    float SolveLeg(const FArachneLeg& Leg, const FTransform& MeshTransform, const FVector& FootWorld, TArray<FTransform>& Pose, const UWorld* DebugWorld = nullptr) const;

    /** Abdomen sway, pedipalps, chelicerae and fangs. */
    void AnimateExtras(const FArachneExtrasInput& In, TArray<FTransform>& Pose) const;

    /** Uploads a component-space pose to a poseable mesh. */
    static void ApplyPose(UPoseableMeshComponent* Mesh, const TArray<FTransform>& ComponentPose);

    /**
     * Foot placement for a held pose (corners, ceilings). Casts from the hip towards and around the rest foot position,
     * so in a corner each leg grabs whichever surface it meets first: floor, walls or ceiling.
     * MeshBase = mesh component world transform for the held body pose, Up = body up.
     */
    static bool FindAnchorFoot(const UWorld* World, const FArachneLeg& Leg, const FTransform& MeshBase, const FVector& Up, const AActor* Ignore, FHitResult& OutHit);
};
