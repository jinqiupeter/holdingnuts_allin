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
#include "GameController.hpp"
#include "GameLogic.hpp"
#include "Card.hpp"

#include "game.hpp"


using namespace std;

// temporary buffer for chat/snap data
static char msg[1024];

// max players for mtt/sng
static int MAX_PLAYERS_PER_TABLE = 9;
static int MIN_PLAYERS_PER_TABLE = 6;
static int MAX_DIFF = 2;

GameController::GameController()
{
	reset();
	
	max_players = 10;
	restart = false;
	
	player_stakes = 1500;
	
	blind.blinds_time = 60 * 4;
	blind.blinds_factor = 20;
	blind.start = 10;
    blind.level = 1;
	
	name = "game";
	password = "";
	owner = -1;

    tid = -1;

    addBlindLevels();
}

GameController::GameController(const GameController& g)
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


GameController::~GameController()
{
	// remove all players
	for (players_type::iterator e = players.begin(); e != players.end();)
	{
		delete e->second;
		players.erase(e++);
	}
}

void GameController::addBlindLevels()
{
    BlindLevel blind_level;
    blind_level.level = 1;
    blind_level.big_blind = getBlindsStart();
    blind_level.ante = 0;
    
    blind_levels.push_back(blind_level);

    blind.level = 1;
}

void GameController::reset()
{
	game_id = -1;
	
	type = SNG;	// FIXME
	blind.blindrule = BlindByTime;	// FIXME:
	
    status = Created;
	hand_no = 0;

	ante = 0;
	mandatory_straddle = false;
    enable_insurance = true;	
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

bool GameController::rebuy(int cid, chips_type rebuy_stake)
{
	Player *p = findPlayer(cid);
	if (!p)
		return false;
	
	p->setRebuyStake(rebuy_stake);
    return true;
}

bool GameController::addTimeout(int cid, unsigned int timeout_to_add)
{
	Player *p = findPlayer(cid);
	if (!p)
		return false;
	
	p->setTimeout(p->getTimeout() + timeout_to_add);

    // send pot-win snapshot
    int tid = p->getTableNo();
    Table *t = tables[tid];
    int time_elapsed = (unsigned int)difftime(time(NULL), t->timeout_start);
    int time_left = p->getTimeout() - time_elapsed;
    snprintf(msg, sizeof(msg), "%d %d %d", p->client_id, timeout_to_add, time_left);
    snap(t->table_id, SnapRespite, msg);

    return true;
}


bool GameController::addPlayer(int cid, const std::string &uuid)
{
    return addPlayer(cid, uuid, player_stakes);
}

bool GameController::isPlayer(int cid) const
{
	players_type::const_iterator it = players.find(cid);
	
	if (it == players.end())
		return false;
	
	return true;
}

bool GameController::addSpectator(int cid)
{
	// is the client already a spectator (or a player)?
	if (isSpectator(cid) || isPlayer(cid))
		return false;
	
	spectators.insert(cid);
	
	return true;
}

bool GameController::removeSpectator(int cid)
{
	spectators_type::iterator it = spectators.find(cid);
	
	if (it == spectators.end())
		return false;
	
	spectators.erase(it);
	
	return true;
}

bool GameController::isSpectator(int cid) const
{
	spectators_type::const_iterator it = spectators.find(cid);
	
	if (it == spectators.end())
		return false;
	
	return true;
}

Player* GameController::findPlayer(int cid)
{
	players_type::const_iterator it = players.find(cid);
	
	if (it == players.end())
		return NULL;
	
	return it->second;
}

bool GameController::setPlayerMax(unsigned int max)
{
	if (max < 2)
		return false;
	
	max_players = max;
	return true;
}

bool GameController::setPlayerStakes(chips_type stake)
{
	if (!stake)
		return false;
	
	player_stakes = stake;
	
	return true;
}

bool GameController::getListenerList(vector<int> &client_list) const
{
    client_list.clear();

    for (players_type::const_iterator e = players.begin(); e != players.end(); e++)
        client_list.push_back(e->first);

    for (spectators_type::const_iterator e = spectators.begin(); e != spectators.end(); e++)
        client_list.push_back(*e);

    return true;
}

void GameController::getFinishList(vector<Player*> &player_list) const
{
    for (finish_list_type::const_iterator e = finish_list.begin(); e != finish_list.end(); e++)
        player_list.push_back(*e);
}

void GameController::selectNewOwner()
{
    players_type::const_iterator e = players.begin();
    if (e == players.end())
        return;

    owner = e->second->client_id;
}

void GameController::chat(int tid, const char* msg)
{
    // players
    for (players_type::const_iterator e = players.begin(); e != players.end(); e++)
        client_chat(game_id, tid, e->first, msg);

    // spectators
    for (spectators_type::const_iterator e = spectators.begin(); e != spectators.end(); e++)
        client_chat(game_id, tid, *e, msg);
}

void GameController::chat(int cid, int tid, const char* msg)
{
    client_chat(game_id, tid, cid, msg);
}

void GameController::snap(int tid, int sid, const char* msg)
{
    // players
    for (players_type::const_iterator e = players.begin(); e != players.end(); e++) {
        client_snapshot(game_id, tid, e->first, sid, msg);
    }

    // spectators
    for (spectators_type::const_iterator e = spectators.begin(); e != spectators.end(); e++)
        client_snapshot(game_id, tid, *e, sid, msg);
}

void GameController::snap(int cid, int tid, int sid, const char* msg)
{
    client_snapshot(game_id, tid, cid, sid, msg);
}

bool GameController::setPlayerAction(int cid, Player::PlayerAction action, chips_type amount)
{
    Player *p = findPlayer(cid);

    if (!p)
        return false;

    if (action == Player::ResetAction)   // reset a previously set action
    {
        p->next_action.valid = false;
        return true;
    }
    else if (action == Player::Sitout)   // player wants to sit out
    {
        p->sitout = true;
        return true;
    }
    else if (action == Player::Back)     // player says "I'm back", end sitout
    {
        p->sitout = false;
        return true;
    }
    p->next_action.valid = true;
    p->next_action.action = action;
    p->next_action.amount = amount;
    p->setTimedoutCount(0);

    return true;
}

bool GameController::createWinlist(Table *t, vector< vector<HandStrength> > &winlist)
{
    vector<HandStrength> wl;

    unsigned int showdown_player = t->last_bet_player;
    for (unsigned int i=0; i < t->countActivePlayers(); i++)
    {
        Player *p = t->seats[showdown_player].player;

        HandStrength strength;
        GameLogic::getStrength(&(p->holecards), &(t->communitycards), &strength);
        strength.setId(showdown_player);

        wl.push_back(strength);

        showdown_player = t->getNextActivePlayer(showdown_player);
    }

    return GameLogic::getWinList(wl, winlist);
}

void GameController::sendTableSnapshot(Table *t)
{
    // assemble community-cards string
    string scards;
    vector<Card> cards;

    t->communitycards.copyCards(&cards);

    for (unsigned int i=0; i < cards.size(); i++)
    {
        scards += cards[i].getName();

        if (i < cards.size() -1)
            scards += ':';
    }

    // assemble seats string
    string sseats;
    for (unsigned int i=0; i < 10; i++)
    {
        Table::Seat *s = &(t->seats[i]);

        if (!s->occupied)
            continue;

        Player *p = s->player;

        // assemble hole-cards string
        string shole;
        if (t->nomoreaction || s->showcards)
        {
            vector<Card> cards;

            p->holecards.copyCards(&cards);

            for (unsigned int i=0; i < cards.size(); i++)
                shole += cards[i].getName();
        }
        else
            shole = "-";

        int pstate = 0;
        if (s->in_round)
            pstate |= PlayerInRound;
        if (p->sitout)
            pstate |= PlayerSitout;

        char tmp[1024];
        snprintf(tmp, sizeof(tmp),
                "s%d:%d:%d:%d:%d:%d:%d:%s",
                s->seat_no,
                p->client_id,
                pstate,
                p->stake,
                p->getRebuyStake(),
                s->bet,
                p->last_action,
                shole.c_str());

        sseats += tmp;

        sseats += ' ';
    }


    // assemble pots string
    string spots;
    for (unsigned int i=0; i < t->pots.size(); i++)
    {
        Table::Pot *pot = &(t->pots[i]);

        char tmp[1024];

        snprintf(tmp, sizeof(tmp),
                "p%d:%d",
                i, pot->amount);

        spots += tmp;

        if (i < t->pots.size() -1)
            spots += ' ';
    }


    // assemble 'whose-turn' string
    string sturn;
    if (t->state == Table::GameStart ||
            t->state == Table::ElectDealer)
    {
        sturn = "-1";
    }
    else
    {
        char tmp[128];
        snprintf(tmp, sizeof(tmp), "%d:%d:%d:%d:%d",
                t->seats[t->dealer].seat_no,
                t->seats[t->sb].seat_no,
                t->seats[t->bb].seat_no,
                (t->cur_player == -1) ? -1 : (int)t->seats[t->cur_player].seat_no,
                t->seats[t->last_bet_player].seat_no);
        sturn = tmp;
    }


    chips_type minimum_bet;
    if (t->state == Table::Betting)
        minimum_bet = determineMinimumBet(t);
    else
        minimum_bet = 0;

    int next_level = 0;
    int next_amount = 0;
    if (blind.level < blind_levels.size()) {
        next_level = blind.level + 1;
        next_amount = blind_levels[next_level].big_blind;
    }


    snprintf(msg, sizeof(msg),
            "%d:%d "           // <state>:<betting-round>
            "%s "              // <dealer>:<SB>:<BB>:<current>:<last-bet>
            "cc:%s "           // <community-cards>
            "%s "              // seats
            "%s "              // pots
            "%d "              // current big blind amount
            "%d "              // current blind level
            "%d "              // next big blind amount
            "%d "              // next blind level
            "%d "              // last blind time
            "%d",              // minimum bet
            t->state, (t->state == Table::Betting) ? t->betround : -1,
            sturn.c_str(),
            scards.c_str(),
            sseats.c_str(),
            spots.c_str(),
            blind.amount,
            (int)blind.level,
            next_amount,
            next_level,
            (int)blind.last_blinds_time,
            minimum_bet);

    snap(t->table_id, SnapTable, msg);
}

void GameController::sendPlayerShowSnapshot(Table *t, Player *p)
{
    vector<Card> allcards;
    p->holecards.copyCards(&allcards);
    t->communitycards.copyCards(&allcards);

    string hsstr;
    for (vector<Card>::const_iterator e = allcards.begin(); e != allcards.end(); e++)
        hsstr += string(e->getName()) + string(" ");

    snprintf(msg, sizeof(msg), "%d %s",
            p->client_id,
            hsstr.c_str());

    snap(t->table_id, SnapPlayerShow, msg);
}

chips_type GameController::determineMinimumBet(Table *t) const
{
    if (t->bet_amount == 0)
        return blind.amount;
    else
        return t->bet_amount + (t->bet_amount - t->last_bet_amount);
}

void GameController::dealHole(Table *t)
{
    // player in small blind gets first cards
    for (unsigned int i = t->sb, c=0; c < t->countPlayers(); i = t->getNextPlayer(i))
    {
        if (!t->seats[i].occupied)
            continue;

        Player *p = t->seats[i].player;

        HoleCards h;
        Card c1, c2;
        t->deck.pop(c1);
        t->deck.pop(c2);
        p->holecards.setCards(c1, c2);

        char card1[3], card2[3];
        strcpy(card1, c1.getName());
        strcpy(card2, c2.getName());
        snprintf(msg, sizeof(msg), "%d %s %s",
                SnapCardsHole, card1, card2);
        snap(p->client_id, t->table_id, SnapCards, msg);


        // increase the found-player counter
        c++;
    }
}

void GameController::dealFlop(Table *t)
{
    Card f1, f2, f3;
    t->deck.pop(f1);
    t->deck.pop(f2);
    t->deck.pop(f3);
    t->communitycards.setFlop(f1, f2, f3);

    char card1[3], card2[3], card3[3];
    strcpy(card1, f1.getName());
    strcpy(card2, f2.getName());
    strcpy(card3, f3.getName());
    snprintf(msg, sizeof(msg), "%d %s %s %s",
            SnapCardsFlop, card1, card2, card3);
    snap(t->table_id, SnapCards, msg);
}

void GameController::dealTurn(Table *t)
{
    Card tc;
    t->deck.pop(tc);
    t->communitycards.setTurn(tc);

    char card[3];
    strcpy(card, tc.getName());
    snprintf(msg, sizeof(msg), "%d %s",
            SnapCardsTurn, card);
    snap(t->table_id, SnapCards, msg);
}

void GameController::dealRiver(Table *t)
{
    Card r;
    t->deck.pop(r);
    t->communitycards.setRiver(r);

    char card[3];
    strcpy(card, r.getName());
    snprintf(msg, sizeof(msg), "%d %s",
            SnapCardsRiver, card);
    snap(t->table_id, SnapCards, msg);
}


void GameController::stateNewRound(Table *t)
{
    log_msg("game", "state new Round");
    // count up current hand number	
    hand_no++;

    snprintf(msg, sizeof(msg), "%d %d", SnapGameStateNewHand, hand_no);
    snap(t->table_id, SnapGameState, msg);

    log_msg("Table", "Hand #%d (gid=%d tid=%d)", hand_no, game_id, t->table_id);

#ifndef SERVER_TESTING
    // fill and shuffle card-deck
    t->deck.fill();
    t->deck.shuffle();
#else
    // set defined cards for testing
    if (debug_cards.size())
    {
        dbg_msg("deck", "using defined cards");
        t->deck.empty();
        t->deck.debugPushCards(&debug_cards);
    }
    else
    {
        dbg_msg("deck", "using random cards");
        t->deck.fill();
        t->deck.shuffle();
    }
#endif


    // reset round-related
    t->communitycards.clear();

    t->bet_amount = 0;
    t->last_bet_amount = 0;
    t->nomoreaction = false;

    // clear old pots and create initial main pot
    t->pots.clear();
    Table::Pot pot;
    pot.amount = 0;
    pot.final = false;
    t->pots.push_back(pot);


    // reset player-related
    for (unsigned int i = 0; i < 10; i++)
    {
        if (!t->seats[i].occupied)
            continue;

        t->seats[i].in_round = true;
        t->seats[i].showcards = false;
        t->seats[i].bet = 0;


        Player *p = t->seats[i].player;

        p->holecards.clear();
        p->resetLastAction();
        p->clearInsuranceInfo();
        p->stake_before = p->stake;	// remember stake before this hand
        p->setTimeout(timeout);
    }


    // determine who is SB and BB
    bool headsup_rule = (t->countPlayers() == 2);

    if (headsup_rule)   // heads-up rule: only 2 players remain, so swap blinds
    {
        t->bb = t->getNextPlayer(t->dealer);
        t->sb = t->getNextPlayer(t->bb);
    }
    else
    {
        t->sb = t->getNextPlayer(t->dealer);
        t->bb = t->getNextPlayer(t->sb);
    }

    // player under the gun
    t->cur_player = t->getNextPlayer(t->bb);
    t->last_bet_player = t->cur_player;
    
    //t->clearInsuraceInfo();
    sendTableSnapshot(t);

    t->state = Table::Blinds;
}

void GameController::stateBlinds(Table *t)
{
    t->bet_amount = blind.amount;

    Player *pSmall = t->seats[t->sb].player;
    Player *pBig = t->seats[t->bb].player;

    // set the player's SB
    chips_type amount = blind.amount / 2;

    if (amount > pSmall->stake)
        amount = pSmall->stake;

    t->seats[t->sb].bet += amount;
    pSmall->stake -= amount;

    // set the player's BB
    amount = blind.amount;

    if (amount > pBig->stake)
        amount = pBig->stake;

    t->seats[t->bb].bet += amount;
    pBig->stake -= amount;

    // initialize the player's timeout
    t->timeout_start = time(NULL);

    // give out hole-cards
    dealHole(t);

#if 0
    // tell player 'under the gun' it's his turn
    Player *p = t->seats[t->cur_player].player;
    snap(p->client_id, t->table_id, SnapPlayerCurrent);
#endif

    // check if there is any more action possible
    if (t->isAllin())
    {
        if ((pBig->stake == 0 && pSmall->stake == 0) ||   // both players are allin
                (pBig->stake == 0 && t->seats[t->sb].bet >= t->seats[t->bb].bet) ||  // BB is allin, and SB has bet more or equal BB
                (pSmall->stake == 0))  // SB is allin
        {
            dbg_msg("no-more-action", "sb-allin:%s  bb-allin:%s",
                    pSmall->stake ? "no" : "yes",
                    pBig->stake ? "no" : "yes");
            t->nomoreaction = true;
        }
    }
    if (t->straddle_amount > t->bet_amount)
        t->bet_amount = t->straddle_amount;
    t->betround = Table::Preflop;
    t->scheduleState(Table::Betting, 3);

    sendTableSnapshot(t);
}


void GameController::stateBettingEnd(Table *t)
{
    //reset player's timeout
    for (unsigned int i = 0; i < 10; i++)
    {
        if (!t->seats[i].occupied)
            continue;

        Player *p = t->seats[i].player;
        p->setTimeout(timeout);
    }

    t->state = Table::Betting;
    sendTableSnapshot(t);
}

void GameController::stateAskShow(Table *t)
{
    bool chose_action = false;

    Player *p = t->seats[t->cur_player].player;

    if (!p->stake && t->countActivePlayers() > 1) // player went allin and has no option to show/muck
    {
        t->seats[t->cur_player].showcards = true;
        chose_action = true;
        p->next_action.valid = false;
    }
    else if (p->next_action.valid)  // has player set an action?
    {
        if (p->next_action.action == Player::Muck)
        {
            // muck cards
            chose_action = true;
        }
        else if (p->next_action.action == Player::Show)
        {
            // show cards
            t->seats[t->cur_player].showcards = true;

            chose_action = true;
        }

        // reset scheduled action
        p->next_action.valid = false;
    }
    else
    {
#ifndef SERVER_TESTING
        // handle player timeout
        const int timeout = 1;   // FIXME: configurable
        if ((int)difftime(time(NULL), t->timeout_start) > timeout || p->sitout)
        {
            // default on showdown is "to show"
            // Note: client needs to determine if it's hand is
            //       already lost and needs to fold if wanted
            if (t->countActivePlayers() > 1)
                t->seats[t->cur_player].showcards = true;

            chose_action = true;
        }
#else /* SERVER_TESTING */
        t->seats[t->cur_player].showcards = true;
        chose_action = true;
#endif /* SERVER_TESTING */
    }

    // return here if no action chosen till now
    if (!chose_action)
        return;


    // remember action for snapshot
    if (t->seats[t->cur_player].showcards)
        p->last_action = Player::Show;
    else
        p->last_action = Player::Muck;


    // all-players-(except-one)-folded or showdown?
    if (t->countActivePlayers() == 1)
    {
        t->state = Table::AllFolded;

        //sendTableSnapshot(t);
    }
    else
    {
        // player is out if he don't want to show his cards
        if (t->seats[t->cur_player].showcards == false)
            t->seats[t->cur_player].in_round = false;


        if (t->getNextActivePlayer(t->cur_player) == t->last_bet_player)
        {
            t->state = Table::Showdown;
            return;
        }
        else
        {
            // find next player
            t->cur_player = t->getNextActivePlayer(t->cur_player);

            t->timeout_start = time(NULL);

            // send update snapshot
            sendTableSnapshot(t);
        }
    }
}

void GameController::stateAllFolded(Table *t)
{
    // get last remaining player
    Player *p = t->seats[t->cur_player].player;

    // send PlayerShow snapshot if cards were shown
    if (t->seats[t->cur_player].showcards)
        sendPlayerShowSnapshot(t, p);


    p->stake += t->pots[0].amount;
    t->seats[t->cur_player].bet = t->pots[0].amount;

    // send pot-win snapshot
    snprintf(msg, sizeof(msg), "%d %d %d", p->client_id, 0, t->pots[0].amount);
    snap(t->table_id, SnapWinPot, msg);


    sendTableSnapshot(t);
    t->scheduleState(Table::EndRound, 2);
}

void GameController::stateShowdown(Table *t)
{
    // the player who did the last action is first
    unsigned int showdown_player = t->last_bet_player;

    // determine and send out PlayerShow snapshots
    for (unsigned int i=0; i < t->countActivePlayers(); i++)
    {
        if (t->seats[showdown_player].showcards || t->nomoreaction)
        {
            Player *p = t->seats[showdown_player].player;

            sendPlayerShowSnapshot(t, p);
        }

        showdown_player = t->getNextActivePlayer(showdown_player);
    }


    // determine winners
    vector< vector<HandStrength> > winlist;
    createWinlist(t, winlist);

    // for each winner-list
    for (unsigned int i=0; i < winlist.size(); i++)
    {
        vector<HandStrength> &tw = winlist[i];
        const unsigned int winner_count = tw.size();

        // for each pot
        for (unsigned int poti=0; poti < t->pots.size(); poti++)
        {
            Table::Pot *pot = &(t->pots[poti]);
            const unsigned int involved_count = t->getInvolvedInPotCount(pot, tw);


            chips_type win_amount = 0;
            chips_type odd_chips = 0;

            if (involved_count)
            {
                // pot is divided by number of players involved in
                win_amount = pot->amount / involved_count;

                // odd chips
                odd_chips = pot->amount - (win_amount * involved_count);
            }


            chips_type cashout_amount = 0;

            // for each winning-player
            for (unsigned int pi=0; pi < winner_count; pi++)
            {
                const unsigned int seat_num = tw[pi].getId();
                Table::Seat *seat = &(t->seats[seat_num]);
                Player *p = seat->player;

                // skip pot if player not involved in it
                if (!t->isSeatInvolvedInPot(pot, seat_num))
                    continue;
#if 0
                dbg_msg("winlist", "wl #%d involved-count=%d player #%d (seat:%d) pot #%d ($%d) ",
                        i+1, involved_count, pi, seat_num, poti, pot->amount);
#endif

                if (win_amount > 0)
                {
                    // transfer winning amount to player
                    p->stake += win_amount;

                    // put winnings to seat (needed for snapshot)
                    seat->bet += win_amount;

                    // count up overall cashed-out
                    cashout_amount += win_amount;

                    snprintf(msg, sizeof(msg), "%d %d %d", p->client_id, poti, win_amount);
                    snap(t->table_id, SnapWinPot, msg);
                }
            }

            // distribute odd chips
            if (odd_chips)
            {
                // find the next player behind button which is involved in pot
                unsigned int oddchips_player = t->getNextActivePlayer(t->dealer);

                while (!t->isSeatInvolvedInPot(pot, oddchips_player))
                    oddchips_player = t->getNextActivePlayer(oddchips_player);


                Table::Seat *seat = &(t->seats[oddchips_player]);
                Player *p = seat->player;

                p->stake += odd_chips;
                seat->bet += odd_chips;

                snprintf(msg, sizeof(msg), "%d %d %d", p->client_id, poti, odd_chips);
                snap(t->table_id, SnapOddChips, msg);

                cashout_amount += odd_chips;
            }

            // reduce pot about the overall cashed-out
            pot->amount -= cashout_amount;
        }
    }


#if 1
    // check for fatal error: not all pots were distributed
    for (unsigned int i=0; i < t->pots.size(); i++)
    {
        Table::Pot *pot = &(t->pots[i]);

        if (pot->amount > 0)
        {
            log_msg("winlist", "error: remaining chips in pot %d: %d",
                    i, pot->amount);
        }
    }
#endif


    // reset all pots
    t->pots.clear();

    sendTableSnapshot(t);

    t->scheduleState(Table::EndRound, 4);
}

void GameController::stateDelay(Table *t)
{
#ifndef SERVER_TESTING
    if ((unsigned int) difftime(time(NULL), t->delay_start) >= t->delay)
        t->delay = 0;
#else
    t->delay = 0;
#endif
}

void GameController::placeTable(int offset, int total_players)
{
    Table *t = new Table();
    t->setTableId(++tid);
    memset(t->seats, 0, sizeof(Table::Seat) * 10);
    vector<Player*> rndseats;

    players_type::const_iterator start = players.begin();
    for (int i = 0; i < offset; i++) 
	{
        start++;
    }

    players_type::const_iterator end = start;
    for (int i = 0; i < total_players; i++) {
        end++;
    }

    for (players_type::const_iterator e = start; e != end; ++e) {
        rndseats.push_back(e->second);
    }

#ifndef SERVER_TESTING
    random_shuffle(rndseats.begin(), rndseats.end());
#endif

    for (unsigned int i=0; i < 10; i++)
	{
        Table::Seat *seat = &(t->seats[i]);
        seat->seat_no = i;
        seat->occupied = false;
	}

    bool chose_dealer = false;

    const int placement[10][10] = {
        { 4 },					//  1 player
        { 4, 9 },				//  2 players
        { 4, 8, 0 },				//  3 players
        { 3, 5, 8, 0 },				//  4 players
        { 4, 6, 8, 0, 2 },			//  5 players
        { 1, 2, 4, 6, 7, 9 },			//  6 players
        { 4, 6, 2, 7, 1, 8, 0 },		//  7 players
        { 1, 2, 3, 5, 6, 7, 8, 0 },		//  8 players
        { 4, 6, 2, 7, 1, 8, 0, 5, 3 },		//  9 players
        { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9 }	// 10 players
    };

    const unsigned int place_row = players.size() - 1;
    unsigned int place_idx = 0;
    vector<Player*>::const_iterator it = rndseats.begin();

    do
    {
        const unsigned int place = placement[place_row][place_idx];
        Table::Seat *seat = &(t->seats[place]);
        Player *p = *it;
        p->setTableNo(t->getTableId());
        p->setSeatNo(place);

        dbg_msg("placing", "place_row=%d place_idx=%d place=%d player=%d",
                place_row, place_idx, place, p->client_id);

        seat->occupied = true;
        seat->player = (Player*)p;

        // FIXME: implement choosing dealer correctly
        if (!chose_dealer)
        {
            t->dealer = place;
            chose_dealer = true;
        }

        it++;
    } while (++place_idx <= place_row);

    tables[tid] = t;

    snprintf(msg, sizeof(msg), "%d", SnapGameStateStart);
    snap(tid, SnapGameState, msg);
    t->state = Table::GameStart;

    sendTableSnapshot(t);
    t->scheduleState(Table::NewRound, 5);
}

vector<int> GameController::calcTables(int players_to_arrange)
{
    vector<int> tables;
    bool matched = false;

    for (int i = MAX_PLAYERS_PER_TABLE; i >= MIN_PLAYERS_PER_TABLE; i--) {
        int table_count = players_to_arrange / i;
        int remainder = players_to_arrange % i;
        int diff = std::abs(i - remainder);
        if (remainder ==0 || diff <= MAX_DIFF) {
            matched = true;
            do { tables.push_back(i); } while (--table_count > 0);
            tables.push_back(remainder);
            break;
        }
    }

    if (!matched) {
        if (players_to_arrange < 18) {
            log_msg("game", "failed to place players: %d", players_to_arrange);
        } else {
            int first_half = players_to_arrange / 2;
            int remainder = players_to_arrange % 2;
            int second_half = first_half + remainder;

            vector<int> tmp = calcTables(first_half);
            tables.insert(tables.end(), tmp.begin(), tmp.end());

            tmp.clear();
            tmp = calcTables(second_half);
            tables.insert(tables.end(), tmp.begin(), tmp.end());
        }
    }

    return tables;
}

void GameController::pause()
{
    if (status != Started)
        return;

    //send SnapGameStatePause snap to all players
    snprintf(msg, sizeof(msg), "%d", SnapGameStatePause);
    snap(-1, SnapGameState, msg);

    status = Paused;
    log_msg("game", "game %d has been paused", game_id);
}

void GameController::resume()
{
    if (status != Paused)
        return;

    // send SnapGameStateResume snap to all players
    snprintf(msg, sizeof(msg), "%d", SnapGameStateResume);
    snap(-1, SnapGameState, msg);

    status = Started;
    log_msg("game", "game %d has been resumed", game_id);
}

void GameController::stateSuspend(Table *t)
{
//	log_msg("game", "state Suspend %d", t->suspend_times);
    if (t->suspend_times == 0)
	{
		snprintf(msg, sizeof(msg), "%d %d %d", SnapGameStateTableSuspend, t->suspend_reason, t->max_suspend_times - t->suspend_times);
		snap(t->table_id, SnapGameState, msg);
	}

	if (t->suspend_times >= t->max_suspend_times)
	{
		t->scheduleState(Table::Resume, 0);
		return;
	}

	++t->suspend_times;
	t->scheduleState(Table::Suspend, 1);
}

void GameController::stateResume(Table * t)
{
    log_msg("game", "state Resume");
	snprintf(msg, sizeof(msg), "%d", SnapGameStateTableResume);
	snap(t->table_id, SnapGameState, msg);

	t->suspend_times = 0;
	t->max_suspend_times = 0;
	t->suspend_reason = Table::NoReason;
	t->scheduleState(t->resume_state, 0);
}

bool GameController::handleBuyInsurance(Table *t, unsigned int round)
{
	return false;
}
