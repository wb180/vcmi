/*
 * Connection.h, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 *
 */
#pragma once

VCMI_LIB_NAMESPACE_BEGIN

class BinaryDeserializer;
class BinarySerializer;
struct CPack;
class INetworkConnection;
class ConnectionPackReader;
class ConnectionPackWriter;
class CGameState;
class IGameCallback;

/// Wrapper class for game connection
/// Handles serialization and deserialization of data received from network
class DLL_LINKAGE CConnection : boost::noncopyable
{
	/// Non-owning pointer to underlying connection
	std::weak_ptr<INetworkConnection> networkConnection;

	std::unique_ptr<ConnectionPackReader> packReader;
	std::unique_ptr<ConnectionPackWriter> packWriter;
	std::unique_ptr<BinaryDeserializer> deserializer;
	std::unique_ptr<BinarySerializer> serializer;

	void disableStackSendingByID();
	void enableStackSendingByID();
	void disableSmartPointerSerialization();
	void enableSmartPointerSerialization();
	void disableSmartVectorMemberSerialization();
	void enableSmartVectorMemberSerializatoin();

public:
	bool isMyConnection(const std::shared_ptr<INetworkConnection> & otherConnection) const;

	std::string uuid;
	int connectionID;

	CConnection(std::weak_ptr<INetworkConnection> networkConnection);
	~CConnection();

	void sendPack(const CPack * pack);
	CPack * retrievePack(const std::vector<uint8_t> & data);

	void enterLobbyConnectionMode();
	void setCallback(IGameCallback * cb);
	void enterGameplayConnectionMode(CGameState * gs);
};

VCMI_LIB_NAMESPACE_END
