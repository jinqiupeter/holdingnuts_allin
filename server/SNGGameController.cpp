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
#include "SNGGameController.hpp"
#include "GameLogic.hpp"
#include "Card.hpp"

#include "game.hpp"


using namespace std;

// temporary buffer for chat/snap data
static char msg[1024];


SNGGameController::SNGGameController()
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

SNGGameController::SNGGameController(const GameController& g)
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

SNGGameController::~SNGGameController()
{
	// remove all players
	for (players_type::iterator e = players.begin(); e != players.end();)
	{
		delete e->second;
		players.erase(e++);
	}
}

void SNGGameController::addBlindLevels()
{
    int big_blinds[] = {20, 30, 50, 100, 200, 400, 600, 800, 1000, 1200, 1600, 2000, 
        3000, 4000, 6000, 8000, 10000, 12000, 16000, 20000, 24000, 30000, 40000,
        60000, 80000, 100000};

    for (size_t i = 0; i < sizeof(big_blinds); i++ ) {
        BlindLevel blind_level;
        blind_level.level = i + 1;
        blind_level.big_blind = big_blinds[i];
        blind_level.ante = 0;
        blind_levels.push_back(blind_level);
    }
    
    blind.level = 1;
}

void SNGGameController::reset()
{
    game_id = -1;

    type = SNG;
    blind.blindrule = BlindByTime;	

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
}


bool SNGGameController::addPlayer(int cid, const std::string &uuid, chips_type player_stake)
{
    // don't allow registering if game has already been started
    if (status == Started)
        return false;

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

    return true;
}

bool SNGGameController::removePlayer(int cid)
{
    // don't allow removing if game has already been started
    if (status == Started)
        return false;

    players_type::iterator it = players.find(cid);

    if (it == players.end())
        return false;

    bool bIsOwner = false;
    if (owner == cid)
        bIsOwner = true;

    players.erase(it);


    // find a new owner
    if (bIsOwner)
        selectNewOwner();

    return true;
}

bool SNGGameController::getPlayerList(vector<int> &client_list, bool including_wanna_leave) const
{
    client_list.clear();

    for (players_type::const_iterator e = players.begin(); e != players.end(); e++) {
        client_list.push_back(e->first);
    }

    return true;
}

bool SNGGameController::getPlayerList(vector<string> &client_list) const
{
    client_list.clear();

    for (players_type::const_iterator e = players.begin(); e != players.end(); e++) {
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

    return true;
}

void SNGGameController::handleRebuy(Table *t)
{
    // add rebuy stake to players' stake , this should be called only when table state is Table::EndRound
    if (t->state != Table::NewRound)
        return;

    for (players_type::iterator e = players.begin(); e != players.end();)
    {
        Player *p = e->second;
        p->setStake(p->getStake() + p->getRebuyStake());
        p->setRebuyStake(0); //clear rebuy stake so it wont be added next time
        if (t->seats[p->getSeatNo()].occupied == false && p->getStake() >= blind.amount) {
            // player has gone broken and rebought, mark him as occupied so he can join next round
            t->seats[p->getSeatNo()].occupied = true;
        }
        e++;
    }
}

void SNGGameController::stateNewRound(Table *t)
{
    handleRebuy(t);
    GameController::stateNewRound(t);
}

void SNGGameController::stateBlinds(Table *t)
{
    int next_level = 0;
    int next_amount = 0;
    if (blind.level < blind_levels.size()) {
        next_level = blind.level + 1;
        next_amount = blind_levels[next_level].big_blind;
    }

    // new blinds level?
    switch ((int) blind.blindrule)
    {
        case BlindByTime:
            if (difftime(time(NULL), blind.last_blinds_time) > blind.blinds_time && blind.level < blind_levels.size() )
            {
                blind.last_blinds_time = time(NULL);
                BlindLevel blind_level = blind_levels[++blind.level];
                blind.amount = blind_level.big_blind;

                if (blind.level < blind_levels.size()) {
                    next_level = blind.level + 1;
                    next_amount = blind_levels[next_level].big_blind;
                } else {
                    next_level = 0;
                    next_amount = 0;
                }

            }
            break;
    }

    // send out blinds snapshot
    snprintf(msg, sizeof(msg), "%d %d %d %d %d %d %d", 
            SnapGameStateBlinds, 
            blind.amount / 2, 
            blind.amount, 
            (int)blind.level, 
            next_level, 
            next_amount, 
            (int)blind.last_blinds_time);
    snap(t->table_id, SnapGameState, msg);

    GameController::stateBlinds(t);
}

void SNGGameController::stateBetting(Table *t)
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
        if (p->sitout || (unsigned int)difftime(time(NULL), t->timeout_start) > p->getTimeout())
        {
            if (!p->sitout) {
                p->setTimedoutCount(p->getTimedoutCount() + 1);
            }

            // if player timed out more than 3 times, mark user as sitout
            if (p->getTimedoutCount() >= 3) {
                p->sitout = true;
                p->setTimedoutCount(0);
                log_msg("game", "player %d timed out more thatn 3 time, marking as sitout", p->getClientId());
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
                break;

            case Table::Turn:
                t->betround = Table::River;
                dealRiver(t);
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


void SNGGameController::stateEndRound(Table *t)
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
        if (p->stake == 0)
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

int SNGGameController::handleTable(Table *t)
{
    if (t->delay)
    {
        stateDelay(t);
        return 0;
    }

    log_msg("game", "handling table state: %d", t->state);
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

    if (t->countPlayers() == 1)
        return -1;

    return 0;
}



void SNGGameController::placePlayers()
{
    if (tables.size() < 1 ) { 
        placeTable(0, getPlayerCount());
    } 
}

void SNGGameController::start()
{
    // at least 2 players needed
    if (status == Started || players.size() < 2)
        return;

    placePlayers();

    blind.amount = blind.start;
    blind.last_blinds_time = time(NULL);

    log_msg("game", "game %d has been started", game_id);
    status = Started;
    started_time = time(NULL);
}

int SNGGameController::tick()
{
    if (status == Created)
    {
        if (getPlayerCount() == max_players)   {
            log_msg("game", "starting Sit&Go game %d", getGameId());
            start();
        }
        else	// nothing to do, exit early
            return 0;
    }
    else if (status == Ended || status == Expired)
    {
        log_msg("game", "game %d is ended ", getGameId());
        // delay before game gets deleted
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

    return 0;
}
