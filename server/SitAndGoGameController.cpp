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


#include <cstdio>
#include <string>
#include <cstring>
#include <cstdlib>
#include <algorithm>

#include "Config.h"
#include "Logger.h"
#include "Debug.h"
#include "SitAndGoGameController.hpp"
#include "GameLogic.hpp"
#include "Card.hpp"

#include "game.hpp"


using namespace std;

// temporary buffer for chat/snap data
static char msg[1024];


SitAndGoGameController::SitAndGoGameController()
{
	reset();
	
    setPlayerMax(10);
    setRestart(false);
    setPlayerStakes(1500);
    setBlindsStart(10);
    setBlindsFactor(20);
    setBlindsTime(60 * 4);
    setName("game");
    setPassword("");
    setOwner(-1);

    addBlindLevels();
}

SitAndGoGameController::SitAndGoGameController(const GameController& g)
{
	reset();
	
	setName(g.getName());
	setRestart(g.getRestart());
	setOwner(g.getOwner());
	setGameType(g.getGameType());
	setPlayerMax(g.getPlayerMax());
	setPlayerTimeout(g.getPlayerTimeout());
	setPlayerStakes(g.getPlayerStakes());
	setBlindsStart(g.getBlindsStart());
	setBlindsFactor(g.getBlindsFactor());
	setBlindsTime(g.getBlindsTime());
	setPassword(g.getPassword());

    addBlindLevels();
}


SitAndGoGameController::~SitAndGoGameController()
{
	// remove all players
	for (players_type::iterator e = players.begin(); e != players.end();)
	{
		delete e->second;
		players.erase(e++);
	}
}


void SitAndGoGameController::reset()
{
	game_id = -1;
	
	type = RingGame;
	blind.blindrule = BlindNone;	
	
    status = Created;
	hand_no = 0;
	
	// remove all players
	for (players_type::iterator e = players.begin(); e != players.end();)
	{
		delete e->second;
		players.erase(e++);
	}
	
	// remove all spectators
	spectators.clear();
	
	// clear finish list
	finish_list.clear();
	initInsuranceRate();
}

unsigned int randomSeat()
{
    unsigned int seed;
    FILE *urandom;

    urandom = fopen ("/dev/urandom", "r");
    fread (&seed, sizeof (seed), 1, urandom);
    fclose(urandom);

    int max = 8, min = 0;
    srand(seed);
    int randNum = rand()%(max-min + 1) + min;

    return randNum;
}

bool SitAndGoGameController::arrangeSeat(int cid)
{
	Player *p = findPlayer(cid);
	if (!p)
		return false;
	
    // Sit&Go games should always have only one table
	tables_type::iterator e = tables.begin();
    if (e == tables.end()) {
		log_msg("arrangeSeat", "no table found");
        return false;
    }
    Table *t = e->second;

    while(true)
	{
        int i = randomSeat();
		if (t->seats[i].occupied)
			continue;
		
		t->seats[i].in_round = false;
		t->seats[i].showcards = false;
		t->seats[i].bet = 0;
		t->seats[i].occupied = true;

        p->setTableNo(t->getTableId());
        p->setSeatNo(i);
		t->seats[i].player = (Player*)p;
		
		log_msg("game", "placing player %d at seat %d", cid, i);
        break;
	}

    return true;
}

bool SitAndGoGameController::addPlayer(int cid, const std::string &uuid, chips_type player_stake)
{
	// is the game full?
	if (players.size() == max_players)
		return false;
	
	// is the client already a player?
	if (isPlayer(cid))
		return false;
	
	// remove from spectators list as we would receive the snapshots twice
	if (isSpectator(cid))
		removeSpectator(cid);
	
	Player *p = new Player;
	p->client_id = cid;
	p->setStake(player_stake);
    p->setTimeout(timeout);
	
	// save a copy of the UUID (player might disconnect)
	p->uuid = uuid;
	
	players[cid] = p;
	
    if (tables.size() < 1) {
        placeTable(0, getPlayerCount());
    } else {
        log_msg("game ", "arranging seat for player %d", cid);
        if (!arrangeSeat(cid)) {
            log_msg("game ", "failed to arranging seat for player %d", cid);
            return false;
        }
    }

	return true;
}

bool SitAndGoGameController::removePlayer(int cid)
{
	Player *p = findPlayer(cid);
	
	if (!p)
		return false;
	
    if (p->wanna_leave)
        return true;

    // fold players hole card before remove
    setPlayerAction(cid, Player::Fold, 0);

    //mark the player wanna_leave instead of removing it
    p->wanna_leave = true;

	bool bIsOwner = false;
	if (owner == cid)
		bIsOwner = true;

	// find a new owner
	if (bIsOwner)
		selectNewOwner();
	
	return true;
}

bool SitAndGoGameController::resumePlayer(int cid)
{
	Player *p = findPlayer(cid);
	
	if (!p)
		return false;
	
    //mark the player wanna_leave instead of removing it
    p->wanna_leave = false;

	return true;
}

bool SitAndGoGameController::getPlayerList(vector<int> &client_list, bool including_wanna_leave) const
{
	client_list.clear();
	
	for (players_type::const_iterator e = players.begin(); e != players.end(); e++) {
        if (e->second->wanna_leave) {
            if (including_wanna_leave)
                client_list.push_back(e->first);
            continue;
        } else {
            client_list.push_back(e->first);
        }
    }

    return true;
}

bool SitAndGoGameController::getPlayerList(vector<string> &client_list) const
{
	client_list.clear();
	
	for (players_type::const_iterator e = players.begin(); e != players.end(); e++) {
        if (!e->second->wanna_leave) {
            char tmp[1024];
            string sp = "";
            snprintf(tmp, sizeof(tmp),
                    "%d:%d:%d:%d ",
                    e->first,
                    e->second->getTableNo(),
                    e->second->getSeatNo(),
                    e->second->getStake());

            sp += tmp;
            client_list.push_back(sp);
        }
    }

    return true;
}

void SitAndGoGameController::handleRebuy(Table *t)
{
    // add rebuy stake to players' stake , this should be called only when table state is Table::EndRound
    if (t->state != Table::NewRound)
        return;

	chips_type amount = blind.amount;
	if (ante > 0)
	{
		amount += blind.amount / 10 * ante;
	}

    for (players_type::iterator e = players.begin(); e != players.end();)
    {
        Player *p = e->second;
        p->setStake(p->getStake() + p->getRebuyStake());
        p->setRebuyStake(0); //clear rebuy stake so it wont be added next time

		if (t->seats[p->getSeatNo()].occupied == false && p->getStake() >= amount)
		{
            // player has gone broken and rebought, mark him as occupied so he can join next round
            t->seats[p->getSeatNo()].occupied = true;
        }
        e++;
    }
}

void SitAndGoGameController::handleAnte(Table *t)
{
	chips_type ante_amount = 0;

	if (ante > 0)
	{
		ante_amount = blind.amount / 10 * ante;
		for (unsigned int i = 0; i < 10; ++i)
		{
			if (!t->seats[i].occupied)
				continue;

			Player *p = t->seats[i].player;
			t->seats[i].bet += ante_amount;
			p->stake -= ante_amount;
		}
	}
}

void SitAndGoGameController::handleStraddle(Table *t)
{
	if (t->last_straddle != -1)
	{
		chips_type amount = blind.amount;
		int seat_index = t->bb;
		do
		{
			amount *= 2;
			seat_index = t->getNextPlayer(seat_index);
			Player * pPlayer = t->seats[seat_index].player;
			if (pPlayer->stake < amount)
			{
				// break the straddle chain
				t->last_straddle = t->getPrePlayer(seat_index);
				break;
			}
			t->seats[seat_index].bet += amount;
			pPlayer->stake -= amount;
		} while (seat_index != t->last_straddle);
	}

	// set next round straddle
	if (this->mandatory_straddle)
	{
		int next_rount_bb = t->getNextPlayer(t->bb);
		t->last_straddle = t->getNextPlayer(next_rount_bb);
	}
	else
		t->last_straddle = -1;
}

void SitAndGoGameController::handleWannaLeave(Table *t)
{
    // remove players whose wanna_leave is 1, this should be called only when table state is Table::NewRound
    if (t->state != Table::NewRound)
        return;
	bool someone_leave = false;
    for (players_type::iterator e = players.begin(); e != players.end();)
    {
        if (e->second->wanna_leave) {
            log_msg("game", "deleting player %d since it's marked as wanna_leave", e->second->client_id);
            // remove player from occupied seat
            t->removePlayer(e->second->getSeatNo());

            delete e->second;
            players.erase(e);
			someone_leave = true;
        }
        e++;
    }

	if (t->countPlayers() <= 3)
	{
		t->last_straddle = -1;
		return;
	}

	if (someone_leave)
	{
		if (this->mandatory_straddle)
		{ 
			int bb;
			int sb;
			bb = t->getNextPlayer(t->dealer);
			sb = t->getNextPlayer(bb);
			t->last_straddle = t->getNextPlayer(sb);
		}
	}
}

void SitAndGoGameController::stateNewRound(Table *t)
{
    handleRebuy(t);
    handleWannaLeave(t);
    if (t->countPlayers() < 2) 
        return;

    GameController::stateNewRound(t);
}

void SitAndGoGameController::stateBlinds(Table *t)
{
	handleAnte(t);
	handleStraddle(t);

    GameController::stateBlinds(t);

	handleWantToStraddleNextRound(t);
}

void SitAndGoGameController::stateBetting(Table *t)
{
    Player *p = t->seats[t->cur_player].player;

    bool allowed_action = false;  // is action allowed?
    bool auto_action = false;

    Player::PlayerAction action;
    chips_type amount = 0;

    chips_type minimum_bet = determineMinimumBet(t);

    if (t->nomoreaction ||		// early showdown, no more action at table possible, or
            p->stake == 0)		// player is allin and has no more options
    {
        action = Player::None;
        allowed_action = true;
    }
    else if (p->next_action.valid)  // has player set an action?
    {
        action = p->next_action.action;

        if (action == Player::Fold)
            allowed_action = true;
        else if (action == Player::Check)
        {
            // allowed to check?
            if (t->seats[t->cur_player].bet < t->bet_amount)
                chat(p->client_id, t->table_id, "You cannot check! Try call.");
            else
                allowed_action = true;
        }
        else if (action == Player::Call)
        {
            if (t->bet_amount == 0 || t->bet_amount == t->seats[t->cur_player].bet)
            {
                //chat(p->client_id, t->table_id, "You cannot call, nothing was bet! Try check.");

                // retry with this action
                p->next_action.action = Player::Check;
                return;
            }
            else if (t->bet_amount > t->seats[t->cur_player].bet + p->stake)
            {
                // simply convert this action to allin
                p->next_action.action = Player::Allin;
                return;
            }
            else
            {
                allowed_action = true;
                amount = t->bet_amount - t->seats[t->cur_player].bet;
            }
        }
        else if (action == Player::Bet)
        {
            if (t->bet_amount > 0)
                chat(p->client_id, t->table_id, "You cannot bet, there was already a bet! Try raise.");
            else if (p->next_action.amount < minimum_bet)
            {
                snprintf(msg, sizeof(msg), "You cannot bet this amount. Minimum bet is %d.",
                        minimum_bet);
                chat(p->client_id, t->table_id, msg);
            }
            else
            {
                allowed_action = true;
                amount = p->next_action.amount - t->seats[t->cur_player].bet;
            }
        }
        else if (action == Player::Raise)
        {
            if (t->bet_amount == 0)
            {
                //chat(p->client_id, t->table_id, "Err: You cannot raise, nothing was bet! Try bet.");

                // retry with this action
                p->next_action.action = Player::Bet;
                return;
            }
            else if (p->next_action.amount < minimum_bet)
            {
                snprintf(msg, sizeof(msg), "You cannot raise this amount. Minimum bet is %d.",
                        minimum_bet);
                chat(p->client_id, t->table_id, msg);
            }
            else
            {
                allowed_action = true;
                amount = p->next_action.amount - t->seats[t->cur_player].bet;
            }
        }
        else if (action == Player::Allin)
        {
            allowed_action = true;
            amount = p->stake;
        }

        // reset player action
        p->next_action.valid = false;
    }
    else
    { 
        // handle player timeout
#ifndef SERVER_TESTING
        if ( (unsigned int)difftime(time(NULL), t->timeout_start) > p->getTimeout())
        {
            // if player timed out more than 3 times, mark user as wanna leave
            p->setTimedoutCount(p->getTimedoutCount() + 1);
            if (p->getTimedoutCount() >= 3) {
                p->wanna_leave = true;
                log_msg("game", "player %d timed out more thatn 3 time, marking as wanna_leave", p->getClientId());
            }

            
            // auto-action: fold, or check if possible
            if (t->seats[t->cur_player].bet < t->bet_amount)
                action = Player::Fold;
            else
                action = Player::Check;

            allowed_action = true;
            auto_action = true;
        }
#endif /* SERVER_TESTING */
    }


    // return here if no or invalid action
    if (!allowed_action) 
        return;


    // remember action for snapshot
    p->last_action = action;


    // perform action
    if (action == Player::None)
    {
        // do nothing
    }
    else if (action == Player::Fold)
    {
        t->seats[t->cur_player].in_round = false;

        snprintf(msg, sizeof(msg), "%d %d %d", SnapPlayerActionFolded, p->client_id, auto_action ? 1 : 0);
        snap(t->table_id, SnapPlayerAction, msg);
    }
    else if (action == Player::Check)
    {
        snprintf(msg, sizeof(msg), "%d %d %d", SnapPlayerActionChecked, p->client_id, auto_action ? 1 : 0);
        snap(t->table_id, SnapPlayerAction, msg);
    }
    else
    {
        // player can't bet/raise more than his stake
        if (amount > p->stake)
            amount = p->stake;

        // move chips from player's stake to seat-bet
        t->seats[t->cur_player].bet += amount;
        p->stake -= amount;

        if (action == Player::Bet || action == Player::Raise || action == Player::Allin)
        {
            // only re-open betting round if amount greater than table-bet
            // FIXME: bug: other players need to do an action even on none-minimum-bet
            if (t->seats[t->cur_player].bet > t->bet_amount /*&& t->seats[t->cur_player].bet >= minimum_bet*/)
            {
                t->last_bet_player = t->cur_player;
                t->last_bet_amount = t->bet_amount;     // needed for minimum-bet
                t->bet_amount = t->seats[t->cur_player].bet;
            }

            if (action == Player::Allin || p->stake == 0)
                snprintf(msg, sizeof(msg), "%d %d %d", SnapPlayerActionAllin, p->client_id, t->seats[t->cur_player].bet);
            else if (action == Player::Bet)
                snprintf(msg, sizeof(msg), "%d %d %d", SnapPlayerActionBet, p->client_id, t->bet_amount);
            else if (action == Player::Raise)
                snprintf(msg, sizeof(msg), "%d %d %d", SnapPlayerActionRaised, p->client_id, t->bet_amount);
        }
        else
            snprintf(msg, sizeof(msg), "%d %d %d", SnapPlayerActionCalled, p->client_id, amount);


        snap(t->table_id, SnapPlayerAction, msg);
    }

    // all players except one folded, so end this hand
    if (t->countActivePlayers() == 1)
    {
        // collect bets into pot
        t->collectBets();

        t->state = Table::AskShow;

        // set last remaining player as current player
        t->cur_player = t->getNextActivePlayer(t->cur_player);

        // initialize the player's timeout
        t->timeout_start = time(NULL);

        sendTableSnapshot(t);
        t->resetLastPlayerActions();
        return;
    }


    // is next the player who did the last bet/action? if yes, end this betting round
    if (t->getNextActivePlayer(t->cur_player) == t->last_bet_player)
    {
        // collect bets into pot
        t->collectBets();


        // all (or all except one) players are allin
        if (t->isAllin())
        {
            // no further action at table possible
            t->nomoreaction = true;
        }


        // which betting round is next?
        switch ((int)t->betround)
        {
            case Table::Preflop:
                t->betround = Table::Flop;
                dealFlop(t);
                break;

            case Table::Flop:
                t->betround = Table::Turn;
                dealTurn(t);
				if (t->nomoreaction)
					handleInsuranceBenefits(t, 0);
                break;

            case Table::Turn:
                t->betround = Table::River;
                dealRiver(t);
				if (t->nomoreaction)
					handleInsuranceBenefits(t, 1);
                break;

            case Table::River:
                // last_bet_player MUST show his hand
                t->seats[t->last_bet_player].showcards = true;

                // set the player behind last action as current player
                t->cur_player = t->getNextActivePlayer(t->last_bet_player);

                // initialize the player's timeout
                t->timeout_start = time(NULL);


                // end of hand, do showdown/ ask for show
                if (t->nomoreaction)
                    t->state = Table::Showdown;
                else
                    t->state = Table::AskShow;

                sendTableSnapshot(t);

                t->resetLastPlayerActions();
                return;
        }

        // send helper-snapshot // FIXME: do this smarter
        t->cur_player = -1;	// invalidate current player
        sendTableSnapshot(t);


        // reset the highest bet-amount
        t->bet_amount = 0;
        t->last_bet_amount = 0;

        // set current player to SB (or next active behind SB)
        t->cur_player = t->getNextActivePlayer(t->dealer);

        // re-initialize the player's timeout
        t->timeout_start = time(NULL);


        // first action for next betting round is at this player
        t->last_bet_player = t->cur_player;

        t->resetLastPlayerActions();

		if (t->betround == Table::Flop || t->betround == Table::Turn)
		{
			unsigned int round = 0;
			if (t->betround == Table::Turn)
				round = 1;

			if (t->nomoreaction)
			{
				if (handleBuyInsurance(t, round))
				{
					t->resume_state = Table::BettingEnd;
					t->suspend_reason = Table::BuyInsurace;
					t->max_suspend_times = 15;
					t->scheduleState(Table::Suspend, 0);
				}
			}
		}

        t->scheduleState(Table::BettingEnd, 2);
    }
    else
    {
        // preflop: if player on whom the last action was (e.g. UTG) folds,
        // assign 'last action' to next active player
        if (action == Player::Fold && t->cur_player == t->last_bet_player)
            t->last_bet_player = t->getNextActivePlayer(t->last_bet_player);

        // find next player
        t->cur_player = t->getNextActivePlayer(t->cur_player);
        t->timeout_start = time(NULL);

        // reset current player's last action
        p = t->seats[t->cur_player].player;
        p->resetLastAction();

        t->scheduleState(Table::Betting, 1);
        sendTableSnapshot(t);
    }

#if 0
    // tell player it's his turn
    p = t->seats[t->cur_player].player;
    if (!t->nomoreaction && p->stake > 0)
        snap(p->client_id, t->table_id, SnapPlayerCurrent);
#endif
}


void SitAndGoGameController::stateEndRound(Table *t)
{
    multimap<chips_type,unsigned int> broken_players;

    // assemble stake string
    string sstake;

    // find broken players
    for (unsigned int i=0; i < 10; i++)
    {
        if (!t->seats[i].occupied)
            continue;

        Player *p = t->seats[i].player;

        // assemble stake string
        char tmp[1024];
        snprintf(tmp, sizeof(tmp), "%d:%d:%d",
                p->client_id,
                p->stake,
                p->stake - p->stake_before);
        sstake += tmp;
        sstake += ' ';

        // player has no stake left
		int need_stake = 0;
		if (ante > 0)
		{
			need_stake = blind.amount / 10 * ante;
		}

		if (p->stake == 0 || p->stake < need_stake)
            broken_players.insert(pair<chips_type,int>(p->stake_before, i));
        else
        {
            // there is a net win
            if (p->stake > p->stake_before)
            {
                snprintf(msg, sizeof(msg), "%d %d %d",
                        p->client_id,
                        -1,	/* reserved */
                        p->stake - p->stake_before);
                snap(t->table_id, SnapWinAmount, msg);
            }
        }
    }

    // send stake change
    snap(t->table_id, SnapStakeChange, sstake.c_str());

    sendTableSnapshot(t);

    // remove players in right order: sorted by stake_before
    // FIXME: how to handle players which had the same stake?
    for (multimap<chips_type,unsigned int>::iterator n = broken_players.begin(); n != broken_players.end(); n++)
    {
        //const chips_type stake_before = n->first;
        const unsigned int seat_num = n->second;

        Player *p = t->seats[seat_num].player;

        // save finish position
        finish_list.push_back(p);

        // send out player-broke snapshot
        snprintf(msg, sizeof(msg), "%d %d %d",
                SnapGameStateBroke,
                p->client_id,
                getPlayerCount() - (int)finish_list.size() + 1);

        snap(t->table_id, SnapGameState, msg);

        // mark seat as unused
        t->seats[seat_num].occupied = false;
    }

    // determine next dealer
    t->dealer = t->getNextPlayer(t->dealer);

    t->scheduleState(Table::NewRound, 1);

}

int SitAndGoGameController::handleTable(Table *t)
{
    if (t->delay)
    {
        stateDelay(t);
        return 0;
    }

    if (t->state == Table::NewRound)
        stateNewRound(t);
    else if (t->state == Table::Blinds)
        stateBlinds(t);
    else if (t->state == Table::Betting)
        stateBetting(t);
    else if (t->state == Table::BettingEnd)
        stateBettingEnd(t);
    else if (t->state == Table::AskShow)
        stateAskShow(t);
    else if (t->state == Table::AllFolded)
        stateAllFolded(t);
    else if (t->state == Table::Showdown)
        stateShowdown(t);
	else if (t->state == Table::EndRound)
		stateEndRound(t);
	else if (t->state == Table::Suspend)
		stateSuspend(t);
	else if (t->state == Table::Resume)
		stateResume(t);
    return 0;
}



void SitAndGoGameController::placePlayers()
{
    if (tables.size() < 1 ) 
	{ 
        placeTable(0, getPlayerCount());
    } 
}

void SitAndGoGameController::start()
{
    // at least 2 players needed
    if (status == Started)
        return;

    placePlayers();

    blind.amount = blind.start;
    blind.last_blinds_time = time(NULL);

    log_msg("game", "game %d has been started", game_id);
    status = Started;
    started_time = time(NULL);
}

int SitAndGoGameController::tick()
{
    if (status == Created)
    {
        if ( getPlayerCount() >= 1)   {// for Sit&Go , start game if player count >= 1
            log_msg("game", "starting Sit&Go game %d", getGameId());
            start();
        }
        else	// nothing to do, exit early
            return 0;
    }
    else if (status == Ended || status == Expired)
    {
        log_msg("game", "game %d is ended ", getGameId());
        return -1;
    }
    else if (status == Paused)
    {
        log_msg("game", "game %d is paused ", getGameId());
        return 0;
    }

    // handle all tables
    for (tables_type::iterator e = tables.begin(); e != tables.end();)
    {
        Table *t = e->second;

        // table closed?
        if (handleTable(t) < 0)
        {
            // is this the last table?  /* FIXME: very very dirty */
            if (tables.size() == 1)
            {
                status = Ended;
                ended_time = time(NULL);

                snprintf(msg, sizeof(msg), "%d", SnapGameStateEnd);
                snap(-1, SnapGameState, msg);

                // push back last remaining player to finish_list
                for (unsigned int i=0; i < 10; ++i)
                    if (t->seats[i].occupied)
                    {
                        finish_list.push_back(t->seats[i].player);
                        break;
                    }
            }

            delete t;
            tables.erase(e++);
        }
        else
            ++e;
    }

    // handle expiration
    if (difftime(time(NULL), started_time) >= expire_in) { // SNT & MTT games don't expire
        status = Ended;
        ended_time = time(NULL);

        snprintf(msg, sizeof(msg), "%d", SnapGameStateEnd);
        snap(-1, SnapGameState, msg);
    }

    return 0;
}

bool SitAndGoGameController::nextRoundStraddle(int cid)
{
	Player *p = findPlayer(cid);
	if (!p)
		return false;

	// Sit&Go games should always have only one table
	tables_type::iterator e = tables.begin();
	if (e == tables.end())
	{
		log_msg("arrangeSeat", "no table found");
		return false;
	}
	Table *t = e->second;

	if (t->state > Table::Blinds && t->state < Table::EndRound)
	{
		int min_player_count = 4;
		if (getMandatoryStraddle())
			min_player_count = 5;

		if (t->countPlayers() < min_player_count)
		{
			log_msg("nextroundstraddle", "count of player must more than %d", min_player_count - 1);
			return false;
		}

		if (t->last_straddle == -1)
		{
			int pos = t->getNextPlayer(t->bb);
			pos = t->getNextPlayer(pos);
			if (getMandatoryStraddle())
				pos = t->getNextPlayer(pos);

			if (p == t->seats[pos].player)
			{
				t->last_straddle = pos;
			}
			else
			{
				log_msg("nextroundstraddle", "the player straddled was not at correct position");
				return false;
			}
		}
		else
		{
			if (t->last_straddle == t->dealer)
			{
				log_msg("nextroundstraddle", "all players have been straddled");
				return false;
			}
			int pos = t->getNextPlayer(t->last_straddle);
			if (p == t->seats[pos].player)
			{
				t->last_straddle = pos;
			}
			else
			{
				log_msg("nextroundstraddle", "the player straddled was not at correct position");
				return false;
			}
		}
	}
	else
	{
		log_msg("nextroundstraddle", "is not in the correct table state");
		return false;
	}

	if (t->last_straddle != t->dealer)
	{
		int pos = t->getNextPlayer(t->last_straddle);
		
		snap(t->seats[pos].player->client_id, t->table_id, SnapWantToStraddleNextRound);
	}
	
	return true;
}

void SitAndGoGameController::handleWantToStraddleNextRound(Table *t)
{
	int min_player_count = 4;
	if (getMandatoryStraddle())
		min_player_count = 5;

	if (t->countPlayers() < min_player_count)
	{
		return;
	}

	int cid = 0;
	if (t->last_straddle == -1)
	{
		int pos = t->getNextPlayer(t->bb);
		pos = t->getNextPlayer(pos);
		if (getMandatoryStraddle())
			pos = t->getNextPlayer(pos);
		
		cid = t->seats[pos].player->client_id;
	}
	else
	{
		if (t->last_straddle == t->dealer)
		{
			return;
		}
		int pos = t->getNextPlayer(t->last_straddle);
		cid = t->seats[pos].player->client_id;
	}

	snap(cid, t->table_id, SnapWantToStraddleNextRound);
}

bool SitAndGoGameController::handleBuyInsurance(Table *t, unsigned int round)
{
	bool ret = false;
	for (size_t i = 0; i < t->pots.size(); ++i)
	{
		if (t->pots[i].vseats.size() > 1)
		{
			vector<vector<HandStrength>> winlist;
			vector<HandStrength> wl;
			for (size_t j = 0; j < t->pots[i].vseats.size(); ++j)
			{
				unsigned int seat_id = t->pots[i].vseats[j];
				Player *p = t->seats[seat_id].player;
				HandStrength strength;
				GameLogic::getStrength(&(p->holecards), &(t->communitycards), &strength);
				strength.setId(seat_id);
				wl.push_back(strength);
			}
			GameLogic::getWinList(wl, winlist);
			if (winlist.size() > 1)
			{
				// ��ʤ��
				vector<HandStrength> winers = winlist[0];
				
				for (size_t j = 0; j < winers.size(); ++j)
				{
					unsigned int seat_id = winers[j].getId();
					Player *p = t->seats[seat_id].player;

					vector<HandStrength> vloser;
					for (size_t k = 0; k < wl.size(); ++ k)
					{
						if (wl[k].getId() != seat_id)
						{
							vloser.push_back(wl[k]);
						}
					}
					
					GameLogic::getInsuranceOuts(&winers[j], &vloser, t->deck, &(p->insuraceInfo[round].outs), &(p->insuraceInfo[round].every_single_outs));
					if (p->insuraceInfo[round].outs.size() > 0)
					{
						if (round == 1 || p->insuraceInfo[round].outs.size() <= 20 && round == 0)
						{
							ret = true;
						}
					}
					p->insuraceInfo[round].max_payment += t->pots[i].amount / winers.size();
				}
			}
		}
	}

	if (ret)
	{
		ret = false;

		unsigned int seat_id = t->dealer;
		for (unsigned int i = 0; i < t->countActivePlayers(); i++)
		{
			Player *p = t->seats[seat_id].player;
			// ת��:20�����ϲ���������
			if (p->insuraceInfo[round].outs.size() > 20 && round == 0)
			{
				p->insuraceInfo[round].outs.clear();
				p->insuraceInfo[round].every_single_outs.clear();
			}

			if (p->insuraceInfo[round].outs.size() > 0)
			{
 				std::string smsg_outs;

				char scard[5];
				for (size_t j = 0; j < p->insuraceInfo[round].outs.size(); ++j)
				{
					if (j == 0)
						snprintf(scard, sizeof(scard), "%c%c", p->insuraceInfo[round].outs[j].getFaceSymbol(), p->insuraceInfo[round].outs[j].getSuitSymbol());
					else
						snprintf(scard, sizeof(scard), ":%c%c", p->insuraceInfo[round].outs[j].getFaceSymbol(), p->insuraceInfo[round].outs[j].getSuitSymbol());
					smsg_outs += scard;
				}

				std::string smsg_others;
				map<int, vector<Card>>::iterator it = p->insuraceInfo[round].every_single_outs.begin();

				char sother[20];
				while (it != p->insuraceInfo[round].every_single_outs.end())
				{
					if (it->second.size() > 0)
					{
						snprintf(sother, sizeof(sother), "%d:%d:%c%c:%c%c",
							it->first,
							it->second.size(),
							p->holecards.getC1()->getFaceSymbol(),
							p->holecards.getC1()->getSuitSymbol(),
							p->holecards.getC2()->getFaceSymbol(),
							p->holecards.getC2()->getSuitSymbol());
					}
					smsg_others += sother;
					++it;
					if (it != p->insuraceInfo[round].every_single_outs.end())
					{
						smsg_others += '-';
					}
				}

				snprintf(msg, sizeof(msg), "%d %s %s",
					p->insuraceInfo[round].max_payment, 
					smsg_outs.c_str(),
					smsg_others.c_str());

 				snap(p->client_id, t->table_id, SnapBuyInsurance, msg);
				ret = true;
 			}
			seat_id = t->getNextActivePlayer(seat_id);
		}
	}
	return ret;
}

void SitAndGoGameController::initInsuranceRate()
{
	insurance_rate[1] = 32.0f;
	insurance_rate[2] = 16.0f;
	insurance_rate[3] = 10.0f;
	insurance_rate[4] = 7.5f;
	insurance_rate[5] = 6.0f;
	insurance_rate[6] = 5.0f;
	insurance_rate[7] = 4.5f;
	insurance_rate[8] = 3.5f;
	insurance_rate[9] = 3.0f;
	insurance_rate[10] = 2.5f;
	insurance_rate[11] = 2.3f;
	insurance_rate[12] = 2.1f;
	insurance_rate[13] = 1.9f;
	insurance_rate[14] = 1.8f;
	insurance_rate[15] = 1.4f;
	insurance_rate[16] = 1.2f;
	insurance_rate[17] = 1.1f;
	insurance_rate[18] = 1.0f;
	insurance_rate[19] = 0.9f;
	insurance_rate[20] = 0.8f;
}

bool SitAndGoGameController::clientBuyInsurance(int cid, chips_type buy_amount, std::vector<Card> & cards)
{
	// ��ȡ��ǰ�ǵڼ�������
	Player *p = findPlayer(cid);
	if (!p)
		return false;

	// Sit&Go games should always have only one table
	tables_type::iterator e = tables.begin();
	if (e == tables.end())
	{
		log_msg("clientBuyInsurance", "no table found");
		return false;
	}
	Table *t = e->second;

	if (t->state != Table::Suspend || t->suspend_reason != Table::BuyInsurace)
	{
		log_msg("clientBuyInsurance", "is not in the correct table state");
		return false;
	}

	unsigned int round = 0;
	if (t->betround == Table::Flop || t->betround == Table::Turn)
	{
		if (t->betround == Table::Turn)
			round = 1;
	}
	else
	{
		log_msg("clientBuyInsurance", "is not in the correct table betround");
		return false;
	}

	if (p->insuraceInfo[round].bought)
	{
		log_msg("clientBuyInsurance", "insurance has bought");
		return false;
	}

	if (cards.size() < 1)
	{
		// ��������
		p->insuraceInfo[round].every_single_outs.clear();
		p->insuraceInfo[round].outs.clear();
	}
	else
	{
		// ���Ƿ���ȷ
		for (size_t i = 0; i < cards.size(); ++i)
		{
			bool find = false;
			for (size_t j = 0; j < p->insuraceInfo[round].outs.size(); ++ j)
			{
				if (cards[i].getFace() == p->insuraceInfo[round].outs[j].getFace() &&
					cards[i].getSuit() == p->insuraceInfo[round].outs[j].getSuit())
				{
					find = true;
					break;
				}
			}
			if (!find)
			{
				log_msg("clientBuyInsurance", "buy card not in outs");
				return false;
			}
		}

		// ���㹺��������Ƿ�Ϸ�
		size_t index = cards.size();
		index = index > 20 ? 20 : index;
		float rate = insurance_rate[index];
		chips_type max_buy = p->insuraceInfo[round].max_payment;
		if (rate > 1)
		{
			max_buy = (chips_type)ceil(p->insuraceInfo[round].max_payment / rate);
		}

		if (buy_amount > max_buy)
		{
			log_msg("clientBuyInsurance", "buy too much insurance");
			return false;
		}
	
		p->insuraceInfo[round].bought = true;
		p->insuraceInfo[round].buy_amount = buy_amount;
		p->insuraceInfo[round].buy_cards.insert(p->insuraceInfo[round].buy_cards.begin(), cards.begin(), cards.end());
	}

	// ����Ƿ������˶������˱���
	unsigned int pos = t->dealer;
	bool all_bought = true;
	for (size_t i = 0; i < t->countActivePlayers(); ++ i)
	{
		Player *player = t->seats[pos].player;
		if (player->insuraceInfo[round].outs.size() > 0 && player->insuraceInfo[round].bought == false)
		{
			all_bought = false;
			break;
		}
		pos = t->getNextActivePlayer(pos);
	}

	if (all_bought)
	{
		t->scheduleState(Table::Resume, 0);
	}

	return true;
}

void SitAndGoGameController::stateShowdown(Table *t)
{
	GameController::stateShowdown(t);

	chips_type insurance_res = 0;

	unsigned int pos = t->dealer;
	for (size_t i = 0; i < t->countActivePlayers(); ++i)
	{
		Player *p = t->seats[pos].player;
		for (size_t j = 0; j < 2; ++ j)
		{
			if (p->insuraceInfo[j].bought)
			{
				insurance_res += p->insuraceInfo[j].res_amount;
			}
		}
		// ���㱣��֧��
		if (insurance_res > 0)
		{
			p->stake -= insurance_res;
			// ����Ϣ
		}
		
		pos = t->getNextActivePlayer(pos);
	}
}

void SitAndGoGameController::handleInsuranceBenefits(Table *t, unsigned int round)
{
	vector<Card> cards;
	t->communitycards.copyCards(&cards);
	size_t card_index = 3;
	if (round == 1)
		card_index = 4;
	
	if (cards.size() < card_index + 1)
		return;

	Card card = cards[card_index];

	unsigned int pos = t->dealer;
	for (size_t i = 0; i < t->countActivePlayers(); ++i)
	{
		Player *p = t->seats[pos].player;

		// �жϱ����Ƿ�����
		if (p->insuraceInfo[round].bought)
		{
			do 
			{
				if (!GameLogic::cardInList(card, &(p->insuraceInfo[round].outs)))
				{
					// �������Ʋ���outs�У��������ȣ�����ʱ����Ҫ�۹����յķ���
					// ���㱣�շ�
					if (p->insuraceInfo[round].outs.size() == p->insuraceInfo[round].buy_cards.size())
					{
						// ȫ��
						p->insuraceInfo[round].res_amount = p->insuraceInfo[round].buy_amount;
					}
					else
					{
						size_t no_buy_card_size = p->insuraceInfo[round].outs.size() - p->insuraceInfo[round].buy_cards.size();
						no_buy_card_size > 20 ? 20 : no_buy_card_size;
						chips_type take_back_amount = (chips_type)ceil(p->insuraceInfo[round].buy_amount / insurance_rate[no_buy_card_size]);
						// ���򲿷֣���Ҫ������ر��շ�
						p->insuraceInfo[round].res_amount = p->insuraceInfo[round].buy_amount + take_back_amount;
					}
					break;
				}

				// �ڹ����outs��
				if (GameLogic::cardInList(card, &(p->insuraceInfo[round].buy_cards)))
				{
					size_t rate_index = p->insuraceInfo[round].buy_cards.size();
					rate_index > 20 ? 20 : rate_index;
					chips_type payment = p->insuraceInfo[round].buy_amount * insurance_rate[rate_index];
					payment > p->insuraceInfo[round].max_payment ? p->insuraceInfo[round].max_payment : payment;

					if (p->insuraceInfo[round].outs.size() == p->insuraceInfo[round].buy_cards.size())
					{
						// ȫ��,�⸶
						p->stake += payment;
						// ������Ϣ����Ǯ
					}
					else
					{
						size_t no_buy_card_size = p->insuraceInfo[round].outs.size() - p->insuraceInfo[round].buy_cards.size();
						no_buy_card_size > 20 ? 20 : no_buy_card_size;
						chips_type take_back_amount = (chips_type)ceil(p->insuraceInfo[round].buy_amount / insurance_rate[no_buy_card_size]);
						payment -= take_back_amount;
						p->stake += payment;
						// ������Ϣ����Ǯ
					}
					break;
				}

				// ��outs�У�����û�򣬴���
				
			} while (0);
		}

		pos = t->getNextActivePlayer(pos);
	}
}

void SitAndGoGameController::stateResume(Table * t)
{
	if (t->suspend_reason == Table::BuyInsurace)
	{
		// �����ֹ����յĵ����ֱ��빺��
		if (t->betround == Table::Turn)
		{
			unsigned int pos = t->dealer;
			for (size_t i = 0; i < t->countActivePlayers(); ++i)
			{
				Player *p = t->seats[pos].player;

				if (p->insuraceInfo[0].bought && !p->insuraceInfo[1].bought)
				{
					if (p->insuraceInfo[1].outs.size() > 0)
					{
						p->insuraceInfo[1].bought = true;
						p->insuraceInfo[1].buy_cards = p->insuraceInfo[1].outs;
						size_t rate_index = p->insuraceInfo[1].outs.size();
						rate_index > 20 ? 20 : rate_index;
						p->insuraceInfo[1].buy_amount = ceil(p->insuraceInfo[0].buy_amount / insurance_rate[rate_index]);
					}
				}

				pos = t->getNextActivePlayer(pos);
			}
		}
	}
	GameController::stateResume(t);
}
