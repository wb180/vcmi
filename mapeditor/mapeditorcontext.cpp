#include "mapeditorcontext.h"

#include "../client/mapView/MapRendererContextState.h"
#include "../lib/mapping/CMap.h"

MapEditorContext::MapEditorContext(const MapRendererContextState & viewState) : MapRendererBaseContext(viewState)
{

}

int3 MapEditorContext::getMapSize() const
{
	return viewState.getMapSize();
}

bool MapEditorContext::isInMap(const int3 & coordinates) const
{
	return viewState.isInTheMap(coordinates);
}

const CGObjectInstance * MapEditorContext::getObject(ObjectInstanceID objectID) const
{
	return viewState.getMap()->objects.at(objectID.getNum());
}

bool MapEditorContext::showGrid() const
{
	return true;
}

bool MapEditorContext::showVisitable() const
{
	return true;
}

bool MapEditorContext::showBlocked() const
{
	return true;
}
