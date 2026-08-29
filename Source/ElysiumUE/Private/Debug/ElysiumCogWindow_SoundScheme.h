#pragma once

#include "CogCommon.h"

#if ENABLE_COG

#include "CoreMinimal.h"
#include "Debug/ElysiumCogWindow.h"
#include "imgui.h"

// Live game-state view for the ambient/music scheme system. Reads the map's
// FElysiumSoundSchemeManager (active scheme, ambient bed, music stems, polar random-one-shot
// scheduler) and drives the music state machine (Explore/Combat/Alert) that combat scoring owns.
// Also lists this map's ambient_soundscheme anchors with FadeIn/FadeOut buttons that crossfade
// schemes through the same path the Source I/O uses. Immediate-mode over the plain-C++ manager
// (the substrate is invisible to Cog's UObject reflection), the FElysiumCogWindow pattern.
class FElysiumCogWindow_SoundScheme : public FElysiumCogWindow
{
	typedef FElysiumCogWindow Super;

protected:
	virtual void Initialize() override;
	virtual void RenderHelp() override;
	virtual void RenderContent() override;

private:
	// Set the debug music state (writes elysium.MusicState; the manager applies it on its next tick).
	void SetMusicStateCvar(int32 State);
};

#endif // ENABLE_COG
