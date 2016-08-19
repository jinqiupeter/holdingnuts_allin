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


#ifndef _SITANDGOGAMECONTROLLER_H
#define _SITANDGOGAMECONTROLLER_H

#include <vector>
#include <string>
#include <map>
#include <set>
#include <string>
#include <ctime>

#include "Card.hpp"
#include "Deck.hpp"
#include "HoleCards.hpp"
#include "CommunityCards.hpp"
#include "Table.hpp"
#include "Player.hpp"
#include "GameController.hpp"
#include "GameLogic.hpp"


class SitAndGoGameController : public GameController
{
friend class TestCaseGameController;

public:
	
	SitAndGoGameController();
	SitAndGoGameController(const GameController& g);
	~SitAndGoGameController();
	
    void reset();
	
	bool getPlayerList(std::vector<int> &client_list, bool including_wanna_leave = false) const;
	bool getPlayerList(std::vector<std::string> &client_list) const;

	bool nextRoundStraddle(int cid);
	
	void stateNewRound(Table *t) ;
    void stateBetting(Table *t);
    void stateBlinds(Table *t);
	void stateEndRound(Table *t);
	int handleTable(Table *t) ;
    void placePlayers();
	
    void handleWannaLeave(Table *t);
	void handleWantToStraddleNextRound(Table *t);
    void handleRebuy(Table *t);
	void handleAnte(Table *t);
	void handleStraddle(Table *t);
    bool arrangeSeat(int cid);
    bool addPlayer(int cid, const std::string &uuid, chips_type player_stake);
	bool removePlayer(int cid);
	bool resumePlayer(int cid);
	
	void start() ;
	int tick();
	
    void setExpireIn(int iExpireIn) { expire_in = iExpireIn; };
    int getExpireIn() const { return expire_in; };
	
private:
    int expire_in;  // game is expiring in expire_in seconds
};

#endif /* _SITANDGOGAMECONTROLLER_H */
