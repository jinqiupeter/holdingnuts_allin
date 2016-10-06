/*
 * Copyright 2008, 2009, Dominik Geyer
 *
 * This file is part of HoldingNuts.
 *
 * HoldingNuts is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * HoldingNuts is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with HoldingNuts.  If not, see <http://www.gnu.org/licenses/>.
 *
 * Authors:
 *     Dominik Geyer <dominik.geyer@holdingnuts.net>
 */


#include "Debug.h"
#include "Player.hpp"
#include "../system/Logger.h"
using namespace std;

Player::Player()
{
	next_action.valid = false;
	last_action = Player::None;
	sitout = false;
    wanna_leave = false;

    table_no = -1;
    seat_no = -1;
    stake = 0;
    rebuy_stake = 0;
    timedout_count = 0;
    timeout = 0;
}

void Player::clearInsuranceInfo()
{
    for(size_t i = 0; i < 2; ++i)
    {
        insuraceInfo[i].bought = false;
        insuraceInfo[i].buy_amount = 0;
        insuraceInfo[i].max_payment = 0;
        insuraceInfo[i].buy_cards.clear();
        insuraceInfo[i].outs.clear();
        insuraceInfo[i].outs_divided.clear();
        insuraceInfo[i].every_single_outs.clear();
        insuraceInfo[i].res_amount = 0;
    }
    log_msg("player", "%d clear insurance info", seat_no);
}



