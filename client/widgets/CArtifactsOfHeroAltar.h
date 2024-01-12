/*
 * CArtifactsOfHeroAltar.h, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 *
 */
#pragma once

#include "CArtifactsOfHeroBase.h"

#include "../../lib/CArtHandler.h"

class CArtifactsOfHeroAltar : public CArtifactsOfHeroBase
{
public:
	std::set<const CArtifactInstance*> artifactsOnAltar;

	CArtifactsOfHeroAltar(const Point & position);
	~CArtifactsOfHeroAltar();
	void scrollBackpack(int offset) override;
	void pickUpArtifact(CArtPlace & artPlace);
	void swapArtifacts(const ArtifactLocation & srcLoc, const ArtifactLocation & dstLoc);
};
