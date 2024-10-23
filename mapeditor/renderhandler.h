#pragma once

#include "../client/renderSDL/RenderHandler.h"
#include "../client/mapView/MapRenderer.h"

class RenderHandlerExt : public RenderHandler
{
public:
	RenderHandlerExt();

	int getScalingFactor() const override;
};

class MapTileStorageExt : public MapTileStorage
{
public:
	explicit MapTileStorageExt(IRenderHandler & handler, size_t capacity);

private:
	IRenderHandler & handler;

	// MapTileStorage interface
public:
	IRenderHandler & getHandler() const override;
};

class MapRendererTerrainExt : public MapRendererTerrain
{
public:
	MapRendererTerrainExt();
};

class MapRendererRiverExt : public MapRendererRiver
{
public:
	MapRendererRiverExt();
};

class MapRendererRoadExt : MapRendererRoad
{
public:
	MapRendererRoadExt();
};
