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

#include <doclone/Partition.h>
#include <doclone/Clone.h>
#include <doclone/Logger.h>
#include <doclone/PartedDevice.h>
#include <doclone/FsFactory.h>
#include <doclone/Util.h>

#include <doclone/exception/CancelException.h>
#include <doclone/exception/ReadDataException.h>
#include <doclone/exception/WriteDataException.h>
#include <doclone/exception/ReceiveDataException.h>
#include <doclone/exception/SendDataException.h>
#include <doclone/exception/FormatException.h>
#include <doclone/exception/MountException.h>
#include <doclone/exception/UmountException.h>
#include <doclone/exception/InvalidImageException.h>
#include <doclone/exception/FileNotFoundException.h>

#include <stdlib.h>
#include <sys/mount.h>
#include <stdio.h>
#include <mntent.h>
#include <sys/statvfs.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>

#include <blkid/blkid.h>

#include <string>

namespace Doclone {

/**
 * \brief This constructor is used in the process of creation of an image
 *
 * \param path
 * 		The device path of the partition
 */
Partition::Partition(const std::string &path) throw(Exception)
		: _path(path), _partNum(), _minSize(), _startPos(), _usedPart(),
		  _fs(), _type(), _flags(), _partition() {
	this->initNum();
	this->initType();
	this->initFS();
	this->initUUID();
	this->initMinSize();
	this->initStartPos();
	this->initUsedPart();
	this->initFlags();
	this->initLabel();
}

/**
 * \brief Called for each partition to be restored
 *
 * \param partition
 * 		Metadata of the current partition
 */
Partition::Partition(Doclone::partInfo partition) throw(Exception)
		: _path(), _partNum(), _minSize(), _startPos(), _usedPart(),
		  _fs(), _type(), _flags(), _partition() {
	this->_partition = partition;
	
	this->_type=(Doclone::partType)partition.type;
	
	try {
		this->_fs = FsFactory::createFilesystem(reinterpret_cast<char*>(partition.fsName));
	} catch (const Exception &ex) {
		this->_fs = FsFactory::createFilesystem("nofs");
	}
	
	this->_minSize = partition.min_size;
	
	double *tmpStartPos = reinterpret_cast<double*>(&partition.start_pos);
	this->_startPos = *tmpStartPos;
	if(this->_startPos>1) {
		InvalidImageException ex;
		throw ex;
	}

	double *tmpUsedPart = reinterpret_cast<double*>(&partition.used_part);
	this->_usedPart = *tmpUsedPart;
	if(this->_usedPart>1) {
		InvalidImageException ex;
		throw ex;
	}
	
	// Flags
	this->_flags = partition.flags;
	
	// Label
	this->_label = reinterpret_cast<char*>(partition.label);
	this->_fs->setLabel(this->_label);
	
	// UUID
	this->_uuid = reinterpret_cast<char*>(partition.uuid);
	this->_fs->setUUID(this->_uuid);
}

/**
 * \brief Free this->_fs
 */
Partition::~Partition() {
	this->doUmount();

	if(this->_fs != 0) {
		delete this->_fs;
		this->_fs = 0;
	}
}

// Getters and setters
Doclone::partType Partition::getType() const {
	return this->_type;
}

uint64_t Partition::getMinSize() const {
	return this->_minSize;
}

double Partition::getStartPos() const {
	return this->_startPos;
}

double Partition::getUsedPart() const {
	return this->_usedPart;
}

const std::string &Partition::getPath() const {
	return this->_path;
}

void Partition::setPath(const std::string &path) {
	this->_path = path;
}

Doclone::partInfo Partition::getPartition() const {
	return this->_partition;
}

void Partition::setPartNum(unsigned int num) {
	this->_partNum = num;
}

unsigned int Partition::getPartNum() const {
	return this->_partNum;
}

Filesystem * Partition::getFileSystem() {
	return this->_fs;
}

/**
 * \brief Initializes the attribute this->_type
 */
void Partition::initType() throw(Exception) {
	Logger *log = Logger::getInstance();
	log->debug("Partition::initType() start");

	PartedDevice *pedDev = PartedDevice::getInstance();
	pedDev->open();
	PedDisk *pDisk = pedDev->getDisk();
	PedPartition *pPart = ped_disk_get_partition(pDisk, this->_partNum);
	
	switch (pPart->type) {
		case PED_PARTITION_NORMAL: {
			this->_type = Doclone::PARTITION_PRIMARY;
			break;
		}
		case PED_PARTITION_EXTENDED: {
			this->_type = Doclone::PARTITION_EXTENDED;
			break;
		}
		case PED_PARTITION_LOGICAL: {
			this->_type = Doclone::PARTITION_LOGICAL;
			break;
		}
		default: {
			break;
		}
	}
	
	pedDev->close();
	
	log->debug("Partition::initType() end");
}

/**
 * \brief Initializes the attribute this->_fs
 */
void Partition::initFS() throw(Exception) {
	Logger *log = Logger::getInstance();
	log->debug("Partition::initFS() start");

	std::string fs_type;
	const char *type, *value;
	blkid_cache cache = 0;
	blkidInfo info;

	blkid_get_cache(&cache, "/dev/null"); //Do not use blkid cache
	blkid_dev blDev = blkid_get_dev(cache, this->_path.c_str(), BLKID_DEV_NORMAL);
	if(!blDev) {
		info.type = "nofs";
	} else {
		blkid_tag_iterate iter = blkid_tag_iterate_begin(blDev);

		while (blkid_tag_next(iter, &type, &value) == 0) {
			if (!strcmp(type, "TYPE")) {
				info.type = value;
			}
			if (!strcmp(type, "SEC_TYPE")) {
				info.sec_type = value;
			}
		}
		blkid_tag_iterate_end(iter);
	}
	
	this->_fs = FsFactory::createFilesystem(info);

	log->debug("Partition::initFS() end");
}

/**
 * \brief Initializes the attribute this->_minSize
 */
void Partition::initMinSize() throw(Exception) {
	Logger *log = Logger::getInstance();
	log->debug("Partition::initMinSize() start");

	if(this->_type == Doclone::PARTITION_EXTENDED
		|| this->_fs->getType() == Doclone::FSTYPE_NONE) {
		this->_minSize = 0;
	}
	else {
		this->_minSize = this->usedSpace();
	}
	
	log->debug("Partition::initMinSize() end");
}

/**
 * \brief Initializes the attribute this->_startPos
 */
void Partition::initStartPos() throw(Exception) {
	Logger *log = Logger::getInstance();
	log->debug("Partition::initStartPos() start");

	PartedDevice *pedDev = PartedDevice::getInstance();
	pedDev->open();
	PedDevice *pDev = pedDev->getDevice();
	PedDisk *pDisk = pedDev->getDisk();
	PedPartition *pPart = ped_disk_get_partition(pDisk, this->_partNum);

	this->_startPos = static_cast<double>(pPart->geom.start) / pDev->length;

	pedDev->close();

	log->debug("Partition::initStartPos() end");
}

/**
 * \brief Initializes the attribute this->_usedPart
 */
void Partition::initUsedPart() throw(Exception) {
	Logger *log = Logger::getInstance();
	log->debug("Partition::initUsedPart() start");

	PartedDevice *pedDev = PartedDevice::getInstance();
	pedDev->open();
	PedDevice *pDev = pedDev->getDevice();
	PedDisk *pDisk = pedDev->getDisk();
	PedPartition *pPart = ped_disk_get_partition(pDisk, this->_partNum);
	
	this->_usedPart = static_cast<double>(pPart->geom.length) / pDev->length;
		  
	pedDev->close();
	
	log->debug("Partition::initUsedPart() end");
}

/**
 * \brief Initializes the attribute this->_flags
 */
void Partition::initFlags() throw(Exception) {
	Logger *log = Logger::getInstance();
	log->debug("Partition::initFlags() start");
	
	PartedDevice *pedDev = PartedDevice::getInstance();
	pedDev->open();
	PedDisk *pDisk = pedDev->getDisk();
	PedPartition *pPart = ped_disk_get_partition(pDisk, this->_partNum);
	
	this->_flags = 0;
	
	if(ped_partition_get_flag(pPart, PED_PARTITION_BOOT)) {
		this->_flags|=Doclone::F_BOOT;
	}
	if(ped_partition_get_flag(pPart, PED_PARTITION_ROOT)) {
		this->_flags|=Doclone::F_ROOT;
	}
	if(ped_partition_get_flag(pPart, PED_PARTITION_SWAP)) {
		this->_flags|=Doclone::F_SWAP;
	}
	if(ped_partition_get_flag(pPart, PED_PARTITION_HIDDEN)) {
		this->_flags|=Doclone::F_HIDDEN;
	}
	if(ped_partition_get_flag(pPart, PED_PARTITION_RAID)) {
		this->_flags|=Doclone::F_RAID;
	}
	if(ped_partition_get_flag(pPart, PED_PARTITION_LVM)) {
		this->_flags|=Doclone::F_LVM;
	}
	if(ped_partition_get_flag(pPart, PED_PARTITION_LBA)) {
			this->_flags|=Doclone::F_LBA;
	}
	if(ped_partition_get_flag(pPart, PED_PARTITION_HPSERVICE)) {
		this->_flags|=Doclone::F_HPSERVICE;
	}
	if(ped_partition_get_flag(pPart, PED_PARTITION_PALO)) {
		this->_flags|=Doclone::F_PALO;
	}
	if(ped_partition_get_flag(pPart, PED_PARTITION_PREP)) {
		this->_flags|=Doclone::F_PREP;
	}
	if(ped_partition_get_flag(pPart, PED_PARTITION_MSFT_RESERVED)) {
		this->_flags|=Doclone::F_MSFT_RESERVED;
	}
	if(ped_partition_get_flag(pPart, PED_PARTITION_BIOS_GRUB)) {
		this->_flags|=Doclone::F_BIOS_GRUB;
	}
	if(ped_partition_get_flag(pPart, PED_PARTITION_APPLE_TV_RECOVERY)) {
		this->_flags|=Doclone::F_APPLE_TV_RECOVERY;
	}
	if(ped_partition_get_flag(pPart, PED_PARTITION_DIAG)) {
		this->_flags|=Doclone::F_DIAG;
	}
	
	pedDev->close();
	
	log->debug("Partition::initFlags() end");
}

/**
 * \brief Initializes the attribute this->_partNum
 */
void Partition::initNum() throw(Exception) {
	Logger *log = Logger::getInstance();
	log->debug("Partition::initNum() start");

	this->_partNum = Util::getPartNum(this->_path);
	
	log->debug("Partition::initNum() end");
}

/**
 * \brief Initializes the attribute this->_label
 */
void Partition::initLabel() throw(Exception) {
	Logger *log = Logger::getInstance();
	log->debug("Partition::initLabel() start");

	this->_label = this->_fs->readLabel(this->_path);

	log->debug("Partition::initLabel() end");
}

/**
 * \brief Initializes the attribute this->_uuid
 */
void Partition::initUUID() throw(Exception) {
	Logger *log = Logger::getInstance();
	log->debug("Partition::initUUID() start");

	this->_uuid = this->_fs->readUUID(this->_path);

	log->debug("Partition::initUUID() end");
}

/**
 * \brief Calculates the used space in partition
 *
 * \return Used space in bytes
 */
uint64_t Partition::usedSpace() throw(Exception) {
	Logger *log = Logger::getInstance();
	log->debug("Partition::usedSpace() start");
	
	struct statvfs info;
	uint64_t used_blocks;

	this->doMount();
	std::string mountDir = this->_fs->getMountPoint();
	
	try {
		if (statvfs (mountDir.c_str(), &info) < 0) {
			FileNotFoundException ex(mountDir);
			throw ex;
		}

		used_blocks = info.f_blocks - info.f_bfree;
	} catch (const CancelException &ex) {
		this->doUmount();
		throw;
	}
	
	this->doUmount();
	
	uint64_t retValue = used_blocks * info.f_bsize;
	
	log->debug("Partition::usedSpace(retValue=>%d) end", retValue);
	return retValue;
}

/**
 * \brief Checks if the image fits in the assigned device
 *
 * \return True or false
 */
bool Partition::fitInDevice() throw(Exception) {
	Logger *log = Logger::getInstance();
	log->debug("Partition::fitInDevice() start");
	
	PartedDevice *pedDev = PartedDevice::getInstance();
	pedDev->open();
	
	uint64_t devSize=(pedDev->getDevSize() * this->_usedPart);
	bool retValue = this->_minSize < devSize;
	
	pedDev->close();
		
	log->debug("Partition::fitInDevice(retValue=>%d) end", retValue);
	return retValue;
}

/**
 * \brief Mounts the partition using an external tool
 */
void Partition::externalMount() throw(Exception) {
	Logger *log = Logger::getInstance();
	log->debug("Partition::externalMount() start");
	
	char tmpDir[32]=TMP_PREFIX;
	
	if(!mkdtemp(tmpDir)) {
		MountException ex(this->_path);
		throw ex;
	}
	
	this->_fs->setMountPoint(tmpDir);
	
	std::string cmdline="mount."+this->_fs->getMountName();
	cmdline.append(" ")
	.append(this->_path.c_str())
	.append(" ")
	.append(tmpDir)
	.append(" ")
	.append("-o")
	.append(" ")
	.append("rw")
	.append(" ")
	.append(">/dev/null 2>&1");
			 
	int exitValue;
	Util::spawn_command_line_sync(cmdline, &exitValue);

	if (exitValue<0) {
		MountException ex(this->_path);
		throw ex;
	}
	
	log->debug("Partition::externalMount() end");
}

/**
 * \brief Mount the partition
 */
void Partition::doMount() throw(Exception) {
	Logger *log = Logger::getInstance();
	log->debug("Partition::doMount() start");
	
	if(!this->_fs->getMountSupport()) {
		MountException ex(this->_path);
		throw ex;
	}
	
	if(this->isMounted()) {
		log->debug("Partition::doMount() end");
		return;
	}
	
	if(this->_fs->getMountType() == Doclone::MOUNT_EXTERNAL) {
		// perform external doMount
		this->externalMount();
	}
	else {
		char tmpDir[32]=TMP_PREFIX;
		
		if(!mkdtemp(tmpDir)) {
			MountException ex(this->_path);
			ex.logMsg();
			throw ex;
		}
		
		this->_fs->setMountPoint(tmpDir);
		
		if(mount (this->_path.c_str(), tmpDir,
			this->_fs->getMountName().c_str(), 0,
			this->_fs->getMountOptions().c_str()) < 0) {
			MountException ex(this->_path);
			ex.logMsg();
			throw ex;
		}
	}
	
	// After mounting, write a new line in /etc/mtab
	Util::addMtabEntry(this->_path, this->_fs->getMountPoint(),
		this->_fs->getMountName(), this->_fs->getMountOptions());
	
	log->debug("Partition::doMount() end");
}

/**
 * \brief Unmount the partition
 */
void Partition::doUmount() throw(Exception) {
	Logger *log = Logger::getInstance();
	log->debug("Partition::doUmount() start");
	
	if(!this->isMounted() // If it is not mounted or is mounted but not in /tmp
			|| !Util::match(this->_fs->getMountPoint(), TMP_PREFIX_REGEXP)) {
		/*
		 * If the mount point is not in /tmp, it means the user has mounted it
		 * manually before running libdoclone, so do not unmount it.
		 */
		log->debug("Partition::doUmount() end");
		return;
	}

	sync();

	if(umount2(this->_fs->getMountPoint().c_str(), MNT_DETACH)<0) {
		UmountException ex(this->_fs->getMountPoint().c_str());
		ex.logMsg();
	}

	remove(this->_fs->getMountPoint().c_str());

	// After unmounting, we must delete the entry of /etc/mtab
	Util::updateMtab(this->_path);
	
	log->debug("Partition::doUmount() end");
}

/**
 * \brief Checks if the partition is mounted
 */
bool Partition::isMounted() throw(Exception) {
	Logger *log = Logger::getInstance();
	log->debug("Partition::isMounted() start");

	bool retValue = false;
	
	struct mntent *filesys;
	FILE *f;
	f = setmntent ("/etc/mtab", "r");
	if (!f) {
		FileNotFoundException ex("/etc/mtab");
		throw ex;
	}

	std::string uuidDevPath = "/dev/disk/by-uuid/"+this->_uuid;

	while ((filesys = getmntent (f))) {
		// If this->_path or uuidDevPath are in /etc/mtab
		if(!this->_path.compare(filesys->mnt_fsname)) {
			this->_fs->setMountPoint(filesys->mnt_dir);
			retValue = true;
			break;
		} else if (!uuidDevPath.compare(filesys->mnt_fsname)) {
			if(!Util::isUUIDRepeated(this->_uuid.c_str())) {
				this->_fs->setMountPoint(filesys->mnt_dir);
				retValue = true;
				break;
			}
		}
	}

	endmntent (f);
	
	log->debug("Partition::isMounted(retValue=>%d) end", retValue);
	return retValue;
}

/**
 * \brief Formats the partition
 */
void Partition::format() const throw(Exception) {
	Logger *log = Logger::getInstance();
	log->debug("Partition::format() start");
	
	std::string cmdline;
	
	cmdline+=this->_fs->getCommand()+" "+this->_fs->getFormatOptions()+
		" "+this->_path.c_str()+" >/dev/null 2>&1";

	int exitValue;
	Util::spawn_command_line_sync(cmdline, &exitValue);

	if (exitValue<0) {
		FormatException ex;
		throw ex;
	}

	log->debug("Partition::format() end");
}

/**
 * \brief Creates the partition metadata
 */
void Partition::createPartInfo() throw(Exception) {
	Logger *log = Logger::getInstance();
	log->debug("Partition::createPartInfo() start");

	// Initialize to 0
	memset(&this->_partition, 0, sizeof(this->_partition));
	
	this->_partition.type = this->_type;
	this->_partition.min_size = this->_minSize;

	uint64_t *tmpStartPos = reinterpret_cast<uint64_t*>(&this->_startPos);
	this->_partition.start_pos = *tmpStartPos;

	uint64_t *tmpUsedPart = reinterpret_cast<uint64_t*>(&this->_usedPart);
	this->_partition.used_part = *tmpUsedPart;

	this->_partition.flags = this->_flags;
	strncpy(reinterpret_cast<char*>(this->_partition.fsName), this->_fs->getdocloneName().c_str(), 32);
	strncpy(reinterpret_cast<char*>(this->_partition.label), this->_label.c_str(), 28);
	strncpy(reinterpret_cast<char*>(this->_partition.uuid), this->_uuid.c_str(), 37);
	
	log->debug("Partition::createPartInfo() end");
}

/**
 * \brief Writes fs label
 */
void Partition::writeLabel() const throw(Exception) {
	Logger *log = Logger::getInstance();
	log->debug("Partition::writeLabel() start");
	
	this->_fs->writeLabel(this->_path);

	log->debug("Partition::writeLabel() end");
}

/**
 * \brief Writes fs uuid
 */
void Partition::writeUUID() const throw(Exception) {
	Logger *log = Logger::getInstance();
	log->debug("Partition::writeUUID() start");
	
	this->_fs->writeUUID(this->_path);

	log->debug("Partition::writeUUID() end");
}

/**
 * \brief Writes partition flags
 */
void Partition::writeFlags() const throw(Exception) {
	Logger *log = Logger::getInstance();
	log->debug("Partition::writeFlags() start");
	
	PartedDevice *pedDev = PartedDevice::getInstance();
	pedDev->open();
	PedDisk *pDisk = pedDev->getDisk();
	PedPartition *pPart = ped_disk_get_partition(pDisk, this->_partNum);

	dcFlag fBoot = this->_flags & Doclone::F_BOOT;
	dcFlag fRoot = this->_flags & Doclone::F_ROOT;
	dcFlag fSwap = this->_flags & Doclone::F_SWAP;
	dcFlag fHidden = this->_flags & Doclone::F_HIDDEN;
	dcFlag fRaid = this->_flags & Doclone::F_RAID;
	dcFlag fLvm = this->_flags & Doclone::F_LVM;
	dcFlag fLba = this->_flags & Doclone::F_LBA;
	dcFlag fHpservice = this->_flags & Doclone::F_HPSERVICE;
	dcFlag fPalo = this->_flags & Doclone::F_PALO;
	dcFlag fPrep = this->_flags & Doclone::F_PREP;
	dcFlag fMsftReserved = this->_flags & Doclone::F_MSFT_RESERVED;
	dcFlag fBiosGrub = this->_flags & Doclone::F_BIOS_GRUB;
	dcFlag fAppleTvRecovery = this->_flags & Doclone::F_APPLE_TV_RECOVERY;
	dcFlag fDiag = this->_flags & Doclone::F_DIAG;

	if(ped_partition_is_flag_available(pPart, PED_PARTITION_BOOT) == 1) {
		ped_partition_set_flag(pPart, PED_PARTITION_BOOT, fBoot);
	}
	if(ped_partition_is_flag_available(pPart, PED_PARTITION_ROOT) == 1) {
		ped_partition_set_flag(pPart, PED_PARTITION_ROOT, fRoot);
	}
	if(ped_partition_is_flag_available(pPart, PED_PARTITION_SWAP) == 1) {
		ped_partition_set_flag(pPart, PED_PARTITION_SWAP, fSwap);
	}
	if(ped_partition_is_flag_available(pPart, PED_PARTITION_HIDDEN) == 1) {
		ped_partition_set_flag(pPart, PED_PARTITION_HIDDEN, fHidden);
	}
	if(ped_partition_is_flag_available(pPart, PED_PARTITION_RAID) == 1) {
		ped_partition_set_flag(pPart, PED_PARTITION_RAID, fRaid);
	}
	if(ped_partition_is_flag_available(pPart, PED_PARTITION_LVM) == 1) {
		ped_partition_set_flag(pPart, PED_PARTITION_LVM, fLvm);
	}
	if(ped_partition_is_flag_available(pPart, PED_PARTITION_LBA) == 1) {
		ped_partition_set_flag(pPart, PED_PARTITION_LBA, fLba);
	}
	if(ped_partition_is_flag_available(pPart, PED_PARTITION_HPSERVICE) == 1) {
		ped_partition_set_flag(pPart, PED_PARTITION_HPSERVICE, fHpservice);
	}
	if(ped_partition_is_flag_available(pPart, PED_PARTITION_PALO) == 1) {
		ped_partition_set_flag(pPart, PED_PARTITION_PALO, fPalo);
	}
	if(ped_partition_is_flag_available(pPart, PED_PARTITION_PREP) == 1) {
		ped_partition_set_flag(pPart, PED_PARTITION_PREP, fPrep);
	}
	if(ped_partition_is_flag_available(pPart, PED_PARTITION_MSFT_RESERVED) == 1) {
		ped_partition_set_flag(pPart, PED_PARTITION_MSFT_RESERVED, fMsftReserved);
	}
	if(ped_partition_is_flag_available(pPart, PED_PARTITION_BIOS_GRUB) == 1) {
		ped_partition_set_flag(pPart, PED_PARTITION_BIOS_GRUB, fBiosGrub);
	}
	if(ped_partition_is_flag_available(pPart, PED_PARTITION_APPLE_TV_RECOVERY) == 1) {
		ped_partition_set_flag(pPart, PED_PARTITION_APPLE_TV_RECOVERY, fAppleTvRecovery);
	}
	if(ped_partition_is_flag_available(pPart, PED_PARTITION_DIAG) == 1) {
		ped_partition_set_flag(pPart, PED_PARTITION_DIAG, fDiag);
	}
	
	pedDev->commit();
	pedDev->close();
	
	Clone *dcl = Clone::getInstance();
	dcl->markCompleted(Doclone::OP_WRITE_PARTITION_FLAGS, this->_path);

	log->debug("Partition::writeFlags() end");
}

/**
 * \brief Checks if this partition can hold data
 *
 * \return True of false
 */
bool Partition::isWritable() const throw(Exception) {
	Logger *log = Logger::getInstance();
	log->debug("Partition::isWritable() start");
	
	bool retValue;
	
	if(this->_type == Doclone::PARTITION_EXTENDED // It is extended
		|| this->_fs->getType() == Doclone::FSTYPE_NONE // It doesn't have a unix or dos filesystem
		|| !this->_fs->getMountSupport() // System can't mount it
		|| this->_usedPart == 0) { // It isn't a data partition
		retValue = false;
	}
	else {
		retValue = true;
	}
	
	log->debug("Partition::isWritable(retValue=>%d) end", retValue);
	return retValue;
}

/**
 * \brief Reads the data of the partition
 */
void Partition::read() throw(Exception) {
	Logger *log = Logger::getInstance();
	log->debug("Partition::read() start");
	
	if(!this->isWritable()) {
		log->debug("Partition::read() end");
		return;
	}

	this->doMount();
	try {
		std::string rootDir = this->_fs->getMountPoint();
		if(rootDir[rootDir.length()-1]!='/') {
			rootDir.push_back('/');
		}
		this->_fs->readDir(rootDir);
	} catch (const CancelException &ex) {
		this->doUmount();
		throw;
	} catch (const ReadDataException &ex) {
		this->doUmount();
		throw;
	} catch (const SendDataException &ex) {
		this->doUmount();
		throw;
	}
	
	this->doUmount();
	
	log->debug("Partition::read() end");
}

/**
 * \brief Writes the data of the partition
 */
void Partition::write() throw(Exception) {
	Logger *log = Logger::getInstance();
	log->debug("Partition::write() start");
	
	if(!this->isWritable()) {
		log->debug("Partition::write() end");
		return;
	}

	this->doMount();
	
	try {
		std::string rootDir = this->_fs->getMountPoint();
		if(rootDir[rootDir.length()-1]!='/') {
			rootDir.push_back('/');
		}
		this->_fs->writeDir(rootDir);
	} catch (const CancelException &ex) {
		this->doUmount();
		throw;
	} catch (const WriteDataException &ex) {
		this->doUmount();
		throw;
	} catch (const ReceiveDataException &ex) {
		this->doUmount();
		throw;
	}
	
	this->doUmount();

	log->debug("Partition::write() end");
}

}