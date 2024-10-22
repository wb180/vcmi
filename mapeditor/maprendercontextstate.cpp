#include "maprendercontextstate.h"

#include "../lib/mapping/CMap.h"

MapRendererContextStateExt::MapRendererContextStateExt(CMap * m) : map(m)
{

}

MapRendererContextStateExt * MapRendererContextStateExt::createRendererContextState(CMap * map)
{
	MapRendererContextStateExt * state = new MapRendererContextStateExt(map);
	state->init();
	return state;
}

void MapRendererContextStateExt::setMap(CMap * newMap)
{
	map = newMap;
	init();
}

const CMap * MapRendererContextStateExt::getMap() const
{
	return map;
}

int3 MapRendererContextStateExt::getMapSize() const
{
	if(map)
		return int3(map->width, map->height, map->twoLevel ? 2 : 1);

	return int3(0, 0, 0);
}


bool MapRendererContextStateExt::isInTheMap(const int3 & coordinates) const
{
	if(map)
		return map->isInTheMap(coordinates);

	return false;
}
