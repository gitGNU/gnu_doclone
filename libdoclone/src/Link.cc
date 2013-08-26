/*
 *  libdoclone - library for cloning GNU/Linux systems
 *  Copyright (C) 2013 Joan Lledó <joanlluislledo@gmail.com>
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

#include <doclone/Link.h>
#include <doclone/Clone.h>
#include <doclone/Logger.h>
#include <doclone/Operation.h>
#include <doclone/PartedDevice.h>
#include <doclone/DataTransfer.h>
#include <doclone/Util.h>
#include <doclone/Image.h>
#include <doclone/DiskLabel.h>
#include <doclone/DlFactory.h>

#include <doclone/exception/Exception.h>
#include <doclone/exception/CancelException.h>
#include <doclone/exception/ConnectionException.h>
#include <doclone/exception/ReadDataException.h>
#include <doclone/exception/WriteDataException.h>
#include <doclone/exception/ReceiveDataException.h>
#include <doclone/exception/SendDataException.h>
#include <doclone/exception/NoBlockDeviceException.h>
#include <doclone/exception/CreateImageException.h>
#include <doclone/exception/RestoreImageException.h>

#include <netdb.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <endian.h>

namespace Doclone {

/**
 * \brief Establishes a communication with the sender via UDP to inform it
 * that this node is available to receive data.
 *
 * This function is executed in each link and communicates with the function
 * netScan of the server.
 *
 * \return The IP of the next link in integer format
 */
int Link::answer() const throw(Exception) {
	Logger *log = Logger::getInstance();
	log->debug("Link::answer() start");
	
	int sock_udp;
	uint32_t next_link;
	sockaddr_in udp = {};
	socklen_t addrlen = sizeof (sockaddr);

	if ((sock_udp = socket (AF_INET, SOCK_DGRAM, 0)) < 0) {
		ConnectionException ex;
		throw ex;
	}

	udp.sin_family = AF_INET;
	udp.sin_port = htons (Doclone::PORT_PING);
	udp.sin_addr.s_addr = htonl(INADDR_ANY);

	if ((bind (sock_udp, reinterpret_cast<sockaddr*>(&udp), addrlen)) < 0) {
		ConnectionException ex;
		throw ex;
	}
	
	u_int loop = 0;
	setsockopt(sock_udp, IPPROTO_IP, IP_MULTICAST_LOOP, &loop, sizeof(loop));

	// We join the broadcast group
	ip_mreq mReq;
	mReq.imr_multiaddr.s_addr = inet_addr (Doclone::MULTICAST_GROUP);
	mReq.imr_interface.s_addr = htonl(INADDR_ANY);

	setsockopt (sock_udp, IPPROTO_IP, IP_ADD_MEMBERSHIP, &mReq, sizeof(mReq));

	dcCommand srvRequest=0;

	while(1) {
		if ((recvfrom (sock_udp, &srvRequest, sizeof(srvRequest), 0,
				reinterpret_cast<sockaddr*>(&udp), &addrlen) < 0)) {
			ConnectionException ex;
			ex.logMsg();
			throw ex;
		}

		if(srvRequest & Doclone::C_LINK_SERVER_OK) {
			break;
		}
	}

	// Leaving the broadcast group
	setsockopt (sock_udp, IPPROTO_IP, IP_DROP_MEMBERSHIP, &mReq, sizeof(mReq));

	dcCommand response = Doclone::C_LINK_CLIENT_OK;
	if ((sendto (sock_udp, &response, sizeof(response), 0,
			reinterpret_cast<sockaddr*>(&udp), addrlen)) < 0) {
		ConnectionException ex;
		throw ex;
	}

	while(1) {
		dcCommand srvCommand = 0;
		if ((recvfrom (sock_udp, &srvCommand, sizeof(srvCommand), 0,
				reinterpret_cast<sockaddr*>(&udp), &addrlen) < 0)) {
			ConnectionException ex;
			ex.logMsg();
			throw ex;
		}

		if(srvCommand & Doclone::C_NEXT_LINK_IP) {
			break;
		}
	}

	if ((recvfrom
		 (sock_udp, &next_link, sizeof (next_link), 0,
				 reinterpret_cast<sockaddr*>(&udp), &addrlen)) < 0) {
		ConnectionException ex;
		throw ex;
	}

	close(sock_udp);

	// Set the correct byte order
	next_link = ntohl(next_link);

	log->debug("Link::answer(next_link=>%d) end", next_link);
	return next_link;
}

/**
 * \brief Sends a signal to the net via UDP broadcasting and waits for the
 * answers of the links.
 *
 * This function communicates with the function answer of the links.
 *
 * \return The IP address of the first link in the chain.
 */
int Link::netScan() const throw(Exception) {
	Logger *log = Logger::getInstance();
	log->debug("Link::netScan() start");
	
	int sock_udp;
	fd_set readSet;
	in_addr_t links[this->_linksNum];
	sockaddr_in udp = {};
	timeval timeout = { 3, 0 };
	socklen_t addrlen = sizeof (sockaddr);

	if ((sock_udp = socket (AF_INET, SOCK_DGRAM, 0)) < 0) {
		ConnectionException ex;
		throw ex;
	}

	udp.sin_family = AF_INET;
	udp.sin_port = htons (Doclone::PORT_PING);
	udp.sin_addr.s_addr = inet_addr (Doclone::MULTICAST_GROUP);

	dcCommand request = Doclone::C_LINK_SERVER_OK;
	if ((sendto (sock_udp, &request, sizeof(request), 0,
			reinterpret_cast<sockaddr*>(&udp), addrlen)) < 0) {
		ConnectionException ex;
		throw ex;
	}

	FD_ZERO (&readSet);
	FD_SET (sock_udp, &readSet);
	
	unsigned int i = 0;
	while (select (sock_udp + 1, &readSet, 0, 0, &timeout) > 0) {
		if(FD_ISSET(sock_udp, &readSet)) {
			sockaddr_in tmpSock = {};
			tmpSock.sin_family = AF_INET;
			tmpSock.sin_port = htons (Doclone::PORT_PING);
			tmpSock.sin_addr.s_addr = htonl (INADDR_ANY);

			if (i <= this->_linksNum - 1) {
				dcCommand response = 0;
				recvfrom (sock_udp, &response, sizeof(response), 0,
						reinterpret_cast<sockaddr*>(&tmpSock), &addrlen);

				if(!(response & Doclone::C_LINK_CLIENT_OK)) {
					continue;
				}

				links[i] = tmpSock.sin_addr.s_addr;
			}
			else {
				continue;
			}
			links[i + 1] = 0;
			i++;
		}
	}

	if (!i) {
		ConnectionException ex;
		throw ex;
	}

	for (unsigned int j = 0; j < i; j++) {
		udp.sin_addr.s_addr = links[j];
		uint32_t next_link = htonl(links[j + 1]);

		dcCommand command = Doclone::C_NEXT_LINK_IP;
		if ((sendto (sock_udp, &command, sizeof(command), 0,
				reinterpret_cast<sockaddr*>(&udp), addrlen)) < 0) {
			ConnectionException ex;
			throw ex;
		}

		if ((sendto
			 (sock_udp, &next_link, sizeof (links[j + 1]), 0,
					 reinterpret_cast<sockaddr*>(&udp), addrlen)) < 0) {
			ConnectionException ex;
			ex.logMsg();
			throw ex;
		}
	}

	close(sock_udp);

	log->debug("Link::netScan(links[0]=>%d) end", links[0]);
	return links[0];
}

/**
 * \brief Starts a server to send data via TCP to all receivers, using the
 * link mode.
 */
void Link::linkServer() throw(Exception) {
	Logger *log = Logger::getInstance();
	log->debug("Link::linkServer() start");
	
	sockaddr_in host_receiver;
	socklen_t size = sizeof (sockaddr);
	DataTransfer *trns = DataTransfer::getInstance();

	host_receiver.sin_family = AF_INET;
	host_receiver.sin_port = htons (Doclone::PORT_DATA);
	host_receiver.sin_addr.s_addr = this->netScan();

	int fd;
	if ((fd = socket (AF_INET, SOCK_STREAM, 0)) < 0) {
		ConnectionException ex;
		throw ex;
	}
	sleep (1);

	if ((connect (fd, reinterpret_cast<sockaddr*>(&host_receiver), size)) < 0) {
		ConnectionException ex;
		throw ex;
	}
	
	trns->setFdd(fd, inet_ntoa (host_receiver.sin_addr));

	log->debug("Link::linkServer() end");
}

/**
 * \brief Starts a client that will receive data via TCP, using the
 * link mode.
 */
void Link::linkClient() throw(Exception) {
	Logger *log = Logger::getInstance();
	log->debug("Link::linkClient() start");
	
	int fdo;
	int ip_next_link;
	int sock_sender;
	sockaddr_in host_sender;
	sockaddr_in host_receiver;
	socklen_t size = sizeof (sockaddr);
	DataTransfer *trns = DataTransfer::getInstance();

	ip_next_link = this->answer();

	host_sender.sin_family = AF_INET;
	host_sender.sin_port = htons (Doclone::PORT_DATA);
	host_sender.sin_addr.s_addr = INADDR_ANY;

	if ((sock_sender = socket (AF_INET, SOCK_STREAM, 0)) < 0) {
		ConnectionException ex;
		throw ex;
	}

	// Connect even in TIME_WAIT state
	int iSetOption = 1;
	setsockopt(sock_sender, SOL_SOCKET, SO_REUSEADDR,
			&iSetOption, sizeof(iSetOption));

	if ((bind (sock_sender,
			reinterpret_cast<sockaddr*>(&host_sender), size)) < 0) {
		ConnectionException ex;
		throw ex;
	}

	if ((listen (sock_sender, 1)) < 0) {
		ConnectionException ex;
		throw ex;
	}
	
	if ((fdo=
		 accept (sock_sender,
				 reinterpret_cast<sockaddr*>(&host_sender), &size)) < 0) {
		ConnectionException ex;
		throw ex;
	}

	// Set the origin descriptor
	trns->setFdo(fdo);
	this->_srcIP = inet_ntoa (host_sender.sin_addr);

	// Notify the views
	Clone *dcl = Clone::getInstance();
	dcl->triggerEvent(Doclone::EVT_NEW_CONNECION, this->_srcIP);

	host_receiver.sin_family = AF_INET;
	host_receiver.sin_port = htons (Doclone::PORT_DATA);
	host_receiver.sin_addr.s_addr = ip_next_link;

	if (host_receiver.sin_addr.s_addr) {
		int fdd;
		if ((fdd = socket (AF_INET, SOCK_STREAM, 0)) < 0) {
			ConnectionException ex;
			ex.logMsg();
			throw ex;
		}

		sleep (1);

		if ((connect (fdd,
				reinterpret_cast<sockaddr*>(&host_receiver), size)) < 0) {
			ConnectionException ex;
			ex.logMsg();
			throw ex;
		}

		// Set the destiny descriptor
		trns->setFdd(fdd, inet_ntoa (host_receiver.sin_addr));
	}
	
	
	log->debug("Link::linkClient() end");
}

/**
 * \brief Sends an image to the chain.
 *
 * \param image
 * 		The path of the image
 */
void Link::sendFromImage(const std::string &image) throw(Exception) {
	Logger *log = Logger::getInstance();
	log->debug("Link::sendFromImage(image=>%s) start", image.c_str());

	uint64_t totalSize = Util::getFileSize(image);

	DataTransfer *trns = DataTransfer::getInstance();
	trns->setTotalSize(totalSize);

	Operation *waitOp = new Operation(
			Doclone::OP_WAIT_CLIENTS, "");

	Clone *dcl = Clone::getInstance();
	dcl->addOperation(waitOp);

	this->linkServer();

	dcl->markCompleted(Doclone::OP_WAIT_CLIENTS, "");

	int fd = Util::openFile(image);

	Operation *transferOp = new Operation(
			Doclone::OP_TRANSFER_DATA, "");

	dcl->addOperation(transferOp);

	/*
	 * Before sending the data, it sends its size. So the client/s can
	 * calculate the completed percentage. All the data is sent in big-endian.
	 * If the system is little-endian, it's necessary to convert totalSize to
	 * big-endian.
	 */
	uint64_t tmpTotalSize = htobe64(totalSize);
	trns->transferFrom(&tmpTotalSize, static_cast<size_t>(sizeof(uint64_t)));

	trns->transferAllFrom(fd);

	dcl->markCompleted(Doclone::OP_TRANSFER_DATA, "");

	Util::closeFile(fd);

	this->closeConnection();

	log->debug("Link::sendFromImage() end");
}

/**
 * \brief Sends a device to the chain.
 *
 * \param device
 * 		The path of the device
 */
void Link::sendFromDevice(const std::string &device) throw(Exception) {
	Logger *log = Logger::getInstance();
	log->debug("Link::sendFromDevice(device=>%s) start", device.c_str());

	PartedDevice *pedDev = PartedDevice::getInstance();
	pedDev->initialize(Util::getDiskPath(device));

	Operation *waitOp = new Operation(
			Doclone::OP_WAIT_CLIENTS, "");

	Clone *dcl = Clone::getInstance();
	dcl->addOperation(waitOp);

	this->linkServer();

	dcl->markCompleted(Doclone::OP_WAIT_CLIENTS, "");

	if(!Util::isBlockDevice(device)) {
		NoBlockDeviceException ex;
		throw ex;
	}

	PartedDevice *pDevice = PartedDevice::getInstance();
	std::string target = pDevice->getPath();
	Disk *dcDisk = DlFactory::createDiskLabel();
	Image image;

	if(Util::isDisk(device)) {
		image.setType(Doclone::IMAGE_DISK);
	}
	else {
		image.setType(Doclone::IMAGE_PARTITION);
	}

	Operation *readPartTableOp = new Operation(
			Doclone::OP_READ_PARTITION_TABLE, target);

	dcl->addOperation(readPartTableOp);

	image.readPartitionTable(device);

	// Mark the operation to read partition table as completed
	dcl->markCompleted(Doclone::OP_READ_PARTITION_TABLE, target);

	if(image.canCreateCheck() == false) {
		CreateImageException ex;
		throw ex;
	}

	image.initCreateOperations();

	image.createImageHeader(dcDisk);

	/*
	 * Before sending the data, it sends its size. So the client/s can
	 * calculate the completed percentage. All the data is sent in big-endian.
	 * If the system is little-endian, it's necessary to convert totalSize to
	 * big-endian.
	 */
	DataTransfer *trns = DataTransfer::getInstance();
	uint64_t tmpTotalSize = htobe64(image.getHeader().image_size);
	trns->transferFrom(&tmpTotalSize, static_cast<size_t>(sizeof(uint64_t)));

	image.writeImageHeader();

	image.readPartitionsData();

	this->closeConnection();

	delete dcDisk;

	log->debug("Link::sendFromDevice() end");
}

/**
 * \brief Initializes the link server.
 */
void Link::send() throw(Exception) {
	Logger *log = Logger::getInstance();
	log->debug("Link::send() start");
	
	Clone *dcl = Clone::getInstance();

	try {
		if(dcl->getDevice().empty()) {
			this->sendFromImage(dcl->getImage());
		} else {
			this->sendFromDevice(dcl->getDevice());
		}
	} catch (const CancelException &ex) {
		this->closeConnection();
		throw;
	} catch (const ReadDataException &ex) {
		this->closeConnection();
		throw;
	} catch (const SendDataException &ex) {
		this->closeConnection();
		throw;
	} catch (const ErrorException &ex) {
		this->closeConnection();
		throw;
	}

	log->debug("Link::send() end");
}

/**
 * \brief Receives an image from the chain.
 *
 * \param image
 * 		The path of the image
 */
void Link::receiveToImage(const std::string &image) throw(Exception) {
	Logger *log = Logger::getInstance();
	log->debug("Link::receiveToImage(image=>%s) start", image.c_str());

	DataTransfer *trns = DataTransfer::getInstance();

	Operation *waitOp = new Operation(
			Doclone::OP_WAIT_SERVER, "");

	Clone *dcl = Clone::getInstance();
	dcl->addOperation(waitOp);

	this->linkClient();

	dcl->markCompleted(Doclone::OP_WAIT_SERVER, "");

	Util::createFile(image);
	int fd = Util::openFile(image);

	Operation *transferOp = new Operation(
			Doclone::OP_TRANSFER_DATA, "");

	dcl->addOperation(transferOp);

	uint64_t totalSize;
	trns->transferTo(&totalSize, static_cast<size_t>(sizeof(uint64_t)));

	/*
	 * The given totalSize is received in big-endian. If the system is
	 * little-endian, it must be converted to little-endian.
	 */
	uint64_t tmpTotalSize = be64toh(totalSize);
	trns->setTotalSize(tmpTotalSize);

	trns->transferAllTo(fd);

	dcl->markCompleted(Doclone::OP_TRANSFER_DATA, "");

	Util::closeFile(fd);

	this->closeConnection();

	log->debug("Link::receiveToImage() end");
}

/**
 * \brief Receives a device from the chain.
 *
 * \param device
 * 		The path of the device
 */
void Link::receiveToDevice(const std::string &device) throw(Exception) {
	Logger *log = Logger::getInstance();
	log->debug("Link::receiveToDevice(device=>%s) start", device.c_str());

	PartedDevice *pedDev = PartedDevice::getInstance();
	pedDev->initialize(Util::getDiskPath(device));

	Operation *waitOp = new Operation(
			Doclone::OP_WAIT_SERVER, "");

	Clone *dcl = Clone::getInstance();
	dcl->addOperation(waitOp);

	this->linkClient();

	dcl->markCompleted(Doclone::OP_WAIT_SERVER, "");

	if(!Util::isBlockDevice(device)) {
		NoBlockDeviceException ex;
		throw ex;
	}

	uint64_t totalSize;
	DataTransfer *trns = DataTransfer::getInstance();
	trns->transferTo(&totalSize, static_cast<size_t>(sizeof(uint64_t)));

	/*
	 * The given totalSize is received in big-endian. If the system is
	 * little-endian, it must be converted to little-endian.
	 */
	uint64_t tmpTotalSize = be64toh(totalSize);
	trns->setTotalSize(tmpTotalSize);

	Image image;

	image.readImageHeader(device);

	image.openImageHeader();

	Disk *dcDisk = DlFactory::createDiskLabel(image.getLabelType(),
			pedDev->getPath());

	if(image.canRestoreCheck(device, dcDisk->getSize()) == false) {
		RestoreImageException ex;
		throw ex;
	}

	image.initRestoreOperations(device);

	image.writePartitionTable(device);

	image.writePartitionsData();

	if(image.getHeader().image_type==(Doclone::imageType)IMAGE_DISK) {
		dcDisk->setPartitions(image.getPartitions());
		dcDisk->restoreGrub();
	}

	this->closeConnection();

	delete dcDisk;

	log->debug("Link::receiveToDevice() end");
}

/**
 * \brief Sets up a receiver in the chain
 */
void Link::receive() throw(Exception) {
	Logger *log = Logger::getInstance();
	log->debug("Link::receive() start");

	Clone *dcl = Clone::getInstance();
	
	try {
		if(dcl->getDevice().empty()) {
			this->receiveToImage(dcl->getImage());
		} else {
			this->receiveToDevice(dcl->getDevice());
		}
	} catch (const CancelException &ex) {
		this->closeConnection();
		throw;
	} catch (const WriteDataException &ex) {
		this->closeConnection();
		throw;
	} catch (const ReceiveDataException &ex) {
		this->closeConnection();
		throw;
	} catch (const ErrorException &ex) {
		this->closeConnection();
		throw;
	}

	log->debug("Link::receive() end");
}

}