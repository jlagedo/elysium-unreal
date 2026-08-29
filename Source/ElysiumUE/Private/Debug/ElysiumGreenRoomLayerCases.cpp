#include "Debug/ElysiumGreenRoomRun.h"

#include "Debug/ElysiumGreenRoomShared.h"

#include "ElysiumAnimationIntent.h"   // the grip table a layer case reports its mask from
#include "Visual/ElysiumNpcVisual.h"   // IsStemBaked — a preset case only stands what the mount has

#include "Engine/GameInstance.h"
#include "Engine/World.h"

// Preset acceptance cases: one click stands a whole layer claim.

namespace
{
	// One case per claim the rung has to answer. The candidate lists are ordered and the first label
	// the standing body carries wins, because the weapon roster a body resolves differs by bank set —
	// naming exactly one clip would make the button a coin toss on the body standing.
	struct FElysiumLayerCase
	{
		const TCHAR* Name;
		// Composed under its own bone mask, in the overlay slot.
		TArray<FString> Overlay;
		// Composed on top, in the additive slot. Both may be set: they are separate slots.
		TArray<FString> Additive;
		float AimYaw = 0.f;
		float AimPitch = 0.f;
		const TCHAR* Expect = TEXT("");
	};

	const TArray<FElysiumLayerCase>& LayerCases()
	{
		static const TArray<FElysiumLayerCase> Cases = {
			{
				TEXT("Aim grid"),
				// **`glock_aim_layer` is LAST on purpose — it is an authored defect.** Measured over
				// all 25 male grids it is the single sign outlier: its `CR` cell poses the head ~42
				// degrees to the LEFT where every other family poses right, and its `CC` is off-family
				// too. The grid structure and animation indices match `anaconda`'s exactly, so this is
				// Troika's own clip content rather than anything the decode did. Judging the mask or
				// the axis mapping against it reads a real defect as ours.
				{ TEXT("anaconda_aim_layer"), TEXT("smith_aim_layer"), TEXT("steyr_aim_layer"),
					TEXT("deserteagle_aim_layer"), TEXT("glock_aim_layer") },
				{},
				35.f, -20.f,   // inside every grid's own -45..45 span
				TEXT("torso aims off-centre, legs keep their gait. Sweep aim_yaw: the upper body "
					"tracks and the lower body does not.")
			},
			{
				TEXT("1-hand melee"),
				{ TEXT("katana_bobble_layer"), TEXT("knife_bobble_layer"),
					TEXT("baseballbat_bobble_layer"), TEXT("stake_bobble_layer"),
					TEXT("tireiron_bobble_layer") },
				{},
				0.f, 0.f,
				TEXT("the RIGHT ARM carries the weapon and the torso stays with the base — a 24-bone "
					"mask, and no aim parameter.")
			},
			{
				TEXT("2-hand melee"),
				{ TEXT("bushhook_bobble_layer"), TEXT("sledgehammer_bobble_layer") },
				{},
				0.f, 0.f,
				TEXT("the SAME 49-bone upper-body gate the firearms use, not the right-arm mask. This "
					"is the case a resolver keyed on `is this melee` gets wrong.")
			},
			{
				TEXT("Grid + delta"),
				// Same ordering rule as the aim case above, and the same reason.
				{ TEXT("anaconda_aim_layer"), TEXT("smith_aim_layer"), TEXT("glock_aim_layer") },
				{ TEXT("anaconda_bobble_delta"), TEXT("smith_bobble_delta"),
					TEXT("glock_bobble_delta"), TEXT("anaconda_attack_delta") },
				20.f, 0.f,
				TEXT("both slots at once — the readout says `2 composing`. The overlay poses the "
					"torso and the additive rides on top of it.")
			},
		};
		return Cases;
	}

	// The weapon tag a layer label starts with, which is what the grip table is keyed on.
	FString WeaponTagOf(const FString& Label)
	{
		int32 Underscore = INDEX_NONE;
		return Label.FindChar(TEXT('_'), Underscore) ? Label.Left(Underscore) : Label;
	}
}

bool FElysiumGreenRoomRun::PickLayerCaseBody(const TArray<FString>& Candidates, FString& OutStem)
{
	const UWorld* World = GetWorld();
	const UGameInstance* GI = World ? World->GetGameInstance() : nullptr;
	UElysiumAnimSubsystem* Anims = GI ? GI->GetSubsystem<UElysiumAnimSubsystem>() : nullptr;
	if (Anims == nullptr)
	{
		return false;
	}
	// The PLAYER body first, and the same one the rung's own acceptance names
	// (`docs/project/three-cs-roadmap.md`). It matters which: a player body resolves its gaits
	// through the PC-only bank while the cast resolves the same labels through the shared one, so a
	// layer judged over a cast body is being judged over the wrong host.
	//
	// Preferred, not hardcoded — a body the export has not covered falls through to the scan below
	// rather than failing, which is what keeps the button working on a partial export.
	TArray<FString> Stems = { TEXT("tremere_male_armor_0") };
	// Sorted so two runs of the same export pick the same fallback: `TMap` iteration order is not
	// stable across runs, and a preset that stood a different model each time would make a pose
	// difference unattributable.
	TArray<FString> Rest;
	Anims->GetIndex().Npcs.GetKeys(Rest);
	Rest.Sort();
	Stems.Append(Rest);

	for (const FString& Stem : Stems)
	{
		// Baked first: the vocabulary lookup below is cheap but the mount check is what decides
		// whether anything can actually stand.
		if (!ElysiumNpcVisual::IsStemBaked(Stem))
		{
			continue;
		}
		const FElysiumNpcClipSet* Set = Anims->GetClipSet(Stem);
		if (Set == nullptr)
		{
			continue;
		}
		for (const FString& Label : Candidates)
		{
			if (Set->Find(Label) != nullptr)
			{
				OutStem = Stem;
				return true;
			}
		}
	}
	return false;
}

int32 FElysiumGreenRoomRun::LayerCaseCount()
{
	return LayerCases().Num();
}

const TCHAR* FElysiumGreenRoomRun::LayerCaseName(int32 Index)
{
	return LayerCases().IsValidIndex(Index) ? LayerCases()[Index].Name : TEXT("");
}

bool FElysiumGreenRoomRun::LabLoadLayerCase(int32 Index, FString& OutSummary, FString& OutError)
{
	OutSummary.Reset();
	if (!LayerCases().IsValidIndex(Index))
	{
		OutError = TEXT("no such layer case");
		return false;
	}
	const FElysiumLayerCase& Case = LayerCases()[Index];

	// **A body, if the session has none.** `elysium.gr` opens a bare stage on purpose — the drive path
	// refuses to invent a default PC body, because that would be inventing content. A preset case is
	// the one place naming one is legitimate: it exists to stand a specific claim. It is still not
	// hardcoded — the first BAKED body that actually carries this case's layer wins, so the button
	// selects from what the export produced rather than from a wish.
	if (ReviewStem.IsEmpty())
	{
		FString Chosen;
		if (!PickLayerCaseBody(Case.Overlay.IsEmpty() ? Case.Additive : Case.Overlay, Chosen))
		{
			OutError = TEXT("no baked body in this export carries this case's layer — run "
				"`uv run elysium export characters`");
			return false;
		}
		FString BodyError;
		if (!LabSetBody(Chosen, FString(), BodyError))
		{
			OutError = BodyError;
			return false;
		}
	}

	// **Drive, because the claim is about a moving host.** Standing still, an aim layer over a static
	// pose cannot show that the legs keep their gait while the torso turns — which is the whole thing
	// being judged. Entering it here is what makes the button one click from a cold `elysium.gr`.
	if (!IsDriving())
	{
		FString ModeError;
		if (!LabSetMode(ELabMode::Drive, ModeError))
		{
			OutError = FString::Printf(TEXT("could not enter drive mode: %s"), *ModeError);
			return false;
		}
	}

	// Checked HERE and not on the way in. The stand above is what a cold `elysium.gr` relies on — the
	// stage is deliberately bare — so a body check ahead of it refuses the one session the button
	// exists to serve, and reports an empty stage as the reason it would not fill the stage.
	if (LabBody() == nullptr)
	{
		OutError = TEXT("nothing is standing on the stage");
		return false;
	}

	// Cleared first so a case is exactly what it says, rather than what it says plus whatever the
	// previous click left riding.
	LabClearLayers();

	// The FIRST candidate's refusal is kept, not the last: the later entries are alternatives for a
	// body with a different bank set, so their "not in this vocabulary" would bury the real reason
	// the intended one was declined.
	FString FirstRefusal;
	auto ArmFirst = [this, &FirstRefusal](const TArray<FString>& Candidates, FString& OutChosen)
	{
		for (const FString& Label : Candidates)
		{
			FString Why;
			if (LabSetLayer(Label, 1.0f, Why))
			{
				OutChosen = Label;
				return true;
			}
			if (FirstRefusal.IsEmpty())
			{
				FirstRefusal = MoveTemp(Why);
			}
		}
		return false;
	};

	FString Overlay;
	FString Additive;
	const bool bWantsOverlay = !Case.Overlay.IsEmpty();
	const bool bWantsAdditive = !Case.Additive.IsEmpty();
	const bool bGotOverlay = bWantsOverlay && ArmFirst(Case.Overlay, Overlay);
	const bool bGotAdditive = bWantsAdditive && ArmFirst(Case.Additive, Additive);

	if ((bWantsOverlay && !bGotOverlay) || (bWantsAdditive && !bGotAdditive))
	{
		// Naming the whole candidate list is the point: the fix is either to export this body or to
		// stand one whose bank set carries the family, and the operator cannot tell which from a
		// bare "not found".
		const TArray<FString>& Missing = bGotOverlay ? Case.Additive : Case.Overlay;
		OutError = FString::Printf(TEXT("'%s' armed none of %s.\n%s"),
			Case.Name, *FString::Join(Missing, TEXT(", ")), *FirstRefusal);
		return false;
	}

	LabSetLayerAim(Case.AimYaw, Case.AimPitch);

	// The grip is REPORTED rather than assumed, off the same table the resolver picks the mask with,
	// so the two melee cases state which mask they should be wearing instead of leaving it to the eye.
	if (!Overlay.IsEmpty())
	{
		const FString Tag = WeaponTagOf(Overlay);
		const bool bTwoHanded =
			ElysiumAnimIntent::WeaponGrip(Tag) == EElysiumWeaponGrip::TwoHanded;
		OutSummary = FString::Printf(TEXT("%s: overlay `%s` (%s; %s grip -> %s mask)"),
			Case.Name, *Overlay, *ReviewLayerArmed,
			bTwoHanded ? TEXT("two-handed") : TEXT("one-handed"),
			bTwoHanded ? TEXT("49-bone upper body") : TEXT("24-bone right arm"));
	}
	else
	{
		OutSummary = FString::Printf(TEXT("%s:"), Case.Name);
	}
	if (!Additive.IsEmpty())
	{
		OutSummary += FString::Printf(TEXT("  + additive `%s`"), *Additive);
	}
	OutSummary += FString::Printf(TEXT("\nExpect: %s"), Case.Expect);

	UE_LOG(LogElysiumGreenRoom, Log, TEXT("lab: layer case '%s' -> overlay '%s' additive '%s'"),
		Case.Name, *Overlay, *Additive);
	return true;
}
