/*
*  Copyright 2021 Michail Vourlakos <mvourlakos@gmail.com>
*
*  This file is part of Latte-Dock
*
*  Latte-Dock is free software; you can redistribute it and/or
*  modify it under the terms of the GNU General Public License as
*  published by the Free Software Foundation; either version 2 of
*  the License, or (at your option) any later version.
*
*  Latte-Dock is distributed in the hope that it will be useful,
*  but WITHOUT ANY WARRANTY; without even the implied warranty of
*  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
*  GNU General Public License for more details.
*
*  You should have received a copy of the GNU General Public License
*  along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

import QtQuick 2.7

import org.kde.latte.abilities.definition 0.1 as AbilityDefinition

AbilityDefinition.Indicators {
    id: apis

    readonly property Item publicApi: Item {
        readonly property alias isEnabled: apis.isEnabled

        readonly property alias padding: apis.padding

        readonly property alias type: apis.type

        readonly property alias configuration: apis.configuration
        readonly property alias resources: apis.resources

        readonly property alias indicatorComponent: apis.indicatorComponent

        readonly property alias info: apis.info
    }
}