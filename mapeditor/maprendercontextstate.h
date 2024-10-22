#pragma once

#include "../client/mapView/MapRendererContextState.h"

VCMI_LIB_NAMESPACE_BEGIN

class CMap;

VCMI_LIB_NAMESPACE_END

class MapRendererContextStateExt : public MapRendererContextState
{
protected:
	explicit MapRendererContextStateExt(CMap * map);


public:
	static MapRendererContextStateExt * createRendererContextState(CMap * map);

private:
	CMap * map;

	// MapRendererContextState interface
public:
	const CMap * getMap() const override;
	int3 getMapSize() const override;
	bool isInTheMap(const int3 & coordinates) const override;
	void setMap(CMap * newMap);
};
