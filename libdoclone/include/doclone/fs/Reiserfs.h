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

#ifndef REISERFS_H_
#define REISERFS_H_

#include <doclone/UnixFS.h>

#include <doclone/exception/Exception.h>

#include <string>

namespace Doclone {

/**
 * \addtogroup Filesystems
 * @{
 */

/**
 * \def BLKID_REGEXP_REISERFS
 * Regular expression that matches with all the TYPE constants of libblkid
 * for Reiserfs filesystems.
 */
#define BLKID_REGEXP_REISERFS "^reiserfs$"

/**
 * \class Reiserfs
 * \brief Operations for Reiserfs
 * Writes UUID and label
 * \date August, 2011
 */
class Reiserfs : public UnixFS {
public:
	Reiserfs();
	
	void writeLabel(const std::string &dev) const throw(Exception);
	void writeUUID(const std::string &dev) const throw(Exception);

private:
	void checkSupport();
};
/**@}*/

}

#endif /* REISERFS_H_ */