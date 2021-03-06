/*
 *  libdoclone - library for cloning GNU/Linux systems
 *  Copyright (C) 2015 Joan Lledó <joanlluislledo@gmail.com>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 3 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#ifndef NETNODE_H_
#define NETNODE_H_

#include <arpa/inet.h>

#include <string>

#include <doclone/Node.h>
#include <doclone/exception/Exception.h>

namespace Doclone {

/**
 * \typedef dcPort
 *
 * A network port
 */
typedef uint16_t dcPort;

/**
 * \var PORT_PING
 *
 * UDP port used in the link mode, 7772
 */
const dcPort PORT_PING = 7772;

/**
 * \var PORT_DATA
 *
 * TCP port used to transfer the data
 */
const dcPort PORT_DATA = 7773;

/**
 * \typedef dcGroup
 *
 * An ip address
 */
typedef char dcGroup[10];

/**
 * \var MULTICAST_GROUP
 *
 * Ip address of the udp multicast group. Its value is "225.0.1.2"
 */
const dcGroup MULTICAST_GROUP="225.0.1.2";

/**
 * \typedef dcNum
 *
 * Number of nodes of the chain
 */
typedef uint16_t dcNum;

/**
 * \var LINKS_NUM
 *
 * Maximum Number of links in the chain of the link mode. 64 by default.
 */
const dcNum LINKS_NUM = 64;

/**
 * \typedef dcCommand
 *
 * Codes for network coordination.
 *
 * This codes will be sent between clients & servers for coordinate the job.
 *
 * The codes are:
 * C_LINK_SERVER_OK = 1 << 0;
 * C_LINK_CLIENT_OK = 1 << 1;
 * C_NEXT_LINK_IP = 1 << 2;
 */
typedef uint8_t dcCommand;

/**
 * \var C_LINK_SERVER_OK
 *
 * The link server is ready to work.
 */
const dcCommand C_LINK_SERVER_OK = 1 << 0;

/**
 * \var C_LINK_CLIENT_OK
 *
 * The link client is ready to work.
 */
const dcCommand C_LINK_CLIENT_OK = 1 << 1;

/**
 * \var C_NEXT_LINK_IP
 *
 * The next data of the link server will be the next IP of the chain.
 */
const dcCommand C_NEXT_LINK_IP = 1 << 2;

/**
 * \var C_SERVER_OK
 *
 * The Unicast/Multicast server is ready to work.
 */
const dcCommand C_SERVER_OK = 1 << 3;

/**
 * \var C_RECEIVER_OK
 *
 * The Unicast/Multicast client is ready to work.
 */
const dcCommand C_RECEIVER_OK = 1 << 4;

/**
 * \class NetNode
 * \brief Common methods and attributes for all network nodes
 * \date May, 2015
 */
class NetNode : public Node {
public:
	NetNode() : _srcIP() {}
	virtual ~NetNode() {}

	virtual void send() throw(Exception) = 0;
	virtual void receive() throw(Exception) = 0;

protected:
	virtual void closeConnection() throw(Exception) = 0;

	virtual void sendFromImage() throw(Exception) = 0;
	virtual void sendFromDevice() throw(Exception) = 0;

	virtual void receiveToImage() throw(Exception) = 0;
	virtual void receiveToDevice() throw(Exception) = 0;

	/// Human readable server's IP (only for clients)
	std::string _srcIP;
};

}

#endif /* NETNODE_H_ */
