#include "World/ArachneCampPoint.h"
#include "Creature/ArachneMath.h"
#include "Components/PoseableMeshComponent.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/World.h"
#include "DrawDebugHelpers.h"

static const TCHAR* CampPreviewMeshPath = TEXT("/Game/ARACHNE/Characters/SK_Arachne.SK_Arachne");

AArachneCampPoint::AArachneCampPoint()
{
    WaitTimeMin = WaitTimeMax = 0.f;
    AcceptRadius = 260.f;   // the anchor glide covers the last stretch from any surface
    PreviewPawnClass = AArachnePawn::StaticClass();

    Preview = CreateDefaultSubobject<UPoseableMeshComponent>(TEXT("PosePreview"));
    Preview->SetupAttachment(Root);
    Preview->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    Preview->SetCanEverAffectNavigation(false);
    Preview->SetHiddenInGame(true);
    Preview->SetCastShadow(false);
    Preview->SetUsingAbsoluteLocation(true);
    Preview->SetUsingAbsoluteRotation(true);
    Preview->SetUsingAbsoluteScale(true);
}

const AArachnePawn* AArachneCampPoint::GetPawnDefaults() const
{
    const UClass* Class = PreviewPawnClass ? PreviewPawnClass.Get() : AArachnePawn::StaticClass();
    return Class->GetDefaultObject<AArachnePawn>();
}

void AArachneCampPoint::BeginPlay()
{
    ComputeAnchor(CachedAnchor);
    bAnchorCached = true;
    Super::BeginPlay();
    if (bShowPreviewInGame) UpdatePreview();
}

FArachneAnchor AArachneCampPoint::GetAnchor() const
{
    if (bAnchorCached) return CachedAnchor;
    FArachneAnchor Anchor;
    ComputeAnchor(Anchor);
    return Anchor;
}

FVector AArachneCampPoint::GetArrivalLocation() const
{
    return GetAnchor().Body.GetLocation();
}

bool AArachneCampPoint::ComputeAnchor(FArachneAnchor& OutAnchor, TArray<FHitResult>* OutSurfaces) const
{
    OutAnchor = FArachneAnchor();
    OutAnchor.BlendTime = EnterBlendTime;
    OutAnchor.Body = FTransform(GetActorQuat(), GetActorLocation());
    const UWorld* World = GetWorld();
    if (!World) return false;

    const AArachnePawn* Defaults = GetPawnDefaults();
    const double Clearance = SurfaceClearance > 0.f ? SurfaceClearance : (Defaults ? Defaults->BodyHeight : 75.f);
    const FVector P0 = GetActorLocation();

    // 1. Surfaces around: casts along the world axes plus the actor's own down axis (for slanted spots).
    const FVector Dirs[7] = {FVector::UpVector, -FVector::UpVector, FVector::ForwardVector, -FVector::ForwardVector,
                             FVector::RightVector, -FVector::RightVector, -GetActorUpVector()};
    FCollisionObjectQueryParams Objects;
    Objects.AddObjectTypesToQuery(ECC_WorldStatic);
    Objects.AddObjectTypesToQuery(ECC_WorldDynamic);
    FCollisionQueryParams Params(SCENE_QUERY_STAT(ArachneCampSurfaces), false, this);
    TArray<FHitResult> Surfaces;
    for (const FVector& D : Dirs)
    {
        FHitResult Hit;
        if (!World->LineTraceSingleByObjectType(Hit, P0, P0 + D * SurfaceSearchDistance, Objects, Params) || Hit.bStartPenetrating) continue;
        // One entry per plane: keep the nearer of two hits with (almost) the same normal.
        bool bMerged = false;
        for (FHitResult& S : Surfaces)
        {
            if (FVector::DotProduct(S.ImpactNormal, Hit.ImpactNormal) > .95)
            {
                if (Hit.Distance < S.Distance) S = Hit;
                bMerged = true;
                break;
            }
        }
        if (!bMerged) Surfaces.Add(Hit);
    }
    // Opposite surfaces (a narrow corridor, a tunnel) cannot both be held: keep the nearer one.
    Surfaces.Sort([](const FHitResult& A, const FHitResult& B) { return A.Distance < B.Distance; });
    for (int32 I = Surfaces.Num() - 1; I > 0; --I)
        for (int32 J = 0; J < I; ++J)
            if (FVector::DotProduct(Surfaces[I].ImpactNormal, Surfaces[J].ImpactNormal) < -.5) { Surfaces.RemoveAt(I); break; }
    if (OutSurfaces) *OutSurfaces = Surfaces;
    if (Surfaces.Num() == 0) return false;

    // 2. Body up: points away from everything it clings to.
    FVector Up = GetActorUpVector();
    if (!bUseActorUpAsBodyUp)
    {
        FVector Sum = FVector::ZeroVector;
        for (const FHitResult& S : Surfaces) Sum += S.ImpactNormal;
        Up = Sum.GetSafeNormal();
        if (Up.IsNearlyZero()) Up = Surfaces[0].ImpactNormal;
    }

    // 3. Body centre: Clearance away from every surface (Kaczmarz projections onto the offset planes, starting at the actor).
    FVector P = P0;
    for (int32 Iter = 0; Iter < 24; ++Iter)
        for (const FHitResult& S : Surfaces)
            P += S.ImpactNormal * (Clearance - FVector::DotProduct(P - S.ImpactPoint, S.ImpactNormal));

    // 4. Facing: the actor arrow projected onto the body plane.
    FVector Forward = FVector::VectorPlaneProject(GetActorForwardVector(), Up);
    if (Forward.SizeSquared() < 1e-4) Forward = FVector::VectorPlaneProject(GetActorUpVector(), Up);
    if (Forward.SizeSquared() < 1e-4) { FVector Y; Up.FindBestAxisVectors(Forward, Y); }
    OutAnchor.Body = FTransform(FRotationMatrix::MakeFromZX(Up, Forward.GetSafeNormal()).ToQuat(), P);
    return true;
}

void AArachneCampPoint::OnConstruction(const FTransform& Transform)
{
    Super::OnConstruction(Transform);
    UpdatePreview();
}

void AArachneCampPoint::RefreshPreview()
{
    UpdatePreview();
}

void AArachneCampPoint::SnapToPose()
{
    FArachneAnchor Anchor;
    if (!ComputeAnchor(Anchor)) return;
    Modify();
    SetActorLocation(Anchor.Body.GetLocation());
    UpdatePreview();
}

void AArachneCampPoint::UpdatePreview()
{
    if (!Preview || !Preview->IsRegistered()) return;
    Preview->SetVisibility(bShowPreview);
    Preview->SetHiddenInGame(!bShowPreviewInGame);
    if (!bShowPreview) return;

    const AArachnePawn* Defaults = GetPawnDefaults();
    USkeletalMesh* Mesh = Defaults ? Defaults->SpiderAsset.Get() : nullptr;
    if (!Mesh) Mesh = LoadObject<USkeletalMesh>(nullptr, CampPreviewMeshPath, nullptr, LOAD_NoWarn);
    if (!Mesh) return;
    if (Preview->GetSkinnedAsset() != Mesh) Preview->SetSkinnedAssetAndUpdate(Mesh);
    if (!PreviewRig.IsValid() || PreviewRigMesh.Get() != Mesh)
    {
        PreviewRig.Initialize(Mesh);
        PreviewRigMesh = Mesh;
    }
    if (!PreviewRig.IsValid()) return;

    // Same math as the live pawn: mesh base = body centre - up * BodyHeight, feet from FindAnchorFoot, planar FABRIK.
    FArachneAnchor Anchor;
    ComputeAnchor(Anchor);
    const double BodyHeight = Defaults ? Defaults->BodyHeight : 75.f;
    const FVector Up = Anchor.Body.GetRotation().GetUpVector();
    const FTransform MeshBase = FTransform(FVector(0, 0, -BodyHeight)) * Anchor.Body;
    Preview->SetWorldTransform(MeshBase);

    TArray<FTransform> Pose = PreviewRig.ReferenceCS;
    for (const FArachneLeg& Leg : PreviewRig.Legs)
    {
        FHitResult Hit;
        const FVector Home = MeshBase.TransformPosition(Leg.Rest.Last());
        const FVector Foot = FArachneRig::FindAnchorFoot(GetWorld(), Leg, MeshBase, Up, this, Hit)
            ? Hit.ImpactPoint
            : Home + Up * 8.0;   // no contact: tucked, like the live pawn
        PreviewRig.SolveLeg(Leg, MeshBase, Foot, Pose);
    }
    FArachneExtrasInput Extras;
    Extras.Stillness = 1.f;
    PreviewRig.AnimateExtras(Extras, Pose);
    FArachneRig::ApplyPose(Preview, Pose);
}
