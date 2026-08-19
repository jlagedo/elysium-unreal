#include "Debug/ElysiumGreenRoomRun.h"

#include "Debug/ElysiumGreenRoomShared.h"

#include "RenderingThread.h"   // the wield window flushes so its two render packets are one frame
#include "SkeletalRenderPublic.h"   // the wield check reads the drawn mesh's own skinning matrices
#include "Visual/ElysiumNpcVisual.h"

#include "Components/SkeletalMeshComponent.h"
#include "Engine/SkeletalMesh.h"

// --- the wielded weapon (CCC10.2) ----------------------------------------------------------------

EElysiumWieldResult FElysiumGreenRoomRun::LabSetWield(const FString& Classname, bool bFemale,
	FString& OutDetail)
{
	USkeletalMeshComponent* Body = LabBody();
	if (!IsLabReady() || Body == nullptr)
	{
		OutDetail = TEXT("nothing is standing on the stage");
		return EElysiumWieldResult::NoWearer;
	}

	bReviewWieldFemale = bFemale;
	const TCHAR* const Sex = bFemale ? TEXT("female") : TEXT("male");

	const FElysiumWieldModelRef* Ref = nullptr;
	const EElysiumWieldResult Lookup =
		UElysiumWieldTable::FindRow(FName(*Classname), bFemale, Ref);

	switch (Lookup)
	{
	case EElysiumWieldResult::Found:
	{
		if (ElysiumNpcVisual::InstallWieldModel(Body, *Ref, Classname) == nullptr)
		{
			// InstallWieldModel has already named the package it could not load.
			ReviewWield.Reset();
			OutDetail = FString::Printf(
				TEXT("%s resolves a row whose package the bake did not produce — see the log"),
				*Classname);
			return EElysiumWieldResult::MeshMissing;
		}
		ReviewWield = Classname;
		OutDetail = FString::Printf(TEXT("%s (%s): %s on %s, hand %s"),
			*Classname, Sex, *Ref->Mesh.GetAssetName(),
			*Ref->MountBone.ToString(), *Ref->HandBone.ToString());
		UE_LOG(LogElysiumGreenRoom, Log, TEXT("lab wield: %s"), *OutDetail);
		break;
	}
	case EElysiumWieldResult::NoGeometry:
		// The corpus's ordinary answer, not a missing asset: 296 of its 488 rows name `w_null.mdl`
		// or nothing at all. The hands are emptied because that is what the row says to show.
		LabClearWield();
		OutDetail = FString::Printf(
			TEXT("%s holds no wield model for a %s wielder — an authored answer, not a missing asset"),
			*Classname, Sex);
		break;
	case EElysiumWieldResult::WorldModel:
		LabClearWield();
		OutDetail = FString::Printf(
			TEXT("%s clears shows_view_model — its world model supplies the geometry, not a wield model"),
			*Classname);
		break;
	case EElysiumWieldResult::UnknownItem:
		OutDetail = FString::Printf(
			TEXT("'%s' is not an item definition the wield table carries"), *Classname);
		break;
	case EElysiumWieldResult::NoTable:
		OutDetail = TEXT("the wield table is not on the mount — run `uv run elysium export wield`");
		break;
	case EElysiumWieldResult::MeshMissing:
	case EElysiumWieldResult::NoWearer:
		// Not FindRow's vocabulary: both are this function's own outcomes and are returned directly
		// above, so reaching them here would mean the lookup answered something it cannot.
		OutDetail = FString::Printf(
			TEXT("the wield table answered an install-side result for '%s'"), *Classname);
		break;
	}
	return Lookup;
}

void FElysiumGreenRoomRun::LabClearWield()
{
	if (USkeletalMeshComponent* Body = LabBody())
	{
		ElysiumNpcVisual::ClearWieldModel(Body);
	}
	ReviewWield.Reset();
}

// What the DRAWN mesh is doing, read from the skinning matrices in the mesh object's own dynamic
// data — the last packet the render thread actually received, not anything recomputed on demand.
// Both cheaper readings lie about a follower: GetSocketTransform answers through the leader bone
// map, and GetCurrentRefToLocalMatrices rebuilds fresh matrices from current game-thread state —
// each once claimed a weapon rode the hand at 0.0000 while the mesh drew frozen at its reference
// pose off a proxy nothing had updated. The dynamic data is the one reading that carries that
// staleness.
//
// `OutMount` is the rendered world transform of the mount's bind frame. `OutBindToWorld` is the
// rigid map taking the mesh's bind-space positions to where the mount's skinning matrix draws
// them — exact for the rigid-under-the-mount majority the corpus measures. `OutCentre` is the
// bind-space bounds centre through that map: honest in the way that matters — it moves only when
// the drawn mesh moves, bind offsets included. False when the bone, the mesh object or its first
// packet is missing (never rendered is not riding anything); HaveValidDynamicData guards the
// install frame, whose packet GetReferenceToLocalMatrices dereferences unchecked.
static bool ElysiumRenderedWield(const USkeletalMeshComponent& Wield, FName Mount,
	FTransform& OutMount, FTransform& OutBindToWorld, FVector& OutCentre)
{
	const USkeletalMesh* const Mesh = Wield.GetSkeletalMeshAsset();
	const FSkeletalMeshObject* const MeshObject = Wield.GetMeshObject();
	const int32 Index = Mesh != nullptr && MeshObject != nullptr
			&& MeshObject->HaveValidDynamicData()
		? Mesh->GetRefSkeleton().FindBoneIndex(Mount) : INDEX_NONE;
	if (Index == INDEX_NONE)
	{
		return false;
	}
	const TConstArrayView<FMatrix44f> RefToLocals = MeshObject->GetReferenceToLocalMatrices();
	const TArray<FMatrix44f>& InvBind = Mesh->GetRefBasesInvMatrix();
	if (!RefToLocals.IsValidIndex(Index) || !InvBind.IsValidIndex(Index))
	{
		return false;
	}
	const FMatrix RefToLocal(RefToLocals[Index]);
	const FTransform ComponentToWorld = Wield.GetComponentTransform();
	// RefToLocal = InvBind * ComponentSpace, so the bind matrix on the left recovers the rendered
	// component-space bone; the component transform then lifts it to the world. The bounds centre
	// is already bind-space, so it goes through RefToLocal as-is.
	const FMatrix RenderedCS = FMatrix(InvBind[Index].Inverse()) * RefToLocal;
	OutMount = FTransform(RenderedCS * ComponentToWorld.ToMatrixWithScale());
	OutBindToWorld = FTransform(RefToLocal * ComponentToWorld.ToMatrixWithScale());
	OutCentre = OutBindToWorld.TransformPosition(FVector(Mesh->GetImportedBounds().Origin));
	return true;
}

// The wearer's HAND read from the body's OWN render packet — the same frame the follower's
// packet was filled from. The game-thread socket answer is one-or-more frames ahead of what is
// on screen, and comparing across that seam reads pipeline latency as an attachment defect:
// mid-swing the skew scales with hand speed, and across the review loop's wrap it is the full
// end-pose→start-pose snap at ANY playback speed — 16–31 cm on `baseballbat_attack_heavy`
// while the paused pose agrees to 0.00 cm. Same recovery as `ElysiumRenderedWield`, same
// guards; false means the packet cannot answer this frame.
//
// Only a SKINNED bone may be read this way. The render matrices exist for skinning, so a
// zero-weight bone — every prop mount — is left at identity in the packet and reads as the
// reference pose standing still in component space (`Bat` answered 80 cm from its game-thread
// bone while the drawn weapon rode it exactly). The hand is skinned on every body; the mount
// side of the mapping gate reads game-thread locals instead.
static bool ElysiumRenderedBodyBone(const USkeletalMeshComponent& Body, FName Bone,
	FTransform& OutWorld)
{
	const USkeletalMesh* const Mesh = Body.GetSkeletalMeshAsset();
	const FSkeletalMeshObject* const MeshObject = Body.GetMeshObject();
	const int32 Index = Mesh != nullptr && MeshObject != nullptr
			&& MeshObject->HaveValidDynamicData()
		? Mesh->GetRefSkeleton().FindBoneIndex(Bone) : INDEX_NONE;
	if (Index == INDEX_NONE)
	{
		return false;
	}
	const TConstArrayView<FMatrix44f> RefToLocals = MeshObject->GetReferenceToLocalMatrices();
	const TArray<FMatrix44f>& InvBind = Mesh->GetRefBasesInvMatrix();
	if (!RefToLocals.IsValidIndex(Index) || !InvBind.IsValidIndex(Index))
	{
		return false;
	}
	const FMatrix RenderedCS = FMatrix(InvBind[Index].Inverse()) * FMatrix(RefToLocals[Index]);
	OutWorld = FTransform(RenderedCS * Body.GetComponentTransform().ToMatrixWithScale());
	return true;
}

// The placement gate's "touch" floor: how far a hand bone may sit from the drawn geometry's box
// before the weapon is not being held. The yardstick is the corpus's own authored data, measured
// over every model's reference pose against its own hand bone — the widest shipped gap is
// 7.2 cm (`w_m_pistol_glock`, `w_m_pineapple`: the wrist bone sits behind the gripped geometry),
// plus margin for the wearer's finger-chain pose differing from the weapon's own. A tighter
// figure fails retail's own authored grips; the defect this gate exists for reads 15–115+ cm.
static constexpr float ElysiumWieldTouchCm = 10.0f;

// The mapping gate's rotation tolerance. With both sides of the comparison read from the same
// rendered frame, a matched mount's orientation error is compression noise (well under a degree);
// a real misride — the mount copied from the wrong bone, or a bind orientation defect — reads in
// tens of degrees. Gated separately from centimetres because a rotation about the mount origin
// moves the mount nowhere while it swings the geometry's far end by half a metre.
static constexpr float ElysiumWieldMountDeg = 5.0f;

// How far `WorldPoint` sits from the DRAWN mesh's own bind-space bounding box — zero when the
// point is inside it. This is the placement gate's "the hand actually touches the mesh", made
// literal: a centre-to-sphere-radius proxy fails a faithfully-held compact weapon (a gripped
// pistol's wrist bone sits at the box's edge, outside the bounding sphere of a centre up at the
// slide) while a box distance still reads the old defect's metre-scale orbit as metres.
static float ElysiumHandToDrawnBoxCm(const USkeletalMesh* Mesh,
	const FTransform& BindToWorld, const FVector& WorldPoint)
{
	if (Mesh == nullptr)
	{
		return 0.0f;
	}
	const FBoxSphereBounds Bounds = Mesh->GetImportedBounds();
	const FBox Box(Bounds.Origin - Bounds.BoxExtent, Bounds.Origin + Bounds.BoxExtent);
	// The drawn box is the bind-space box under a rigid map, so distance is measured in the
	// box's own frame rather than against a world-space AABB of a rotated box.
	return static_cast<float>(FMath::Sqrt(
		Box.ComputeSquaredDistanceToPoint(BindToWorld.InverseTransformPosition(WorldPoint))));
}

// The mount's authored local in the HAND's frame, composed from the weapon mesh's own reference
// skeleton — the constant the leader-pose bind fallback rides an undeclared mount on. True only
// when the hand is the mount's skeleton ancestor (every mount the corpus grips hangs directly
// under a hand bone); a mount anchored elsewhere has no constant hand-local to assert.
static bool ElysiumMountLocalInHand(const USkeletalMesh& Mesh, FName Mount, FName Hand,
	FTransform& OutLocal)
{
	const FReferenceSkeleton& Ref = Mesh.GetRefSkeleton();
	const int32 MountIndex = Ref.FindBoneIndex(Mount);
	const int32 HandIndex = Ref.FindBoneIndex(Hand);
	if (MountIndex == INDEX_NONE || HandIndex == INDEX_NONE)
	{
		return false;
	}
	FTransform Local = Ref.GetRefBonePose()[MountIndex];
	int32 Index = Ref.GetParentIndex(MountIndex);
	while (Index != INDEX_NONE && Index != HandIndex)
	{
		Local = Local * Ref.GetRefBonePose()[Index];
		Index = Ref.GetParentIndex(Index);
	}
	if (Index != HandIndex)
	{
		return false;
	}
	OutLocal = Local;
	return true;
}

// The other hand of `Hand` — "Bip01 R Hand" <-> "Bip01 L Hand" — or NAME_None for a bone that
// does not name a sided hand. The off-hand gate exists only for the two-handed carries, and a
// carry's off hand is by definition the hand the mount is not riding.
static FName ElysiumOffHandOf(FName Hand)
{
	FString Name = Hand.ToString();
	if (Name.Contains(TEXT("R Hand")))
	{
		return FName(*Name.Replace(TEXT("R Hand"), TEXT("L Hand")));
	}
	if (Name.Contains(TEXT("L Hand")))
	{
		return FName(*Name.Replace(TEXT("L Hand"), TEXT("R Hand")));
	}
	return NAME_None;
}

// The bind-space -> world map the AUTHORED recipe predicts for the drawn weapon this frame: the
// wearer's game-thread mount (its own bone when declared, else the hand carrying the weapon's
// authored mount local) with the mount's inverse bind on the left. This is the verified retail
// composition restated from live game-thread state, so the drawn packet can be scored against
// it — the render diverging from this map IS the defect class this file instruments.
static bool ElysiumAuthoredBindToWorld(const USkeletalMeshComponent& Body,
	const USkeletalMesh& WieldMesh, FName Mount, FName Hand, bool bWearerDeclares,
	const FTransform& MountLocalInHand, FTransform& OutBindToWorld)
{
	const int32 MountIndex = WieldMesh.GetRefSkeleton().FindBoneIndex(Mount);
	const TArray<FMatrix44f>& InvBind = WieldMesh.GetRefBasesInvMatrix();
	if (!InvBind.IsValidIndex(MountIndex))
	{
		return false;
	}
	const FTransform MountWorld = bWearerDeclares
		? Body.GetSocketTransform(Mount, RTS_World)
		: MountLocalInHand * Body.GetSocketTransform(Hand, RTS_World);
	OutBindToWorld = FTransform(FMatrix(InvBind[MountIndex])) * MountWorld;
	return true;
}

// The wearer mesh's own reference-pose transform of `Bone`, component space — the value an
// untracked bone resolves to at runtime, and therefore the yardstick the single-frame numbers
// below are read against. Identity when the mesh or the bone is missing; the caller has already
// established both exist.
static FTransform ElysiumRefPoseComponentSpace(const USkeletalMeshComponent& Body, FName Bone)
{
	const USkeletalMesh* const Mesh = Body.GetSkeletalMeshAsset();
	if (Mesh == nullptr)
	{
		return FTransform::Identity;
	}
	const FReferenceSkeleton& RefSkeleton = Mesh->GetRefSkeleton();
	FTransform Out = FTransform::Identity;
	for (int32 Index = RefSkeleton.FindBoneIndex(Bone); Index != INDEX_NONE;
		Index = RefSkeleton.GetParentIndex(Index))
	{
		Out *= RefSkeleton.GetRefBonePose()[Index];
	}
	return Out;
}

bool FElysiumGreenRoomRun::LabWieldCheck(FString& OutReport) const
{
	const USkeletalMeshComponent* const Body = LabBody();
	USkeletalMeshComponent* const Wield = ElysiumNpcVisual::FindWieldModel(Body);
	if (Body == nullptr || Wield == nullptr)
	{
		OutReport = TEXT("nothing is held");
		return false;
	}

	const FElysiumWieldModelRef* Ref = nullptr;
	if (UElysiumWieldTable::FindRow(FName(*ReviewWield), bReviewWieldFemale, Ref)
		!= EElysiumWieldResult::Found)
	{
		OutReport = FString::Printf(TEXT("'%s' no longer resolves"), *ReviewWield);
		return false;
	}

	const FName Mount = Ref->MountBone;
	// Whether the WEARER declares the mount decides which of the two compositions is rendering, and
	// it is read off the wearer rather than assumed from the binding: the manifest's classification
	// is metadata, and the body standing here is what actually answers.
	const bool bWearerDeclares = Body->GetBoneIndex(Mount) != INDEX_NONE;
	FTransform MountAt;
	FTransform BindToWorld;
	FVector CentreAt;
	if (!ElysiumRenderedWield(*Wield, Mount, MountAt, BindToWorld, CentreAt))
	{
		OutReport = FString::Printf(
			TEXT("%s: the weapon's render data does not answer for mount '%s' yet"),
			*ReviewWield, *Mount.ToString());
		return false;
	}
	// The hand from the BODY's render packet — the same frame the weapon's packet was filled
	// from — so these distances measure the attachment, not the pipeline's frame of latency.
	FTransform HandAt;
	if (!ElysiumRenderedBodyBone(*Body, Ref->HandBone, HandAt))
	{
		OutReport = FString::Printf(
			TEXT("%s: the body's render data does not answer for hand '%s' yet"),
			*ReviewWield, *Ref->HandBone.ToString());
		return false;
	}
	const float CentreCm = FVector::Dist(CentreAt, HandAt.GetLocation());
	const float TouchCm =
		ElysiumHandToDrawnBoxCm(Wield->GetSkeletalMeshAsset(), BindToWorld, HandAt.GetLocation());

	// No verdict lives on this line — a single frame cannot prove tracking — but the distances
	// are the DRAWN geometry's, so a weapon drawing away from the body reads as far away here
	// rather than hiding behind the leader array's answer. The verdict is gr_wield_check's window.
	OutReport = FString::Printf(
		TEXT("%s: mount '%s' is %s; the drawn geometry's centre sits %.1f cm from %s and %s "
		     "sits %.1f cm from the drawn mesh's bounds — the verdict is gr_wield_check's"),
		*ReviewWield, *Mount.ToString(),
		bWearerDeclares ? TEXT("the wearer's own bone")
		                : TEXT("NOT declared by this body (it rides the hand off its reference pose)"),
		CentreCm, *Ref->HandBone.ToString(), *Ref->HandBone.ToString(), TouchCm);

	// The same frame in numbers, all expressed in the hand's frame — the frame the mapping gate
	// scores in, because a zero-weight mount has no render matrix of its own: `drawn` is the
	// renderer's mount against the renderer's hand, `wearer` is the body's game-thread mount
	// against its game-thread hand — the local the wearer's clip authors — and `ref` is the body
	// mesh's reference pose, the value an untracked mount resolves to.
	const FTransform DrawnInHand = MountAt.GetRelativeTransform(HandAt);
	OutReport += FString::Printf(TEXT("\n  drawn mount in hand frame: t=(%.2f %.2f %.2f)"),
		DrawnInHand.GetLocation().X, DrawnInHand.GetLocation().Y, DrawnInHand.GetLocation().Z);
	// The undeclared mount's authored local under the hand, from the weapon's own skeleton: the
	// drawn local must equal it, rotation included — a wrong constant rotation holds translation
	// and drift at zero while it swings the far geometry off the other hand.
	FTransform MountLocal;
	const USkeletalMesh* const WieldMesh = Wield->GetSkeletalMeshAsset();
	if (!bWearerDeclares && WieldMesh != nullptr
		&& ElysiumMountLocalInHand(*WieldMesh, Mount, Ref->HandBone, MountLocal))
	{
		OutReport += FString::Printf(
			TEXT("; weapon's own authored local t=(%.2f %.2f %.2f), gap %.2f cm / %.2f deg"),
			MountLocal.GetLocation().X, MountLocal.GetLocation().Y, MountLocal.GetLocation().Z,
			FVector::Dist(DrawnInHand.GetLocation(), MountLocal.GetLocation()),
			FMath::RadiansToDegrees(
				DrawnInHand.GetRotation().AngularDistance(MountLocal.GetRotation())));
	}
	// The off hand against the drawn geometry, beside what the authored recipe predicts for the
	// same frame — on a two-handed carry both sit near zero together.
	const FName OffHand = ElysiumOffHandOf(Ref->HandBone);
	FTransform OffDrawn;
	FTransform AuthoredBindToWorld;
	if (OffHand != NAME_None && Body->GetBoneIndex(OffHand) != INDEX_NONE && WieldMesh != nullptr
		&& ElysiumRenderedBodyBone(*Body, OffHand, OffDrawn)
		&& ElysiumAuthoredBindToWorld(*Body, *WieldMesh, Mount, Ref->HandBone, bWearerDeclares,
			MountLocal, AuthoredBindToWorld))
	{
		OutReport += FString::Printf(
			TEXT("\n  off hand '%s': %.1f cm from the drawn mesh's bounds (authored pose "
			     "puts it %.1f cm)"),
			*OffHand.ToString(),
			ElysiumHandToDrawnBoxCm(WieldMesh, BindToWorld, OffDrawn.GetLocation()),
			ElysiumHandToDrawnBoxCm(WieldMesh, AuthoredBindToWorld,
				Body->GetSocketTransform(OffHand, RTS_World).GetLocation()));
	}
	if (bWearerDeclares)
	{
		const FTransform WearerInHand =
			Body->GetSocketTransform(Mount, RTS_World).GetRelativeTransform(
				Body->GetSocketTransform(Ref->HandBone, RTS_World));
		const FTransform RefInHand = ElysiumRefPoseComponentSpace(*Body, Mount)
			.GetRelativeTransform(ElysiumRefPoseComponentSpace(*Body, Ref->HandBone));
		OutReport += FString::Printf(
			TEXT("\n  wearer '%s' in hand frame: t=(%.2f %.2f %.2f); body ref local "
			     "t=(%.2f %.2f %.2f)\n  gaps: drawn-vs-wearer %.2f cm / %.2f deg, "
			     "wearer-vs-ref %.2f cm / %.2f deg"),
			*Mount.ToString(),
			WearerInHand.GetLocation().X, WearerInHand.GetLocation().Y,
			WearerInHand.GetLocation().Z,
			RefInHand.GetLocation().X, RefInHand.GetLocation().Y, RefInHand.GetLocation().Z,
			FVector::Dist(DrawnInHand.GetLocation(), WearerInHand.GetLocation()),
			FMath::RadiansToDegrees(
				DrawnInHand.GetRotation().AngularDistance(WearerInHand.GetRotation())),
			FVector::Dist(WearerInHand.GetLocation(), RefInHand.GetLocation()),
			FMath::RadiansToDegrees(
				WearerInHand.GetRotation().AngularDistance(RefInHand.GetRotation())));
	}
	return true;
}

bool FElysiumGreenRoomRun::LabWieldTrackStart(float Seconds, float ToleranceCm, FString& OutError)
{
	if (IsDriving())
	{
		OutError = TEXT("the tracking check samples the review stage — `elysium.gr_mode review` first");
		return false;
	}
	const USkeletalMeshComponent* const Body = LabBody();
	const USkeletalMeshComponent* const Wield = ElysiumNpcVisual::FindWieldModel(Body);
	if (Body == nullptr || Wield == nullptr)
	{
		OutError = TEXT("nothing is held — `elysium.gr_wield <item>` first");
		return false;
	}
	const FElysiumWieldModelRef* Ref = nullptr;
	if (UElysiumWieldTable::FindRow(FName(*ReviewWield), bReviewWieldFemale, Ref)
		!= EElysiumWieldResult::Found)
	{
		OutError = FString::Printf(TEXT("'%s' no longer resolves"), *ReviewWield);
		return false;
	}
	// The hand is the frame every sample is expressed in, so a body without it has nothing to
	// measure against — GetSocketTransform would quietly answer the component transform and the
	// window would measure the stage.
	if (Body->GetBoneIndex(Ref->HandBone) == INDEX_NONE)
	{
		OutError = FString::Printf(TEXT("the standing body does not declare the hand bone '%s'"),
			*Ref->HandBone.ToString());
		return false;
	}

	WieldTrack = FWieldTrackProbe();
	WieldTrack.bRunning = true;
	// One loop of the standing clip is the natural window: every frame the base can show has shown
	// once. A graph-posed grid reports no duration, so the clamp's floor is the window there.
	const float Loop = LabDuration() > KINDA_SMALL_NUMBER
		? LabDuration() / FMath::Max(LabViewState.Speed, 0.01f) : 0.0f;
	WieldTrack.SecondsWanted = Seconds > 0.0f ? Seconds : FMath::Clamp(Loop, 1.0f, 20.0f);
	WieldTrack.ToleranceCm = ToleranceCm > 0.0f ? ToleranceCm : 5.0f;
	WieldTrack.Classname = ReviewWield;
	WieldTrack.MountBone = Ref->MountBone;
	WieldTrack.HandBone = Ref->HandBone;
	WieldTrack.bWearerDeclares = Body->GetBoneIndex(Ref->MountBone) != INDEX_NONE;
	WieldTrack.BindRadiusCm = Wield->GetSkeletalMeshAsset() != nullptr
		? static_cast<float>(Wield->GetSkeletalMeshAsset()->GetImportedBounds().SphereRadius)
		: 0.0f;
	WieldTrack.Body = Body;
	WieldTrack.Wield = Wield;
	// The authored mount-under-hand local: the mount-local gate for an undeclared mount, and one
	// half of the off-hand gate's authored recipe. NAME_None or an unanchored mount simply leaves
	// the corresponding gate unarmed — both are structural facts of the rig, not failures.
	const USkeletalMesh* const WieldMesh = Wield->GetSkeletalMeshAsset();
	WieldTrack.bMountLocalExpected = !WieldTrack.bWearerDeclares && WieldMesh != nullptr
		&& ElysiumMountLocalInHand(*WieldMesh, WieldTrack.MountBone, WieldTrack.HandBone,
			WieldTrack.ExpectedMountLocal);
	WieldTrack.OffHandBone = ElysiumOffHandOf(WieldTrack.HandBone);
	WieldTrack.bOffHand = WieldTrack.OffHandBone != NAME_None
		&& Body->GetBoneIndex(WieldTrack.OffHandBone) != INDEX_NONE && WieldMesh != nullptr
		&& (WieldTrack.bWearerDeclares || WieldTrack.bMountLocalExpected);
	UE_LOG(LogElysiumGreenRoom, Log,
		TEXT("wield track: sampling '%s' mount '%s' against hand '%s' for %.1f s "
		     "(tolerance %.1f cm)"),
		*WieldTrack.Classname, *WieldTrack.MountBone.ToString(), *WieldTrack.HandBone.ToString(),
		WieldTrack.SecondsWanted, WieldTrack.ToleranceCm);
	return true;
}

void FElysiumGreenRoomRun::TickWieldTrack(float DeltaSeconds)
{
	if (!WieldTrack.bRunning)
	{
		return;
	}
	const USkeletalMeshComponent* const Body = WieldTrack.Body.Get();
	const USkeletalMeshComponent* const Wield = WieldTrack.Wield.Get();
	if (Body == nullptr || Wield == nullptr || Body != LabBody()
		|| ReviewWield != WieldTrack.Classname)
	{
		CloseWieldTrack(false, FString::Printf(
			TEXT("%s: aborted — the body or the held weapon changed under the window"),
			*WieldTrack.Classname));
		return;
	}

	// The drawn side of every comparison below is a RENDER packet — the weapon's own matrices,
	// with the wearer's hand read from the BODY's packet of the same frame. Reading the hand off
	// the game thread instead compares across the pipeline's frame of latency, and that seam
	// read 16–31 cm of pure skew on `baseballbat_attack_heavy` (worst across the review loop's
	// wrap, where the end-pose→start-pose snap survives any playback speed) while the paused
	// pose agreed to 0.00 cm.
	// The weapon's and the body's packets are separate objects the render thread updates one
	// command apart, and a game-thread read mid-frame can catch the pair straddling two frames —
	// 4.7 cm / 7.6 deg of phantom misride at swing speed. Draining the render thread first makes
	// the pair one coherent frame. A stall per sample is the price of an honest number, and only
	// this window pays it.
	FlushRenderingCommands();
	FTransform MountAt;
	FTransform BindToWorld;
	FVector CentreAt;
	FTransform HandAt;
	if (!ElysiumRenderedWield(*Wield, WieldTrack.MountBone, MountAt, BindToWorld, CentreAt)
		|| !ElysiumRenderedBodyBone(*Body, WieldTrack.HandBone, HandAt))
	{
		// The install frame has no render packet yet; skip rather than abort, so a check issued in
		// the same breath as the equip still opens. A packet that never arrives is caught by the
		// sample floor at close.
		WieldTrack.SecondsSeen += DeltaSeconds;
		return;
	}
	const FVector HandComp =
		HandAt.GetRelativeTransform(Body->GetComponentTransform()).GetLocation();

	// MAPPING — a declared mount coincides with the wearer's bone, every frame, swing or not.
	// A zero-weight mount has no render matrix (see ElysiumRenderedBodyBone), so the comparison
	// is made in the HAND's frame, where both sides are self-consistent: the drawn mount against
	// the rendered hand, the wearer's mount against the game-thread hand. The mount's hand-local
	// is what the wearer's clip authors, so it is the frame-invariant quantity — whole-arm motion
	// cancels, and only a genuine misride (wrong bone, frozen follower, a bind disagreement)
	// survives. Against a clip that animates the mount in the hand's frame the two sides sit one
	// pipeline frame apart, so each drawn local is scored against the wearer's current AND
	// previous locals, keeping the wrap's one-frame local snap out of the verdict.
	if (WieldTrack.bWearerDeclares)
	{
		const FTransform DrawnLocal = MountAt.GetRelativeTransform(HandAt);
		const FTransform WearerLocal =
			Body->GetSocketTransform(WieldTrack.MountBone, RTS_World).GetRelativeTransform(
				Body->GetSocketTransform(WieldTrack.HandBone, RTS_World));
		float MapCm = FVector::Dist(DrawnLocal.GetLocation(), WearerLocal.GetLocation());
		float MapDeg = FMath::RadiansToDegrees(
			DrawnLocal.GetRotation().AngularDistance(WearerLocal.GetRotation()));
		if (WieldTrack.bPrevWearerLocal)
		{
			MapCm = FMath::Min(MapCm, static_cast<float>(FVector::Dist(
				DrawnLocal.GetLocation(), WieldTrack.PrevWearerLocal.GetLocation())));
			MapDeg = FMath::Min(MapDeg, FMath::RadiansToDegrees(
				DrawnLocal.GetRotation().AngularDistance(
					WieldTrack.PrevWearerLocal.GetRotation())));
		}
		WieldTrack.PrevWearerLocal = WearerLocal;
		WieldTrack.bPrevWearerLocal = true;
		if (MapCm > WieldTrack.WorstMapCm)
		{
			WieldTrack.WorstMapCm = MapCm;
			WieldTrack.WorstMapSample = WieldTrack.Samples;
			WieldTrack.WorstMapTime = LabClipTime;
		}
		WieldTrack.WorstMapDeg = FMath::Max(WieldTrack.WorstMapDeg, MapDeg);
	}

	// TRACKING — the drawn centre holds its offset in the hand's frame, whatever that offset is.
	const FVector CentreInHand = HandAt.InverseTransformPosition(CentreAt);
	if (WieldTrack.Samples == 0)
	{
		WieldTrack.Baseline = CentreInHand;
		WieldTrack.HandStart = HandComp;
	}
	const float DriftCm = FVector::Dist(CentreInHand, WieldTrack.Baseline);
	if (DriftCm > WieldTrack.WorstDriftCm)
	{
		WieldTrack.WorstDriftCm = DriftCm;
		WieldTrack.WorstDriftSample = WieldTrack.Samples;
		WieldTrack.WorstDriftTime = LabClipTime;
	}

	// PLACEMENT — the hand actually touches the mesh: its distance from the drawn geometry's own
	// bind-space bounding box, zero while the hand is inside it. Measured against the box, not
	// centre-against-sphere-radius: a gripped pistol's hand sits at the box's edge — outside the
	// bounding sphere of a centre up at the slide — while the old defect's metre-scale orbit
	// still reads as metres either way.
	const USkeletalMesh* const WieldMesh = Wield->GetSkeletalMeshAsset();
	const float PlaceOverCm =
		ElysiumHandToDrawnBoxCm(WieldMesh, BindToWorld, HandAt.GetLocation());
	if (PlaceOverCm > WieldTrack.WorstPlaceCm)
	{
		WieldTrack.WorstPlaceCm = PlaceOverCm;
		WieldTrack.WorstPlaceDistCm = FVector::Dist(CentreAt, HandAt.GetLocation());
		WieldTrack.WorstPlaceSample = WieldTrack.Samples;
		WieldTrack.WorstPlaceTime = LabClipTime;
	}

	// MOUNT LOCAL — an undeclared mount's drawn local in the hand's frame must equal the weapon
	// skeleton's own authored chain, rotation included, every frame. Both sides of this
	// comparison come from the same rendered frame, so there is no pipeline skew to forgive —
	// and a constant wrong rotation, which holds drift at zero and keeps the gripping hand
	// inside the box while it swings the far geometry off the other hand, is exactly what only
	// this gate can catch.
	if (WieldTrack.bMountLocalExpected)
	{
		const FTransform DrawnLocal = MountAt.GetRelativeTransform(HandAt);
		const float LocalCm = FVector::Dist(DrawnLocal.GetLocation(),
			WieldTrack.ExpectedMountLocal.GetLocation());
		const float LocalDeg = FMath::RadiansToDegrees(DrawnLocal.GetRotation().AngularDistance(
			WieldTrack.ExpectedMountLocal.GetRotation()));
		if (LocalCm > WieldTrack.WorstMountLocalCm)
		{
			WieldTrack.WorstMountLocalCm = LocalCm;
			WieldTrack.WorstMountLocalSample = WieldTrack.Samples;
			WieldTrack.WorstMountLocalTime = LabClipTime;
		}
		if (LocalDeg > WieldTrack.WorstMountLocalDeg)
		{
			WieldTrack.WorstMountLocalDeg = LocalDeg;
			WieldTrack.WorstMountLocalSample = WieldTrack.Samples;
			WieldTrack.WorstMountLocalTime = LabClipTime;
		}
	}

	// OFF HAND — on a two-handed carry the authored pose keeps the off hand on the weapon, and
	// the drawn frame must agree. Each sample scores the DRAWN off-hand-to-box distance against
	// the AUTHORED one (the game-thread pose under the verified recipe) for the same frame, so a
	// clip that legitimately swings the off hand away is not a failure — only the drawn frame
	// disagreeing with the recipe is. Whether the carry is two-handed at all is the window's
	// minimum authored distance, read at close.
	if (WieldTrack.bOffHand)
	{
		FTransform AuthoredBindToWorld;
		FTransform OffDrawn;
		if (ElysiumAuthoredBindToWorld(*Body, *WieldMesh, WieldTrack.MountBone,
				WieldTrack.HandBone, WieldTrack.bWearerDeclares, WieldTrack.ExpectedMountLocal,
				AuthoredBindToWorld)
			&& ElysiumRenderedBodyBone(*Body, WieldTrack.OffHandBone, OffDrawn))
		{
			const float AuthoredCm = ElysiumHandToDrawnBoxCm(WieldMesh, AuthoredBindToWorld,
				Body->GetSocketTransform(WieldTrack.OffHandBone, RTS_World).GetLocation());
			const float DrawnCm =
				ElysiumHandToDrawnBoxCm(WieldMesh, BindToWorld, OffDrawn.GetLocation());
			WieldTrack.MinAuthoredOffHandCm =
				FMath::Min(WieldTrack.MinAuthoredOffHandCm, AuthoredCm);
			// The drawn side trails the game thread by a pipeline frame, the same seam the
			// mapping gate crosses: score against the current and previous authored values.
			float ErrCm = FMath::Abs(DrawnCm - AuthoredCm);
			if (WieldTrack.bPrevAuthoredOffHand)
			{
				ErrCm = FMath::Min(ErrCm,
					FMath::Abs(DrawnCm - WieldTrack.PrevAuthoredOffHandCm));
			}
			WieldTrack.PrevAuthoredOffHandCm = AuthoredCm;
			WieldTrack.bPrevAuthoredOffHand = true;
			if (ErrCm > WieldTrack.WorstOffHandErrCm)
			{
				WieldTrack.WorstOffHandErrCm = ErrCm;
				WieldTrack.WorstOffHandDrawnCm = DrawnCm;
				WieldTrack.WorstOffHandAuthoredCm = AuthoredCm;
				WieldTrack.WorstOffHandSample = WieldTrack.Samples;
				WieldTrack.WorstOffHandTime = LabClipTime;
			}
		}
	}

	WieldTrack.HandPeakCm = FMath::Max(WieldTrack.HandPeakCm,
		static_cast<float>(FVector::Dist(HandComp, WieldTrack.HandStart)));
	++WieldTrack.Samples;
	WieldTrack.SecondsSeen += DeltaSeconds;
	if (WieldTrack.SecondsSeen < WieldTrack.SecondsWanted)
	{
		return;
	}

	// A pass is a claim about motion, so the base has to have supplied some: a hand that never left
	// its first position bounds the possible drift at zero even for a weapon nailed to the stage.
	const float NeedHandCm = FMath::Max(WieldTrack.ToleranceCm * 3.0f, 15.0f);
	if (WieldTrack.Samples < 10)
	{
		CloseWieldTrack(false, FString::Printf(
			TEXT("%s: unproven — only %d samples landed in %.1f s; widen the window"),
			*WieldTrack.Classname, WieldTrack.Samples, WieldTrack.SecondsSeen));
	}
	else if (WieldTrack.HandPeakCm < NeedHandCm)
	{
		CloseWieldTrack(false, FString::Printf(
			TEXT("%s: unproven — the base moved '%s' only %.1f cm (need >= %.0f). Stand a base "
			     "that swings the hand (a walk, an attack) and rerun; is the clip paused?"),
			*WieldTrack.Classname, *WieldTrack.HandBone.ToString(), WieldTrack.HandPeakCm,
			NeedHandCm));
	}
	else if (WieldTrack.bWearerDeclares && (WieldTrack.WorstMapCm > WieldTrack.ToleranceCm
		|| WieldTrack.WorstMapDeg > ElysiumWieldMountDeg))
	{
		CloseWieldTrack(false, FString::Printf(
			TEXT("%s: FAIL(mapping) — in the hand's frame the rendered mount sat %.1f cm / "
			     "%.1f deg off the wearer's own '%s' it must coincide with, worst at sample %d "
			     "(clip t=%.2f s), over %d samples (tolerance %.1f cm / %.1f deg)"),
			*WieldTrack.Classname, WieldTrack.WorstMapCm, WieldTrack.WorstMapDeg,
			*WieldTrack.MountBone.ToString(), WieldTrack.WorstMapSample, WieldTrack.WorstMapTime,
			WieldTrack.Samples, WieldTrack.ToleranceCm, ElysiumWieldMountDeg));
	}
	else if (WieldTrack.bMountLocalExpected && (WieldTrack.WorstMountLocalCm > WieldTrack.ToleranceCm
		|| WieldTrack.WorstMountLocalDeg > ElysiumWieldMountDeg))
	{
		CloseWieldTrack(false, FString::Printf(
			TEXT("%s: FAIL(mountlocal) — in the hand's frame the drawn mount sat %.1f cm / "
			     "%.1f deg off the weapon skeleton's own authored '%s'-under-'%s' local, worst "
			     "at sample %d (clip t=%.2f s), over %d samples (tolerance %.1f cm / %.1f deg)"),
			*WieldTrack.Classname, WieldTrack.WorstMountLocalCm, WieldTrack.WorstMountLocalDeg,
			*WieldTrack.MountBone.ToString(), *WieldTrack.HandBone.ToString(),
			WieldTrack.WorstMountLocalSample, WieldTrack.WorstMountLocalTime, WieldTrack.Samples,
			WieldTrack.ToleranceCm, ElysiumWieldMountDeg));
	}
	else if (WieldTrack.WorstDriftCm > WieldTrack.ToleranceCm)
	{
		CloseWieldTrack(false, FString::Printf(
			TEXT("%s: FAIL(tracking) — the drawn geometry drifted %.1f cm off its offset from "
			     "hand '%s', worst at sample %d (clip t=%.2f s), over %d samples (tolerance "
			     "%.1f cm)"),
			*WieldTrack.Classname, WieldTrack.WorstDriftCm, *WieldTrack.HandBone.ToString(),
			WieldTrack.WorstDriftSample, WieldTrack.WorstDriftTime, WieldTrack.Samples,
			WieldTrack.ToleranceCm));
	}
	else if (WieldTrack.WorstPlaceCm > FMath::Max(WieldTrack.ToleranceCm, ElysiumWieldTouchCm))
	{
		CloseWieldTrack(false, FString::Printf(
			TEXT("%s: FAIL(placement) — hand '%s' sat %.0f cm outside the drawn geometry's own "
			     "bounds (its centre %.0f cm away against a %.0f cm mesh radius), worst at sample "
			     "%d (clip t=%.2f s), over %d samples — no runtime offset corrects this; the bake "
			     "is wrong upstream"),
			*WieldTrack.Classname, *WieldTrack.HandBone.ToString(), WieldTrack.WorstPlaceCm,
			WieldTrack.WorstPlaceDistCm, WieldTrack.BindRadiusCm, WieldTrack.WorstPlaceSample,
			WieldTrack.WorstPlaceTime, WieldTrack.Samples));
	}
	else if (WieldTrack.bOffHand && WieldTrack.MinAuthoredOffHandCm <= ElysiumWieldTouchCm
		&& WieldTrack.WorstOffHandErrCm > WieldTrack.ToleranceCm)
	{
		CloseWieldTrack(false, FString::Printf(
			TEXT("%s: FAIL(offhand) — a two-handed carry (the authored pose keeps '%s' within "
			     "%.1f cm of the weapon) drew that hand %.1f cm from the drawn geometry where "
			     "the authored recipe put it %.1f cm, worst at sample %d (clip t=%.2f s), over "
			     "%d samples (tolerance %.1f cm)"),
			*WieldTrack.Classname, *WieldTrack.OffHandBone.ToString(),
			WieldTrack.MinAuthoredOffHandCm, WieldTrack.WorstOffHandDrawnCm,
			WieldTrack.WorstOffHandAuthoredCm, WieldTrack.WorstOffHandSample,
			WieldTrack.WorstOffHandTime, WieldTrack.Samples, WieldTrack.ToleranceCm));
	}
	else
	{
		const bool bTwoHanded = WieldTrack.bOffHand
			&& WieldTrack.MinAuthoredOffHandCm <= ElysiumWieldTouchCm;
		CloseWieldTrack(true, FString::Printf(
			TEXT("%s: PASS — the drawn geometry rode hand '%s' (drift %.2f cm) touching its "
			     "bounds within %.1f cm (mesh radius %.0f cm)%s%s%s, over %d samples of an "
			     "animated base (the hand travelled %.0f cm)"),
			*WieldTrack.Classname, *WieldTrack.HandBone.ToString(), WieldTrack.WorstDriftCm,
			WieldTrack.WorstPlaceCm, WieldTrack.BindRadiusCm,
			WieldTrack.bWearerDeclares
				? *FString::Printf(TEXT(", on the wearer's '%s' within %.2f cm / %.1f deg"),
					*WieldTrack.MountBone.ToString(), WieldTrack.WorstMapCm,
					WieldTrack.WorstMapDeg)
				: TEXT(""),
			WieldTrack.bMountLocalExpected
				? *FString::Printf(TEXT(", on its own authored local within %.2f cm / %.1f deg"),
					WieldTrack.WorstMountLocalCm, WieldTrack.WorstMountLocalDeg)
				: TEXT(""),
			bTwoHanded
				? *FString::Printf(
					TEXT(", two-handed: off hand '%s' held its authored contact within %.1f cm"),
					*WieldTrack.OffHandBone.ToString(), WieldTrack.WorstOffHandErrCm)
				: TEXT(""),
			WieldTrack.Samples, WieldTrack.HandPeakCm));
	}
}

void FElysiumGreenRoomRun::CloseWieldTrack(bool bPass, const FString& Verdict)
{
	WieldTrack.bRunning = false;
	WieldTrack.bPassed = bPass;
	WieldTrack.Verdict = Verdict;
	if (bPass)
	{
		UE_LOG(LogElysiumGreenRoom, Display, TEXT("wield track: %s"), *Verdict);
	}
	else
	{
		UE_LOG(LogElysiumGreenRoom, Warning, TEXT("wield track: %s"), *Verdict);
	}
}

void FElysiumGreenRoomRun::ReapplyWield()
{
	if (ReviewWield.IsEmpty())
	{
		return;
	}
	USkeletalMeshComponent* Body = LabBody();
	const FElysiumWieldModelRef* Ref = nullptr;
	if (Body == nullptr
		|| UElysiumWieldTable::FindRow(FName(*ReviewWield), bReviewWieldFemale, Ref)
			!= EElysiumWieldResult::Found)
	{
		// The row resolved once to get here, so losing it now means the table changed underneath a
		// live session. Say so rather than leaving a body silently empty-handed.
		UE_LOG(LogElysiumGreenRoom, Warning,
			TEXT("lab wield: '%s' no longer resolves — the standing body holds nothing"),
			*ReviewWield);
		ReviewWield.Reset();
		return;
	}
	ElysiumNpcVisual::InstallWieldModel(Body, *Ref, ReviewWield);
}

TArray<FString> FElysiumGreenRoomRun::LabWieldClassnames(bool bFemale)
{
	TArray<FString> Names;
	const UElysiumWieldTable* const Table = UElysiumWieldTable::Load();
	if (Table == nullptr)
	{
		return Names;
	}
	for (const TPair<FName, FElysiumWieldRow>& Pair : Table->Rows)
	{
		const FElysiumWieldModelRef& Ref = bFemale ? Pair.Value.Female : Pair.Value.Male;
		if (Pair.Value.bShowsWieldModel && Ref.Binding != EElysiumWieldBinding::None
			&& !Ref.Mesh.IsNull())
		{
			Names.Add(Pair.Key.ToString());
		}
	}
	Names.Sort();
	return Names;
}
