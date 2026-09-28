#include "Creature/ArachneRig.h"
#include "Creature/ArachneMath.h"
#include "Components/PoseableMeshComponent.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/World.h"
#include "DrawDebugHelpers.h"

DEFINE_LOG_CATEGORY_STATIC(LogArachneRig, Log, All);

using namespace ArachneMath;

void FArachneSpring::Step(const FVector& Target, double Frequency, double Damping, double Dt)
{
    const double W = 2.0 * PI * Frequency;
    V += ((Target - X) * (W * W) - V * (2.0 * Damping * W)) * Dt;
    X += V * Dt;
}

FArachneRig::FArachneRig()
{
    for (int32 S = 0; S < 2; ++S)
    {
        for (int32 K = 0; K < 4; ++K) { PalpBones[S][K] = INDEX_NONE; PalpPivots[S][K] = FVector::ZeroVector; }
    }
}

// =====================================================================================================================
// Setup
// =====================================================================================================================

bool FArachneRig::Initialize(const USkeletalMesh* Mesh)
{
    Legs.Empty();
    ReferenceCS.Empty();
    AbdomenSubtree.Empty();
    if (!Mesh) { UE_LOG(LogArachneRig, Error, TEXT("ARACHNE: skeletal mesh missing")); return false; }

    const FReferenceSkeleton& Ref = Mesh->GetRefSkeleton();
    ReferenceCS = Ref.GetRefBonePose();
    TArray<TArray<int32>> Children;
    Children.SetNum(ReferenceCS.Num());
    for (int32 I = 0; I < ReferenceCS.Num(); ++I)
    {
        const int32 Parent = Ref.GetParentIndex(I);
        if (Parent != INDEX_NONE) { ReferenceCS[I] = ReferenceCS[I] * ReferenceCS[Parent]; Children[Parent].Add(I); }
    }

    static const TCHAR* Parts[] = {TEXT("coxa"), TEXT("trochanter"), TEXT("femur"), TEXT("patella"), TEXT("tibia"), TEXT("metatarsus"), TEXT("tarsus")};
    TArray<FArachneLeg> Built;
    for (int32 Pair = 1; Pair <= 4; ++Pair)
    {
        for (int32 Side = 0; Side < 2; ++Side)
        {
            FArachneLeg L;
            L.Pair = Pair;
            L.Side = Side;
            const TCHAR* S = Side == 0 ? TEXT("l") : TEXT("r");
            for (const TCHAR* Part : Parts) L.Bones.Add(Ref.FindBoneIndex(FName(*FString::Printf(TEXT("leg_%02d_%s_%s"), Pair, Part, S))));
            L.Bones.Add(Ref.FindBoneIndex(FName(*FString::Printf(TEXT("foot_%02d_%s"), Pair, S))));
            if (L.Bones.Contains(INDEX_NONE)) { UE_LOG(LogArachneRig, Error, TEXT("ARACHNE: missing leg bones %d %s"), Pair, S); continue; }
            for (const int32 Bone : L.Bones) L.Rest.Add(ReferenceCS[Bone].GetLocation());
            for (int32 J = 0; J < 7; ++J) L.Lengths.Add(FVector::Distance(L.Rest[J], L.Rest[J + 1]));
            for (int32 J = 2; J < 7; ++J) L.Reach += L.Lengths[J];
            Built.Add(L);
        }
    }
    if (Built.Num() == 8) Legs = Built;

    // Abdomen + everything hanging off it (spinnerets, shields)
    AbdomenBone = Ref.FindBoneIndex(TEXT("abdomen"));
    if (AbdomenBone != INDEX_NONE)
    {
        TArray<int32> Stack = {AbdomenBone};
        while (Stack.Num()) { const int32 B = Stack.Pop(); AbdomenSubtree.Add(B); Stack.Append(Children[B]); }
    }

    // Pedipalps. The Blender rig placed the right palp joints on the left side, so mirror the left pivots when needed.
    for (int32 K = 0; K < 4; ++K)
    {
        PalpBones[0][K] = Ref.FindBoneIndex(FName(*FString::Printf(TEXT("pedipalp_%02d_l"), K + 1)));
        PalpBones[1][K] = Ref.FindBoneIndex(FName(*FString::Printf(TEXT("pedipalp_%02d_r"), K + 1)));
        for (int32 S = 0; S < 2; ++S)
            PalpPivots[S][K] = PalpBones[S][K] != INDEX_NONE ? ReferenceCS[PalpBones[S][K]].GetLocation() : FVector::ZeroVector;
        if (PalpBones[0][K] != INDEX_NONE && PalpBones[1][K] != INDEX_NONE && PalpPivots[0][K].Y * PalpPivots[1][K].Y > 1.0)
            PalpPivots[1][K] = FVector(PalpPivots[0][K].X, -PalpPivots[0][K].Y, PalpPivots[0][K].Z);
    }
    for (int32 S = 0; S < 2; ++S)
    {
        ChelBones[S] = Ref.FindBoneIndex(S == 0 ? TEXT("chelicera_l") : TEXT("chelicera_r"));
        FangBones[S] = Ref.FindBoneIndex(S == 0 ? TEXT("fang_l") : TEXT("fang_r"));
    }
    return IsValid();
}

// =====================================================================================================================
// IK
// =====================================================================================================================

float FArachneRig::SolveLeg(const FArachneLeg& L, const FTransform& MeshT, const FVector& FootWorld, TArray<FTransform>& Pose, const UWorld* DebugWorld) const
{
    const FVector Target = MeshT.InverseTransformPosition(FootWorld);
    const FVector Z = FVector::UpVector;
    TArray<FVector, TInlineAllocator<8>> P;
    P.Append(L.Rest);

    // 1. Yaw: the coxa takes a third, the trochanter the rest, so the leg's bend plane faces the foot.
    const double TotalYaw = FMath::Clamp(SignedAngleAround(L.Rest[7] - L.Rest[1], Target - L.Rest[1], Z), -1.2, 1.2);
    const FQuat CoxaYaw(Z, TotalYaw * .35);
    for (int32 J = 1; J < 8; ++J) P[J] = L.Rest[0] + CoxaYaw.RotateVector(L.Rest[J] - L.Rest[0]);
    const double TrochAngle = FMath::Clamp(SignedAngleAround(P[7] - P[1], Target - P[1], Z), -1.2, 1.2);
    const FQuat TrochYaw(Z, TrochAngle);
    for (int32 J = 2; J < 8; ++J) P[J] = P[1] + TrochYaw.RotateVector(P[J] - P[1]);
    const FQuat LegYaw = TrochYaw * CoxaYaw;

    // 2. Planar FABRIK femur -> claw. The bend plane contains the knee (patella), so knees always stay high.
    const FVector Root = P[2];
    FVector Goal = Target;
    if (FVector::Dist(Root, Goal) > L.Reach * .999) Goal = Root + (Goal - Root).GetSafeNormal() * (L.Reach * .999);
    const FVector PlaneN = FVector::CrossProduct(Goal - Root, P[4] - Root).GetSafeNormal();
    const bool bPlane = !PlaneN.IsNearlyZero();
    for (int32 Iter = 0; Iter < 16; ++Iter)
    {
        P[7] = Goal;
        for (int32 J = 6; J >= 2; --J) P[J] = P[J + 1] + (P[J] - P[J + 1]).GetSafeNormal() * L.Lengths[J];
        if (bPlane) for (int32 J = 3; J < 7; ++J) P[J] = FVector::PointPlaneProject(P[J], Root, PlaneN);
        P[2] = Root;
        for (int32 J = 2; J < 7; ++J) P[J + 1] = P[J] + (P[J + 1] - P[J]).GetSafeNormal() * L.Lengths[J];
        if (FVector::DistSquared(P[7], Goal) < .04) break;
    }
    const float Error = static_cast<float>(FVector::Dist(P[7], Target));

    // 3. Bone rotations = rest rotation, then yaw, then the swing that lines the segment up with the solved chain.
    for (int32 J = 0; J < 7; ++J)
    {
        const int32 Bone = L.Bones[J];
        const FQuat Yaw = J == 0 ? CoxaYaw : LegYaw;
        const FVector From = Yaw.RotateVector(L.Rest[J + 1] - L.Rest[J]).GetSafeNormal();
        const FVector To = (P[J + 1] - P[J]).GetSafeNormal();
        const FQuat Rot = (FQuat::FindBetweenNormals(From, To) * Yaw * ReferenceCS[Bone].GetRotation()).GetNormalized();
        Pose[Bone] = FTransform(Rot, P[J], ReferenceCS[Bone].GetScale3D());
    }
    const int32 Tarsus = L.Bones[6], Claw = L.Bones[7];
    const FQuat TarsusDelta = Pose[Tarsus].GetRotation() * ReferenceCS[Tarsus].GetRotation().Inverse();
    Pose[Claw] = FTransform((TarsusDelta * ReferenceCS[Claw].GetRotation()).GetNormalized(), P[7], ReferenceCS[Claw].GetScale3D());

    if (DebugWorld)
    {
        for (int32 J = 0; J < 7; ++J)
            DrawDebugLine(DebugWorld, MeshT.TransformPosition(P[J]), MeshT.TransformPosition(P[J + 1]), FColor::Yellow, false, 0.f, 0, 1.5f);
    }
    return Error;
}

// =====================================================================================================================
// Secondary animation
// =====================================================================================================================

void FArachneRig::AnimateExtras(const FArachneExtrasInput& In, TArray<FTransform>& Pose) const
{
    const double Alive = 1.0 - FMath::Clamp(In.Stillness, 0.f, 1.f) * .9;

    // Abdomen: spring driven sway about the pedicel.
    if (AbdomenBone != INDEX_NONE)
    {
        const FVector A = In.AbdomenEuler;
        const FQuat Q = FRotator(A.Y, A.Z, A.X).Quaternion();
        const FTransform Delta = RotateAbout(ReferenceCS[AbdomenBone].GetLocation(), Q);
        for (const int32 B : AbdomenSubtree) Pose[B] = Pose[B] * Delta;
    }

    // Pedipalps: they feel the ground ahead in counter-phase while walking and fidget while idle.
    const double SpeedRatio = FMath::Clamp(In.SpeedRatio, 0.0, 1.5);
    static const double Share[4] = {.45, .8, .6, .35};
    for (int32 S = 0; S < 2; ++S)
    {
        const double Side = S == 0 ? 1.0 : -1.0;
        const double Walk = FMath::Sin(In.GaitPhase + S * PI) * 13.0 * SpeedRatio;
        const double Idle = (FMath::Sin(In.AnimTime * 1.3 + S * 2.1) * 4.0 + FMath::Sin(In.AnimTime * 3.7 + S) * 1.8) * (1.0 - FMath::Min(SpeedRatio, 1.0)) * Alive;
        const double Pitch = FMath::DegreesToRadians((Walk + Idle) * In.PedipalpMotion);
        const double Yaw = FMath::DegreesToRadians(FMath::Sin(In.AnimTime * .9 + S * 1.3) * 5.0 * In.PedipalpMotion * Alive) * Side;
        FTransform Acc = FTransform::Identity;
        for (int32 K = 0; K < 4; ++K)
        {
            const int32 Bone = PalpBones[S][K];
            if (Bone == INDEX_NONE) break;
            const FVector Pivot = Acc.TransformPosition(PalpPivots[S][K]);
            const FQuat Q = FQuat(Acc.GetRotation().RotateVector(FVector::YAxisVector), Pitch * Share[K])
                          * FQuat(Acc.GetRotation().RotateVector(FVector::ZAxisVector), Yaw * Share[K]);
            Acc = Acc * RotateAbout(Pivot, Q);
            Pose[Bone] = ReferenceCS[Bone] * Acc;
        }
    }

    // Chelicerae + fangs: slow idle flex.
    for (int32 S = 0; S < 2; ++S)
    {
        if (ChelBones[S] == INDEX_NONE) continue;
        const FQuat QC(FVector::YAxisVector, FMath::DegreesToRadians(FMath::Sin(In.AnimTime * .7 + S * .4) * 2.5 * Alive));
        const FTransform DC = RotateAbout(ReferenceCS[ChelBones[S]].GetLocation(), QC);
        Pose[ChelBones[S]] = ReferenceCS[ChelBones[S]] * DC;
        if (FangBones[S] != INDEX_NONE)
        {
            const FVector FangPivot = DC.TransformPosition(ReferenceCS[FangBones[S]].GetLocation());
            const FQuat QF(DC.GetRotation().RotateVector(FVector::YAxisVector), FMath::DegreesToRadians((FMath::Sin(In.AnimTime * 1.1 + S) * .5 + .5) * 6.0 * Alive));
            Pose[FangBones[S]] = ReferenceCS[FangBones[S]] * DC * RotateAbout(FangPivot, QF);
        }
    }
}

void FArachneRig::ApplyPose(UPoseableMeshComponent* Mesh, const TArray<FTransform>& Pose)
{
    if (!Mesh || !Mesh->GetSkinnedAsset()) return;
    const FReferenceSkeleton& Ref = Mesh->GetSkinnedAsset()->GetRefSkeleton();
    if (Ref.GetNum() != Pose.Num()) return;
    Mesh->BoneSpaceTransforms.SetNum(Pose.Num());
    for (int32 I = 0; I < Pose.Num(); ++I)
    {
        const int32 Parent = Ref.GetParentIndex(I);
        Mesh->BoneSpaceTransforms[I] = Parent == INDEX_NONE ? Pose[I] : Pose[I].GetRelativeTransform(Pose[Parent]);
    }
    Mesh->MarkRefreshTransformDirty();
    Mesh->RefreshBoneTransforms();
}

// =====================================================================================================================
// Held-pose foot placement
// =====================================================================================================================

bool FArachneRig::FindAnchorFoot(const UWorld* World, const FArachneLeg& Leg, const FTransform& MeshBase, const FVector& Up, const AActor* Ignore, FHitResult& OutHit)
{
    if (!World) return false;
    const FVector Home = MeshBase.TransformPosition(Leg.Rest.Last());
    const FVector Hip = MeshBase.TransformPosition(Leg.Rest[2]);
    const double Reach = Leg.Reach * .97;
    const FVector Out = (Home - Hip).GetSafeNormal();
    if (Out.IsNearlyZero()) return false;

    FCollisionQueryParams Params(SCENE_QUERY_STAT(ArachneAnchorFoot), false, Ignore);
    auto Trace = [&](const FVector& A, const FVector& B, FHitResult& H)
    {
        return World->LineTraceSingleByChannel(H, A, B, ECC_Visibility, Params) && !H.bStartPenetrating && H.GetComponent();
    };

    // Fan of rays from the hip: straight at the rest foot, then tilted towards the body's belly and back.
    // In a corner the first surface a ray meets is the one the leg would naturally grab.
    const FVector Dirs[5] = {
        Out,
        (Out - Up * .6).GetSafeNormal(),
        (Out - Up * 1.6).GetSafeNormal(),
        (Out + Up * .5).GetSafeNormal(),
        (Out + Up * 1.2).GetSafeNormal()};
    bool bFound = false;
    double Best = TNumericLimits<double>::Max();
    for (const FVector& D : Dirs)
    {
        FHitResult H;
        if (!Trace(Hip, Hip + D * Reach, H)) continue;
        const double Score = FVector::DistSquared(H.ImpactPoint, Home);
        if (Score < Best) { Best = Score; OutHit = H; bFound = true; }
    }
    // Plain surface under the rest foot (flat floors / ceilings).
    FHitResult Column;
    if (Trace(Home + Up * 40.0, Home - Up * 110.0, Column) && FVector::Dist(Hip, Column.ImpactPoint) <= Reach)
    {
        const double Score = FVector::DistSquared(Column.ImpactPoint, Home);
        if (Score < Best) { OutHit = Column; bFound = true; }
    }
    return bFound;
}
