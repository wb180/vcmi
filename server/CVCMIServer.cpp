/*
 * CVCMIServer.cpp, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 *
 */
#include "StdInc.h"
#include <boost/program_options.hpp>

#include "../lib/filesystem/Filesystem.h"
#include "../lib/campaign/CampaignState.h"
#include "../lib/CThreadHelper.h"
#include "../lib/CArtHandler.h"
#include "../lib/CGeneralTextHandler.h"
#include "../lib/CHeroHandler.h"
#include "../lib/CTownHandler.h"
#include "../lib/CBuildingHandler.h"
#include "../lib/spells/CSpellHandler.h"
#include "../lib/CCreatureHandler.h"
#include "zlib.h"
#include "CVCMIServer.h"
#include "../lib/StartInfo.h"
#include "../lib/mapping/CMapHeader.h"
#include "../lib/rmg/CMapGenOptions.h"
#include "LobbyNetPackVisitors.h"
#ifdef VCMI_ANDROID
#include <jni.h>
#include <android/log.h>
#include "lib/CAndroidVMHelper.h"
#endif
#include "../lib/VCMI_Lib.h"
#include "../lib/VCMIDirs.h"
#include "CGameHandler.h"
#include "processors/PlayerMessageProcessor.h"
#include "../lib/mapping/CMapInfo.h"
#include "../lib/network/NetworkServer.h"
#include "../lib/network/NetworkClient.h"
#include "../lib/GameConstants.h"
#include "../lib/logging/CBasicLogConfigurator.h"
#include "../lib/CConfigHandler.h"
#include "../lib/ScopeGuard.h"
#include "../lib/serializer/CMemorySerializer.h"
#include "../lib/serializer/Cast.h"
#include "../lib/serializer/Connection.h"

#include "../lib/UnlockGuard.h"

// for applier
#include "../lib/registerTypes/RegisterTypesLobbyPacks.h"

// UUID generation
#include <boost/uuid/uuid.hpp>
#include <boost/uuid/uuid_io.hpp>
#include <boost/uuid/uuid_generators.hpp>

#include "../lib/gameState/CGameState.h"

template<typename T> class CApplyOnServer;

class CBaseForServerApply
{
public:
	virtual bool applyOnServerBefore(CVCMIServer * srv, void * pack) const =0;
	virtual void applyOnServerAfter(CVCMIServer * srv, void * pack) const =0;
	virtual ~CBaseForServerApply() {}
	template<typename U> static CBaseForServerApply * getApplier(const U * t = nullptr)
	{
		return new CApplyOnServer<U>();
	}
};

template <typename T> class CApplyOnServer : public CBaseForServerApply
{
public:
	bool applyOnServerBefore(CVCMIServer * srv, void * pack) const override
	{
		T * ptr = static_cast<T *>(pack);
		ClientPermissionsCheckerNetPackVisitor checker(*srv);
		ptr->visit(checker);

		if(checker.getResult())
		{
			ApplyOnServerNetPackVisitor applier(*srv);
			ptr->visit(applier);
			return applier.getResult();
		}
		else
			return false;
	}

	void applyOnServerAfter(CVCMIServer * srv, void * pack) const override
	{
		T * ptr = static_cast<T *>(pack);
		ApplyOnServerAfterAnnounceNetPackVisitor applier(*srv);
		ptr->visit(applier);
	}
};

template <>
class CApplyOnServer<CPack> : public CBaseForServerApply
{
public:
	bool applyOnServerBefore(CVCMIServer * srv, void * pack) const override
	{
		logGlobal->error("Cannot apply plain CPack!");
		assert(0);
		return false;
	}
	void applyOnServerAfter(CVCMIServer * srv, void * pack) const override
	{
		logGlobal->error("Cannot apply plain CPack!");
		assert(0);
	}
};

class CVCMIServerPackVisitor : public VCMI_LIB_WRAP_NAMESPACE(ICPackVisitor)
{
private:
	CVCMIServer & handler;
	std::shared_ptr<CGameHandler> gh;

public:
	CVCMIServerPackVisitor(CVCMIServer & handler, std::shared_ptr<CGameHandler> gh)
			:handler(handler), gh(gh)
	{
	}

	virtual bool callTyped() override { return false; }

	virtual void visitForLobby(CPackForLobby & packForLobby) override
	{
		handler.handleReceivedPack(std::unique_ptr<CPackForLobby>(&packForLobby));
	}

	virtual void visitForServer(CPackForServer & serverPack) override
	{
		if (gh)
			gh->handleReceivedPack(&serverPack);
		else
			logNetwork->error("Received pack for game server while in lobby!");
	}

	virtual void visitForClient(CPackForClient & clientPack) override
	{
	}
};

std::string SERVER_NAME_AFFIX = "server";
std::string SERVER_NAME = GameConstants::VCMI_VERSION + std::string(" (") + SERVER_NAME_AFFIX + ')';

CVCMIServer::CVCMIServer(boost::program_options::variables_map & opts)
	: state(EServerState::LOBBY), cmdLineOptions(opts), currentClientId(1), currentPlayerId(1), restartGameplay(false)
{
	uuid = boost::uuids::to_string(boost::uuids::random_generator()());
	logNetwork->trace("CVCMIServer created! UUID: %s", uuid);
	applier = std::make_shared<CApplier<CBaseForServerApply>>();
	registerTypesLobbyPacks(*applier);

	uint16_t port = 3030;
	if(cmdLineOptions.count("port"))
		port = cmdLineOptions["port"].as<uint16_t>();
	logNetwork->info("Port %d will be used", port);

	networkServer = std::make_unique<NetworkServer>(*this);
	networkServer->start(port);
	logNetwork->info("Listening for connections at port %d", port);
}

CVCMIServer::~CVCMIServer() = default;

void CVCMIServer::onNewConnection(const std::shared_ptr<NetworkConnection> & connection)
{
	if (activeConnections.empty())
		establishOutgoingConnection();

	if(state == EServerState::LOBBY)
	{
		activeConnections.push_back(std::make_shared<CConnection>(connection));
		activeConnections.back()->enterLobbyConnectionMode();
	}
	else
	{
		networkServer->closeConnection(connection);
	}
}

void CVCMIServer::onPacketReceived(const std::shared_ptr<NetworkConnection> & connection, const std::vector<uint8_t> & message)
{
	std::shared_ptr<CConnection> c = findConnection(connection);
	auto pack = c->retrievePack(message);
	pack->c = c;
	CVCMIServerPackVisitor visitor(*this, this->gh);
	pack->visit(visitor);
}

void CVCMIServer::onConnectionFailed(const std::string & errorMessage)
{
	//TODO: handle failure to connect to lobby
}

void CVCMIServer::onConnectionEstablished(const std::shared_ptr<NetworkConnection> &)
{
	//TODO: handle connection to lobby - login?
}

void CVCMIServer::setState(EServerState value)
{
	state = value;
}

EServerState CVCMIServer::getState() const
{
	return state;
}

std::shared_ptr<CConnection> CVCMIServer::findConnection(const std::shared_ptr<NetworkConnection> & netConnection)
{
	for (auto const & gameConnection : activeConnections)
	{
		if (gameConnection->isMyConnection(netConnection))
			return gameConnection;
	}

	throw std::runtime_error("Unknown connection received in CVCMIServer::findConnection");
}

void CVCMIServer::run()
{
#if defined(VCMI_ANDROID) && !defined(SINGLE_PROCESS_APP)
	if(!restartGameplay)
	{
		CAndroidVMHelper vmHelper;
		vmHelper.callStaticVoidMethod(CAndroidVMHelper::NATIVE_METHODS_DEFAULT_CLASS, "onServerReady");
	}
#endif

	static const int serverUpdateIntervalMilliseconds = 50;
	auto clockInitial = std::chrono::steady_clock::now();
	int64_t msPassedLast = 0;

	while(state != EServerState::SHUTDOWN)
	{
		networkServer->run(std::chrono::milliseconds(serverUpdateIntervalMilliseconds));

		const auto clockNow = std::chrono::steady_clock::now();
		const auto clockPassed = clockNow - clockInitial;
		const int64_t msPassedNow = std::chrono::duration_cast<std::chrono::milliseconds>(clockPassed).count();
		const int64_t msDelta = msPassedNow - msPassedLast;
		msPassedLast = msPassedNow;

		if (state == EServerState::GAMEPLAY)
			gh->tick(msDelta);
	}
}

void CVCMIServer::onTimer()
{
	// FIXME: move GameHandler updates here
}

void CVCMIServer::establishOutgoingConnection()
{
	if(!cmdLineOptions.count("lobby"))
		return;

	uuid = cmdLineOptions["lobby-uuid"].as<std::string>();
	auto address = cmdLineOptions["lobby"].as<std::string>();
	int port = cmdLineOptions["lobby-port"].as<ui16>();
	logNetwork->info("Establishing connection to remote at %s:%d with uuid %s", address, port, uuid);

	outgoingConnection = std::make_unique<NetworkClient>(*this);

	outgoingConnection->start(address, port);//, SERVER_NAME, uuid);

//	connections.insert(c);
//	remoteConnections.insert(c);
}

void CVCMIServer::prepareToRestart()
{
	if(state == EServerState::GAMEPLAY)
	{
		restartGameplay = true;
		* si = * gh->gs->initialOpts;
		si->seedToBeUsed = si->seedPostInit = 0;
		state = EServerState::LOBBY;
		if (si->campState)
		{
			assert(si->campState->currentScenario().has_value());
			campaignMap = si->campState->currentScenario().value_or(CampaignScenarioID(0));
			campaignBonus = si->campState->getBonusID(campaignMap).value_or(-1);
		}
	}
	
	for(auto c : activeConnections)
		c->enterLobbyConnectionMode();

	boost::unique_lock<boost::recursive_mutex> queueLock(mx);
	gh = nullptr;
}

bool CVCMIServer::prepareToStartGame()
{
	Load::ProgressAccumulator progressTracking;
	Load::Progress current(1);
	progressTracking.include(current);
	auto currentProgress = std::numeric_limits<Load::Type>::max();
	
	auto progressTrackingThread = boost::thread([this, &progressTracking, &currentProgress]()
	{
		while(!progressTracking.finished())
		{
			if(progressTracking.get() != currentProgress)
			{
				//FIXME: UNGUARDED MULTITHREADED ACCESS!!!
				currentProgress = progressTracking.get();
				std::unique_ptr<LobbyLoadProgress> loadProgress(new LobbyLoadProgress);
				loadProgress->progress = currentProgress;
				announcePack(std::move(loadProgress));
			}
			boost::this_thread::sleep(boost::posix_time::milliseconds(50));
		}
	});
	
	gh = std::make_shared<CGameHandler>(this);
	switch(si->mode)
	{
	case StartInfo::CAMPAIGN:
		logNetwork->info("Preparing to start new campaign");
		si->startTimeIso8601 = vstd::getDateTimeISO8601Basic(std::time(nullptr));
		si->fileURI = mi->fileURI;
		si->campState->setCurrentMap(campaignMap);
		si->campState->setCurrentMapBonus(campaignBonus);
		gh->init(si.get(), progressTracking);
		break;

	case StartInfo::NEW_GAME:
		logNetwork->info("Preparing to start new game");
		si->startTimeIso8601 = vstd::getDateTimeISO8601Basic(std::time(nullptr));
		si->fileURI = mi->fileURI;
		gh->init(si.get(), progressTracking);
		break;

	case StartInfo::LOAD_GAME:
		logNetwork->info("Preparing to start loaded game");
		if(!gh->load(si->mapname))
		{
			current.finish();
			progressTrackingThread.join();
			return false;
		}
		break;
	default:
		logNetwork->error("Wrong mode in StartInfo!");
		assert(0);
		break;
	}
	
	current.finish();
	progressTrackingThread.join();
	
	return true;
}

void CVCMIServer::startGameImmediately()
{
	for(auto c : activeConnections)
		c->enterGameplayConnectionMode(gh->gs);

	gh->start(si->mode == StartInfo::LOAD_GAME);
	state = EServerState::GAMEPLAY;
}

void CVCMIServer::onDisconnected(const std::shared_ptr<NetworkConnection> & connection)
{
	logNetwork->error("Network error receiving a pack. Connection has been closed");

	std::shared_ptr<CConnection> c = findConnection(connection);

	inactiveConnections.push_back(c);
	vstd::erase(activeConnections, c);

	if(activeConnections.empty() || hostClientId == c->connectionID)
		state = EServerState::SHUTDOWN;

	if(gh && state == EServerState::GAMEPLAY)
	{
		gh->handleClientDisconnection(c);
	}

	boost::unique_lock<boost::recursive_mutex> queueLock(mx);

//	if(c->connected)
//	{
//		auto lcd = std::make_unique<LobbyClientDisconnected>();
//		lcd->c = c;
//		lcd->clientId = c->connectionID;
//		handleReceivedPack(std::move(lcd));
//	}
//
//	logNetwork->info("Thread listening for %s ended", c->toString());
}

void CVCMIServer::handleReceivedPack(std::unique_ptr<CPackForLobby> pack)
{
	CBaseForServerApply * apply = applier->getApplier(CTypeList::getInstance().getTypeID(pack.get()));
	if(apply->applyOnServerBefore(this, pack.get()))
		announcePack(std::move(pack));
}

void CVCMIServer::announcePack(std::unique_ptr<CPackForLobby> pack)
{
	for(auto c : activeConnections)
	{
		// FIXME: we need to avoid sending something to client that not yet get answer for LobbyClientConnected
		// Until UUID set we only pass LobbyClientConnected to this client
		//if(c->uuid == uuid && !dynamic_cast<LobbyClientConnected *>(pack.get()))
		//	continue;

		c->sendPack(pack.get());
	}

	applier->getApplier(CTypeList::getInstance().getTypeID(pack.get()))->applyOnServerAfter(this, pack.get());
}

void CVCMIServer::announceMessage(const std::string & txt)
{
	logNetwork->info("Show message: %s", txt);
	auto cm = std::make_unique<LobbyShowMessage>();
	cm->message = txt;
	announcePack(std::move(cm));
}

void CVCMIServer::announceTxt(const std::string & txt, const std::string & playerName)
{
	logNetwork->info("%s says: %s", playerName, txt);
	auto cm = std::make_unique<LobbyChatMessage>();
	cm->playerName = playerName;
	cm->message = txt;
	announcePack(std::move(cm));
}

bool CVCMIServer::passHost(int toConnectionId)
{
	for(auto c : activeConnections)
	{
		if(isClientHost(c->connectionID))
			continue;
		if(c->connectionID != toConnectionId)
			continue;

		hostClientId = c->connectionID;
		announceTxt(boost::str(boost::format("Pass host to connection %d") % toConnectionId));
		return true;
	}
	return false;
}

void CVCMIServer::clientConnected(std::shared_ptr<CConnection> c, std::vector<std::string> & names, std::string uuid, StartInfo::EMode mode)
{
	if(state != EServerState::LOBBY)
		throw std::runtime_error("CVCMIServer::clientConnected called while game is not accepting clients!");

	c->connectionID = currentClientId++;

	if(hostClientId == -1)
	{
		hostClientId = c->connectionID;
		si->mode = mode;
	}

	logNetwork->info("Connection with client %d established. UUID: %s", c->connectionID, c->uuid);
	
	for(auto & name : names)
	{
		logNetwork->info("Client %d player: %s", c->connectionID, name);
		ui8 id = currentPlayerId++;

		ClientPlayer cp;
		cp.connection = c->connectionID;
		cp.name = name;
		playerNames.insert(std::make_pair(id, cp));
		announceTxt(boost::str(boost::format("%s (pid %d cid %d) joins the game") % name % id % c->connectionID));

		//put new player in first slot with AI
		for(auto & elem : si->playerInfos)
		{
			if(elem.second.isControlledByAI() && !elem.second.compOnly)
			{
				setPlayerConnectedId(elem.second, id);
				break;
			}
		}
	}
}

void CVCMIServer::clientDisconnected(std::shared_ptr<CConnection> c)
{
	vstd::erase(activeConnections, c);

	if(activeConnections.empty() || hostClientId == c->connectionID)
	{
		state = EServerState::SHUTDOWN;
		return;
	}
	
	PlayerReinitInterface startAiPack;
	startAiPack.playerConnectionId = PlayerSettings::PLAYER_AI;
	
	for(auto it = playerNames.begin(); it != playerNames.end();)
	{
		if(it->second.connection != c->connectionID)
		{
			++it;
			continue;
		}

		int id = it->first;
		std::string playerLeftMsgText = boost::str(boost::format("%s (pid %d cid %d) left the game") % id % playerNames[id].name % c->connectionID);
		announceTxt(playerLeftMsgText); //send lobby text, it will be ignored for non-lobby clients
		auto * playerSettings = si->getPlayersSettings(id);
		if(!playerSettings)
		{
			++it;
			continue;
		}
		
		it = playerNames.erase(it);
		setPlayerConnectedId(*playerSettings, PlayerSettings::PLAYER_AI);
		
		if(gh && si && state == EServerState::GAMEPLAY)
		{
			gh->playerMessages->broadcastMessage(playerSettings->color, playerLeftMsgText);
	//		gh->connections[playerSettings->color].insert(hostClient);
			startAiPack.players.push_back(playerSettings->color);
		}
	}
	
	if(!startAiPack.players.empty())
		gh->sendAndApply(&startAiPack);
}

void CVCMIServer::reconnectPlayer(int connId)
{
	PlayerReinitInterface startAiPack;
	startAiPack.playerConnectionId = connId;
	
	if(gh && si && state == EServerState::GAMEPLAY)
	{
		for(auto it = playerNames.begin(); it != playerNames.end(); ++it)
		{
			if(it->second.connection != connId)
				continue;
			
			int id = it->first;
			auto * playerSettings = si->getPlayersSettings(id);
			if(!playerSettings)
				continue;
			
			std::string messageText = boost::str(boost::format("%s (cid %d) is connected") % playerSettings->name % connId);
			gh->playerMessages->broadcastMessage(playerSettings->color, messageText);
			
			startAiPack.players.push_back(playerSettings->color);
		}

		if(!startAiPack.players.empty())
			gh->sendAndApply(&startAiPack);
	}
}

void CVCMIServer::setPlayerConnectedId(PlayerSettings & pset, ui8 player) const
{
	if(vstd::contains(playerNames, player))
		pset.name = playerNames.find(player)->second.name;
	else
		pset.name = VLC->generaltexth->allTexts[468]; //Computer

	pset.connectedPlayerIDs.clear();
	if(player != PlayerSettings::PLAYER_AI)
		pset.connectedPlayerIDs.insert(player);
}

void CVCMIServer::updateStartInfoOnMapChange(std::shared_ptr<CMapInfo> mapInfo, std::shared_ptr<CMapGenOptions> mapGenOpts)
{
	mi = mapInfo;
	if(!mi)
		return;

	auto namesIt = playerNames.cbegin();
	si->playerInfos.clear();
	if(mi->scenarioOptionsOfSave)
	{
		si = CMemorySerializer::deepCopy(*mi->scenarioOptionsOfSave);
		si->mode = StartInfo::LOAD_GAME;
		if(si->campState)
			campaignMap = si->campState->currentScenario().value();

		for(auto & ps : si->playerInfos)
		{
			if(!ps.second.compOnly && ps.second.connectedPlayerIDs.size() && namesIt != playerNames.cend())
			{
				setPlayerConnectedId(ps.second, namesIt++->first);
			}
			else
			{
				setPlayerConnectedId(ps.second, PlayerSettings::PLAYER_AI);
			}
		}
	}
	else if(si->mode == StartInfo::NEW_GAME || si->mode == StartInfo::CAMPAIGN)
	{
		if(mi->campaign)
			return;

		for(int i = 0; i < mi->mapHeader->players.size(); i++)
		{
			const PlayerInfo & pinfo = mi->mapHeader->players[i];

			//neither computer nor human can play - no player
			if(!(pinfo.canHumanPlay || pinfo.canComputerPlay))
				continue;

			PlayerSettings & pset = si->playerInfos[PlayerColor(i)];
			pset.color = PlayerColor(i);
			if(pinfo.canHumanPlay && namesIt != playerNames.cend())
			{
				setPlayerConnectedId(pset, namesIt++->first);
			}
			else
			{
				setPlayerConnectedId(pset, PlayerSettings::PLAYER_AI);
				if(!pinfo.canHumanPlay)
				{
					pset.compOnly = true;
				}
			}

			pset.castle = pinfo.defaultCastle();
			pset.hero = pinfo.defaultHero();

			if(pset.hero != HeroTypeID::RANDOM && pinfo.hasCustomMainHero())
			{
				pset.hero = pinfo.mainCustomHeroId;
				pset.heroNameTextId = pinfo.mainCustomHeroNameTextId;
				pset.heroPortrait = pinfo.mainCustomHeroPortrait;
			}

			pset.handicap = PlayerSettings::NO_HANDICAP;
		}

		if(mi->isRandomMap && mapGenOpts)
			si->mapGenOptions = std::shared_ptr<CMapGenOptions>(mapGenOpts);
		else
			si->mapGenOptions.reset();
	}
	si->mapname = mi->fileURI;
}

void CVCMIServer::updateAndPropagateLobbyState()
{
	// Update player settings for RMG
	// TODO: find appropriate location for this code
	if(si->mapGenOptions && si->mode == StartInfo::NEW_GAME)
	{
		for(const auto & psetPair : si->playerInfos)
		{
			const auto & pset = psetPair.second;
			si->mapGenOptions->setStartingTownForPlayer(pset.color, pset.castle);
			si->mapGenOptions->setStartingHeroForPlayer(pset.color, pset.hero);
			if(pset.isControlledByHuman())
			{
				si->mapGenOptions->setPlayerTypeForStandardPlayer(pset.color, EPlayerType::HUMAN);
			}
		}
	}

	auto lus = std::make_unique<LobbyUpdateState>();
	lus->state = *this;
	announcePack(std::move(lus));
}

void CVCMIServer::setPlayer(PlayerColor clickedColor)
{
	struct PlayerToRestore
	{
		PlayerColor color;
		int id;
		void reset() { id = -1; color = PlayerColor::CANNOT_DETERMINE; }
		PlayerToRestore(){ reset(); }
	} playerToRestore;


	PlayerSettings & clicked = si->playerInfos[clickedColor];

	//identify clicked player
	int clickedNameID = 0; //number of player - zero means AI, assume it initially
	if(clicked.isControlledByHuman())
		clickedNameID = *(clicked.connectedPlayerIDs.begin()); //if not AI - set appropiate ID

	if(clickedNameID > 0 && playerToRestore.id == clickedNameID) //player to restore is about to being replaced -> put him back to the old place
	{
		PlayerSettings & restPos = si->playerInfos[playerToRestore.color];
		setPlayerConnectedId(restPos, playerToRestore.id);
		playerToRestore.reset();
	}

	int newPlayer; //which player will take clicked position

	//who will be put here?
	if(!clickedNameID) //AI player clicked -> if possible replace computer with unallocated player
	{
		newPlayer = getIdOfFirstUnallocatedPlayer();
		if(!newPlayer) //no "free" player -> get just first one
			newPlayer = playerNames.begin()->first;
	}
	else //human clicked -> take next
	{
		auto i = playerNames.find(clickedNameID); //clicked one
		i++; //player AFTER clicked one

		if(i != playerNames.end())
			newPlayer = i->first;
		else
			newPlayer = 0; //AI if we scrolled through all players
	}

	setPlayerConnectedId(clicked, newPlayer); //put player

	//if that player was somewhere else, we need to replace him with computer
	if(newPlayer) //not AI
	{
		for(auto i = si->playerInfos.begin(); i != si->playerInfos.end(); i++)
		{
			int curNameID = *(i->second.connectedPlayerIDs.begin());
			if(i->first != clickedColor && curNameID == newPlayer)
			{
				assert(i->second.connectedPlayerIDs.size());
				playerToRestore.color = i->first;
				playerToRestore.id = newPlayer;
				setPlayerConnectedId(i->second, PlayerSettings::PLAYER_AI); //set computer
				break;
			}
		}
	}
}

void CVCMIServer::setPlayerName(PlayerColor color, std::string name)
{
	if(color == PlayerColor::CANNOT_DETERMINE)
		return;

	PlayerSettings & player = si->playerInfos.at(color);

	if(!player.isControlledByHuman())
		return;

	if(player.connectedPlayerIDs.empty())
		return;

	int nameID = *(player.connectedPlayerIDs.begin()); //if not AI - set appropiate ID

	playerNames[nameID].name = name;
	setPlayerConnectedId(player, nameID);
}

void CVCMIServer::optionNextCastle(PlayerColor player, int dir)
{
	PlayerSettings & s = si->playerInfos[player];
	FactionID & cur = s.castle;
	auto & allowed = getPlayerInfo(player).allowedFactions;
	const bool allowRandomTown = getPlayerInfo(player).isFactionRandom;

	if(cur == FactionID::NONE) //no change
		return;

	if(cur == FactionID::RANDOM) //first/last available
	{
		if(dir > 0)
			cur = *allowed.begin(); //id of first town
		else
			cur = *allowed.rbegin(); //id of last town

	}
	else // next/previous available
	{
		if((cur == *allowed.begin() && dir < 0) || (cur == *allowed.rbegin() && dir > 0))
		{
			if(allowRandomTown)
			{
				cur = FactionID::RANDOM;
			}
			else
			{
				if(dir > 0)
					cur = *allowed.begin();
				else
					cur = *allowed.rbegin();
			}
		}
		else
		{
			assert(dir >= -1 && dir <= 1); //othervice std::advance may go out of range
			auto iter = allowed.find(cur);
			std::advance(iter, dir);
			cur = *iter;
		}
	}

	if(s.hero.isValid() && !getPlayerInfo(player).hasCustomMainHero()) // remove hero unless it set to fixed one in map editor
	{
		s.hero = HeroTypeID::RANDOM;
	}
	if(!cur.isValid() && s.bonus == PlayerStartingBonus::RESOURCE)
		s.bonus = PlayerStartingBonus::RANDOM;
}

void CVCMIServer::optionSetCastle(PlayerColor player, FactionID id)
{
	PlayerSettings & s = si->playerInfos[player];
	FactionID & cur = s.castle;
	auto & allowed = getPlayerInfo(player).allowedFactions;

	if(cur == FactionID::NONE) //no change
		return;

	if(allowed.find(id) == allowed.end() && id != FactionID::RANDOM) // valid id
		return;

	cur = static_cast<FactionID>(id);

	if(s.hero.isValid() && !getPlayerInfo(player).hasCustomMainHero()) // remove hero unless it set to fixed one in map editor
	{
		s.hero = HeroTypeID::RANDOM;
	}
	if(!cur.isValid() && s.bonus == PlayerStartingBonus::RESOURCE)
		s.bonus = PlayerStartingBonus::RANDOM;
}

void CVCMIServer::setCampaignMap(CampaignScenarioID mapId)
{
	campaignMap = mapId;
	si->difficulty = si->campState->scenario(mapId).difficulty;
	campaignBonus = -1;
	updateStartInfoOnMapChange(si->campState->getMapInfo(mapId));
}

void CVCMIServer::setCampaignBonus(int bonusId)
{
	campaignBonus = bonusId;

	const CampaignScenario & scenario = si->campState->scenario(campaignMap);
	const std::vector<CampaignBonus> & bonDescs = scenario.travelOptions.bonusesToChoose;
	if(bonDescs[bonusId].type == CampaignBonusType::HERO)
	{
		for(auto & elem : si->playerInfos)
		{
			if(elem.first == PlayerColor(bonDescs[bonusId].info1))
				setPlayerConnectedId(elem.second, 1);
			else
				setPlayerConnectedId(elem.second, PlayerSettings::PLAYER_AI);
		}
	}
}

void CVCMIServer::optionNextHero(PlayerColor player, int dir)
{
	PlayerSettings & s = si->playerInfos[player];
	if(!s.castle.isValid() || s.hero == HeroTypeID::NONE)
		return;

	if(s.hero == HeroTypeID::RANDOM) // first/last available
	{
		if (dir > 0)
			s.hero = nextAllowedHero(player, HeroTypeID(-1), dir);
		else
			s.hero = nextAllowedHero(player, HeroTypeID(VLC->heroh->size()), dir);
	}
	else
	{
		s.hero = nextAllowedHero(player, s.hero, dir);
	}
}

void CVCMIServer::optionSetHero(PlayerColor player, HeroTypeID id)
{
	PlayerSettings & s = si->playerInfos[player];
	if(!s.castle.isValid() || s.hero == HeroTypeID::NONE)
		return;

	if(id == HeroTypeID::RANDOM)
	{
		s.hero = HeroTypeID::RANDOM;
	}
	if(canUseThisHero(player, id))
		s.hero = static_cast<HeroTypeID>(id);
}

HeroTypeID CVCMIServer::nextAllowedHero(PlayerColor player, HeroTypeID initial, int direction)
{
	HeroTypeID first(initial.getNum() + direction);

	if(direction > 0)
	{
		for (auto i = first; i.getNum() < VLC->heroh->size(); ++i)
			if(canUseThisHero(player, i))
				return i;
	}
	else
	{
		for (auto i = first; i.getNum() >= 0; --i)
			if(canUseThisHero(player, i))
				return i;
	}
	return HeroTypeID::RANDOM;
}

void CVCMIServer::optionNextBonus(PlayerColor player, int dir)
{
	PlayerSettings & s = si->playerInfos[player];
	PlayerStartingBonus & ret = s.bonus = static_cast<PlayerStartingBonus>(static_cast<int>(s.bonus) + dir);

	if(s.hero == HeroTypeID::NONE &&
		!getPlayerInfo(player).heroesNames.size() &&
		ret == PlayerStartingBonus::ARTIFACT) //no hero - can't be artifact
	{
		if(dir < 0)
			ret = PlayerStartingBonus::RANDOM;
		else
			ret = PlayerStartingBonus::GOLD;
	}

	if(ret > PlayerStartingBonus::RESOURCE)
		ret = PlayerStartingBonus::RANDOM;
	if(ret < PlayerStartingBonus::RANDOM)
		ret = PlayerStartingBonus::RESOURCE;

	if(s.castle == FactionID::RANDOM && ret == PlayerStartingBonus::RESOURCE) //random castle - can't be resource
	{
		if(dir < 0)
			ret = PlayerStartingBonus::GOLD;
		else
			ret = PlayerStartingBonus::RANDOM;
	}
}

void CVCMIServer::optionSetBonus(PlayerColor player, PlayerStartingBonus id)
{
	PlayerSettings & s = si->playerInfos[player];

	if(s.hero == HeroTypeID::NONE &&
		!getPlayerInfo(player).heroesNames.size() &&
		id == PlayerStartingBonus::ARTIFACT) //no hero - can't be artifact
			return;

	if(id > PlayerStartingBonus::RESOURCE)
		return;
	if(id < PlayerStartingBonus::RANDOM)
		return;

	if(s.castle == FactionID::RANDOM && id == PlayerStartingBonus::RESOURCE) //random castle - can't be resource
		return;

	s.bonus = id;
}

bool CVCMIServer::canUseThisHero(PlayerColor player, HeroTypeID ID)
{
	return VLC->heroh->size() > ID
		&& si->playerInfos[player].castle == VLC->heroh->objects[ID]->heroClass->faction
		&& !vstd::contains(getUsedHeroes(), ID)
		&& mi->mapHeader->allowedHeroes.count(ID);
}

std::vector<HeroTypeID> CVCMIServer::getUsedHeroes()
{
	std::vector<HeroTypeID> heroIds;
	for(auto & p : si->playerInfos)
	{
		const auto & heroes = getPlayerInfo(p.first).heroesNames;
		for(auto & hero : heroes)
			if(hero.heroId >= 0) //in VCMI map format heroId = -1 means random hero
				heroIds.push_back(hero.heroId);

		if(p.second.hero != HeroTypeID::RANDOM)
			heroIds.push_back(p.second.hero);
	}
	return heroIds;
}

ui8 CVCMIServer::getIdOfFirstUnallocatedPlayer() const
{
	for(auto i = playerNames.cbegin(); i != playerNames.cend(); i++)
	{
		if(!si->getPlayersSettings(i->first))
			return i->first;
	}

	return 0;
}

static void handleCommandOptions(int argc, const char * argv[], boost::program_options::variables_map & options)
{
	namespace po = boost::program_options;
	po::options_description opts("Allowed options");
	opts.add_options()
	("help,h", "display help and exit")
	("version,v", "display version information and exit")
	("run-by-client", "indicate that server launched by client on same machine")
	("uuid", po::value<std::string>(), "")
	("port", po::value<ui16>(), "port at which server will listen to connections from client")
	("lobby", po::value<std::string>(), "address to remote lobby")
	("lobby-port", po::value<ui16>(), "port at which server connect to remote lobby")
	("lobby-uuid", po::value<std::string>(), "")
	("connections", po::value<ui16>(), "amount of connections to remote lobby");

	if(argc > 1)
	{
		try
		{
			po::store(po::parse_command_line(argc, argv, opts), options);
		}
		catch(boost::program_options::error & e)
		{
			std::cerr << "Failure during parsing command-line options:\n" << e.what() << std::endl;
		}
	}
	
#ifdef SINGLE_PROCESS_APP
	options.emplace("run-by-client", po::variable_value{true, true});
#endif

	po::notify(options);

#ifndef SINGLE_PROCESS_APP
	if(options.count("help"))
	{
		auto time = std::time(nullptr);
		printf("%s - A Heroes of Might and Magic 3 clone\n", GameConstants::VCMI_VERSION.c_str());
		printf("Copyright (C) 2007-%d VCMI dev team - see AUTHORS file\n", std::localtime(&time)->tm_year + 1900);
		printf("This is free software; see the source for copying conditions. There is NO\n");
		printf("warranty; not even for MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.\n");
		printf("\n");
		std::cout << opts;
		exit(0);
	}

	if(options.count("version"))
	{
		printf("%s\n", GameConstants::VCMI_VERSION.c_str());
		std::cout << VCMIDirs::get().genHelpString();
		exit(0);
	}
#endif
}

#ifdef SINGLE_PROCESS_APP
#define main server_main
#endif

#if VCMI_ANDROID_DUAL_PROCESS
void CVCMIServer::create()
{
	const int argc = 1;
	const char * argv[argc] = { "android-server" };
#else
int main(int argc, const char * argv[])
{
#endif

#if !defined(VCMI_ANDROID) && !defined(SINGLE_PROCESS_APP)
	// Correct working dir executable folder (not bundle folder) so we can use executable relative paths
	boost::filesystem::current_path(boost::filesystem::system_complete(argv[0]).parent_path());
#endif

#ifndef VCMI_IOS
	console = new CConsoleHandler();
#endif
	CBasicLogConfigurator logConfig(VCMIDirs::get().userLogsPath() / "VCMI_Server_log.txt", console);
	logConfig.configureDefault();
	logGlobal->info(SERVER_NAME);

	boost::program_options::variables_map opts;
	handleCommandOptions(argc, argv, opts);
	preinitDLL(console, false);
	logConfig.configure();

	loadDLLClasses();
	srand((ui32)time(nullptr));

	CVCMIServer server(opts);

#ifdef SINGLE_PROCESS_APP
	boost::condition_variable * cond = reinterpret_cast<boost::condition_variable *>(const_cast<char *>(argv[0]));
	cond->notify_one();
#endif

	server.run();

#if VCMI_ANDROID_DUAL_PROCESS
	CAndroidVMHelper envHelper;
	envHelper.callStaticVoidMethod(CAndroidVMHelper::NATIVE_METHODS_DEFAULT_CLASS, "killServer");
#endif
	logConfig.deconfigure();
	vstd::clear_pointer(VLC);

#if !VCMI_ANDROID_DUAL_PROCESS
	return 0;
#endif
}

#if VCMI_ANDROID_DUAL_PROCESS
extern "C" JNIEXPORT void JNICALL Java_eu_vcmi_vcmi_NativeMethods_createServer(JNIEnv * env, jclass cls)
{
	__android_log_write(ANDROID_LOG_INFO, "VCMI", "Got jni call to init server");
	CAndroidVMHelper::cacheVM(env);

	CVCMIServer::create();
}

extern "C" JNIEXPORT void JNICALL Java_eu_vcmi_vcmi_NativeMethods_initClassloader(JNIEnv * baseEnv, jclass cls)
{
	CAndroidVMHelper::initClassloader(baseEnv);
}
#elif defined(SINGLE_PROCESS_APP)
void CVCMIServer::create(boost::condition_variable * cond, const std::vector<std::string> & args)
{
	std::vector<const void *> argv = {cond};
	for(auto & a : args)
		argv.push_back(a.c_str());
	main(argv.size(), reinterpret_cast<const char **>(&*argv.begin()));
}

#ifdef VCMI_ANDROID
void CVCMIServer::reuseClientJNIEnv(void * jniEnv)
{
	CAndroidVMHelper::initClassloader(jniEnv);
	CAndroidVMHelper::alwaysUseLoadedClass = true;
}
#endif // VCMI_ANDROID
#endif // VCMI_ANDROID_DUAL_PROCESS
