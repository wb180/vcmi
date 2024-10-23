#include "renderhandler.h"

#include "../lib/VCMI_Lib.h"

#include <QGlobalStatic>

Q_GLOBAL_STATIC(RenderHandlerExt, renderHandler)

#include "../lib/RoadHandler.h"
#include "../lib/RiverHandler.h"
#include "../lib/TerrainHandler.h"

RenderHandlerExt::RenderHandlerExt()
{

}

int RenderHandlerExt::getScalingFactor() const
{
	return 1;
}


MapTileStorageExt::MapTileStorageExt(IRenderHandler & h, size_t capacity) : handler(h), MapTileStorage(capacity)
{

}

IRenderHandler & MapTileStorageExt::getHandler() const
{
	return handler;
}

MapRendererTerrainExt::MapRendererTerrainExt()
{
	storage = std::make_shared<MapTileStorageExt>(*renderHandler, VLC->terrainTypeHandler->objects.size());
}

MapRendererRiverExt::MapRendererRiverExt()
{
	storage = std::make_shared<MapTileStorageExt>(*renderHandler, VLC->riverTypeHandler->objects.size());
}

MapRendererRoadExt::MapRendererRoadExt()
{
	storage = std::make_shared<MapTileStorageExt>(*renderHandler, VLC->roadTypeHandler->objects.size());
}
