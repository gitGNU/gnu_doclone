/*
 * doclone - a frontend for libdoclone
 * Copyright (C) 2013-2015 Joan Lledó <joanlluislledo@gmail.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <config.h>

#include <locale.h>
#include <libintl.h>

#include <ConsoleView/ConsoleView.h>

int main(int argc, char** argv) {
	setlocale(LC_ALL, "");
	textdomain(PACKAGE);
	bindtextdomain(PACKAGE, LOCALEDIR);

	ConsoleView view;
	view.initView(argc, argv);

	return 0;
}
