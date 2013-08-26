/*
 * doclone - a frontend for libdoclone
 * Copyright (C) 2013 Joan Lledó <joanlluislledo@gmail.com>
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

#include <CursesView/LocalMenu.h>
#include <CursesView/CursesView.h>
#include <CursesView/ScreenFactory.h>

#include <libintl.h>
#include <curses.h>

#include <vector>
#include <string>

LocalMenu::LocalMenu() {
	std::string notice=_("Choose what you want to do:");

	mvwprintw(this->_win, 2, 2, notice.c_str());

	// Refresh the text at the top bar
	this->_topBarNotice=_("Select the option and press [Enter] or push <Continue>.");
	this->_topBarNotice.append(" ");
	this->_topBarNotice.append(_("Press [Tab] to move."));

	char *choices[] = {
			_("Create image"),
			_("Restore image"),
			0
	};
	char *descriptions[] = {
			_("Save the content of a device into a image file"),
			_("Restore the image file into a device"),
			0
	};

	this->_menu = new Menu();
	this->_menu->setWindow(this->_win);
	this->_menu->setContent(choices, descriptions);
	this->_menu->setColors(
				COLOR_PAIR(CursesView::MENU_BKGD),
				COLOR_PAIR(CursesView::SELECTED_OPTION),
				COLOR_PAIR(CursesView::UNSELECTABLE_OPTION));
	this->_menu->setMargin(true);
	this->_menu->post();

	this->_menuBack->post();
	this->_menuContinue->post();

	this->_listWidgets.push_back(this->_menu);
	this->_listWidgets.push_back(this->_menuBack);
	this->_listWidgets.push_back(this->_menuContinue);

	this->_it = this->_listWidgets.begin();

	wrefresh(this->_win);
}

/**
 * Reads the state of the widgets and determines what will be the next screen
 *
 * \return The code of the next screen
 */
int LocalMenu::getNextScreen() {
	CursesView *curView = CursesView::getInstance();
	int retVal = ScreenFactory::NONE;

	int index = this->_menu->getIndex();

	if(*this->_it == this->_menuBack) {
		retVal = ScreenFactory::BACK;
	} else {
		switch(index) {
		case 0: {
			dcCursesRole role = CURSES_ROLE_READ;
			curView->setRole(role);

			retVal = ScreenFactory::SELECTDEVICE;
			break;
		}
		case 1: {
			dcCursesRole role = CURSES_ROLE_WRITE;
			curView->setRole(role);

			retVal = ScreenFactory::SELECTFILE;
			break;
		}
		}
	}

	return retVal;
}

LocalMenu::~LocalMenu() {
	delete this->_menu;
}