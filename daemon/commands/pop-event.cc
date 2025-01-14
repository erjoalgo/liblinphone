/*
 * Copyright (c) 2010-2019 Belledonne Communications SARL.
 *
 * This file is part of Liblinphone.
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program. If not, see <http://www.gnu.org/licenses/>.
 */

#include "pop-event.h"

using namespace std;

PopEventCommand::PopEventCommand() :
		DaemonCommand("pop-event", "pop-event", "Pop an event from event queue and display it.") {
	addExample(make_unique<DaemonCommandExample>("pop-event",
						"Status: Ok\n\n"
						"Size: 0"));
}
void PopEventCommand::exec(Daemon *app, const string& args) {
	app->pullEvent();
}
