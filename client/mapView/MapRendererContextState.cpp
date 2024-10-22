/*
 * MapRendererContext.cpp, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 *
 */

#include "StdInc.h"
#include "MapRendererContextState.h"

#include "IMapRendererContext.h"
#include "mapHandler.h"

#include "../../CCallback.h"
#include "../CGameInfo.h"
#include "../CPlayerInterface.h"
#include "../adventureMap/AdventureMapInterface.h"

#include "../../lib/mapObjects/CGHeroInstance.h"
#include "../../lib/mapping/CMap.h"

static bool compareObjectBlitOrder(const CMap * map, ObjectInstanceID left, ObjectInstanceID right)
{
	return CMapHandler::compareObjectBlitOrder(map->objects[left.getNum()], map->objects[right.getNum()]);
}

MapRendererContextState::MapRendererContextState()
{

}

void MapRendererContextState::addObject(const CGObjectInstance * obj)
{
	if(!obj)
		return;

	for(int fx = 0; fx < obj->getWidth(); ++fx)
	{
		for(int fy = 0; fy < obj->getHeight(); ++fy)
		{
			int3 currTile(obj->anchorPos().x - fx, obj->anchorPos().y - fy, obj->anchorPos().z);

			if(isInTheMap(currTile) && obj->coveringAt(currTile))
			{
				auto & container = objects[currTile.z][currTile.x][currTile.y];

				container.push_back(obj->id);
				boost::range::sort(container, std::bind(compareObjectBlitOrder, getMap(), std::placeholders::_1, std::placeholders::_2));
			}
		}
	}
}

void MapRendererContextState::addMovingObject(const CGObjectInstance * object, const int3 & tileFrom, const int3 & tileDest)
{
	int xFrom = std::min(tileFrom.x, tileDest.x) - object->getWidth();
	int xDest = std::max(tileFrom.x, tileDest.x);
	int yFrom = std::min(tileFrom.y, tileDest.y) - object->getHeight();
	int yDest = std::max(tileFrom.y, tileDest.y);

	for(int x = xFrom; x <= xDest; ++x)
	{
		for(int y = yFrom; y <= yDest; ++y)
		{
			int3 currTile(x, y, object->anchorPos().z);

			if(isInTheMap(currTile))
			{
				auto & container = objects[currTile.z][currTile.x][currTile.y];

				container.push_back(object->id);
				boost::range::sort(container, std::bind(compareObjectBlitOrder, getMap(), std::placeholders::_1, std::placeholders::_2));
			}
		}
	}
}

void MapRendererContextState::removeObject(const CGObjectInstance * object)
{
	for(int z = 0; z < getMapSize().z; z++)
		for(int x = 0; x < getMapSize().x; x++)
			for(int y = 0; y < getMapSize().y; y++)
				vstd::erase(objects[z][x][y], object->id);
}

void MapRendererContextState::init()
{
	auto mapSize = getMapSize();

	objects.resize(boost::extents[mapSize.z][mapSize.x][mapSize.y]);

	if(getMap())
	{
		logGlobal->debug("Loading map objects");
		for(const auto & obj : getMap()->objects)
			addObject(obj);
		logGlobal->debug("Done loading map objects");
	}
}

const CMap * MapRendererContextState::getMap() const
{
	return CGI->mh->getMap();
}

int3 MapRendererContextState::getMapSize() const
{
	return LOCPLINT->cb->getMapSize();
}

bool MapRendererContextState::isInTheMap(const int3 & coordinates) const
{
	return LOCPLINT->cb->isInTheMap(coordinates);
}

MapRendererContextState * MapRendererContextState::createRendererContextState()
{
	MapRendererContextState * state = new MapRendererContextState;
	state->init();
	return state;
}
