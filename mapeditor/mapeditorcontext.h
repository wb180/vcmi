#pragma once

#include "../client/mapView/MapRendererContext.h"

class MapEditorContext : public MapRendererBaseContext
{
public:
	explicit MapEditorContext(const MapRendererContextState & viewState);

	int3 getMapSize() const override;
	bool isInMap(const int3 & coordinates) const override;

	const CGObjectInstance * getObject(ObjectInstanceID objectID) const override;

	bool showGrid() const override;
	bool showVisitable() const override;
	bool showBlocked() const override;
	const TerrainTile & getMapTile(const int3 & coordinates) const override;
};
